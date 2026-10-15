#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
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

// The library source is compiled into this test with test-only settings, the
// library is not linked. SIMPLE_FILE_DIALOG_TRUSTED_DIRS is the directory of
// the fake zenity program (the fallback backend) which the test writes and
// removes. The XDG desktop portal is a fake service on a private D-Bus
// session bus, run the test with:
//     dbus-run-session -- ./SimpleFileDialogPortalTest [test name filter]
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

/// Directory of the fake zenity (the trusted directory of this test).
const std::string g_trustedDir{SIMPLE_FILE_DIALOG_TRUSTED_DIRS};
/// Calls of the fake zenity.
const std::string g_zenityCallsFile{g_trustedDir + "_zenity.calls"};
/// Result of the fallback.
const std::string g_fallbackFile{"/fallback/zenity.mp4"};
/// Name of the portal.
constexpr const char* FAKE_PORTAL_NAME{"org.freedesktop.portal.Desktop"};
/// Object path of the portal.
constexpr const char* FAKE_PORTAL_PATH{"/org/freedesktop/portal/desktop"};



/// Behaviour of the fake portal.
enum class Mode : std::uint8_t
{
    /// Reply to OpenFile, then the Response signal.
    RESPOND,
    /// Response signal before the reply.
    RESPOND_FIRST,
    /// Reply with another request path (a portal which ignores the request
    /// token). A response with another URI on the expected path is sent first,
    /// then the real Response on the returned path.
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
    std::ifstream file(g_zenityCallsFile, std::ios::binary);
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
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_titles;
    }

    /// Tokens of the requests.
    std::vector<std::string> tokens() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_tokens;
    }

private:

    /// Thread function.
    void run(std::promise<bool>& ready)
    {
        sd_bus* forger{nullptr};
        if (sd_bus_open_user(&m_bus) < 0 || sd_bus_open_user(&forger) < 0 ||
            sd_bus_request_name(m_bus, FAKE_PORTAL_NAME, 0) < 0 ||
            sd_bus_add_object(m_bus, nullptr, FAKE_PORTAL_PATH, onMessage, this) < 0)
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
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_titles.emplace_back(title);
            m_tokens.push_back(token);
        }

        const char* sender{sd_bus_message_get_sender(message)};
        std::string escapedSender{sender + 1};
        std::replace(escapedSender.begin(), escapedSender.end(), '.', '_');
        const std::string handle{std::string(FAKE_PORTAL_PATH) + "/request/" + escapedSender + "/" +
                                 token};
        const std::string otherHandle{std::string(FAKE_PORTAL_PATH) + "/request/" + escapedSender +
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
            sendResponse(m_bus, sender, std::string(FAKE_PORTAL_PATH) + "/request/other/x", wrong,
                         token, false);
            sendResponse(m_bus, sender, FAKE_PORTAL_PATH, wrong, token, false);
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



/// Remove a file.
/// @return TRUE if the file does not exist any more.
bool removeFile(const std::string& path)
{
    return unlink(path.c_str()) == 0 || errno == ENOENT;
}



/// Remove the fake zenity, the file with its calls and the trusted directory.
/// @return TRUE if all of them do not exist any more.
bool removeTestFiles()
{
    const bool zenityRemoved{removeFile(g_trustedDir + "/zenity")};
    const bool callsRemoved{removeFile(g_zenityCallsFile)};
    const bool directoryRemoved{
            rmdir(g_trustedDir.c_str()) == 0 || errno == ENOENT};
    return zenityRemoved && callsRemoved && directoryRemoved;
}



/// Call the dialog, the test ends with code 2 if it does not return in time.
std::string dialogWithTimeout(const std::string& title, int seconds = 20)
{
    std::future<std::string> result{std::async(std::launch::async, [title]()
    {
        return cr::utils::SimpleFileDialog::dialog(title);
    })};
    if (result.wait_for(std::chrono::seconds(seconds)) !=
        std::future_status::ready)
    {
        std::cout << "HANG: the dialog did not return within " << seconds
                  << " s" << std::endl;
        static_cast<void>(removeTestFiles());
        _exit(2);
    }
    return result.get();
}



/// Run the dialog with a fake portal.
std::string runScenario(const Scenario& scenario,
                        const std::string& title = "Portal test",
                        std::vector<std::string>* titles = nullptr,
                        std::vector<std::string>* tokens = nullptr,
                        int seconds = 20)
{
    static_cast<void>(unlink(g_zenityCallsFile.c_str()));
    FakePortal portal(scenario);
    if (!portal.start())
    {
        std::cout << "The portal name can not be acquired: run the test with "
                     "dbus-run-session" << std::endl;
        static_cast<void>(removeTestFiles());
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


/// Filter of the test names (first argument of the command line): only the
/// tests with the filter in the name are run, all tests if it is empty.
std::string g_testFilter;



/// Files of the test: the trusted directory with the fake zenity (the
/// fallback backend). They are removed at the end of the scope, a file which
/// can not be removed is a failed check.
class TestFiles
{
public:

    /// Create the trusted directory and the fake zenity (files of an
    /// interrupted run are removed first).
    TestFiles()
    {
        static_cast<void>(removeTestFiles());
        CHECK(mkdir(g_trustedDir.c_str(), 0755) == 0);
        CHECK(chmod(g_trustedDir.c_str(), 0755) == 0);
        const std::string zenity{g_trustedDir + "/zenity"};
        writeFile(zenity, "#!/bin/sh\necho called >> '" + g_zenityCallsFile +
                          "'\necho " + g_fallbackFile + "\n");
        CHECK(chmod(zenity.c_str(), 0755) == 0);
    }

    /// Remove the files.
    ~TestFiles()
    {
        CHECK(removeTestFiles());
    }

    TestFiles(const TestFiles&) = delete;
    TestFiles(TestFiles&&) = delete;
    TestFiles& operator=(const TestFiles&) = delete;
    TestFiles& operator=(TestFiles&&) = delete;
};



/// Run one test and print the result: OK if no check of the test failed.
void runTest(const char* name, void (*test)())
{
    std::cout << name << ":" << std::endl;
    const int failedBefore{g_failCount};
    test();
    std::cout << (g_failCount == failedBefore ? "OK" : "ERROR") << std::endl
              << std::endl;
}



/// Valid responses: the file, the title, the request token.
void testSuccess()
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
    const std::string file{
            runScenario(Scenario{}, "Title", nullptr, &nextTokens)};
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



/// The title is valid UTF-8 without control characters (sd-bus rejects
/// invalid UTF-8: the portal call would fail).
void testTitle()
{
    std::vector<std::string> titles;
    CHECK(runScenario(Scenario{}, "A\nB\x01" "C \xFF \xEF\xBF\xBE end",
                      &titles).compare(0, 5, "/tmp/") == 0);
    CHECK(zenityCalls() == 0);
    CHECK(titles.size() == 1 &&
          titles.at(0) ==
          "A B C \xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD end");
}



/// The user cancelled the dialog: no fallback.
void testCancel()
{
    Scenario scenario;
    scenario.code = 1;
    CHECK(runScenario(scenario).empty());
    CHECK(zenityCalls() == 0);
}



/// Errors of the portal: the fallback is used.
void testError()
{
    Scenario scenario;
    scenario.code = 2;
    CHECK(runScenario(scenario) == g_fallbackFile);
    CHECK(zenityCalls() == 1);
    scenario.code = 77;
    CHECK(runScenario(scenario) == g_fallbackFile);
    scenario = Scenario{};
    scenario.mode = Mode::ERROR_REPLY;
    CHECK(runScenario(scenario) == g_fallbackFile);
    scenario.mode = Mode::BAD_SIGNATURE;
    CHECK(runScenario(scenario) == g_fallbackFile);
}



/// Invalid results: the fallback is used.
void testResults()
{
    Scenario scenario;
    scenario.urisType = "";
    CHECK(runScenario(scenario) == g_fallbackFile);
    scenario.urisType = "s";
    CHECK(runScenario(scenario) == g_fallbackFile);
    scenario.urisType = "as";
    scenario.uris = {};
    CHECK(runScenario(scenario) == g_fallbackFile);
    scenario.uris = {"file:///tmp/a.mp4", "file:///tmp/b.mp4"};
    CHECK(runScenario(scenario) == g_fallbackFile);
}



/// Invalid URIs: the fallback is used.
void testUri()
{
    for (const std::string& uri : std::vector<std::string>{
             "file:///tmp/a%00b.mp4", "file:///tmp/a%0Ab.mp4",
             "file:///tmp/a%1B[2J", "file:///tmp/a%7F",
             "file://evil.example.com/etc/passwd", "file:relative.mp4",
             "ftp://localhost/tmp/x.mp4", "file:///tmp/%zz",
             "file:///tmp/%4", "file:///tmp/a?b", "file:///tmp/a#b", "",
             "file://", "file:///" + std::string(5000, 'a'),
             "file:///" + std::string(13000, 'a')})
    {
        Scenario scenario;
        scenario.uris = {uri};
        CHECK(runScenario(scenario) == g_fallbackFile);
        CHECK(zenityCalls() == 1);
    }
}



/// The order of the reply and of the response.
/// @param mode Mode of the fake portal.
void testOrder(Mode mode)
{
    Scenario scenario;
    scenario.uris = {"file:///tmp/ordered.mp4"};
    scenario.mode = mode;
    CHECK(runScenario(scenario) == "/tmp/ordered.mp4");
    CHECK(zenityCalls() == 0);
}



/// Responses of another client of the bus are ignored.
void testForged()
{
    Scenario scenario;
    scenario.uris = {"file:///tmp/real.mp4"};
    scenario.mode = Mode::FORGED_FIRST;
    CHECK(runScenario(scenario) == "/tmp/real.mp4");
    CHECK(zenityCalls() == 0);
}



/// The portal leaves the bus while the dialog is open: no hang.
void testVanish()
{
    Scenario scenario;
    scenario.mode = Mode::VANISH;
    CHECK(runScenario(scenario) == g_fallbackFile);
    CHECK(zenityCalls() == 1);
}



/// No portal on the bus: the fallback is used.
void testNoPortal()
{
    static_cast<void>(unlink(g_zenityCallsFile.c_str()));
    CHECK(dialogWithTimeout("No portal") == g_fallbackFile);
    CHECK(zenityCalls() == 1);
}



/// Two dialogs at the same time: separate requests and results.
void testThreads()
{
    static_cast<void>(unlink(g_zenityCallsFile.c_str()));
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
        CHECK((results == std::set<std::string>{
                  "/tmp/" + tokens.at(0) + ".mp4",
                  "/tmp/" + tokens.at(1) + ".mp4"}));
    }
    CHECK(zenityCalls() == 0);
}



/// OpenFile is never answered: the call times out (25 s), the fallback is
/// used.
void testTimeout()
{
    Scenario scenario;
    scenario.mode = Mode::NO_REPLY;
    CHECK(runScenario(scenario, "Timeout", nullptr, nullptr, 60) ==
          g_fallbackFile);
}



/// The bus daemon stops while the dialog is open: no hang. The test needs the
/// process ID of the daemon in SFD_TEST_DBUS_PID and is skipped without it.
void testBusGone()
{
    const char* daemonPid{getenv("SFD_TEST_DBUS_PID")};
    char* end{nullptr};
    const long value{daemonPid != nullptr ? std::strtol(daemonPid, &end, 10)
                                          : 0};
    if (daemonPid == nullptr || end == daemonPid || *end != '\0' || value <= 1)
    {
        std::cout << "Skipped: SFD_TEST_DBUS_PID is not set to a process ID"
                  << std::endl;
        return;
    }
    Scenario scenario;
    scenario.mode = Mode::NO_RESPONSE;
    const pid_t pid{static_cast<pid_t>(value)};
    std::thread killer([pid]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        kill(pid, SIGTERM);
    });
    CHECK(runScenario(scenario) == g_fallbackFile);
    killer.join();
}



/// Test with a name for the filter of the command line.
struct TestCase
{
    /// Name of the test.
    const char* name;
    /// Test function.
    void (*function)();
};

}



/// Entry point: runs the tests (all or the tests with the name filter given
/// as the first argument), exit code 0 if all checks passed, 1 otherwise, 2 if
/// a dialog call hangs. Run it with dbus-run-session.
int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        g_testFilter = argv[1];
    }

    {
        const TestFiles files;
        const std::vector<TestCase> tests{
            {"success", testSuccess},
            {"title", testTitle},
            {"cancel", testCancel},
            {"error", testError},
            {"results", testResults},
            {"uri", testUri},
            {"order_respond_first",
             []() { testOrder(Mode::RESPOND_FIRST); }},
            {"order_other_handle",
             []() { testOrder(Mode::OTHER_HANDLE); }},
            {"order_wrong_path",
             []() { testOrder(Mode::WRONG_PATH_FIRST); }},
            {"forged", testForged},
            {"vanish", testVanish},
            {"noportal", testNoPortal},
            {"threads", testThreads},
            {"timeout", testTimeout},
            {"busgone", testBusGone}};
        for (const TestCase& test : tests)
        {
            if (g_testFilter.empty() ||
                std::string(test.name).find(g_testFilter) != std::string::npos)
            {
                runTest(test.name, test.function);
            }
        }
    }

    std::cout << "Checks: " << g_checkCount << ", failed: " << g_failCount
              << std::endl;
    return g_failCount == 0 ? 0 : 1;
}
