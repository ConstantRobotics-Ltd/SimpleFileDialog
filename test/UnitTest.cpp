#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

/// Replaces IFileDialog::Show in the library (defined below).
HRESULT testShow(IFileOpenDialog* fileDialog);
#define SIMPLE_FILE_DIALOG_TEST_SHOW testShow

#elif defined(__linux__)
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <fstream>
#include <iterator>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef SIMPLE_FILE_DIALOG_TRUSTED_DIRS
#define SIMPLE_FILE_DIALOG_TRUSTED_DIRS "/tmp/simplefiledialog_unit_test_trusted"
#endif
#endif

// The library source is compiled into this test with test-only settings, the
// library is not linked:
// - Linux: SIMPLE_FILE_DIALOG_TRUSTED_DIRS is the directory of the fake zenity
//   and kdialog programs (shell scripts) which the test writes and removes.
// - Windows: SIMPLE_FILE_DIALOG_TEST_SHOW replaces the interactive
//   IFileDialog::Show call, so the COM life cycle is tested.
#include "SimpleFileDialog.cpp"



namespace
{

/// Number of checks.
int g_checkCount{0};
/// Number of failed checks.
int g_failCount{0};



/**
 * @brief Record the result of a check.
 * @param condition Result of the check.
 * @param description Checked expression.
 * @param line Source line of the check.
 */
void check(bool condition, const char* description, int line)
{
    ++g_checkCount;
    if (!condition)
    {
        ++g_failCount;
        std::cout << "FAILED (line " << line << "): " << description << std::endl;
    }
}

}

/// Check a condition, the expression and the line are printed on failure.
#define CHECK(condition) check((condition), #condition, __LINE__)



namespace
{

/// Call of the public method with a title.
std::string runDialog(const std::string& title = "Test")
{
    return cr::utils::SimpleFileDialog::dialog(title);
}



/// Run one group of checks and print the result: OK if no check of the group
/// failed.
void runTest(const char* title, void (*test)())
{
    std::cout << title << ":" << std::endl;
    const int failedBefore{g_failCount};
    test();
    std::cout << (g_failCount == failedBefore ? "OK" : "ERROR") << std::endl
              << std::endl;
}



/// State of the random generator of the tests. The tests use an own
/// deterministic generator: the results are reproducible on all platforms.
uint32_t g_randomState{20261015U};



/// Get the next random number in [0, limit).
int randomInt(int limit)
{
    g_randomState = g_randomState * 1664525U + 1013904223U;
    return static_cast<int>((g_randomState >> 8) %
                            static_cast<uint32_t>(limit));
}



/// Get a random text of up to maxSize pieces: ASCII text, control characters,
/// bytes which are not UTF-8 and valid UTF-8 sequences of 2, 3 and 4 bytes.
std::string randomText(int maxSize)
{
    static const std::vector<std::string> pieces{
        "a", "Z", "0", "/", " ", "%", "\n", "\x1B", "\x7F", std::string(1, '\0'),
        "\xFF", "\xC0\xAF", "\xE2\x82", "\xC3\xA9", "\xE2\x82\xAC",
        "\xF0\x9F\x98\x80", "\xED\xA0\x80", "\xEF\xBF\xBE"};
    std::string text;
    const int count{randomInt(maxSize + 1)};
    for (int i = 0; i < count; ++i)
    {
        const int index{randomInt(static_cast<int>(pieces.size()))};
        text += pieces[static_cast<size_t>(index)];
    }
    return text;
}



/// Tests of the title sanitizing (common part).
void testTitle()
{
    CHECK(sanitizeTitle("Select a video file") == "Select a video file");
    CHECK(sanitizeTitle("").empty());
    CHECK(sanitizeTitle("a\nb\tc\rd\x7F" "e\x1B[2J") == "a b c d e [2J");
    CHECK(sanitizeTitle(std::string("a\0b", 3)) == "a b");
    // Valid UTF-8 is kept: Cyrillic, CJK, emoji (4 bytes).
    const std::string utf8{"\xD0\x92\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE \xE8\xA6\x96 "
                           "\xF0\x9F\x98\x80"};
    CHECK(sanitizeTitle(utf8) == utf8);
    // Cut to 256 bytes, never in the middle of a sequence.
    CHECK(sanitizeTitle(std::string(1000, 'x')) == std::string(256, 'x'));
    std::string twoByte;
    for (int i = 0; i < 300; ++i)
    {
        twoByte += "\xC3\xA9";
    }
    CHECK(sanitizeTitle(twoByte) == twoByte.substr(0, 256));
    const std::string shifted{"x" + twoByte};
    CHECK(sanitizeTitle(shifted) == shifted.substr(0, 255));
    std::string fourByte;
    for (int i = 0; i < 100; ++i)
    {
        fourByte += "\xF0\x9F\x98\x80";
    }
    CHECK(sanitizeTitle("ab" + fourByte).size() == 254);

    // UTF-8 sequence sizes.
    CHECK(utf8SequenceSize("a", 0) == 1);
    CHECK(utf8SequenceSize("\xC3\xA9", 0) == 2);
    CHECK(utf8SequenceSize("\xE2\x82\xAC", 0) == 3);
    CHECK(utf8SequenceSize("\xF4\x8F\xBF\xBD", 0) == 4);
    CHECK(utf8SequenceSize("\xC0\xAF", 0) == 0);          // overlong '/'
    CHECK(utf8SequenceSize("\xE0\x80\xAF", 0) == 0);      // overlong '/'
    CHECK(utf8SequenceSize("\xF0\x80\x80\xAF", 0) == 0);  // overlong '/'
    CHECK(utf8SequenceSize("\xED\xA0\x80", 0) == 0);      // surrogate
    CHECK(utf8SequenceSize("\xF4\x90\x80\x80", 0) == 0);  // above U+10FFFF
    CHECK(utf8SequenceSize("\xF5\x80\x80\x80", 0) == 0);
    CHECK(utf8SequenceSize("\xEF\xBF\xBE", 0) == 0);      // U+FFFE
    CHECK(utf8SequenceSize("\xEF\xB7\x90", 0) == 0);      // U+FDD0
    CHECK(utf8SequenceSize("\xF0\x9F\xBF\xBF", 0) == 0);  // U+1FFFF
    CHECK(utf8SequenceSize("\xEF\xBF\xBD", 0) == 3);      // U+FFFD
    CHECK(utf8SequenceSize("\xC3", 0) == 0);              // truncated
    CHECK(utf8SequenceSize("\xE2\x82", 0) == 0);
    CHECK(utf8SequenceSize("\xE2\x28\xA1", 0) == 0);      // bad continuation
    CHECK(utf8SequenceSize("\x80", 0) == 0);
    CHECK(utf8SequenceSize("\xFF", 0) == 0);
    CHECK(utf8SequenceSize("a\xC3\xA9", 1) == 2);

#if defined(__linux__)
    // Invalid UTF-8 is replaced by U+FFFD.
    const std::string replacement{"\xEF\xBF\xBD"};
    CHECK(sanitizeTitle("a\xFF" "b") == "a" + replacement + "b");
    CHECK(sanitizeTitle("\xC0\xAF") == replacement + replacement);
    CHECK(sanitizeTitle("\xEF\xBF\xBE") == replacement + replacement + replacement);
    CHECK(sanitizeTitle(std::string(1000, '\xFF')).size() == 255);
#else
    // Windows: invalid UTF-8 (ANSI code page) is kept.
    CHECK(sanitizeTitle("a\xE9" "b") == "a\xE9" "b");
    CHECK(sanitizeTitle(std::string(1000, '\xE9')) == std::string(256, '\xE9'));
#endif
}



/// Check if a string has a control character.
bool hasControlCharacter(const std::string& text)
{
    return std::any_of(text.begin(), text.end(), isControlCharacter);
}



#if defined(__linux__)
/// Check if a string is valid UTF-8 (the rules of the title sanitizing).
bool isValidUtf8(const std::string& text)
{
    for (size_t pos = 0; pos < text.size();)
    {
        const size_t size{utf8SequenceSize(text, pos)};
        if (size == 0)
        {
            return false;
        }
        pos += size;
    }
    return true;
}
#endif



/// Sanitized random titles: at most 256 bytes, no control characters, the
/// sanitizing is idempotent, valid UTF-8 on Linux.
void testTitleCampaign()
{
    int wrongTitles{0};
    for (int i = 0; i < 20000; ++i)
    {
        const std::string result{sanitizeTitle(randomText(300))};
        bool wrong{result.size() > MAX_TITLE_SIZE ||
                   hasControlCharacter(result) ||
                   sanitizeTitle(result) != result};
#if defined(__linux__)
        wrong = wrong || !isValidUtf8(result);
#endif
        if (wrong)
        {
            ++wrongTitles;
        }
    }
    CHECK(wrongTitles == 0);
}



#if defined(_WIN32)
/// Result returned by testShow.
HRESULT g_showResult{S_OK};
/// Number of testShow calls.
int g_showCalls{0};
/// Options of the dialog seen by testShow.
FILEOPENDIALOGOPTIONS g_shownOptions{0};
/// If TRUE, testShow keeps a reference to the dialog object in g_keptDialog.
bool g_keepDialog{false};
/// Dialog object kept by testShow.
IFileOpenDialog* g_keptDialog{nullptr};



/// Prepare testShow for a call.
void setShow(HRESULT result, bool keep)
{
    g_showResult = result;
    g_keepDialog = keep;
    g_keptDialog = nullptr;
    g_showCalls = 0;
    g_shownOptions = 0;
}



/// Tests of the conversions (Windows).
void testConversions()
{
    // UTF-8 title, a title in the ANSI code page is converted too.
    CHECK(titleToWide("\xD0\x92\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE \xF0\x9F\x98\x80") ==
          L"\x0412\x0438\x0434\x0435\x043E \xD83D\xDE00");
    CHECK(titleToWide("Select") == L"Select");
    CHECK(!titleToWide("caf\xE9").empty());
    CHECK(titleToWide("").empty());

    // Path from UTF-16 to UTF-8 (characters outside of the ANSI code page).
    std::string path;
    CHECK(wideToUtf8(L"C:\\\x0412\x0438\x0434\x0435\x043E\\\x8996\x983B\xD83D\xDE00.mp4",
                     path) &&
          path == "C:\\\xD0\x92\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE\\\xE8\xA6\x96\xE9\xA0\xBB"
                  "\xF0\x9F\x98\x80.mp4");
    CHECK(!wideToUtf8(L"C:\\a\xD800" L"b.mp4", path));  // unpaired surrogate
    CHECK(!wideToUtf8(L"", path));
    const std::wstring longPath(MAX_WIDE_PATH_SIZE + 1, L'a');
    CHECK(!wideToUtf8(longPath.c_str(), path));
    const std::wstring maxPath(MAX_WIDE_PATH_SIZE, L'a');
    CHECK(wideToUtf8(maxPath.c_str(), path) && path.size() == MAX_WIDE_PATH_SIZE);

    CHECK(isValidPath("C:\\video.mp4"));
    CHECK(!isValidPath(""));
    CHECK(!isValidPath("C:\\a\nb"));

    // Random UTF-16 strings with paired and unpaired surrogates: a path which
    // is converted is converted back to the same string, a path which is
    // not converted has an unpaired surrogate.
    int mismatches{0};
    for (int i = 0; i < 20000; ++i)
    {
        std::wstring wide;
        const int size{1 + randomInt(40)};
        for (int j = 0; j < size; ++j)
        {
            const int unit{randomInt(3) == 0 ? 0xD800 + randomInt(0x800)
                                             : 1 + randomInt(0xFFFF)};
            wide += static_cast<wchar_t>(unit);
        }
        bool unpaired{false};
        for (size_t j = 0; j < wide.size(); ++j)
        {
            const bool high{wide[j] >= 0xD800 && wide[j] <= 0xDBFF};
            const bool low{wide[j] >= 0xDC00 && wide[j] <= 0xDFFF};
            if (high && j + 1 < wide.size() && wide[j + 1] >= 0xDC00 &&
                wide[j + 1] <= 0xDFFF)
            {
                ++j;
            }
            else if (high || low)
            {
                unpaired = true;
            }
        }
        std::string utf8;
        std::wstring back;
        const bool converted{wideToUtf8(wide.c_str(), utf8)};
        if (converted == unpaired ||
            (converted && (!toWide(utf8, CP_UTF8, MB_ERR_INVALID_CHARS, back) ||
                           back != wide)))
        {
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}



/// Tests of the COM life cycle (the dialog is created but not shown).
void testCom()
{
    const FILEOPENDIALOGOPTIONS requiredOptions{FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                                FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR};

    // 1. Thread without COM: initialized and uninitialized by the library.
    std::thread([&requiredOptions]()
    {
        setShow(HRESULT_FROM_WIN32(ERROR_CANCELLED), false);
        CHECK(runDialog("Title").empty());
        CHECK(g_showCalls == 1);
        CHECK((g_shownOptions & requiredOptions) == requiredOptions);
        // COM is not initialized any more: any concurrency model can be set.
        const HRESULT initialized{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        CHECK(initialized == S_OK);
        if (SUCCEEDED(initialized))
        {
            CoUninitialize();
        }
    }).join();

    // 2. Single-threaded apartment of the caller: the library balances its
    // CoInitializeEx (S_FALSE) and releases the dialog object.
    std::thread([]()
    {
        CHECK(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK);
        setShow(HRESULT_FROM_WIN32(ERROR_CANCELLED), true);
        CHECK(runDialog("Title").empty());
        CHECK(g_showCalls == 1 && g_keptDialog != nullptr);
        if (g_keptDialog != nullptr)
        {
            // The reference of the test is the last one.
            CHECK(g_keptDialog->Release() == 0);
        }
        CoUninitialize();
        const HRESULT initialized{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        CHECK(initialized == S_OK);
        if (SUCCEEDED(initialized))
        {
            CoUninitialize();
        }
    }).join();

    // 3. Multithreaded apartment of the caller (RPC_E_CHANGED_MODE): the dialog
    // is used, COM of the caller is not uninitialized.
    std::thread([]()
    {
        CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK);
        setShow(HRESULT_FROM_WIN32(ERROR_CANCELLED), true);
        CHECK(runDialog("Title").empty());
        CHECK(g_showCalls == 1 && g_keptDialog != nullptr);
        if (g_keptDialog != nullptr)
        {
            CHECK(g_keptDialog->Release() == 0);
        }
        CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_FALSE);
        CoUninitialize();
        CoUninitialize();
    }).join();

    // 4. Show succeeds but there is no result (GetResult fails): the item
    // pointer is not used.
    std::thread([]()
    {
        setShow(S_OK, false);
        CHECK(runDialog("Title").empty());
        CHECK(g_showCalls == 1);
    }).join();

    // 5. Show fails.
    std::thread([]()
    {
        setShow(E_FAIL, false);
        CHECK(runDialog("\xD0\x92\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE \xE9 \n").empty());
        CHECK(g_showCalls == 1);
    }).join();

    // 6. Many calls in one thread: no handle leak.
    std::thread([]()
    {
        setShow(HRESULT_FROM_WIN32(ERROR_CANCELLED), false);
        for (int i = 0; i < 5; ++i)
        {
            runDialog();
        }
        DWORD handlesBefore{0};
        DWORD handlesAfter{0};
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore) != 0);
        for (int i = 0; i < 100; ++i)
        {
            runDialog();
        }
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) != 0);
        CHECK(g_showCalls == 105);
        std::cout << "Handles before / after 100 calls: " << handlesBefore << " / "
                  << handlesAfter << std::endl;
        CHECK(handlesAfter <= handlesBefore + 2);
    }).join();
}
#endif

}



#if defined(_WIN32)
HRESULT testShow(IFileOpenDialog* fileDialog)
{
    ++g_showCalls;
    if (FAILED(fileDialog->GetOptions(&g_shownOptions)))
    {
        g_shownOptions = 0;
    }
    if (g_keepDialog)
    {
        fileDialog->AddRef();
        g_keptDialog = fileDialog;
    }
    return g_showResult;
}
#endif



#if defined(__linux__)
namespace
{

/// Directory of the fake dialog programs (the trusted directory of this test).
const std::string g_trustedDir{SIMPLE_FILE_DIALOG_TRUSTED_DIRS};
/// Directory for the files of the fake programs and of the test.
const std::string g_logDir{g_trustedDir + "_log"};
/// Directory not in the trusted list (found via PATH only).
const std::string g_untrustedDir{g_trustedDir + "_untrusted"};
/// Result of the fallback program.
const std::string g_kdialogFile{"/from/kdialog.mp4"};



/// Write a file.
void writeFile(const std::string& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}



/// Read a file, empty string if it does not exist.
std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}



/// Check if a file exists.
bool fileExists(const std::string& path)
{
    struct stat status{};
    return lstat(path.c_str(), &status) == 0;
}



/// Remove a file if it exists.
void removeFile(const std::string& path)
{
    static_cast<void>(unlink(path.c_str()));
}



/// Write a fake program (shell script) to a directory.
void writeProgram(const std::string& directory, const std::string& name,
                  const std::string& body, mode_t mode = 0755)
{
    const std::string path{directory + "/" + name};
    removeFile(path);
    writeFile(path, "#!/bin/sh\necho called >> '" + g_logDir + "/" + name + ".calls'\n" +
                    body + "\n");
    CHECK(chmod(path.c_str(), mode) == 0);
}



/// Write a fake program which prints the given output and exits with the code.
void writeOutputProgram(const std::string& name, const std::string& output,
                        int exitCode)
{
    writeFile(g_logDir + "/" + name + ".out", output);
    writeProgram(g_trustedDir, name, "cat '" + g_logDir + "/" + name + ".out'\nexit " +
                                   std::to_string(exitCode));
}



/// Number of calls of a fake program since the last reset.
size_t callCount(const std::string& name)
{
    const std::string calls{readFile(g_logDir + "/" + name + ".calls")};
    return static_cast<size_t>(std::count(calls.begin(), calls.end(), '\n'));
}



/// Remove the fake programs and their logs.
void resetPrograms()
{
    for (const std::string name : {"zenity", "kdialog"})
    {
        removeFile(g_trustedDir + "/" + name);
        removeFile(g_logDir + "/" + name + ".calls");
        removeFile(g_logDir + "/" + name + ".out");
    }
}



/// Fake zenity with the given output and exit code, fake kdialog as fallback.
std::string runWithZenity(const std::string& output, int exitCode)
{
    resetPrograms();
    writeOutputProgram("zenity", output, exitCode);
    writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
    return runDialog();
}



/**
 * @brief Remove a directory with the files in it (the test creates no
 * subdirectories).
 * @param path Directory.
 * @return TRUE if the directory does not exist any more.
 */
bool removeDirectory(const std::string& path)
{
    DIR* directory{opendir(path.c_str())};
    if (directory == nullptr)
    {
        return errno == ENOENT;
    }
    bool removed{true};
    while (const dirent* entry = readdir(directory))
    {
        const std::string name{entry->d_name};
        if (name != "." && name != "..")
        {
            removed = unlink((path + "/" + name).c_str()) == 0 && removed;
        }
    }
    closedir(directory);
    return rmdir(path.c_str()) == 0 && removed;
}



/// Directory of the test: created empty, removed with its files at the end of
/// the scope. A directory which can not be removed is a failed check.
class TestDirectory
{
public:

    /// Create the directory (an old one of an interrupted run is removed).
    explicit TestDirectory(const std::string& path) : m_path(path)
    {
        static_cast<void>(removeDirectory(m_path));
        CHECK(mkdir(m_path.c_str(), 0755) == 0);
        CHECK(chmod(m_path.c_str(), 0755) == 0);
    }

    /// Remove the directory.
    ~TestDirectory()
    {
        CHECK(removeDirectory(m_path));
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory(TestDirectory&&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
    TestDirectory& operator=(TestDirectory&&) = delete;

private:

    /// Path of the directory.
    const std::string m_path;
};



/// Number of open descriptors of the process.
size_t countDescriptors()
{
    size_t count{0};
    DIR* directory{opendir("/proc/self/fd")};
    if (directory == nullptr)
    {
        return 0;
    }
    while (const dirent* entry = readdir(directory))
    {
        if (entry->d_name[0] != '.')
        {
            ++count;
        }
    }
    closedir(directory);
    return count - 1;
}



/// Tests of the path validation, environment filter and output reading.
void testLogic()
{
    CHECK(isValidPath("/tmp/video.mp4"));
    CHECK(isValidPath("/"));
    CHECK(isValidPath("/tmp/\xFF\xFE non UTF-8 'quotes' \"$x\""));
    CHECK(!isValidPath(""));
    CHECK(!isValidPath("video.mp4"));
    CHECK(!isValidPath("~/video.mp4"));
    CHECK(!isValidPath("/tmp/a\nb"));
    CHECK(!isValidPath("/tmp/a\rb"));
    CHECK(!isValidPath("/tmp/a\tb"));
    CHECK(!isValidPath("/tmp/a\x7F"));
    CHECK(!isValidPath("/tmp/a\x1B[2J"));
    CHECK(!isValidPath(std::string("/tmp/a\0b", 8)));
    CHECK(isValidPath("/" + std::string(PATH_MAX - 2, 'a')));
    CHECK(!isValidPath("/" + std::string(PATH_MAX - 1, 'a')));

    // Environment allowlist.
    CHECK(isAllowedVariable("DISPLAY=:0"));
    CHECK(isAllowedVariable("WAYLAND_DISPLAY=wayland-0"));
    CHECK(isAllowedVariable("XAUTHORITY=/run/user/1000/xauth"));
    CHECK(isAllowedVariable("DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"));
    CHECK(isAllowedVariable("XDG_RUNTIME_DIR=/run/user/1000"));
    CHECK(isAllowedVariable("LC_ALL=C.UTF-8"));
    CHECK(isAllowedVariable("LANG=de_DE.UTF-8"));
    CHECK(isAllowedVariable("GTK_THEME=Adwaita:dark"));
    CHECK(isAllowedVariable("QT_QPA_PLATFORM=xcb"));
    CHECK(isAllowedVariable("KDE_FULL_SESSION=true"));
    CHECK(isAllowedVariable("GNOME_DESKTOP_SESSION_ID=this-is-deprecated"));
    CHECK(isAllowedVariable("HOME="));
    CHECK(!isAllowedVariable("PATH=/tmp/evil:/usr/bin"));
    CHECK(!isAllowedVariable("LD_PRELOAD=/tmp/evil.so"));
    CHECK(!isAllowedVariable("LD_LIBRARY_PATH=/tmp"));
    CHECK(!isAllowedVariable("LD_AUDIT=/tmp/evil.so"));
    CHECK(!isAllowedVariable("BASH_ENV=/tmp/evil.sh"));
    CHECK(!isAllowedVariable("ENV=/tmp/evil.sh"));
    CHECK(!isAllowedVariable("IFS=x"));
    CHECK(!isAllowedVariable("GTK_MODULES=evil"));
    CHECK(!isAllowedVariable("GTK_PATH=/tmp"));
    CHECK(!isAllowedVariable("GTK_EXE_PREFIX=/tmp"));
    CHECK(!isAllowedVariable("GTK_IM_MODULE_FILE=/tmp/x"));
    CHECK(!isAllowedVariable("QT_PLUGIN_PATH=/tmp"));
    CHECK(!isAllowedVariable("QT_QPA_PLATFORM_PLUGIN_PATH=/tmp"));
    CHECK(!isAllowedVariable("GIO_EXTRA_MODULES=/tmp"));
    CHECK(!isAllowedVariable("DISPLAYX=1"));
    CHECK(!isAllowedVariable("DISPLAY"));
    CHECK(!isAllowedVariable("=DISPLAY"));
    CHECK(!isAllowedVariable(""));

    const char* environment[]{"PATH=/evil", "DISPLAY=:1", "LD_PRELOAD=/evil.so",
                              "LC_CTYPE=C", "SECRET=1", nullptr};
    const std::vector<std::string> filtered{filterEnvironment(environment)};
    CHECK((filtered == std::vector<std::string>{CHILD_PATH, "DISPLAY=:1", "LC_CTYPE=C"}));
    CHECK(filterEnvironment(nullptr) == std::vector<std::string>{CHILD_PATH});

    // Bounded output reading from a pipe (the writer gets EPIPE when the reader
    // stops).
    const auto oldPipeHandler = signal(SIGPIPE, SIG_IGN);
    for (const size_t size : {size_t{0}, size_t{10}, MAX_OUTPUT_SIZE, MAX_OUTPUT_SIZE + 1,
                              size_t{100000}})
    {
        std::array<int, 2> fds{-1, -1};
        CHECK(pipe2(fds.data(), O_CLOEXEC) == 0);
        std::thread writer([&fds, size]()
        {
            const std::string data(size, 'a');
            size_t written{0};
            while (written < data.size())
            {
                const ssize_t count{write(fds[1], data.data() + written, data.size() - written)};
                if (count <= 0)
                {
                    break;
                }
                written += static_cast<size_t>(count);
            }
            close(fds[1]);
        });
        OutputBuffer buffer{};
        size_t outputSize{0};
        const bool complete{readOutput(fds[0], buffer, outputSize)};
        close(fds[0]);
        writer.join();
        CHECK(complete == (size <= MAX_OUTPUT_SIZE));
        CHECK(outputSize == std::min(size, MAX_OUTPUT_SIZE + 1));
    }
    static_cast<void>(signal(SIGPIPE, oldPipeHandler));
    OutputBuffer buffer{};
    size_t outputSize{0};
    CHECK(!readOutput(-1, buffer, outputSize));

#if SIMPLE_FILE_DIALOG_SD_BUS
    // File URIs.
    std::string path;
    CHECK(uriToPath("file:///tmp/a%20b.mp4", path) && path == "/tmp/a b.mp4");
    CHECK(uriToPath("file://localhost/tmp/x.mp4", path) && path == "/tmp/x.mp4");
    CHECK(uriToPath("file:///tmp/%C3%a9%e2%82%AC", path) &&
          path == "/tmp/\xC3\xA9\xE2\x82\xAC");
    CHECK(uriToPath("file:///tmp/%FF%FE", path) && path == "/tmp/\xFF\xFE");
    CHECK(uriToPath("file:///tmp/a%2Fb", path) && path == "/tmp/a/b");
    CHECK(uriToPath("file:///tmp/%27%22%24%28%29%60", path) && path == "/tmp/'\"$()`");
    CHECK(uriToPath("file:///", path) && path == "/");
    CHECK(uriToPath("file://" + std::string("/") + std::string(PATH_MAX - 2, 'a'), path));
    path = "unchanged";
    for (const std::string& uri : std::vector<std::string>{
             "file:///tmp/a%00b", "file:///tmp/a%0Ab", "file:///tmp/a%0db", "file:///tmp/a%09",
             "file:///tmp/a%1F", "file:///tmp/a%7F", "file:///tmp/a\nb", "file:///tmp/a\x7F",
             "file://evil.example.com/etc/passwd", "file://localhost.evil/x",
             "file://LOCALHOST/x", "file://user@localhost/x", "file://localhost", "file://",
             "file:", "file:/tmp/x", "file:tmp/x", "FILE:///tmp/x", "ftp://localhost/x",
             "/tmp/x", "", "file:///tmp/%", "file:///tmp/%4", "file:///tmp/%zz",
             "file:///tmp/%g0", "file:///tmp/%0g", "file:///tmp/%%41", "file:///tmp/a?b",
             "file:///tmp/a#b", "file:///tmp/%00",
             "file://" + std::string("/") + std::string(PATH_MAX - 1, 'a')})
    {
        const bool converted{uriToPath(uri, path)};
        CHECK(!converted);
        if (converted)
        {
            std::cout << "  accepted URI: " << uri << std::endl;
        }
    }
    CHECK(path == "unchanged");
    std::string longUri{"file://localhost"};
    while (longUri.size() < MAX_URI_SIZE)
    {
        longUri += "/";
    }
    CHECK(longUri.size() == MAX_URI_SIZE && !uriToPath(longUri, path));
    CHECK(!uriToPath(longUri + "/", path));
    std::string encodedUri{"file:///"};
    for (size_t i = 0; i < PATH_MAX - 2; ++i)
    {
        encodedUri += "%41";
    }
    CHECK(uriToPath(encodedUri, path) && path == "/" + std::string(PATH_MAX - 2, 'A'));
#endif
}



/// Tests of the program search in the trusted directories.
void testTrustedPrograms()
{
    resetPrograms();
    std::string path;
    CHECK(!findTrustedProgram("zenity", path));
    writeOutputProgram("zenity", "/x\n", 0);
    CHECK(findTrustedProgram("zenity", path) && path == g_trustedDir + "/zenity");

    // Not executable, writable by the group or by others, not a regular file:
    // the program is not used, kdialog is the fallback.
    for (const mode_t mode : {mode_t{0644}, mode_t{0775}, mode_t{0757}, mode_t{0777}})
    {
        resetPrograms();
        writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
        writeProgram(g_trustedDir, "zenity", "echo /zenity.mp4", mode);
        CHECK(runDialog() == g_kdialogFile);
        CHECK(callCount("zenity") == 0);
    }
    resetPrograms();
    CHECK(mkdir((g_trustedDir + "/zenity").c_str(), 0755) == 0);
    writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
    CHECK(runDialog() == g_kdialogFile);
    CHECK(rmdir((g_trustedDir + "/zenity").c_str()) == 0);

    // A symbolic link to a protected program is used, to a writable one not.
    resetPrograms();
    writeProgram(g_logDir, "linked", "echo /linked.mp4");
    CHECK(symlink((g_logDir + "/linked").c_str(), (g_trustedDir + "/zenity").c_str()) == 0);
    CHECK(runDialog() == "/linked.mp4");
    CHECK(chmod((g_logDir + "/linked").c_str(), 0777) == 0);
    CHECK(runDialog().empty());
    removeFile(g_logDir + "/linked");
    removeFile(g_logDir + "/linked.calls");

    // A trusted directory writable by others is not used.
    resetPrograms();
    writeOutputProgram("zenity", "/zenity.mp4\n", 0);
    CHECK(chmod(g_trustedDir.c_str(), 0777) == 0);
    CHECK(runDialog().empty());
    CHECK(callCount("zenity") == 0);
    CHECK(chmod(g_trustedDir.c_str(), 0755) == 0);
    CHECK(runDialog() == "/zenity.mp4");
}



/// Tests of the dialog programs: exit codes, output, fallback.
void testPrograms()
{
    // File chosen: kdialog is not started.
    CHECK(runWithZenity("/tmp/video file.mp4\n", 0) == "/tmp/video file.mp4");
    CHECK(callCount("zenity") == 1 && callCount("kdialog") == 0);
    // Cancelled (exit code 1, with or without output): no fallback.
    CHECK(runWithZenity("", 1).empty());
    CHECK(callCount("zenity") == 1 && callCount("kdialog") == 0);
    CHECK(runWithZenity("/tmp/x.mp4\n", 1).empty());
    CHECK(callCount("kdialog") == 0);
    // Error exit codes: kdialog is the fallback.
    for (const int exitCode : {2, 5, 126, 127, 255})
    {
        CHECK(runWithZenity("/tmp/x.mp4\n", exitCode) == g_kdialogFile);
        CHECK(callCount("zenity") == 1 && callCount("kdialog") == 1);
    }
    // Exit code 0 without a valid path: failure, kdialog is the fallback.
    for (const std::string& output : {
             std::string(), std::string("\n"), std::string("video.mp4\n"),
             std::string("/tmp/a\nb.mp4\n"), std::string("/tmp/a.mp4\n\n"),
             std::string("/tmp/a.mp4\r\n"), std::string("/tmp/a\tb\n"),
             std::string("/tmp/a\0b\n", 9), std::string("/tmp/a\x1B[2J\n"),
             std::string("/") + std::string(PATH_MAX - 1, 'a') + "\n",
             std::string("/") + std::string(PATH_MAX, 'a'),
             std::string("/") + std::string(100000, 'a') + "\n"})
    {
        CHECK(runWithZenity(output, 0) == g_kdialogFile);
    }
    // Longest valid path, no trailing new line, special characters, non-UTF-8.
    const std::string longest{"/" + std::string(PATH_MAX - 2, 'a')};
    CHECK(runWithZenity(longest + "\n", 0) == longest);
    CHECK(runWithZenity(longest, 0) == longest);
    CHECK(runWithZenity("/tmp/a", 0) == "/tmp/a");
    for (const std::string& name : {
             std::string("/tmp/a b  c.mp4"), std::string("/tmp/it's \"q\" `id` $(id) ;|&<>*?.mp4"),
             std::string("/tmp/\xFF\xFE\x80.mp4"), std::string("/tmp/-rf"),
             std::string("/tmp/\xD0\x92\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE.mp4")})
    {
        CHECK(runWithZenity(name + "\n", 0) == name);
    }

    // Only kdialog, no program.
    resetPrograms();
    writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
    CHECK(runDialog() == g_kdialogFile);
    resetPrograms();
    writeOutputProgram("kdialog", "", 1);
    CHECK(runDialog().empty());
    CHECK(callCount("kdialog") == 1);
    resetPrograms();
    CHECK(runDialog().empty());

    // Killed by a signal: failure, fallback.
    for (const char* signalName : {"KILL", "TERM", "SEGV", "PIPE"})
    {
        resetPrograms();
        writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
        writeProgram(g_trustedDir, "zenity", "echo /tmp/x.mp4\nkill -" + std::string(signalName) +
                                           " $$\nexit 0");
        CHECK(runDialog() == g_kdialogFile);
    }

    // Endless output, SIGPIPE ignored by the program: stopped, no hang.
    resetPrograms();
    writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
    writeProgram(g_trustedDir, "zenity", "trap '' PIPE\nwhile :; do echo aaaaaaaaaaaaaaaa; done");
    auto start = std::chrono::steady_clock::now();
    CHECK(runDialog() == g_kdialogFile);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));

    // Program which closes its output and exits later: the exit code is used.
    resetPrograms();
    writeOutputProgram("kdialog", g_kdialogFile + "\n", 0);
    writeProgram(g_trustedDir, "zenity", "echo /tmp/late.mp4\nexec 1>&-\nsleep 0.3\nexit 0");
    CHECK(runDialog() == "/tmp/late.mp4");
    writeProgram(g_trustedDir, "zenity", "echo /tmp/late.mp4\nexec 1>&-\nsleep 0.3\nexit 3");
    CHECK(runDialog() == g_kdialogFile);
}



/**
 * @brief Get a signal mask from the text of /proc/PID/status.
 * @param status Text with lines "Name:<tab>hexadecimal mask".
 * @param name Name of the mask, for example "SigBlk".
 * @return Mask or all bits set if the line does not exist.
 */
uint64_t signalMask(const std::string& status, const std::string& name)
{
    const std::string prefix{name + ":\t"};
    const size_t pos{status.find(prefix)};
    if (pos == std::string::npos)
    {
        return UINT64_MAX;
    }
    return std::strtoull(status.c_str() + pos + prefix.size(), nullptr, 16);
}



/// Tests of the arguments, environment, descriptors and signals of the program.
void testProgramContext()
{
    const std::string argsFile{g_logDir + "/args.txt"};
    const std::string envFile{g_logDir + "/env.txt"};

    // Arguments: no shell, the title is one argument.
    resetPrograms();
    writeProgram(g_trustedDir, "zenity", "for a in \"$@\"; do printf '[%s]\\n' \"$a\"; done > '" +
                                       argsFile + "'\nexit 1");
    const std::string hostile{"it's \"$(touch " + g_logDir + "/pwned1)\" `touch " + g_logDir +
                              "/pwned2`; touch " + g_logDir + "/pwned3 | & > < * ~ \\"};
    CHECK(runDialog(hostile).empty());
    CHECK(readFile(argsFile) == "[--file-selection]\n[--title=" + hostile + "]\n");
    CHECK(!fileExists(g_logDir + "/pwned1") && !fileExists(g_logDir + "/pwned2") &&
          !fileExists(g_logDir + "/pwned3"));
    CHECK(runDialog("--help\nline\ttab\x1B").empty());
    CHECK(readFile(argsFile) == "[--file-selection]\n[--title=--help line tab ]\n");
    CHECK(runDialog(std::string(1000, 't')).empty());
    CHECK(readFile(argsFile) == "[--file-selection]\n[--title=" + std::string(256, 't') + "]\n");
    CHECK(runDialog("").empty());
    CHECK(readFile(argsFile) == "[--file-selection]\n[--title=]\n");
    resetPrograms();
    writeProgram(g_trustedDir, "kdialog", "for a in \"$@\"; do printf '[%s]\\n' \"$a\"; done > '" +
                                        argsFile + "'\nexit 1");
    CHECK(runDialog("K title").empty());
    CHECK(readFile(argsFile) == "[--getopenfilename]\n[--title=K title]\n");

    // Hostile PATH: the program in PATH is not used.
    writeProgram(g_untrustedDir, "zenity", "touch '" + g_logDir + "/evil'\necho /evil.mp4");
    writeProgram(g_untrustedDir, "kdialog", "touch '" + g_logDir + "/evil'\necho /evil.mp4");
    const char* currentPath{getenv("PATH")};
    const std::string oldPath{currentPath != nullptr ? currentPath : ""};
    CHECK(setenv("PATH", (g_untrustedDir + ":" + oldPath).c_str(), 1) == 0);
    resetPrograms();
    CHECK(runDialog().empty());
    CHECK(!fileExists(g_logDir + "/evil"));
    writeOutputProgram("zenity", "/trusted.mp4\n", 0);
    CHECK(runDialog() == "/trusted.mp4");
    CHECK(!fileExists(g_logDir + "/evil"));

    // Environment: allowlist and fixed PATH.
    const std::vector<std::pair<std::string, std::string>> variables{
        {"LD_PRELOAD", "/nonexistent/evil.so"}, {"LD_LIBRARY_PATH", "/tmp"},
        {"BASH_ENV", "/tmp/evil.sh"}, {"ENV", "/tmp/evil.sh"},
        {"GTK_MODULES", "evil"}, {"QT_PLUGIN_PATH", "/tmp"}, {"SFD_SECRET", "secret"},
        {"DISPLAY", ":77"}, {"XDG_RUNTIME_DIR", "/run/user/77"}, {"LC_ALL", "C"},
        {"GTK_THEME", "Adwaita"}, {"QT_QPA_PLATFORM", "xcb"}};
    for (const auto& variable : variables)
    {
        CHECK(setenv(variable.first.c_str(), variable.second.c_str(), 1) == 0);
    }
    resetPrograms();
    writeProgram(g_trustedDir, "zenity", "env > '" + envFile + "'\necho /env.mp4");
    CHECK(runDialog() == "/env.mp4");
    const std::string environment{readFile(envFile)};
    for (const char* line : {"PATH=/usr/bin:/bin:/usr/local/bin\n", "DISPLAY=:77\n",
                             "XDG_RUNTIME_DIR=/run/user/77\n", "LC_ALL=C\n",
                             "GTK_THEME=Adwaita\n", "QT_QPA_PLATFORM=xcb\n"})
    {
        CHECK(environment.find(line) != std::string::npos);
    }
    for (const char* name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "BASH_ENV", "\nENV=",
                             "GTK_MODULES", "QT_PLUGIN_PATH", "SFD_SECRET", "evil",
                             g_untrustedDir.c_str()})
    {
        CHECK(environment.find(name) == std::string::npos);
    }
    for (const auto& variable : variables)
    {
        CHECK(unsetenv(variable.first.c_str()) == 0);
    }
    CHECK(setenv("PATH", oldPath.c_str(), 1) == 0);

    // Descriptors: only standard input, output and error are inherited (the
    // descriptor 3 is the one of "ls"). Standard input is /dev/null, standard
    // error goes to /dev/null.
    const int notCloseOnExec{open("/dev/null", O_RDONLY)};
    CHECK(notCloseOnExec > 2);
    resetPrograms();
    writeProgram(g_trustedDir, "zenity",
                 "ls /proc/self/fd > '" + g_logDir + "/fds.txt'\n"
                 "readlink /proc/self/fd/0 > '" + g_logDir + "/stdin.txt'\n"
                 "readlink /proc/self/fd/2 > '" + g_logDir + "/stderr.txt'\n"
                 "cat > '" + g_logDir + "/input.txt'\n"
                 "echo noise >&2\n"
                 "grep -E '^Sig(Blk|Ign)' /proc/self/status > '" + g_logDir + "/signals.txt'\n"
                 "echo /fd.mp4");
    // The process has a pipe as standard input which is never closed: if the
    // program inherited it, "cat" would wait forever.
    std::array<int, 2> inputPipe{-1, -1};
    CHECK(pipe(inputPipe.data()) == 0);
    CHECK(write(inputPipe[1], "secret\n", 7) == 7);
    const int savedInput{dup(STDIN_FILENO)};
    CHECK(dup2(inputPipe[0], STDIN_FILENO) == STDIN_FILENO);
    // SIGPIPE ignored and SIGTERM blocked by the application: not inherited.
    const auto oldPipeHandler = signal(SIGPIPE, SIG_IGN);
    sigset_t blocked{};
    sigset_t oldMask{};
    CHECK(sigemptyset(&blocked) == 0 && sigaddset(&blocked, SIGTERM) == 0);
    CHECK(pthread_sigmask(SIG_BLOCK, &blocked, &oldMask) == 0);
    CHECK(runDialog() == "/fd.mp4");
    CHECK(pthread_sigmask(SIG_SETMASK, &oldMask, nullptr) == 0);
    static_cast<void>(signal(SIGPIPE, oldPipeHandler));
    CHECK(dup2(savedInput, STDIN_FILENO) == STDIN_FILENO);
    close(savedInput);
    close(inputPipe[0]);
    close(inputPipe[1]);
    close(notCloseOnExec);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    CHECK(readFile(g_logDir + "/fds.txt") == "0\n1\n2\n3\n");
#endif
    CHECK(readFile(g_logDir + "/stdin.txt") == "/dev/null\n");
    CHECK(readFile(g_logDir + "/stderr.txt") == "/dev/null\n");
    CHECK(readFile(g_logDir + "/input.txt").empty());
    // No signal is blocked and no standard signal (1 - 31) is ignored. The
    // signals 32 and above are not compared for ignoring: glibc before 2.38
    // ignores its two internal signals (32, 33) in the started program and
    // an emulator (qemu-user) ignores two others.
    const std::string signals{readFile(g_logDir + "/signals.txt")};
    CHECK(signalMask(signals, "SigBlk") == 0);
    CHECK((signalMask(signals, "SigIgn") & 0x7FFFFFFFU) == 0);
}



#if SIMPLE_FILE_DIALOG_SD_BUS
/// Mutate a string: replace random bytes (a part of the bytes given in
/// per mille), cut it or repeat a part of it.
std::string mutate(const std::string& text, int perMille)
{
    std::string result{text};
    for (char& byte : result)
    {
        if (randomInt(1000) < perMille)
        {
            byte = static_cast<char>(randomInt(256));
        }
    }
    const int kind{randomInt(4)};
    if (kind == 0 && !result.empty())
    {
        result.resize(static_cast<size_t>(randomInt(
                static_cast<int>(result.size()) + 1)));
    }
    else if (kind == 1 && !result.empty())
    {
        const size_t from{static_cast<size_t>(randomInt(
                static_cast<int>(result.size())))};
        result.insert(from, result.substr(from));
    }
    return result;
}
#endif



/// Random paths: the result of isValidPath is the same as of an independent
/// check.
void checkPathCampaign()
{
    int wrongPaths{0};
    for (int i = 0; i < 20000; ++i)
    {
        std::string path{"/" + randomText(60)};
        if (randomInt(10) == 0)
        {
            path.erase(0, 1);
        }
        const bool expected{!path.empty() && path.front() == '/' &&
                            path.size() < MAX_PATH_SIZE &&
                            !hasControlCharacter(path)};
        if (isValidPath(path) != expected)
        {
            ++wrongPaths;
        }
    }
    CHECK(wrongPaths == 0);
}



/// Random environments: PATH is fixed, every other entry is allowed and was in
/// the given environment, nothing which loads code is passed.
void checkEnvironmentCampaign()
{
    const std::vector<std::string> names{
        "DISPLAY", "LD_PRELOAD", "GTK_MODULES", "XDG_X", "LC_ALL", "PATH",
        "QT_PLUGIN_PATH", "HOME", "SECRET", "", "LD_LIBRARY_PATH", "GTK_THEME"};
    int wrongEnvironments{0};
    for (int i = 0; i < 5000; ++i)
    {
        std::vector<std::string> entries;
        const int count{randomInt(9)};
        for (int j = 0; j < count; ++j)
        {
            std::string value{randomText(8)};
            value.erase(std::remove(value.begin(), value.end(), '\0'),
                        value.end());
            const int index{randomInt(static_cast<int>(names.size()))};
            entries.push_back(names[static_cast<size_t>(index)] +
                              (randomInt(5) == 0 ? "" : "=") + value);
        }
        std::vector<const char*> environment;
        for (const std::string& entry : entries)
        {
            environment.push_back(entry.c_str());
        }
        environment.push_back(nullptr);
        const std::vector<std::string> filtered{
                filterEnvironment(environment.data())};
        bool wrong{filtered.empty() || filtered.front() != CHILD_PATH};
        for (size_t j = 1; j < filtered.size(); ++j)
        {
            const std::string& entry = filtered[j];
            wrong = wrong || !isAllowedVariable(entry) ||
                    std::find(entries.begin(), entries.end(), entry) ==
                    entries.end() || entry.rfind("PATH=", 0) == 0 ||
                    entry.rfind("LD_", 0) == 0 ||
                    entry.rfind("GTK_MODULES=", 0) == 0 ||
                    entry.rfind("QT_PLUGIN_PATH=", 0) == 0;
        }
        if (wrong)
        {
            ++wrongEnvironments;
        }
    }
    CHECK(wrongEnvironments == 0);
}



#if SIMPLE_FILE_DIALOG_SD_BUS
/// Percent decode the path of a file URI which was accepted (the part from the
/// first slash after "file://").
std::string decodePath(const std::string& uri)
{
    std::string decoded;
    const size_t start{uri.find('/', 7)};
    for (size_t pos = start; start != std::string::npos && pos < uri.size();
         ++pos)
    {
        if (uri[pos] == '%' && uri.size() - pos >= 3)
        {
            decoded += static_cast<char>(std::strtol(
                    uri.substr(pos + 1, 2).c_str(), nullptr, 16));
            pos += 2;
        }
        else
        {
            decoded += uri[pos];
        }
    }
    return decoded;
}



/// Random file URIs: valid URIs with random bytes replaced (0.1, 0.5 and 2 %),
/// cut and with a repeated part. An accepted URI gives a valid path equal to
/// the percent decoded one, a rejected URI does not change the path.
void checkUriCampaign()
{
    const std::vector<std::string> uris{
        "file:///tmp/a%20b.mp4", "file://localhost/home/user/video%C3%A9.mp4",
        "file:///", "file:///tmp/%FF%FE/x", "file:///a/b/c/d/e/f/g.mp4"};
    const std::array<int, 3> perMilles{1, 5, 20};
    int wrongUris{0};
    for (size_t i = 0; i < 60000; ++i)
    {
        const std::string uri{
                mutate(uris[i % uris.size()], perMilles[i % perMilles.size()])};
        std::string path{"unchanged"};
        if (!uriToPath(uri, path))
        {
            wrongUris += path == "unchanged" ? 0 : 1;
            continue;
        }
        wrongUris += (isValidPath(path) && path == decodePath(uri)) ? 0 : 1;
    }
    CHECK(wrongUris == 0);
}
#endif



/// Damaged data campaign: deterministic random and mutated input of every
/// parser of untrusted data (path, environment, file URI; the title has its
/// own campaign). A failure of a check is reported once per group with the
/// number of wrong results.
void testCorruption()
{
    checkPathCampaign();
    checkEnvironmentCampaign();
#if SIMPLE_FILE_DIALOG_SD_BUS
    checkUriCampaign();
#endif
}



/// Number of SIGUSR1 signals received.
std::atomic<int> g_signalCount{0};



/// Handler of SIGUSR1 (installed without SA_RESTART: interrupts system calls).
void onSignal(int)
{
    ++g_signalCount;
}



/// Tests of interrupted system calls, threads, SIGCHLD, leaks.
void testRobustness()
{
    const size_t descriptorsBefore{countDescriptors()};

    // System calls interrupted by signals (EINTR) are repeated.
    struct sigaction action{};
    action.sa_handler = onSignal;
    CHECK(sigemptyset(&action.sa_mask) == 0);
    action.sa_flags = 0;
    struct sigaction oldAction{};
    CHECK(sigaction(SIGUSR1, &action, &oldAction) == 0);
    resetPrograms();
    writeProgram(g_trustedDir, "zenity", "sleep 0.5\necho /slow.mp4");
    std::atomic<bool> running{true};
    const pthread_t mainThread{pthread_self()};
    std::thread sender([&running, mainThread]()
    {
        while (running)
        {
            pthread_kill(mainThread, SIGUSR1);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    CHECK(runDialog() == "/slow.mp4");
    running = false;
    sender.join();
    CHECK(sigaction(SIGUSR1, &oldAction, nullptr) == 0);
    std::cout << "Signals during the dialog: " << g_signalCount << std::endl;
    CHECK(g_signalCount > 10);

    // Calls from several threads at the same time.
    resetPrograms();
    writeProgram(g_trustedDir, "zenity", "echo \"/thread/$$.mp4\"");
    std::atomic<int> threadResults{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&threadResults]()
        {
            for (int j = 0; j < 25; ++j)
            {
                const std::string file{runDialog()};
                if (file.compare(0, 8, "/thread/") == 0)
                {
                    ++threadResults;
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    CHECK(threadResults == 200);
    CHECK(callCount("zenity") == 200);

    // SIGCHLD ignored by the application: the exit code is lost, the result is
    // empty (documented), no hang and no crash.
    resetPrograms();
    writeOutputProgram("zenity", "/tmp/x.mp4\n", 0);
    const auto oldChildHandler = signal(SIGCHLD, SIG_IGN);
    CHECK(runDialog().empty());
    static_cast<void>(signal(SIGCHLD, oldChildHandler));
    CHECK(runDialog() == "/tmp/x.mp4");

    // No leaked descriptors, no zombie processes.
    resetPrograms();
    CHECK(countDescriptors() == descriptorsBefore);
    int status{0};
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}

}
#endif



/// Entry point.
int main()
{
    const std::string title;
    static_assert(noexcept(cr::utils::SimpleFileDialog::dialog(title)),
                  "dialog must not throw");
    runTest("Dialog title", testTitle);
    runTest("Random titles", testTitleCampaign);
#if defined(_WIN32)
    runTest("Conversions", testConversions);
    runTest("COM life cycle", testCom);
#elif defined(__linux__)
    // Watchdog: a hang ends the test with SIGALRM.
    alarm(300);
    // No real portal: the D-Bus session bus can not be reached.
    CHECK(setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/simplefiledialog", 1) == 0);
    {
        // The directories of the fake programs and of the files of the test
        // are removed at the end of the scope.
        const TestDirectory trustedDirectory{g_trustedDir};
        const TestDirectory logDirectory{g_logDir};
        const TestDirectory untrustedDirectory{g_untrustedDir};
        runTest("Path, environment, output, URI", testLogic);
        runTest("Damaged data campaign", testCorruption);
        runTest("Trusted directories", testTrustedPrograms);
        runTest("Dialog programs", testPrograms);
        runTest("Program context", testProgramContext);
        runTest("Interrupted calls, threads, leaks", testRobustness);
    }
#else
    CHECK(runDialog().empty());
#endif

    std::cout << "Checks: " << g_checkCount << ", failed: " << g_failCount << std::endl;
    return g_failCount == 0 ? 0 : 1;
}
