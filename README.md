![simplefiledialog_logo](./static/simplefiledialog_web_logo.png)



# **SimpleFileDialog C++ library**

**v1.1.0**



# Table of contents

- [Overview](#overview)
- [Versions](#versions)
- [Library files](#library-files)
- [Class declaration](#class-declaration)
- [Build and connect to your project](#build-and-connect-to-your-project)



# Overview

**SimpleFileDialog** C++ library provides a simple file selection dialog for Windows and Linux (tested on Ubuntu 22.04, 22.10, 24.04 and Windows 11). The library is used in projects where a simple file selection dialog is needed. On Linux the library shows the dialog using the **XDG Desktop Portal** FileChooser interface via **D-Bus**, the **zenity** application or the **kdialog** application. The library uses the C++17 standard. The D-Bus backend requires the optional **libsystemd** library on Linux. If it is not found at build time, the D-Bus backend is disabled. The library has no other third-party dependencies. **SimpleFileDialog.h** file includes the declaration of the **SimpleFileDialog** class. **SimpleFileDialog** class includes only one static method **dialog(...)**. The library is licensed under the **Apache 2.0** license.



# Versions

**Table 1** - Library versions.

| Version | Release date | What's new                                     |
| ------- | ------------ | ---------------------------------------------- |
| 1.0.0   | 20.07.2023   | First version.                                 |
| 1.0.2   | 02.08.2023   | - Fixed std::string compiling error for Linux. |
| 1.0.3   | 20.03.2024   | - Documentation updated.                       |
| 1.0.4   | 17.05.2024   | - Documentation updated.                       |
| 1.0.5   | 07.07.2024   | - CMake updated.                               |
| 1.1.0   | 25.09.2026   | - Added **XDG Desktop Portal** (D-Bus) support for Linux.<br/>- Added **kdialog** support.<br/>- Added the ability to set a custom window title. |



# Library files

The library is supplied only by source code. The user is given a set of files in the form of a CMake project (repository). The repository structure is shown below:

```xml
CMakeLists.txt -------------------- Main CMake file of the library.
src ------------------------------- Folder with library source code.
    SimpleFileDialog.cpp ---------- C++ implementation file.
    SimpleFileDialog.h ------------ Library main header file.
    SimpleFileDialogVersion.h ----- Header file with library version.
    SimpleFileDialogVersion.h.in -- Service CMake file to generate version file.
test ------------------------------ Folder of the test application.
    CMakeLists.txt ---------------- CMake file of the test application.
    main.cpp ---------------------- Source C++ file of the test application.
```



# Class declaration

**SimpleFileDialog** class declared in **SimpleFileDialog.h** file. Class declaration:

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
    static std::string dialog(const std::string title = "Select a file");
};
}
}
```

**SimpleFileDialog** class includes only one static method **dialog(...)** which shows a file selection dialog to the user. The optional **title** parameter sets a custom window title on both Windows and Linux (default is **"Select a file"**). The method returns the selected file name. If no file is selected, the method returns an empty string **""**. The method is static, so no **SimpleFileDialog** class instance is required. Example:

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



# Build and connect to your project

On Linux, install the **pkg-config** and **libsystemd-dev** packages if you want D-Bus support (Ubuntu/Debian):

```bash
sudo apt install pkg-config libsystemd-dev
```

Typical commands to build **SimpleFileDialog** library:

```bash
git clone https://github.com/ConstantRobotics-Ltd/SimpleFileDialog.git
cd SimpleFileDialog
mkdir build
cd build
cmake ..
make
```

If you want connect **SimpleFileDialog** library to your CMake project as source code you can make follow. For example, if your repository has structure:

```bash
CMakeLists.txt
src
    CMakeList.txt
    yourLib.h
    yourLib.cpp
```

You can add repository **SimpleFileDialog** as submodule by commands:

```bash
cd <your respository folder>
git submodule add https://github.com/ConstantRobotics-Ltd/SimpleFileDialog.git 3rdparty/SimpleFileDialog
```

In you repository folder will be created folder **3rdparty/SimpleFileDialog** which contains all library files. Also you can copy **SimpleFileDialog** repository folder to **3rdparty** folder of your repository. New structure of your repository:

```bash
CMakeLists.txt
src
    CMakeList.txt
    yourLib.h
    yourLib.cpp
3rdparty
    SimpleFileDialog
```

Create CMakeLists.txt file in **3rdparty** folder. CMakeLists.txt should contain:

```cmake
cmake_minimum_required(VERSION 3.13)

################################################################################
## 3RD-PARTY
## dependencies for the project
################################################################################
project(3rdparty LANGUAGES CXX)

################################################################################
## SETTINGS
## basic 3rd-party settings before use
################################################################################
# To inherit the top-level architecture when the project is used as a submodule.
SET(PARENT ${PARENT}_YOUR_PROJECT_3RDPARTY)
# Disable self-overwriting of parameters inside included subdirectories.
SET(${PARENT}_SUBMODULE_CACHE_OVERWRITE OFF CACHE BOOL "" FORCE)

################################################################################
## CONFIGURATION
## 3rd-party submodules configuration
################################################################################
SET(${PARENT}_SUBMODULE_SIMPLE_FILE_DIALOG              ON  CACHE BOOL "" FORCE)
if (${PARENT}_SUBMODULE_SIMPLE_FILE_DIALOG)
    SET(${PARENT}_SIMPLE_FILE_DIALOG                    ON  CACHE BOOL "" FORCE)
    SET(${PARENT}_SIMPLE_FILE_DIALOG_TEST               OFF CACHE BOOL "" FORCE)
endif()

################################################################################
## INCLUDING SUBDIRECTORIES
## Adding subdirectories according to the 3rd-party configuration
################################################################################
if (${PARENT}_SUBMODULE_SIMPLE_FILE_DIALOG)
    add_subdirectory(SimpleFileDialog)
endif()
```

File **3rdparty/CMakeLists.txt** adds folder **SimpleFileDialog** to your project and excludes test application (SimpleFileDialog class test applications) from compiling (by default test application excluded from compiling if SimpleFileDialog included as sub-repository). Your repository new structure will be:

```bash
CMakeLists.txt
src
    CMakeList.txt
    yourLib.h
    yourLib.cpp
3rdparty
    CMakeLists.txt
    SimpleFileDialog
```

Next you need include folder 3rdparty in main **CMakeLists.txt** file of your repository. Add string at the end of your main **CMakeLists.txt**:

```cmake
add_subdirectory(3rdparty)
```

Next you have to include SimpleFileDialog library in your **src/CMakeLists.txt** file:

```cmake
target_link_libraries(${PROJECT_NAME} SimpleFileDialog)
```

Done!