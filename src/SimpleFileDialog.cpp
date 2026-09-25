#if defined(linux) || defined(__linux) || defined(__linux__)
#if SIMPLE_FILE_DIALOG_SD_BUS
#include <cctype>
#include <cstdint>
#include <systemd/sd-bus.h>
#endif
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <Windows.h>
#include <shobjidl.h>
#endif
#include <cstring>
#include <string.h>
#include "SimpleFileDialog.h"



#if defined(linux) || defined(__linux) || defined(__linux__)
namespace
{
#if SIMPLE_FILE_DIALOG_SD_BUS
/// Result of portal request filled by Response signal handler.
struct PortalResponse
{
    bool received{false};
    /// Response code: 0 - success, 1 - cancelled by user, 2 - other error.
    uint32_t code{2};
    std::string uri;
};



/// Convert "file://" URI to local path (with percent-decoding).
std::string uriToPath(const std::string& uri)
{
    const std::string prefix = "file://";
    if (uri.compare(0, prefix.size(), prefix) != 0)
        return "";

    std::string path;
    for (size_t i = prefix.size(); i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size() &&
            isxdigit((unsigned char)uri[i + 1]) &&
            isxdigit((unsigned char)uri[i + 2]))
        {
            path += (char)std::stoi(uri.substr(i + 1, 2), nullptr, 16);
            i += 2;
        }
        else
        {
            path += uri[i];
        }
    }
    return path;
}



/// Handler of org.freedesktop.portal.Request.Response signal.
int onResponse(sd_bus_message* m, void* userdata, sd_bus_error*)
{
    PortalResponse* response = (PortalResponse*)userdata;
    response->received = true;

    if (sd_bus_message_read(m, "u", &response->code) < 0 ||
        response->code != 0)
        return 0;

    // Results dictionary a{sv}. Only "uris" (as) entry is needed.
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0)
        return 0;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0)
    {
        const char* key = nullptr;
        if (sd_bus_message_read(m, "s", &key) < 0)
            return 0;
        if (strcmp(key, "uris") == 0)
        {
            const char* uri = nullptr;
            if (sd_bus_message_enter_container(m, 'v', "as") > 0 &&
                sd_bus_message_enter_container(m, 'a', "s") > 0 &&
                sd_bus_message_read(m, "s", &uri) > 0)
                response->uri = uri;
            return 0;
        }
        if (sd_bus_message_skip(m, "v") < 0 ||
            sd_bus_message_exit_container(m) < 0)
            return 0;
    }
    return 0;
}



/**
 * @brief Show dialog via org.freedesktop.portal.FileChooser (D-Bus).
 * @param file Chosen file or empty string if dialog cancelled by user.
 * @return TRUE if dialog was shown and closed by user (file chosen or
 * cancelled), FALSE if portal is not available or failed to show dialog.
 */
bool portalDialog(std::string& file)
{
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0)
        return false;

    // Portal creates request object with path
    // /org/freedesktop/portal/desktop/request/SENDER/TOKEN, where SENDER is
    // unique bus name without ':' and with '.' replaced by '_'. Subscribe to
    // Response signal before OpenFile call to not miss fast response.
    const char* uniqueName = nullptr;
    if (sd_bus_get_unique_name(bus, &uniqueName) < 0)
    {
        sd_bus_flush_close_unref(bus);
        return false;
    }
    std::string sender(uniqueName + 1);
    for (char& c : sender)
        if (c == '.')
            c = '_';
    const std::string token = "SimpleFileDialog";
    const std::string requestPath =
    "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

    PortalResponse response;
    sd_bus_slot* slot = nullptr;
    int r = sd_bus_match_signal(bus, &slot, nullptr, requestPath.c_str(),
                                "org.freedesktop.portal.Request", "Response",
                                onResponse, &response);

    // Fails if portal service or FileChooser backend is not installed.
    sd_bus_message* reply = nullptr;
    if (r >= 0)
        r = sd_bus_call_method(bus, "org.freedesktop.portal.Desktop",
                               "/org/freedesktop/portal/desktop",
                               "org.freedesktop.portal.FileChooser", "OpenFile",
                               nullptr, &reply, "ssa{sv}", "", "Open file",
                               1, "handle_token", "s", token.c_str());

    // Old portal versions ignore handle_token and return other request path.
    const char* handle = nullptr;
    if (r >= 0)
        r = sd_bus_message_read(reply, "o", &handle);
    if (r >= 0 && requestPath != handle)
    {
        slot = sd_bus_slot_unref(slot);
        r = sd_bus_match_signal(bus, &slot, nullptr, handle,
                                "org.freedesktop.portal.Request", "Response",
                                onResponse, &response);
    }
    sd_bus_message_unref(reply);

    // Wait until user closes dialog.
    while (r >= 0 && !response.received)
    {
        r = sd_bus_process(bus, nullptr);
        if (r == 0)
            r = sd_bus_wait(bus, UINT64_MAX);
    }

    sd_bus_slot_unref(slot);
    sd_bus_flush_close_unref(bus);

    // Code 2 means dialog was not shown (backend error).
    if (!response.received || response.code > 1)
        return false;
    file = uriToPath(response.uri);
    return true;
}
#endif



/**
 * @brief Check if program is available (can be found by "which" in PATH).
 * @param program Program name. Passed to shell as is, must not contain
 * special shell characters.
 * @return TRUE if program found, FALSE otherwise.
 */
bool checkProgram(const std::string& program)
{
    const std::string command = "which " + program;
    char* const argv[] = {(char*)"/bin/sh", (char*)"-c",
                          (char*)command.c_str(), nullptr};

    // Suppress output of "which".
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);

    pid_t pid = 0;
    int r = posix_spawn(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (r != 0)
        return false;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}



/**
 * @brief Show dialog via zenity application.
 * @param file Chosen file or empty string if dialog cancelled by user.
 * @return TRUE if dialog was shown and closed by user (file chosen or
 * cancelled), FALSE if portal is not available or failed to show dialog.
 */
bool zenityDialog(std::string& file)
{
    if (!checkProgram("zenity"))
        return false;

    FILE* f = popen("zenity --file-selection", "r");
    if (f == nullptr)
        return false;

    // zenity prints nothing if dialog cancelled.
    char filename[4096];
    if (fgets(filename, sizeof(filename), f) != nullptr)
    {
        file = filename;
        if (!file.empty() && file.back() == '\n')
            file.pop_back();
    }
    pclose(f);
    return true;
}
}
#endif // defined(linux) || defined(__linux) || defined(__linux__)



std::string cr::utils::SimpleFileDialog::dialog()
{
#if defined(linux) || defined(__linux) || defined(__linux__)
#if SIMPLE_FILE_DIALOG_SD_BUS
    // zenity is used only if portal is not available or failed to show
    // dialog. If user cancelled portal dialog zenity is not shown.
    std::string file;
    if (portalDialog(file))
        return file;
#endif
    if (zenityDialog(file))
        return file;
#else
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr;
    PWSTR file;
    char* filename;

    hr = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(CLSID_FileOpenDialog,
                              NULL, CLSCTX_ALL,
                              IID_IFileOpenDialog,
                              (void**)&pFileOpen);
        if (SUCCEEDED(hr))
        {
            pFileOpen->SetTitle(L"OPEN VIDEO FILE");
            hr = pFileOpen->Show(NULL);
            if (SUCCEEDED(hr))
            {
                IShellItem* pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr))
                {
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &file);
                    if (SUCCEEDED(hr))
                    {
                        pItem->Release();
                        int count = WideCharToMultiByte(
                        CP_ACP, 0, file, (int)wcslen(file), 0, 0, NULL, NULL);
                        filename = new char[(size_t)count + 1];
                        WideCharToMultiByte(CP_ACP, 0, file, count, filename,
                                            count + 1, NULL, NULL);
                        filename[(size_t)count] = '\0';
                        std::string file(filename);
                        delete[] filename;
                        return file;
                    }
                }
                pItem->Release();
                return "";
            }
        }
    }
    pFileOpen->Release();
    return "";
#endif
}
