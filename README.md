![frame_logo](_static/file_dialog_logo.png)

# **Simple file dialog C++ library**

**v1.0.0**

------

# Overview

**SimpleFileDialog** C++ library provides simple dialog to chose file in Windows and Linux (tested for Ubuntu 22.04, 22.10 and Windows 11). The library used in projects when simple file chose dialog needed. To provide dialog in Linux the library calls "**zenity**" application. The library doesn't have third-party dependencies. **SimpleFileDialog.h** file includes declaration of **SimpleFileDialog** class. **SimpleFileDialog** class includes only one simple static method **dialod()**;

# Versions

**Table 1** - Library versions.

| Version | Release date | What's new     |
| ------- | ------------ | -------------- |
| 1.0.0   | 20.07.2023   | First version. |

# Class description

**SimpleDileDialog** class declared in **SimpleFileDialog.h** file. Class declaration:

```cpp
namespace cr
{
namespace utils
{
/// @brief File dialog class.
class SimpleFileDialog
{
public:
    /// @brief Dialog function.
    static std::string dialog();
};
}
}
```

**SimpleFileDialog** class includes only one static method **dialog()** which shows file chose dialog to user. Method used without **SimpleFileDialog** class instance. Example:

```cpp
#include <iostream>
#include "SimpleFileDialog.h"

int main(void)
{
    // Open file dialog.
    std::string file = cr::utils::SimpleFileDialog::dialog();
    std::cout << "File: " << file << std::endl;

    return -1;
}
```
