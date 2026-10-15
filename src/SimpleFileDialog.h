#pragma once
#include <string>



namespace cr
{
namespace utils
{
/**
 * @brief File dialog class.
 *
 * The class has only the static method dialog(...), no object is required.
 */
class SimpleFileDialog
{
public:

    /**
     * @brief Show a modal dialog to choose an existing file and wait until
     * the user closes it.
     *
     * Windows: common item dialog (COM). The calling thread is initialized as
     * a single-threaded apartment for the time of the call; a thread already
     * initialized as a multithreaded apartment is used as it is. The dialog
     * has no owner window. Only existing files of the file system can be
     * chosen, the working directory of the process is not changed.
     *
     * Linux: XDG desktop portal (D-Bus, if the library is built with
     * libsystemd), zenity and kdialog, in this order. The next backend is
     * tried only if a backend failed, never after the user cancelled a
     * dialog. zenity and kdialog are searched only in /usr/bin, /bin and
     * /usr/local/bin (not via PATH) and are started without a shell with a
     * filtered environment. The D-Bus session bus is selected by D-Bus in the
     * usual way (DBUS_SESSION_BUS_ADDRESS).
     *
     * Other platforms: no dialog, the result is always an empty string.
     *
     * @note Blocks the calling thread until the dialog is closed. There is no
     * timeout: the user may keep the dialog open for any time.
     * @note Thread-safe: the method can be called from several threads at the
     * same time (each call has its own D-Bus connection, request token and
     * dialog process or its own COM objects). The application must not change
     * its environment variables while the method runs. The method must not be
     * called from a signal handler.
     * @note Linux: the dialog program is a child process of the application.
     * If the application ignores SIGCHLD or reaps all child processes itself,
     * the exit code of the program is lost and the result is an empty string.
     *
     * @param title Dialog title (UTF-8). Control characters are replaced by
     * spaces and the title is cut to 256 bytes, never inside a UTF-8 sequence.
     * Linux: bytes which are not valid UTF-8 are replaced by U+FFFD. Windows:
     * a title which is not valid UTF-8 is converted from the ANSI code page.
     * @return Full path of the chosen file (UTF-8 on Windows, the bytes of the
     * file name on Linux) or empty string if the user cancelled the dialog, no
     * dialog is available or an error occurred. A Linux path is absolute,
     * shorter than PATH_MAX and has no control characters, a Windows path has
     * no control characters. The method never throws.
     */
    static std::string dialog(
            const std::string& title = "Select a file") noexcept;
};
}
}
