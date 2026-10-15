#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include "SimpleFileDialog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <cwchar>
#include <memory>
#elif defined(__linux__)
#ifndef SIMPLE_FILE_DIALOG_SD_BUS
#define SIMPLE_FILE_DIALOG_SD_BUS 0
#endif
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <string_view>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if SIMPLE_FILE_DIALOG_SD_BUS
#include <atomic>
#include <cstring>
#include <memory>
#include <systemd/sd-bus.h>
#endif
#endif



namespace
{

/// Maximum size of the dialog title, bytes (a longer title is cut).
constexpr size_t MAX_TITLE_SIZE{256};



/**
 * @brief Check if a character is a control character (C0 set or DEL).
 * @param c Character.
 * @return TRUE for 0x00 - 0x1F and 0x7F, FALSE otherwise.
 */
bool isControlCharacter(char c)
{
    const unsigned char code{static_cast<unsigned char>(c)};
    return code < 0x20 || code == 0x7F;
}



/**
 * @brief Get the size of a well-formed UTF-8 sequence (Unicode table 3-7:
 * no overlong forms, no surrogates, nothing above U+10FFFF). Noncharacters
 * (U+FDD0 - U+FDEF, U+xFFFE, U+xFFFF) are rejected as well: D-Bus (sd-bus)
 * does not accept them in strings.
 * @param text String.
 * @param pos Position of the first byte of the sequence, less than the size
 * of the string.
 * @return Size of the sequence (1 - 4 bytes) or 0 if it is not valid.
 */
size_t utf8SequenceSize(const std::string& text, size_t pos)
{
    const uint32_t lead{static_cast<unsigned char>(text[pos])};
    if (lead < 0x80)
    {
        return 1;
    }

    size_t size{0};
    uint32_t codePoint{0};
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        size = 2;
        codePoint = lead & 0x1F;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        size = 3;
        codePoint = lead & 0x0F;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        size = 4;
        codePoint = lead & 0x07;
    }
    else
    {
        return 0;
    }
    if (text.size() - pos < size)
    {
        return 0;
    }
    for (size_t i = 1; i < size; ++i)
    {
        const uint32_t next{static_cast<unsigned char>(text[pos + i])};
        if ((next & 0xC0) != 0x80)
        {
            return 0;
        }
        codePoint = (codePoint << 6) | (next & 0x3F);
    }

    if ((size == 3 && codePoint < 0x800) ||
        (size == 4 && (codePoint < 0x10000 || codePoint > 0x10FFFF)) ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
        (codePoint >= 0xFDD0 && codePoint <= 0xFDEF) ||
        (codePoint & 0xFFFE) == 0xFFFE)
    {
        return 0;
    }
    return size;
}



/**
 * @brief Make a dialog title safe to show: control characters are replaced
 * by spaces and the title is cut to MAX_TITLE_SIZE bytes without splitting a
 * UTF-8 sequence. Linux: a byte which is not part of a valid UTF-8 sequence
 * is replaced by U+FFFD (GTK, Qt and D-Bus need UTF-8). Windows: such a byte
 * is kept, the title is then converted from the ANSI code page.
 * @param title Title given by the user.
 * @return Title to show.
 */
std::string sanitizeTitle(const std::string& title)
{
    std::string result;
    result.reserve(MAX_TITLE_SIZE);
    size_t pos{0};
    while (pos < title.size())
    {
        const size_t sequenceSize{utf8SequenceSize(title, pos)};
        if (sequenceSize == 0)
        {
#if defined(__linux__)
            const std::string replacement{"\xEF\xBF\xBD"};
#else
            const std::string replacement(1, title[pos]);
#endif
            if (result.size() + replacement.size() > MAX_TITLE_SIZE)
            {
                break;
            }
            result += replacement;
            ++pos;
            continue;
        }
        if (result.size() + sequenceSize > MAX_TITLE_SIZE)
        {
            break;
        }
        if (sequenceSize == 1 && isControlCharacter(title[pos]))
        {
            result += ' ';
        }
        else
        {
            result.append(title, pos, sequenceSize);
        }
        pos += sequenceSize;
    }
    return result;
}



#if defined(_WIN32)
/// Maximum length of a path (extended-length path), UTF-16 code units.
constexpr size_t MAX_WIDE_PATH_SIZE{32767};



/// Releases a COM interface.
struct ComReleaser
{
    /**
     * @brief Release the interface.
     * @param object Interface, not nullptr.
     */
    void operator()(IUnknown* object) const
    {
        object->Release();
    }
};



/// Frees memory allocated by COM.
struct CoTaskMemDeleter
{
    /**
     * @brief Free the memory (nullptr is allowed).
     * @param memory Memory allocated by COM.
     */
    void operator()(wchar_t* memory) const
    {
        CoTaskMemFree(memory);
    }
};



/**
 * @brief COM initialization of the calling thread as a single-threaded
 * apartment for the lifetime of the object.
 */
class ComScope
{
public:

    /// Initialize COM (CoInitializeEx).
    ComScope() :
        m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                COINIT_DISABLE_OLE1DDE))
    {
    }

    /// Uninitialize COM if it was initialized by the constructor (S_OK and
    /// S_FALSE are balanced by CoUninitialize).
    ~ComScope()
    {
        if (SUCCEEDED(m_result))
        {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope& operator=(ComScope&&) = delete;

    /**
     * @brief Check if COM can be used.
     * @return TRUE if COM is initialized: by the constructor or before by the
     * caller with another concurrency model (RPC_E_CHANGED_MODE, the dialog
     * object is then created in a single-threaded apartment of COM).
     */
    bool isReady() const
    {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:

    /// Result of CoInitializeEx.
    const HRESULT m_result;
};



/**
 * @brief Convert a multibyte string to UTF-16.
 * @param text String to convert, not empty.
 * @param codePage Code page of the string.
 * @param flags Conversion flags (MB_ERR_INVALID_CHARS: fail on invalid
 * characters).
 * @param wide Converted string.
 * @return TRUE if converted, FALSE otherwise.
 */
bool toWide(const std::string& text, UINT codePage, DWORD flags,
            std::wstring& wide)
{
    if (text.empty() || text.size() > static_cast<size_t>(INT32_MAX))
    {
        return false;
    }
    const int textSize{static_cast<int>(text.size())};
    const int wideSize{MultiByteToWideChar(codePage, flags, text.data(),
                                           textSize, nullptr, 0)};
    if (wideSize <= 0)
    {
        return false;
    }
    wide.assign(static_cast<size_t>(wideSize), L'\0');
    return MultiByteToWideChar(codePage, flags, text.data(), textSize,
                               &wide[0], wideSize) == wideSize;
}



/**
 * @brief Convert the title to UTF-16: from UTF-8 or, if it is not valid UTF-8,
 * from the ANSI code page.
 * @param title Title (sanitized).
 * @return Title in UTF-16, empty if it can not be converted.
 */
std::wstring titleToWide(const std::string& title)
{
    std::wstring wide;
    if (!toWide(title, CP_UTF8, MB_ERR_INVALID_CHARS, wide) &&
        !toWide(title, CP_ACP, 0, wide))
    {
        wide.clear();
    }
    return wide;
}



/**
 * @brief Convert a path from UTF-16 to UTF-8. An unpaired surrogate (allowed
 * in NTFS file names) can not be represented: the conversion fails.
 * @param widePath Null-terminated path.
 * @param path Path in UTF-8.
 * @return TRUE if converted, FALSE if the path is empty, too long or not valid
 * UTF-16.
 */
bool wideToUtf8(const wchar_t* widePath, std::string& path)
{
    const size_t wideSize{wcsnlen(widePath, MAX_WIDE_PATH_SIZE + 1)};
    if (wideSize == 0 || wideSize > MAX_WIDE_PATH_SIZE)
    {
        return false;
    }
    const int wideLength{static_cast<int>(wideSize)};
    const int size{WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, widePath,
                                       wideLength, nullptr, 0, nullptr,
                                       nullptr)};
    if (size <= 0)
    {
        return false;
    }
    path.assign(static_cast<size_t>(size), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, widePath,
                               wideLength, &path[0], size, nullptr,
                               nullptr) == size;
}



/**
 * @brief Check a path returned by the dialog.
 * @param path Path in UTF-8.
 * @return TRUE if the path is not empty and has no control characters.
 */
bool isValidPath(const std::string& path)
{
    return !path.empty() &&
           std::none_of(path.begin(), path.end(), isControlCharacter);
}



/**
 * @brief Show the common item dialog of Windows.
 * @param title Dialog title (sanitized).
 * @return Chosen file in UTF-8 or empty string.
 */
std::string showDialog(const std::string& title)
{
    // Declared first: COM is uninitialized after all objects are released.
    const ComScope com;
    if (!com.isReady())
    {
        return std::string();
    }

    IFileOpenDialog* rawDialog{nullptr};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&rawDialog))) ||
        rawDialog == nullptr)
    {
        return std::string();
    }
    const std::unique_ptr<IFileOpenDialog, ComReleaser> fileDialog{rawDialog};

    // Only files of the file system (no virtual items such as libraries or
    // devices) which exist; the working directory of the process is kept.
    FILEOPENDIALOGOPTIONS options{0};
    if (FAILED(fileDialog->GetOptions(&options)) ||
        FAILED(fileDialog->SetOptions(options | FOS_FORCEFILESYSTEM |
                                      FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
                                      FOS_NOCHANGEDIR)))
    {
        return std::string();
    }
    const std::wstring wideTitle{titleToWide(title)};
    if (!wideTitle.empty() && FAILED(fileDialog->SetTitle(wideTitle.c_str())))
    {
        return std::string();
    }

#if defined(SIMPLE_FILE_DIALOG_TEST_SHOW)
    // Test build: the interactive call is replaced by a test function.
    const HRESULT shown{SIMPLE_FILE_DIALOG_TEST_SHOW(fileDialog.get())};
#else
    // HRESULT_FROM_WIN32(ERROR_CANCELLED) if the user cancelled the dialog.
    const HRESULT shown{fileDialog->Show(nullptr)};
#endif
    if (FAILED(shown))
    {
        return std::string();
    }

    IShellItem* rawItem{nullptr};
    if (FAILED(fileDialog->GetResult(&rawItem)) || rawItem == nullptr)
    {
        return std::string();
    }
    const std::unique_ptr<IShellItem, ComReleaser> item{rawItem};

    PWSTR rawPath{nullptr};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) ||
        rawPath == nullptr)
    {
        return std::string();
    }
    const std::unique_ptr<wchar_t, CoTaskMemDeleter> widePath{rawPath};

    std::string path;
    if (!wideToUtf8(widePath.get(), path) || !isValidPath(path))
    {
        return std::string();
    }
    return path;
}



#elif defined(__linux__)
#ifndef SIMPLE_FILE_DIALOG_TRUSTED_DIRS
/// Directories searched for the dialog programs. The PATH environment variable
/// is not used. Can be changed only at compile time (tests, distributions with
/// other directories): a comma-separated list of string literals.
#define SIMPLE_FILE_DIALOG_TRUSTED_DIRS "/usr/bin", "/bin", "/usr/local/bin"
#endif

/// Directories searched for the dialog programs, in this order.
constexpr std::array TRUSTED_DIRS{SIMPLE_FILE_DIALOG_TRUSTED_DIRS};

/// PATH environment variable of the dialog programs.
constexpr const char* CHILD_PATH{"PATH=/usr/bin:/bin:/usr/local/bin"};

/// Environment variables passed to the dialog programs (display, session,
/// user, locale and look of the dialog).
constexpr std::array<std::string_view, 16> ALLOWED_VARIABLES{
    "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS",
    "HOME", "USER", "LOGNAME", "LANG", "LANGUAGE", "DESKTOP_SESSION",
    "XMODIFIERS", "GDK_BACKEND", "GDK_SCALE", "GDK_DPI_SCALE",
    "XCURSOR_THEME", "XCURSOR_SIZE"};

/// Prefixes of environment variables passed to the dialog programs.
constexpr std::array<std::string_view, 6> ALLOWED_PREFIXES{
    "XDG_", "LC_", "GTK_", "QT_", "KDE_", "GNOME_"};

/// Variables with an allowed prefix which are not passed: they make GTK or
/// Qt load code (modules, plugins) from any directory.
constexpr std::array<std::string_view, 6> DENIED_VARIABLES{
    "GTK_MODULES", "GTK_PATH", "GTK_EXE_PREFIX", "GTK_IM_MODULE_FILE",
    "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"};

/// Maximum size of a path including the terminating null character, bytes.
constexpr size_t MAX_PATH_SIZE{static_cast<size_t>(PATH_MAX)};

/// Maximum size of the output of a dialog program: a path and a new line.
constexpr size_t MAX_OUTPUT_SIZE{MAX_PATH_SIZE};

/// Buffer for the output of a dialog program (one byte more than the maximum
/// size to detect a longer output).
using OutputBuffer = std::array<char, MAX_OUTPUT_SIZE + 1>;



/// Result of a dialog backend.
enum class DialogStatus : std::uint8_t
{
    /// The user chose a file, the path is valid.
    CHOSEN,
    /// The user closed the dialog without choosing a file.
    CANCELLED,
    /// The backend is not available or failed: the next backend is tried.
    FAILED
};



/**
 * @brief Check a path returned by a dialog. The file does not need to exist.
 * @param path Path.
 * @return TRUE if the path is absolute, shorter than PATH_MAX and has no
 * control characters (NUL, new line etc.), FALSE otherwise.
 */
bool isValidPath(const std::string& path)
{
    return !path.empty() && path.front() == '/' && path.size() < MAX_PATH_SIZE &&
           std::none_of(path.begin(), path.end(), isControlCharacter);
}



/**
 * @brief Check if an environment variable is passed to the dialog programs.
 * @param entry Environment entry "NAME=value".
 * @return TRUE if the variable is in the allowlist, FALSE otherwise.
 */
bool isAllowedVariable(std::string_view entry)
{
    const size_t nameEnd{entry.find('=')};
    if (nameEnd == std::string_view::npos || nameEnd == 0)
    {
        return false;
    }
    const std::string_view name{entry.substr(0, nameEnd)};
    if (std::find(DENIED_VARIABLES.begin(), DENIED_VARIABLES.end(), name) !=
        DENIED_VARIABLES.end())
    {
        return false;
    }
    if (std::find(ALLOWED_VARIABLES.begin(), ALLOWED_VARIABLES.end(), name) !=
        ALLOWED_VARIABLES.end())
    {
        return true;
    }
    return std::any_of(ALLOWED_PREFIXES.begin(), ALLOWED_PREFIXES.end(),
                       [name](std::string_view prefix)
                       {
                           return name.substr(0, prefix.size()) == prefix;
                       });
}



/**
 * @brief Build the environment of the dialog programs: the fixed PATH and the
 * allowed variables of the given environment.
 * @param environment Null-terminated array of "NAME=value" entries or nullptr.
 * @return Environment entries.
 */
std::vector<std::string> filterEnvironment(const char* const* environment)
{
    std::vector<std::string> result{CHILD_PATH};
    for (const char* const* entry = environment;
         entry != nullptr && *entry != nullptr; ++entry)
    {
        if (isAllowedVariable(*entry))
        {
            result.emplace_back(*entry);
        }
    }
    return result;
}



/**
 * @brief Check if a file or a directory can be changed only by root or by the
 * user of the process.
 * @param status File status.
 * @return TRUE if the owner is root or the effective user and the group and
 * other users have no write permission.
 */
bool isProtected(const struct stat& status)
{
    return (status.st_uid == 0 || status.st_uid == geteuid()) &&
           (status.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}



/**
 * @brief Find a program in the trusted directories.
 * @param name Program name.
 * @param path Full path of the program if found.
 * @return TRUE if found: an executable regular file in a directory, both
 * protected (see isProtected), FALSE otherwise.
 */
bool findTrustedProgram(const std::string& name, std::string& path)
{
    for (const char* directory : TRUSTED_DIRS)
    {
        struct stat directoryStatus{};
        if (stat(directory, &directoryStatus) != 0 ||
            !S_ISDIR(directoryStatus.st_mode) || !isProtected(directoryStatus))
        {
            continue;
        }
        const std::string candidate{std::string(directory) + "/" + name};
        struct stat fileStatus{};
        if (stat(candidate.c_str(), &fileStatus) == 0 &&
            S_ISREG(fileStatus.st_mode) && isProtected(fileStatus) &&
            access(candidate.c_str(), X_OK) == 0)
        {
            path = candidate;
            return true;
        }
    }
    return false;
}



/**
 * @brief Close a file descriptor. The result is not checked: on Linux the
 * descriptor is released even if close fails (EINTR, EIO) and must not be
 * closed again.
 * @param fd File descriptor.
 */
void closeDescriptor(int fd)
{
    static_cast<void>(close(fd));
}



/**
 * @brief Start a program: standard output to a pipe, standard input and
 * error from / to /dev/null, default signal handling, no signal blocked.
 * Descriptors of the process are not inherited: the descriptors of the library
 * are close-on-exec (pipe, D-Bus connection), all others are closed in the
 * child with glibc 2.34 or newer.
 * @param path Full path of the program.
 * @param argv Null-terminated arguments.
 * @param envp Null-terminated environment.
 * @param pid Process ID of the started program.
 * @param outputFd Read end of the pipe (close-on-exec), to be closed by the
 * caller.
 * @return TRUE if started, FALSE otherwise.
 */
bool startProgram(const std::string& path, const std::vector<char*>& argv,
                  const std::vector<char*>& envp, pid_t& pid, int& outputFd)
{
    std::array<int, 2> pipeFds{-1, -1};
    if (pipe2(pipeFds.data(), O_CLOEXEC) != 0)
    {
        return false;
    }

    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    const bool actionsReady{posix_spawn_file_actions_init(&actions) == 0};
    const bool attributesReady{posix_spawnattr_init(&attributes) == 0};
    sigset_t noSignals{};
    sigset_t allSignals{};
    int result{(actionsReady && attributesReady && sigemptyset(&noSignals) == 0 &&
                sigfillset(&allSignals) == 0) ? 0 : -1};
    // dup2 first: the pipe may have got descriptor 0 or 2 if the process
    // has no standard input or error.
    if (result == 0)
    {
        result = posix_spawn_file_actions_adddup2(&actions, pipeFds[1],
                                                  STDOUT_FILENO);
    }
    if (result == 0)
    {
        result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                                  "/dev/null", O_RDONLY, 0);
    }
    if (result == 0)
    {
        result = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                                  "/dev/null", O_WRONLY, 0);
    }
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    // Descriptors of the application which are not close-on-exec are not
    // inherited either.
    if (result == 0)
    {
        result = posix_spawn_file_actions_addclosefrom_np(&actions,
                                                          STDERR_FILENO + 1);
    }
#endif
    if (result == 0)
    {
        result = posix_spawnattr_setsigmask(&attributes, &noSignals);
    }
    if (result == 0)
    {
        result = posix_spawnattr_setsigdefault(&attributes, &allSignals);
    }
    if (result == 0)
    {
        result = posix_spawnattr_setflags(&attributes, static_cast<short>(
                                          POSIX_SPAWN_SETSIGMASK |
                                          POSIX_SPAWN_SETSIGDEF));
    }
    if (result == 0)
    {
        result = posix_spawn(&pid, path.c_str(), &actions, &attributes,
                             argv.data(), envp.data());
    }
    if (attributesReady)
    {
        posix_spawnattr_destroy(&attributes);
    }
    if (actionsReady)
    {
        posix_spawn_file_actions_destroy(&actions);
    }

    closeDescriptor(pipeFds[1]);
    if (result != 0)
    {
        closeDescriptor(pipeFds[0]);
        return false;
    }
    outputFd = pipeFds[0];
    return true;
}



/**
 * @brief Read the output of a program until the end of the stream.
 * @param fd Read end of the pipe.
 * @param buffer Buffer for the output.
 * @param size Size of the output, bytes.
 * @return TRUE if the end of the stream is reached, FALSE if the output is
 * longer than MAX_OUTPUT_SIZE or a read failed.
 */
bool readOutput(int fd, OutputBuffer& buffer, size_t& size)
{
    size = 0;
    while (size < buffer.size())
    {
        const ssize_t count{read(fd, buffer.data() + size, buffer.size() - size)};
        if (count == 0)
        {
            return true;
        }
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        size += static_cast<size_t>(count);
    }
    return false;
}



/**
 * @brief Wait for the end of a child process (reaps it).
 * @param pid Process ID.
 * @param exitCode Exit code if the process exited normally.
 * @return TRUE if the process exited normally, FALSE if it was killed by a
 * signal or its status is not available (the application reaps children
 * itself or ignores SIGCHLD).
 */
bool waitForExit(pid_t pid, int& exitCode)
{
    int status{0};
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return false;
        }
    }
    if (!WIFEXITED(status))
    {
        return false;
    }
    exitCode = WEXITSTATUS(status);
    return true;
}



/**
 * @brief Run a dialog program and get the chosen file. The program is started
 * directly (no shell) with the filtered environment.
 * @param name Program name (searched in the trusted directories only).
 * @param arguments Program arguments.
 * @param file Chosen file if the status is CHOSEN.
 * @return CHOSEN if the program exited with code 0 and printed a valid path,
 * CANCELLED if it exited with code 1, FAILED otherwise (not found, not
 * started, other exit code, killed by a signal, output too long or invalid).
 */
DialogStatus runDialogProgram(const std::string& name,
                              const std::vector<std::string>& arguments,
                              std::string& file)
{
    std::string programPath;
    if (!findTrustedProgram(name, programPath))
    {
        return DialogStatus::FAILED;
    }

    std::vector<std::string> argumentStrings{name};
    argumentStrings.insert(argumentStrings.end(), arguments.begin(),
                           arguments.end());
    std::vector<char*> argv;
    argv.reserve(argumentStrings.size() + 1);
    for (std::string& argument : argumentStrings)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    std::vector<std::string> environmentStrings{filterEnvironment(environ)};
    std::vector<char*> envp;
    envp.reserve(environmentStrings.size() + 1);
    for (std::string& variable : environmentStrings)
    {
        envp.push_back(variable.data());
    }
    envp.push_back(nullptr);

    pid_t pid{0};
    int outputFd{-1};
    if (!startProgram(programPath, argv, envp, pid, outputFd))
    {
        return DialogStatus::FAILED;
    }

    // Nothing below throws until the child process is reaped.
    OutputBuffer output{};
    size_t outputSize{0};
    const bool outputComplete{readOutput(outputFd, output, outputSize)};
    closeDescriptor(outputFd);
    if (!outputComplete)
    {
        // Too much output: the program is stopped (it may ignore SIGPIPE). Can
        // fail only if the process was already reaped by the application, then
        // waitForExit fails too.
        static_cast<void>(kill(pid, SIGKILL));
    }
    int exitCode{-1};
    const bool exited{waitForExit(pid, exitCode)};
    if (!outputComplete || !exited || (exitCode != 0 && exitCode != 1))
    {
        return DialogStatus::FAILED;
    }
    if (exitCode == 1)
    {
        return DialogStatus::CANCELLED;
    }

    std::string path(output.data(), outputSize);
    if (!path.empty() && path.back() == '\n')
    {
        path.pop_back();
    }
    if (!isValidPath(path))
    {
        return DialogStatus::FAILED;
    }
    file = std::move(path);
    return DialogStatus::CHOSEN;
}



#if SIMPLE_FILE_DIALOG_SD_BUS
/// Well-known D-Bus name of the XDG desktop portal.
constexpr const char* PORTAL_NAME{"org.freedesktop.portal.Desktop"};

/// Object path of the XDG desktop portal.
constexpr const char* PORTAL_PATH{"/org/freedesktop/portal/desktop"};

/// Interface of the portal request objects.
constexpr const char* REQUEST_INTERFACE{"org.freedesktop.portal.Request"};

/// D-Bus name of the message bus (sender of NameOwnerChanged).
constexpr const char* BUS_NAME{"org.freedesktop.DBus"};

/// Match rule for the changes of the owner of the portal name.
constexpr const char* PORTAL_OWNER_MATCH{
    "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',"
    "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
    "arg0='org.freedesktop.portal.Desktop'"};

/// Maximum size of a file URI: scheme, host "localhost" and a path with all
/// bytes percent-encoded.
constexpr size_t MAX_URI_SIZE{sizeof("file://localhost") - 1 +
                              3 * MAX_PATH_SIZE};

/// Counter of the portal requests (unique request tokens).
std::atomic<uint32_t> g_requestCounter{0};



/// State of a portal request, changed by the signal handlers.
struct PortalRequest
{
    /// Unique bus name of the portal (sender of the reply to OpenFile).
    std::string portalOwner;
    /// Object path of the request (handle returned by OpenFile).
    std::string handle;
    /// TRUE when the request is finished: response received or portal gone.
    bool finished{false};
    /// Result of the request.
    DialogStatus status{DialogStatus::FAILED};
    /// Chosen file if the status is CHOSEN.
    std::string file;
};



/// Closes a bus connection.
struct BusDeleter
{
    /**
     * @brief Flush, close and release the connection.
     * @param bus Connection.
     */
    void operator()(sd_bus* bus) const
    {
        sd_bus_flush_close_unref(bus);
    }
};



/// Releases a bus message.
struct MessageDeleter
{
    /**
     * @brief Release the message (nullptr is allowed).
     * @param message Message.
     */
    void operator()(sd_bus_message* message) const
    {
        sd_bus_message_unref(message);
    }
};



/**
 * @brief Get the value of a hexadecimal digit.
 * @param c Character.
 * @return Value 0 - 15 or -1 if the character is not a hexadecimal digit.
 */
int hexDigitValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}



/**
 * @brief Convert a local file URI (RFC 8089) to a path.
 * @param uri URI: "file://", empty host or "localhost", absolute path with
 * percent-encoded bytes, no query and no fragment, at most MAX_URI_SIZE bytes.
 * @param path Decoded path if the URI is valid.
 * @return TRUE if the URI is valid and the decoded path is valid (see
 * isValidPath: no NUL and no other control characters), FALSE otherwise.
 */
bool uriToPath(const std::string& uri, std::string& path)
{
    const std::string scheme{"file://"};
    if (uri.size() > MAX_URI_SIZE || uri.compare(0, scheme.size(), scheme) != 0)
    {
        return false;
    }
    const size_t pathStart{uri.find('/', scheme.size())};
    if (pathStart == std::string::npos)
    {
        return false;
    }
    const size_t hostSize{pathStart - scheme.size()};
    if (hostSize != 0 && uri.compare(scheme.size(), hostSize, "localhost") != 0)
    {
        return false;
    }

    std::string decoded;
    decoded.reserve(uri.size() - pathStart);
    for (size_t i = pathStart; i < uri.size(); ++i)
    {
        char c{uri[i]};
        if (c == '?' || c == '#')
        {
            return false;
        }
        if (c == '%')
        {
            if (uri.size() - i < 3)
            {
                return false;
            }
            const int high{hexDigitValue(uri[i + 1])};
            const int low{hexDigitValue(uri[i + 2])};
            if (high < 0 || low < 0)
            {
                return false;
            }
            c = static_cast<char>(high * 16 + low);
            i += 2;
        }
        decoded += c;
    }
    if (!isValidPath(decoded))
    {
        return false;
    }
    path = std::move(decoded);
    return true;
}



/**
 * @brief Read the "uris" entry of the response (variant of type "as").
 * @param message Response message positioned at the variant.
 * @param uri First URI.
 * @param count Number of URIs, incremented.
 * @return TRUE if read, FALSE if the entry has another type, a URI is too long
 * or the message is not valid.
 */
bool readUris(sd_bus_message* message, std::string& uri, size_t& count)
{
    if (sd_bus_message_enter_container(message, 'v', "as") <= 0 ||
        sd_bus_message_enter_container(message, 'a', "s") <= 0)
    {
        return false;
    }
    for (;;)
    {
        const char* item{nullptr};
        const int read{sd_bus_message_read(message, "s", &item)};
        if (read < 0 || (read > 0 && item == nullptr))
        {
            return false;
        }
        if (read == 0)
        {
            break;
        }
        if (strnlen(item, MAX_URI_SIZE + 1) > MAX_URI_SIZE)
        {
            return false;
        }
        if (++count == 1)
        {
            uri = item;
        }
    }
    // Leave the array of strings and then the variant.
    if (sd_bus_message_exit_container(message) < 0)
    {
        return false;
    }
    return sd_bus_message_exit_container(message) >= 0;
}



/**
 * @brief Read the response of the portal: code (u) and results (a{sv}). The
 * request status stays FAILED unless the response is valid.
 * @param message Response message.
 * @param request Request.
 */
void parseResponse(sd_bus_message* message, PortalRequest& request)
{
    uint32_t code{2};
    if (sd_bus_message_has_signature(message, "ua{sv}") <= 0 ||
        sd_bus_message_read(message, "u", &code) <= 0)
    {
        return;
    }
    // 0 - success, 1 - cancelled by the user, 2 - other error.
    if (code == 1)
    {
        request.status = DialogStatus::CANCELLED;
        return;
    }
    if (code != 0 || sd_bus_message_enter_container(message, 'a', "{sv}") <= 0)
    {
        return;
    }

    std::string uri;
    size_t uriCount{0};
    for (;;)
    {
        const int entered{sd_bus_message_enter_container(message, 'e', "sv")};
        if (entered < 0)
        {
            return;
        }
        if (entered == 0)
        {
            break;
        }
        const char* key{nullptr};
        if (sd_bus_message_read(message, "s", &key) <= 0 || key == nullptr)
        {
            return;
        }
        if (strcmp(key, "uris") == 0)
        {
            if (!readUris(message, uri, uriCount))
            {
                return;
            }
        }
        else if (sd_bus_message_skip(message, "v") < 0)
        {
            return;
        }
        if (sd_bus_message_exit_container(message) < 0)
        {
            return;
        }
    }

    // One file was requested: exactly one URI.
    std::string path;
    if (uriCount == 1 && uriToPath(uri, path))
    {
        request.file = std::move(path);
        request.status = DialogStatus::CHOSEN;
    }
}



/**
 * @brief Handle the org.freedesktop.portal.Request.Response signal.
 * @param message Signal message.
 * @param request Request.
 */
void handleResponse(sd_bus_message* message, PortalRequest& request)
{
    // Only the portal may answer and only on the request object: any client of
    // the bus can send a signal directly to this connection.
    const char* sender{sd_bus_message_get_sender(message)};
    const char* path{sd_bus_message_get_path(message)};
    if (sender == nullptr || path == nullptr || request.portalOwner != sender ||
        request.handle != path)
    {
        return;
    }

    request.finished = true;
    request.status = DialogStatus::FAILED;
    try
    {
        parseResponse(message, request);
    }
    catch (...)
    {
        // Out of memory: no exception may pass through the C library.
        request.status = DialogStatus::FAILED;
    }
}



/**
 * @brief Handle the org.freedesktop.DBus.NameOwnerChanged signal of the portal
 * name: the request fails if the portal leaves the bus (otherwise the response
 * would be awaited forever).
 * @param message Signal message.
 * @param request Request.
 */
void handlePortalOwnerChanged(sd_bus_message* message, PortalRequest& request)
{
    const char* sender{sd_bus_message_get_sender(message)};
    const char* name{nullptr};
    const char* oldOwner{nullptr};
    const char* newOwner{nullptr};
    if (sender == nullptr || strcmp(sender, BUS_NAME) != 0 ||
        sd_bus_message_has_signature(message, "sss") <= 0 ||
        sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) <= 0 ||
        name == nullptr || newOwner == nullptr || strcmp(name, PORTAL_NAME) != 0)
    {
        return;
    }
    // The portal was started for the request (new owner is the portal) or it
    // has gone or was replaced: the request object does not exist any more.
    if (request.portalOwner != newOwner)
    {
        request.finished = true;
        request.status = DialogStatus::FAILED;
    }
}



/**
 * @brief Handler of all messages received by the connection (filter). A
 * filter is used and not match callbacks: sd-bus does not run a match callback
 * for a message received before the match was installed, and the response may
 * already be received when an old portal returns another request path.
 * @param message Received message.
 * @param userdata Pointer to the PortalRequest.
 * @return Always 0: the result is stored in the request.
 */
int onMessage(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    PortalRequest& request{*static_cast<PortalRequest*>(userdata)};
    if (request.finished || request.portalOwner.empty())
    {
        return 0;
    }
    if (sd_bus_message_is_signal(message, REQUEST_INTERFACE, "Response") > 0)
    {
        handleResponse(message, request);
    }
    else if (sd_bus_message_is_signal(message, BUS_NAME, "NameOwnerChanged") > 0)
    {
        handlePortalOwnerChanged(message, request);
    }
    return 0;
}



/**
 * @brief Match callback which does nothing: the match rules only make the bus
 * route the signals to the connection, they are handled by onMessage.
 * @return Always 0.
 */
int ignoreMessage(sd_bus_message*, void*, sd_bus_error*)
{
    return 0;
}



/**
 * @brief Show the dialog via the org.freedesktop.portal.FileChooser interface
 * of the XDG desktop portal (D-Bus). Each call uses its own bus connection.
 * @param title Dialog title (sanitized, valid UTF-8).
 * @param file Chosen file if the status is CHOSEN.
 * @return CHOSEN, CANCELLED or FAILED (portal not available, error of the
 * portal or of the bus, invalid response).
 */
DialogStatus portalDialog(const std::string& title, std::string& file)
{
    // Declared before the bus: the signal handlers use it until the bus is
    // closed.
    PortalRequest request;
    sd_bus* rawBus{nullptr};
    if (sd_bus_open_user(&rawBus) < 0 || rawBus == nullptr)
    {
        return DialogStatus::FAILED;
    }
    const std::unique_ptr<sd_bus, BusDeleter> bus{rawBus};

    // The portal creates the request object
    // /org/freedesktop/portal/desktop/request/SENDER/TOKEN, SENDER is the
    // unique bus name without ':' and with '.' replaced by '_'.
    const char* uniqueName{nullptr};
    if (sd_bus_get_unique_name(bus.get(), &uniqueName) < 0 ||
        uniqueName == nullptr || uniqueName[0] != ':' || uniqueName[1] == '\0')
    {
        return DialogStatus::FAILED;
    }
    std::string sender{uniqueName};
    sender.erase(0, 1);
    std::replace(sender.begin(), sender.end(), '.', '_');
    const std::string token{"simplefiledialog" +
                            std::to_string(++g_requestCounter)};
    const std::string expectedHandle{std::string(PORTAL_PATH) + "/request/" +
                                     sender + "/" + token};

    // Subscribe before the call: the response may come before the reply.
    if (sd_bus_add_filter(bus.get(), nullptr, onMessage, &request) < 0 ||
        sd_bus_add_match(bus.get(), nullptr, PORTAL_OWNER_MATCH, ignoreMessage,
                         nullptr) < 0 ||
        sd_bus_match_signal(bus.get(), nullptr, PORTAL_NAME,
                            expectedHandle.c_str(), REQUEST_INTERFACE,
                            "Response", ignoreMessage, nullptr) < 0)
    {
        return DialogStatus::FAILED;
    }

    // Fails if the portal or its FileChooser backend is not installed.
    sd_bus_message* rawReply{nullptr};
    const int called{sd_bus_call_method(
        bus.get(), PORTAL_NAME, PORTAL_PATH, "org.freedesktop.portal.FileChooser",
        "OpenFile", nullptr, &rawReply, "ssa{sv}", "", title.c_str(), 1U,
        "handle_token", "s", token.c_str())};
    const std::unique_ptr<sd_bus_message, MessageDeleter> reply{rawReply};
    const char* handle{nullptr};
    const char* owner{called < 0 ? nullptr : sd_bus_message_get_sender(rawReply)};
    if (called < 0 || owner == nullptr ||
        sd_bus_message_read(rawReply, "o", &handle) <= 0 || handle == nullptr)
    {
        return DialogStatus::FAILED;
    }
    request.portalOwner = owner;
    request.handle = handle;

    // Old portal versions ignore handle_token and return another path.
    if (request.handle != expectedHandle &&
        sd_bus_match_signal(bus.get(), nullptr, PORTAL_NAME, handle,
                            REQUEST_INTERFACE, "Response", ignoreMessage,
                            nullptr) < 0)
    {
        return DialogStatus::FAILED;
    }

    // Wait until the user closes the dialog. No timeout: the dialog may stay
    // open for any time. The wait ends with the response, if the portal leaves
    // the bus or if the connection fails.
    while (!request.finished)
    {
        const int processed{sd_bus_process(bus.get(), nullptr)};
        if (processed < 0)
        {
            return DialogStatus::FAILED;
        }
        if (processed > 0)
        {
            continue;
        }
        const int waited{sd_bus_wait(bus.get(), UINT64_MAX)};
        if (waited < 0 && waited != -EINTR)
        {
            return DialogStatus::FAILED;
        }
    }
    if (request.status == DialogStatus::CHOSEN)
    {
        file = request.file;
    }
    return request.status;
}
#endif // SIMPLE_FILE_DIALOG_SD_BUS



/**
 * @brief Show the dialog with the first available backend: XDG desktop portal
 * (if built with libsystemd), zenity, kdialog. The next backend is tried only
 * if a backend failed, not if the user cancelled the dialog.
 * @param title Dialog title (sanitized).
 * @return Chosen file or empty string.
 */
std::string showDialog(const std::string& title)
{
    std::string file;
#if SIMPLE_FILE_DIALOG_SD_BUS
    DialogStatus status{portalDialog(title, file)};
    if (status == DialogStatus::FAILED)
    {
        status = runDialogProgram("zenity", {"--file-selection",
                                             "--title=" + title}, file);
    }
#else
    DialogStatus status{runDialogProgram("zenity", {"--file-selection",
                                                    "--title=" + title}, file)};
#endif
    if (status == DialogStatus::FAILED)
    {
        status = runDialogProgram("kdialog", {"--getopenfilename",
                                              "--title=" + title}, file);
    }
    return status == DialogStatus::CHOSEN ? file : std::string();
}



#else
/**
 * @brief No dialog on this platform.
 * @return Empty string.
 */
std::string showDialog(const std::string&)
{
    return std::string();
}
#endif

}



std::string cr::utils::SimpleFileDialog::dialog(const std::string& title) noexcept
{
    try
    {
        return showDialog(sanitizeTitle(title));
    }
    catch (...)
    {
        // Out of memory: no file.
        return std::string();
    }
}
