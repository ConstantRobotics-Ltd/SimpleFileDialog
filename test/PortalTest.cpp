// Tests of the XDG desktop portal backend of the SimpleFileDialog library
// (Linux, built with libsystemd) with a fake portal service on a private D-Bus
// session bus:
//     dbus-run-session -- ./SimpleFileDialogPortalTest [test name filter]
// The fake zenity program in SIMPLE_FILE_DIALOG_TRUSTED_DIRS is the fallback
// backend. If the environment variable SFD_TEST_DBUS_PID holds the process ID
// of the bus daemon, the last test stops the daemon during a dialog.
// Exit code 0 if all checks passed, 1 otherwise, 2 if a dialog call hangs.
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <systemd/sd-bus.h>
#ifndef SIMPLE_FILE_DIALOG_TRUSTED_DIRS
#define SIMPLE_FILE_DIALOG_TRUSTED_DIRS "/tmp/simplefiledialog_portal_test_trusted"
#endif

#include "SimpleFileDialog.cpp"



namespace
{

/// Number of checks.
int checkCount{0};
/// Number of failed checks.
int failCount{0};



/**
 * @brief Record the result of a check.
 * @param condition Result of the check.
 * @param description Checked expression.
 * @param line Source line of the check.
 */
void check(bool condition, const char* description, int line)
{
    ++checkCount;
    if (!condition)
    {
        ++failCount;
        std::cout << "FAILED (line " << line << "): " << description << std::endl;
    }
}

}

/// Check a condition, the expression and the line are printed on failure.
#define CHECK(condition) check((condition), #condition, __LINE__)



namespace
{

/// Directory of the fake zenity (the trusted directory of this test).
const std::string trustedDir{SIMPLE_FILE_DIALOG_TRUSTED_DIRS};
/// Calls of the fake zenity.
const std::string zenityCallsFile{trustedDir + "_zenity.calls"};
/// Result of the fallback.
const std::string fallbackFile{"/fallback/zenity.mp4"};
/// Name of the portal.
const char* const portalName{"org.freedesktop.portal.Desktop"};
/// Object path of the portal.
const char* const portalPath{"/org/freedesktop/portal/desktop"};



/// Behaviour of the fake portal.
enum class Mode
{
    /// Reply to OpenFile, then the Response signal.
    RESPOND,
    /// Response signal before the reply.
    RESPOND_FIRST,
    /// Reply with another request path (old portal), Response on it. Before, a
    /// response on the expected path (not valid any more) is sent.
    OTHER_HANDLE,
    /// Response on another object path first, then on the request path.
    WRONG_PATH_FIRST,
    /// Response from another bus connection first, then the real one.
    FORGED_FIRST,
    /// Error reply to OpenFile.
    ERROR_REPLY,
    /// No reply to OpenFile (the call times out).
    NO_REPLY,
    /// Reply, then the portal leaves the bus without a response.
    VANISH,
    /// Reply, no response.
    NO_RESPONSE,
    /// Reply, then a Response signal with the signature "u".
    BAD_SIGNATURE
};



/// Scenario of the fake portal.
struct Scenario
{
    /// Behaviour.
    Mode mode{Mode::RESPOND};
    /// Response code.
    uint32_t code{0};
    /// URIs of the response, "TOKEN" is replaced by the request token.
    std::vector<std::string> uris{"file:///tmp/TOKEN.mp4"};
    /// Type of the "uris" entry: "as", "s" (wrong type) or empty (no entry).
    std::string urisType{"as"};
    /// Add other entries before and after "uris".
    bool extraEntries{false};
};



/// Write a file.
void writeFile(const std::string& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
}



/// Number of calls of the fake zenity since the last reset.
size_t zenityCalls()
{
    std::ifstream file(zenityCallsFile, std::ios::binary);
    const std::string calls{std::istreambuf_iterator<char>(file),
                            std::istreambuf_iterator<char>()};
    return static_cast<size_t>(std::count(calls.begin(), calls.end(), '\n'));
}



/// Replace "TOKEN" in a string.
std::string replaceToken(std::string text, const std::string& token)
{
    const size_t pos{text.find("TOKEN")};
    if (pos != std::string::npos)
    {
        text.replace(pos, 5, token);
    }
    return text;
}



/// Append the results dictionary (a{sv}) to a response.
int appendResults(sd_bus_message* message, const Scenario& scenario,
                  const std::string& token)
{
    int r{sd_bus_message_open_container(message, 'a', "{sv}")};
    if (r >= 0 && scenario.extraEntries)
    {
        r = sd_bus_message_append(message, "{sv}", "choices", "a(ss)", 1, "encoding", "utf8");
        if (r >= 0)
        {
            r = sd_bus_message_append(message, "{sv}", "current_filter", "(sa(us))", "Videos",
                                      1, 0u, "*.mp4");
        }
    }
    if (r >= 0 && scenario.urisType == "as")
    {
        r = sd_bus_message_open_container(message, 'e', "sv");
        if (r >= 0)
        {
            r = sd_bus_message_append(message, "s", "uris");
        }
        if (r >= 0)
        {
            r = sd_bus_message_open_container(message, 'v', "as");
        }
        if (r >= 0)
        {
            r = sd_bus_message_open_container(message, 'a', "s");
        }
        for (const std::string& uri : scenario.uris)
        {
            if (r >= 0)
            {
                r = sd_bus_message_append(message, "s", replaceToken(uri, token).c_str());
            }
        }
        for (int i = 0; i < 3 && r >= 0; ++i)
        {
            r = sd_bus_message_close_container(message);
        }
    }
    else if (r >= 0 && scenario.urisType == "s")
    {
        r = sd_bus_message_append(message, "{sv}", "uris", "s",
                                  replaceToken(scenario.uris.at(0), token).c_str());
    }
    if (r >= 0 && scenario.extraEntries)
    {
        r = sd_bus_message_append(message, "{sv}", "writable", "b", 1);
    }
    if (r >= 0)
    {
        r = sd_bus_message_close_container(message);
    }
    return r;
}



/// Send a Response signal directly to a client.
int sendResponse(sd_bus* bus, const char* destination, const std::string& path,
                 const Scenario& scenario, const std::string& token, bool badSignature)
{
    sd_bus_message* message{nullptr};
    int r{sd_bus_message_new_signal(bus, &message, path.c_str(),
                                    "org.freedesktop.portal.Request", "Response")};
    if (r >= 0)
    {
        r = sd_bus_message_set_destination(message, destination);
    }
    if (r >= 0)
    {
        r = sd_bus_message_append(message, "u", scenario.code);
    }
    if (r >= 0 && !badSignature)
    {
        r = appendResults(message, scenario, token);
    }
    if (r >= 0)
    {
        r = sd_bus_send(bus, message, nullptr);
    }
    sd_bus_message_unref(message);
    if (r >= 0)
    {
        r = sd_bus_flush(bus);
    }
    return r;
}



/// Fake XDG desktop portal (FileChooser.OpenFile) running in its own thread.
class FakePortal
{
public:

    /// Create the portal with a scenario.
    explicit FakePortal(const Scenario& scenario) : m_scenario(scenario)
    {
    }

    /// Stop the portal.
    ~FakePortal()
    {
        stop();
    }

    FakePortal(const FakePortal&) = delete;
    FakePortal& operator=(const FakePortal&) = delete;

    /// Start the portal thread, TRUE if the portal name was acquired.
    bool start()
    {
        std::promise<bool> ready;
        std::future<bool> started{ready.get_future()};
        m_thread = std::thread([this, promise = std::move(ready)]() mutable
        {
            run(promise);
        });
        return started.get();
    }

    /// Stop the portal thread.
    void stop()
    {
        m_stop = true;
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    /// Titles of the requests.
    std::vector<std::string> titles() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_titles;
    }

    /// Tokens of the requests.
    std::vector<std::string> tokens() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tokens;
    }

private:

    /// Thread function.
    void run(std::promise<bool>& ready)
    {
        sd_bus* forger{nullptr};
        if (sd_bus_open_user(&m_bus) < 0 || sd_bus_open_user(&forger) < 0 ||
            sd_bus_request_name(m_bus, portalName, 0) < 0 ||
            sd_bus_add_object(m_bus, nullptr, portalPath, onMessage, this) < 0)
        {
            sd_bus_flush_close_unref(m_bus);
            sd_bus_flush_close_unref(forger);
            m_bus = nullptr;
            ready.set_value(false);
            return;
        }
        m_forger = forger;
        ready.set_value(true);
        while (!m_stop && !m_vanish)
        {
            const int processed{sd_bus_process(m_bus, nullptr)};
            if (processed < 0)
            {
                break;
            }
            if (processed == 0)
            {
                sd_bus_wait(m_bus, 50000);
            }
        }
        sd_bus_flush_close_unref(m_bus);
        sd_bus_flush_close_unref(m_forger);
        m_bus = nullptr;
        m_forger = nullptr;
    }

    /// Method call handler.
    static int onMessage(sd_bus_message* message, void* userdata, sd_bus_error*)
    {
        if (sd_bus_message_is_method_call(message, "org.freedesktop.portal.FileChooser",
                                          "OpenFile") <= 0)
        {
            return 0;
        }
        static_cast<FakePortal*>(userdata)->openFile(message);
        return 1;
    }

    /// OpenFile call.
    void openFile(sd_bus_message* message)
    {
        const char* parent{nullptr};
        const char* title{nullptr};
        std::string token;
        if (sd_bus_message_read(message, "ss", &parent, &title) <= 0 ||
            sd_bus_message_enter_container(message, 'a', "{sv}") <= 0)
        {
            sd_bus_reply_method_errorf(message, "org.freedesktop.portal.Error.InvalidArgument",
                                       "bad arguments");
            return;
        }
        while (sd_bus_message_enter_container(message, 'e', "sv") > 0)
        {
            const char* key{nullptr};
            sd_bus_message_read(message, "s", &key);
            if (key != nullptr && strcmp(key, "handle_token") == 0)
            {
                const char* value{nullptr};
                sd_bus_message_read(message, "v", "s", &value);
                token = value != nullptr ? value : "";
            }
            else
            {
                sd_bus_message_skip(message, "v");
            }
            sd_bus_message_exit_container(message);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_titles.emplace_back(title);
            m_tokens.push_back(token);
        }

        const char* sender{sd_bus_message_get_sender(message)};
        std::string escapedSender{sender + 1};
        std::replace(escapedSender.begin(), escapedSender.end(), '.', '_');
        const std::string handle{std::string(portalPath) + "/request/" + escapedSender + "/" +
                                 token};
        const std::string otherHandle{std::string(portalPath) + "/request/" + escapedSender +
                                      "/old" + token};

        switch (m_scenario.mode)
        {
        case Mode::ERROR_REPLY:
            sd_bus_reply_method_errorf(message, "org.freedesktop.portal.Error.Failed",
                                       "test error");
            return;
        case Mode::NO_REPLY:
            return;
        case Mode::RESPOND_FIRST:
            sendResponse(m_bus, sender, handle, m_scenario, token, false);
            sd_bus_reply_method_return(message, "o", handle.c_str());
            return;
        case Mode::OTHER_HANDLE:
        {
            sd_bus_reply_method_return(message, "o", otherHandle.c_str());
            Scenario stale{m_scenario};
            stale.uris = {"file:///stale.mp4"};
            sendResponse(m_bus, sender, handle, stale, token, false);
            sendResponse(m_bus, sender, otherHandle, m_scenario, token, false);
            return;
        }
        default:
            break;
        }

        sd_bus_reply_method_return(message, "o", handle.c_str());
        sd_bus_flush(m_bus);
        if (m_scenario.mode == Mode::WRONG_PATH_FIRST)
        {
            Scenario wrong{m_scenario};
            wrong.uris = {"file:///wrong.mp4"};
            sendResponse(m_bus, sender, std::string(portalPath) + "/request/other/x", wrong,
                         token, false);
            sendResponse(m_bus, sender, portalPath, wrong, token, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (m_scenario.mode == Mode::FORGED_FIRST)
        {
            Scenario forged{m_scenario};
            forged.uris = {"file:///forged.mp4"};
            sendResponse(m_forger, sender, handle, forged, token, false);
            forged.code = 1;
            sendResponse(m_forger, sender, handle, forged, token, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (m_scenario.mode == Mode::VANISH)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            m_vanish = true;
            return;
        }
        if (m_scenario.mode == Mode::NO_RESPONSE)
        {
            return;
        }
        sendResponse(m_bus, sender, handle, m_scenario, token,
                     m_scenario.mode == Mode::BAD_SIGNATURE);
    }

    /// Scenario.
    const Scenario m_scenario;
    /// Stop request.
    std::atomic<bool> m_stop{false};
    /// The portal leaves the bus (portal thread only).
    bool m_vanish{false};
    /// Portal thread.
    std::thread m_thread;
    /// Bus connection of the portal (portal thread only).
    sd_bus* m_bus{nullptr};
    /// Other bus connection which forges responses (portal thread only).
    sd_bus* m_forger{nullptr};
    /// Mutex of the request records.
    mutable std::mutex m_mutex;
    /// Titles of the requests.
    std::vector<std::string> m_titles;
    /// Tokens of the requests.
    std::vector<std::string> m_tokens;
};



/// Call the dialog, the test ends with code 2 if it does not return in time.
std::string dialogWithTimeout(const std::string& title, int seconds = 20)
{
    std::future<std::string> result{std::async(std::launch::async, [title]()
    {
        return cr::utils::SimpleFileDialog::dialog(title);
    })};
    if (result.wait_for(std::chrono::seconds(seconds)) != std::future_status::ready)
    {
        std::cout << "HANG: the dialog did not return within " << seconds << " s"
                  << std::endl;
        _exit(2);
    }
    return result.get();
}



/// Run the dialog with a fake portal.
std::string runScenario(const Scenario& scenario, const std::string& title = "Portal test",
                        std::vector<std::string>* titles = nullptr,
                        std::vector<std::string>* tokens = nullptr, int seconds = 20)
{
    static_cast<void>(unlink(zenityCallsFile.c_str()));
    FakePortal portal(scenario);
    if (!portal.start())
    {
        std::cout << "The portal name can not be acquired: run the test with "
                     "dbus-run-session" << std::endl;
        _exit(1);
    }
    const std::string file{dialogWithTimeout(title, seconds)};
    portal.stop();
    if (titles != nullptr)
    {
        *titles = portal.titles();
    }
    if (tokens != nullptr)
    {
        *tokens = portal.tokens();
    }
    return file;
}



/// Test name filter (first argument).
std::string testFilter;



/// Check if a test is selected.
bool selected(const std::string& name)
{
    const bool run{testFilter.empty() || name.find(testFilter) != std::string::npos};
    if (run)
    {
        std::cout << "Test: " << name << std::endl;
    }
    return run;
}

}



/// Entry point.
int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        testFilter = argv[1];
    }
    const char* daemonPid{getenv("SFD_TEST_DBUS_PID")};

    // Fake zenity: the fallback. Also found via PATH by older versions.
    CHECK(mkdir(trustedDir.c_str(), 0755) == 0 || errno == EEXIST);
    CHECK(chmod(trustedDir.c_str(), 0755) == 0);
    const std::string zenity{trustedDir + "/zenity"};
    writeFile(zenity, "#!/bin/sh\necho called >> '" + zenityCallsFile + "'\necho " +
                      fallbackFile + "\n");
    CHECK(chmod(zenity.c_str(), 0755) == 0);
    const char* path{getenv("PATH")};
    CHECK(setenv("PATH", (trustedDir + ":" + (path != nullptr ? path : "")).c_str(), 1) == 0);

    if (selected("success"))
    {
        std::vector<std::string> titles;
        std::vector<std::string> tokens;
        Scenario scenario;
        scenario.uris = {"file:///tmp/video%20file%27s.mp4"};
        CHECK(runScenario(scenario, "Select a video file", &titles, &tokens) ==
              "/tmp/video file's.mp4");
        CHECK(zenityCalls() == 0);
        CHECK(titles.size() == 1 && titles.at(0) == "Select a video file");
        CHECK(tokens.size() == 1 && !tokens.at(0).empty());
        std::vector<std::string> nextTokens;
        const std::string file{runScenario(Scenario{}, "Title", nullptr, &nextTokens)};
        CHECK(nextTokens.size() == 1 && tokens.size() == 1);
        if (nextTokens.size() == 1 && tokens.size() == 1)
        {
            CHECK(file == "/tmp/" + nextTokens.at(0) + ".mp4");
            CHECK(nextTokens.at(0) != tokens.at(0));
        }
        scenario.uris = {"file://localhost/tmp/%FF%FE%20x.mp4"};
        CHECK(runScenario(scenario) == "/tmp/\xFF\xFE x.mp4");
        scenario.extraEntries = true;
        CHECK(runScenario(scenario) == "/tmp/\xFF\xFE x.mp4");
    }

    if (selected("title"))
    {
        // The title is valid UTF-8 without control characters (sd-bus rejects
        // invalid UTF-8: the portal call would fail).
        std::vector<std::string> titles;
        CHECK(runScenario(Scenario{}, "A\nB\x01" "C \xFF \xEF\xBF\xBE end", &titles).compare(
                  0, 5, "/tmp/") == 0);
        CHECK(zenityCalls() == 0);
        CHECK(titles.size() == 1 &&
              titles.at(0) == "A B C \xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD end");
    }

    if (selected("cancel"))
    {
        Scenario scenario;
        scenario.code = 1;
        CHECK(runScenario(scenario).empty());
        CHECK(zenityCalls() == 0);
    }

    if (selected("error"))
    {
        Scenario scenario;
        scenario.code = 2;
        CHECK(runScenario(scenario) == fallbackFile);
        CHECK(zenityCalls() == 1);
        scenario.code = 77;
        CHECK(runScenario(scenario) == fallbackFile);
        scenario = Scenario{};
        scenario.mode = Mode::ERROR_REPLY;
        CHECK(runScenario(scenario) == fallbackFile);
        scenario.mode = Mode::BAD_SIGNATURE;
        CHECK(runScenario(scenario) == fallbackFile);
    }

    if (selected("results"))
    {
        // Invalid results: the fallback is used.
        Scenario scenario;
        scenario.urisType = "";
        CHECK(runScenario(scenario) == fallbackFile);
        scenario.urisType = "s";
        CHECK(runScenario(scenario) == fallbackFile);
        scenario.urisType = "as";
        scenario.uris = {};
        CHECK(runScenario(scenario) == fallbackFile);
        scenario.uris = {"file:///tmp/a.mp4", "file:///tmp/b.mp4"};
        CHECK(runScenario(scenario) == fallbackFile);
    }

    if (selected("uri"))
    {
        for (const std::string& uri : std::vector<std::string>{
                 "file:///tmp/a%00b.mp4", "file:///tmp/a%0Ab.mp4", "file:///tmp/a%1B[2J",
                 "file:///tmp/a%7F", "file://evil.example.com/etc/passwd", "file:relative.mp4",
                 "http://localhost/tmp/x.mp4", "file:///tmp/%zz", "file:///tmp/%4",
                 "file:///tmp/a?b", "file:///tmp/a#b", "", "file://",
                 "file:///" + std::string(5000, 'a'),
                 "file:///" + std::string(13000, 'a')})
        {
            Scenario scenario;
            scenario.uris = {uri};
            CHECK(runScenario(scenario) == fallbackFile);
            CHECK(zenityCalls() == 1);
        }
    }

    for (const Mode mode : {Mode::RESPOND_FIRST, Mode::OTHER_HANDLE, Mode::WRONG_PATH_FIRST})
    {
        if (selected("order" + std::to_string(static_cast<int>(mode))))
        {
            Scenario scenario;
            scenario.uris = {"file:///tmp/ordered.mp4"};
            scenario.mode = mode;
            CHECK(runScenario(scenario) == "/tmp/ordered.mp4");
            CHECK(zenityCalls() == 0);
        }
    }

    if (selected("forged"))
    {
        // Responses of another client of the bus are ignored.
        Scenario scenario;
        scenario.uris = {"file:///tmp/real.mp4"};
        scenario.mode = Mode::FORGED_FIRST;
        CHECK(runScenario(scenario) == "/tmp/real.mp4");
        CHECK(zenityCalls() == 0);
    }

    if (selected("vanish"))
    {
        // The portal leaves the bus while the dialog is open: no hang.
        Scenario scenario;
        scenario.mode = Mode::VANISH;
        CHECK(runScenario(scenario) == fallbackFile);
        CHECK(zenityCalls() == 1);
    }

    if (selected("noportal"))
    {
        // No portal on the bus.
        static_cast<void>(unlink(zenityCallsFile.c_str()));
        CHECK(dialogWithTimeout("No portal") == fallbackFile);
        CHECK(zenityCalls() == 1);
    }

    if (selected("threads"))
    {
        // Two dialogs at the same time: separate requests and results.
        static_cast<void>(unlink(zenityCallsFile.c_str()));
        FakePortal portal{Scenario{}};
        CHECK(portal.start());
        std::future<std::string> first{std::async(std::launch::async, []()
        {
            return dialogWithTimeout("First");
        })};
        std::future<std::string> second{std::async(std::launch::async, []()
        {
            return dialogWithTimeout("Second");
        })};
        const std::set<std::string> results{first.get(), second.get()};
        portal.stop();
        const std::vector<std::string> tokens{portal.tokens()};
        CHECK(tokens.size() == 2);
        if (tokens.size() == 2)
        {
            CHECK(tokens.at(0) != tokens.at(1));
            CHECK((results == std::set<std::string>{"/tmp/" + tokens.at(0) + ".mp4",
                                                    "/tmp/" + tokens.at(1) + ".mp4"}));
        }
        CHECK(zenityCalls() == 0);
    }

    if (selected("timeout"))
    {
        // OpenFile is never answered: the call times out (25 s), fallback.
        Scenario scenario;
        scenario.mode = Mode::NO_REPLY;
        CHECK(runScenario(scenario, "Timeout", nullptr, nullptr, 60) == fallbackFile);
    }

    if (daemonPid != nullptr && selected("busgone"))
    {
        // The bus daemon stops while the dialog is open: no hang.
        Scenario scenario;
        scenario.mode = Mode::NO_RESPONSE;
        const pid_t pid{static_cast<pid_t>(std::atoi(daemonPid))};
        std::thread killer([pid]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            kill(pid, SIGTERM);
        });
        CHECK(runScenario(scenario) == fallbackFile);
        killer.join();
    }

    std::cout << "Checks: " << checkCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
