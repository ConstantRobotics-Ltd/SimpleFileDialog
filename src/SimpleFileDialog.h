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
     * @return String of file name or empty string if no file selected.
     */
    static std::string dialog();
};
}
}