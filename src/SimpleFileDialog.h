#pragma once
#include <string>



namespace cr
{
namespace utils
{
/// @brief File dialog class.
class SimpleFileDialog
{
public:

    /**
     * @brief Show a modal dialog to choose an existing file and wait until the
     * user closes it. Windows: common item dialog (COM), the calling thread is
     * initialized as a single-threaded apartment for the time of the call (a
     * thread already initialized as a multithreaded apartment is used as it
     * is). Linux: XDG desktop portal (D-Bus, if built with libsystemd), zenity
     * or kdialog, in this order. Other platforms: no dialog.
     * @param title Dialog title. Control characters are replaced by spaces,
     * the title is cut to 256 bytes. Defaults to "Select a file".
     * @return Full path of the chosen file (UTF-8 on Windows, the bytes of the
     * file name on Linux) or empty string if the dialog was cancelled, no
     * dialog is available or an error occurred. The method never throws.
     */
    static std::string dialog(const std::string& title = "Select a file") noexcept;
};
}
}
