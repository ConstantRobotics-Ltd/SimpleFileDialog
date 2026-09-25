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
     * @brief Dialog function.
     * @param title Dialog title. Defaults to "Select a file".
     * @return String of file name or empty string if no file selected.
     */
    static std::string dialog(const std::string title = "Select a file");
};
}
}