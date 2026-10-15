![simplefiledialog_logo](./static/simplefiledialog_web_logo.png)



# **SimpleFileDialog C++ library**

**v1.2.1**



# Table of contents

- [Overview](#overview)
- [Versions](#versions)
- [Library files](#library-files)
- [Class declaration](#class-declaration)
- [Build and connect to your project](#build-and-connect-to-your-project)
  - [Build stand alone](#build-stand-alone)
  - [CMake options](#cmake-options)
  - [Connect to your project](#connect-to-your-project)
- [Portability](#portability)



# Overview

**SimpleFileDialog** C++ library provides a simple file selection dialog for Windows and Linux (tested on Ubuntu 22.04, 22.10, 24.04 and Windows 11). The library is used in projects where a simple file selection dialog is needed. On Windows the library shows the common item dialog (COM). On Linux the library shows the dialog using the **XDG Desktop Portal** FileChooser interface via **D-Bus**, the **zenity** application or the **kdialog** application. On other platforms the library has no dialog and returns an empty string. The library uses the C++17 standard. The D-Bus backend requires the optional **libsystemd** library on Linux. If it is not found at build time, the D-Bus backend is disabled. The library has no other third-party dependencies. **SimpleFileDialog.h** file includes the declaration of the **SimpleFileDialog** class. **SimpleFileDialog** class includes only one static method **dialog(...)**.



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
| 1.2.0   | 07.10.2026   | - Security hardening (EU Cyber Resilience Act, FIPS 140-3 code hygiene).<br/>- Windows: COM life cycle fixed (null pointer dereference and uninitialized pointer release on errors, leaks of the dialog object and of the path, missing CoUninitialize), dialog usable in a multithreaded apartment, file system items only, path returned in UTF-8 (was lossy ANSI code page), title from UTF-8.<br/>- Linux: zenity and kdialog started without a shell from fixed directories with a filtered environment, exit code checked, output bounded, invalid paths rejected.<br/>- Linux: XDG Desktop Portal response accepted only from the portal, strict file URI decoding, unique request token, no hang if the portal leaves the bus or an old portal answers fast.<br/>- **dialog(...)** takes the title by const reference, is noexcept, the title is sanitized.<br/>- Clean stub for unsupported platforms, compiler warnings and exploit mitigations enabled, automatic tests added. |
| 1.2.1   | 15.10.2026   | - Documentation: error behaviour, threads, COM apartment, Linux backends and CMake options are described in the existing sections, **Portability** section added.<br/>- CMake: warnings (**-Wdocumentation** for clang) and exploit mitigation options are applied to the test applications too.<br/>- Tests: the tests remove their files, results are printed per test group, damaged data campaign added.<br/>- Doxygen comment of **dialog(...)** completed (thread safety, environment), no changes of the library behaviour. |



# Library files

The library is supplied only by source code. The user is given a set of files in the form of a CMake project (repository). The repository does not contain sub-repositories. The repository structure is shown below:

```xml
CMakeLists.txt -------------------- Main CMake file of the library.
src ------------------------------- Folder with library source code.
    CMakeLists.txt ---------------- CMake file of the library.
    SimpleFileDialog.cpp ---------- C++ implementation file.
    SimpleFileDialog.h ------------ Library main header file.
    SimpleFileDialogVersion.h ----- Header file with library version.
    SimpleFileDialogVersion.h.in -- Service CMake file to generate version file.
static ---------------------------- Folder with the logo of the README file.
test ------------------------------ Folder of the test applications.
    CMakeLists.txt ---------------- CMake file of the test applications.
    main.cpp ---------------------- Interactive test application (shows the dialog).
    UnitTest.cpp ------------------ Automatic tests (no user interaction).
    PortalTest.cpp ---------------- Automatic tests of the XDG Desktop Portal backend (Linux).
```



# Class declaration

**SimpleFileDialog** class declared in **SimpleFileDialog.h** file. Class declaration:

```cpp
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
```

**SimpleFileDialog** class includes only one static method **dialog(...)** which shows a modal dialog to choose an existing file and waits until the user closes it. The method is static, so no **SimpleFileDialog** class instance is required. The method never throws exceptions. Example:

```cpp
#include <iostream>
#include <string>
#include "SimpleFileDialog.h"

int main()
{
    // Open file dialog.
    const std::string file{cr::utils::SimpleFileDialog::dialog()};
    if (file.empty())
    {
        std::cout << "No file selected" << std::endl;
        return 0;
    }
    std::cout << "File: " << file << std::endl;

    return 0;
}
```

**Table 2** - **dialog(...)** method parameter and result.

| Name   | Description |
| ------ | ----------- |
| title  | Optional window title on both Windows and Linux, default is **"Select a file"**. Control characters (**0x00** - **0x1F** and **0x7F**) are replaced by spaces and the title is cut to 256 bytes (never in the middle of a UTF-8 sequence). On Linux bytes which are not valid UTF-8 are replaced by U+FFFD, on Windows a title which is not valid UTF-8 is converted from the ANSI code page. |
| Result | Full path of the chosen file: UTF-8 on Windows (the **VSourceFile** library and **std::filesystem::u8path** accept it), the bytes of the file name on Linux. If no file is selected, the method returns an empty string **""**. The method returns an empty string also if no dialog can be shown or if an error occurred (including out of memory): these cases can not be distinguished. |

**Linux**: the backends are tried in the order **XDG Desktop Portal** (if the library is built with libsystemd), **zenity**, **kdialog**. The next backend is tried only if a backend **failed**; a dialog cancelled by the user ends the search, so the user never sees a second dialog after cancelling the first one:

**Table 3** - Result of a Linux backend.

| Result of the backend | Portal (response code) | zenity / kdialog (exit code) | Next backend |
| --------------------- | ---------------------- | ---------------------------- | ------------ |
| File chosen, path valid | 0 | 0 | no, the path is returned |
| Cancelled by the user | 1 | 1 | no, empty string |
| Failure | 2 or other, D-Bus error, portal not installed or left the bus, invalid response | program not found, not started, other exit code, killed by a signal, output longer than PATH_MAX, invalid path | yes |

A returned path is checked: absolute, shorter than PATH_MAX (4096 bytes), no NUL and no other control characters (a file whose name contains a new line or another control character can not be chosen: such names are legal on Linux but zenity separates its output by new lines). The file does not need to exist any more when the method returns. The portal returns a file URI: only **file://** URIs with an empty or **localhost** host, well-formed percent encoding (bytes **%00** - **%1F** and **%7F** are rejected), no query and no fragment, exactly one URI, are accepted. The response is accepted only from the unique bus name which answered the **OpenFile** call and only on the request object path. Each call uses its own D-Bus connection and a unique request token.

The portal dialog has no timeout: the user may keep it open for any time. The wait ends with the response of the portal, if the portal leaves the bus (crash or restart) or if the D-Bus connection fails. The call of the portal itself (it returns immediately, the dialog is shown asynchronously) uses the D-Bus default timeout of 25 seconds. A zenity or kdialog dialog also has no timeout; the method returns when the program closes its standard output and exits.

zenity and kdialog are started directly (no shell), the title is one argument (**--title=...**). The programs are searched only in the directories **/usr/bin**, **/bin** and **/usr/local/bin** (in this order), not via the **PATH** environment variable. A program is used only if it is an executable regular file and the file and its directory can be changed only by root or by the user of the process (owner root or the user, no write permission for the group and other users). The list of directories can be changed only at compile time (see [Portability](#portability)). The programs get a filtered environment: **PATH=/usr/bin:/bin:/usr/local/bin** and only the variables needed by a GUI program (**DISPLAY**, **WAYLAND_DISPLAY**, **XAUTHORITY**, **DBUS_SESSION_BUS_ADDRESS**, **HOME**, **USER**, **LOGNAME**, **LANG**, **LANGUAGE**, **DESKTOP_SESSION**, **XMODIFIERS**, **GDK_BACKEND**, **GDK_SCALE**, **GDK_DPI_SCALE**, **XCURSOR_THEME**, **XCURSOR_SIZE** and the variables starting with **XDG_**, **LC_**, **GTK_**, **QT_**, **KDE_**, **GNOME_**). Variables which make GTK or Qt load code from a given directory (**GTK_MODULES**, **GTK_PATH**, **GTK_EXE_PREFIX**, **GTK_IM_MODULE_FILE**, **QT_PLUGIN_PATH**, **QT_QPA_PLATFORM_PLUGIN_PATH**), **LD_PRELOAD**, **LD_LIBRARY_PATH** and all other variables are not passed. Standard input and error of the program are **/dev/null**, its standard output is a pipe which is read up to PATH_MAX bytes: a longer output counts as a failure of the backend and the program is stopped.

**Windows**: the method returns an empty string if COM can not be initialized, if the dialog can not be created or was cancelled, or if the chosen path can not be represented in UTF-8 (a file name with an unpaired UTF-16 surrogate). The method initializes COM on the calling thread as a single-threaded apartment (**CoInitializeEx** with **COINIT_APARTMENTTHREADED**) and uninitializes it before it returns, after all COM objects are released; if the caller has already initialized COM on the thread as a single-threaded apartment, the initialization is just counted. If the thread is already initialized as a multithreaded apartment (**RPC_E_CHANGED_MODE**), the method uses it as it is and does not uninitialize COM: the dialog object is then created by COM in a single-threaded apartment of its own. The dialog object is created in-process only (**CLSCTX_INPROC_SERVER**). The dialog has no owner window; call the method from the GUI thread of an application to keep the dialog in front of its windows.

**Threads**: the method blocks the calling thread until the dialog is closed. It may be called from several threads at the same time. It must not be called from a signal handler. On Linux the dialog program is a child process of the application, which is waited for with **waitpid**: if the application ignores **SIGCHLD** or reaps all child processes itself (**waitpid(-1, ...)** in a SIGCHLD handler), the exit code of the dialog program is lost and the method returns an empty string. Signals interrupting the wait (EINTR) are handled. The application must not change its environment variables (**setenv**, **putenv**) while the method runs.



# Build and connect to your project

## Build stand alone

The repository does not contain sub-repositories and the library does not depend on other repositories. On Linux, install the **pkg-config** and **libsystemd-dev** packages if you want D-Bus support (Ubuntu/Debian):

```bash
sudo apt install pkg-config libsystemd-dev
```

Typical commands to build **SimpleFileDialog** library and the test applications:

```bash
git clone <SimpleFileDialog repository URL>
cd SimpleFileDialog
mkdir build
cd build
cmake ..
make
```

On Windows use `cmake --build . --config Release` instead of `make`.

## CMake options

**Table 4** - CMake options of the repository. In the options below ${PARENT} is the prefix defined by the top-level project (empty if **SimpleFileDialog** is the top-level project).

| Option | Default | Description |
| ------ | ------- | ----------- |
| ${PARENT}_SIMPLE_FILE_DIALOG | ON | Build the library. |
| ${PARENT}_SIMPLE_FILE_DIALOG_TEST | ON (stand alone), OFF (included in other project) | Build the test applications. |
| ${PARENT}_SUBMODULE_CACHE_OVERWRITE | ON | Forced rewriting of the options above in the CMake cache. The top-level project sets it to OFF when it includes **SimpleFileDialog** as a sub-repository. |
| CMAKE_BUILD_TYPE | Release (if not specified) | Build type. |

On Linux the library looks for **libsystemd** with **pkg-config**: if it is found, the library is built with the XDG Desktop Portal backend, otherwise only **zenity** and **kdialog** are used (the CMake message tells which case applies).

## Connect to your project

If you want to connect **SimpleFileDialog** library to your CMake project as source code you can make follow. For example, if your repository has structure:

```bash
CMakeLists.txt
src
    CMakeLists.txt
    yourLib.h
    yourLib.cpp
```

You can add repository **SimpleFileDialog** as sub-repository by commands:

```bash
cd <your repository folder>
git submodule add <SimpleFileDialog repository URL> 3rdparty/SimpleFileDialog
```

In your repository folder will be created folder **3rdparty/SimpleFileDialog** which contains all library files. Also you can copy **SimpleFileDialog** repository folder to **3rdparty** folder of your repository. New structure of your repository:

```bash
CMakeLists.txt
src
    CMakeLists.txt
    yourLib.h
    yourLib.cpp
3rdparty
    SimpleFileDialog
```

The library does not depend on other repositories, so no other folder has to be added to **3rdparty**. Create CMakeLists.txt file in **3rdparty** folder. CMakeLists.txt should contain:

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

File **3rdparty/CMakeLists.txt** adds folder **SimpleFileDialog** to your project and excludes test applications (SimpleFileDialog class test applications) from compiling (by default test applications are excluded from compiling if SimpleFileDialog included as sub-repository). Your repository new structure will be:

```bash
CMakeLists.txt
src
    CMakeLists.txt
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



# Portability

The library contains no code that depends on the byte order or on the size of integers and does not serialize data. It is built for Windows (MSVC, COM) and for Linux (gcc and clang with glibc); on other operating systems the library is compiled as a stub which returns an empty string. The library was built without warnings and its automatic tests were run on: Windows 11 x86-64 with MSVC 2022 (Release and Debug); Linux x86-64 with gcc 12 and 13 and clang 15 and 18 (Release and Debug, with and without libsystemd); Ubuntu 22.10 with gcc 12; Linux aarch64 with gcc 13 (cross build, tests under qemu-user, without libsystemd).

The XDG Desktop Portal backend is compiled if **libsystemd** is found by **pkg-config** when CMake runs: the library target gets the definition **SIMPLE_FILE_DIALOG_SD_BUS** equal to 1 (found) or 0. The directories searched for zenity and kdialog are defined by the macro **SIMPLE_FILE_DIALOG_TRUSTED_DIRS**, a comma-separated list of string literals (default **"/usr/bin", "/bin", "/usr/local/bin"**). The macro can be defined only when the library is compiled (for example with **target_compile_definitions**), it is intended for distributions with other directories. With glibc 2.34 or newer the dialog programs do not inherit the descriptors of the application which are not close-on-exec, with other C libraries they do. The programs zenity and kdialog are not part of the library: without the portal and without both programs the Linux dialog is not available and the method returns an empty string.
