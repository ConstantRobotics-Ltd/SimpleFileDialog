![simplefiledialog_logo](./static/simplefiledialog_web_logo.png)



# **SimpleFileDialog C++ library**

**v1.2.0**



# Table of contents

- [Overview](#overview)
- [Versions](#versions)
- [Library files](#library-files)
- [Class declaration](#class-declaration)
- [Behaviour on cancel and failure](#behaviour-on-cancel-and-failure)
- [Threads and COM apartment](#threads-and-com-apartment)
- [Security and hardening](#security-and-hardening)
- [Build and connect to your project](#build-and-connect-to-your-project)
- [Tests](#tests)



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
| 1.2.0   | 07.10.2026   | - Security hardening (EU Cyber Resilience Act, FIPS 140-3 code hygiene), see [Security and hardening](#security-and-hardening).<br/>- Windows: COM life cycle fixed (null pointer dereference and uninitialized pointer release on errors, leaks of the dialog object and of the path, missing CoUninitialize), dialog usable in a multithreaded apartment, file system items only, path returned in UTF-8 (was lossy ANSI code page), title from UTF-8.<br/>- Linux: zenity and kdialog started without a shell from fixed directories with a filtered environment, exit code checked, output bounded, invalid paths rejected.<br/>- Linux: XDG Desktop Portal response accepted only from the portal, strict file URI decoding, unique request token, no hang if the portal leaves the bus or an old portal answers fast.<br/>- **dialog(...)** takes the title by const reference, is noexcept, the title is sanitized.<br/>- Clean stub for unsupported platforms, compiler warnings and exploit mitigations enabled, automatic tests added. |



# Library files

The library is supplied only by source code. The user is given a set of files in the form of a CMake project (repository). The repository structure is shown below:

```xml
CMakeLists.txt -------------------- Main CMake file of the library.
src ------------------------------- Folder with library source code.
    CMakeLists.txt ---------------- CMake file of the library.
    SimpleFileDialog.cpp ---------- C++ implementation file.
    SimpleFileDialog.h ------------ Library main header file.
    SimpleFileDialogVersion.h ----- Header file with library version.
    SimpleFileDialogVersion.h.in -- Service CMake file to generate version file.
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
/// @brief File dialog class.
class SimpleFileDialog
{
public:
    /// @brief Dialog function.
    static std::string dialog(const std::string& title = "Select a file") noexcept;
};
}
}
```

**SimpleFileDialog** class includes only one static method **dialog(...)** which shows a modal dialog to choose an existing file and waits until the user closes it. The optional **title** parameter sets a custom window title on both Windows and Linux (default is **"Select a file"**). Control characters of the title are replaced by spaces and the title is cut to 256 bytes (never in the middle of a UTF-8 sequence). On Linux bytes which are not valid UTF-8 are replaced by U+FFFD, on Windows a title which is not valid UTF-8 is converted from the ANSI code page. The method returns the full path of the selected file: UTF-8 on Windows (the **VSourceFile** library and **std::filesystem::u8path** accept it), the bytes of the file name on Linux. If no file is selected, the method returns an empty string **""**. The method never throws exceptions. The method is static, so no **SimpleFileDialog** class instance is required. Example:

```cpp
#include <iostream>
#include "SimpleFileDialog.h"

int main(void)
{
    // Open file dialog.
    std::string file = cr::utils::SimpleFileDialog::dialog();
    std::cout << "File: " << file << std::endl;

    return 0;
}
```



# Behaviour on cancel and failure

The method returns an empty string if the user cancelled the dialog, if no dialog can be shown or if an error occurred (including out of memory). It is not possible to distinguish these cases.

On Linux the backends are tried in the order **XDG Desktop Portal** (if built with libsystemd), **zenity**, **kdialog**. The next backend is tried only if a backend **failed**; a dialog cancelled by the user ends the search, so the user never sees a second dialog after cancelling the first one:

| Result of the backend | Portal (response code) | zenity / kdialog (exit code) | Next backend |
| --------------------- | ---------------------- | ---------------------------- | ------------ |
| File chosen, path valid | 0 | 0 | no, the path is returned |
| Cancelled by the user | 1 | 1 | no, empty string |
| Failure | 2 or other, D-Bus error, portal not installed or left the bus, invalid response | program not found, not started, other exit code, killed by a signal, output longer than PATH_MAX, invalid path | yes |

A returned path is checked: absolute, shorter than PATH_MAX (4096 bytes), no NUL and no other control characters (a file whose name contains a new line or another control character can not be chosen: such names are legal on Linux but zenity separates its output by new lines and control characters are a common way to inject text into logs and terminals). The file does not need to exist any more when the method returns. The portal returns a file URI: only **file://** URIs with an empty or **localhost** host, well-formed percent encoding (bytes **%00** - **%1F** and **%7F** are rejected), no query and no fragment, exactly one URI, are accepted.

The portal dialog has no timeout: the user may keep it open for any time. The wait ends with the response of the portal, if the portal leaves the bus (crash or restart) or if the D-Bus connection fails. The call of the portal itself (it returns immediately, the dialog is shown asynchronously) uses the D-Bus default timeout of 25 seconds. A zenity or kdialog dialog also has no timeout; the method returns when the program closes its standard output and exits.

On Windows the method returns an empty string if COM can not be initialized, if the dialog can not be created or was cancelled, or if the chosen path can not be represented in UTF-8 (a file name with an unpaired UTF-16 surrogate).



# Threads and COM apartment

The method blocks the calling thread until the dialog is closed. It may be called from several threads at the same time: each call has its own D-Bus connection, request token and dialog process (Linux) or its own COM objects (Windows). It must not be called from a signal handler.

**Windows**: the method initializes COM on the calling thread as a single-threaded apartment (**CoInitializeEx** with **COINIT_APARTMENTTHREADED**) and uninitializes it before it returns, after all COM objects are released; if the caller has already initialized COM on the thread as a single-threaded apartment, the initialization is just counted. If the thread is already initialized as a multithreaded apartment (**RPC_E_CHANGED_MODE**), the method uses it as it is and does not uninitialize COM: the dialog object is then created by COM in a single-threaded apartment of its own. The dialog has no owner window; call the method from the GUI thread of an application to keep the dialog in front of its windows.

**Linux**: the dialog program is a child process of the application, which is waited for with **waitpid**. If the application ignores **SIGCHLD** or reaps all child processes itself (**waitpid(-1, ...)** in a SIGCHLD handler), the exit code of the dialog program is lost and the method returns an empty string. Signals interrupting the wait (EINTR) are handled.



# Security and hardening

The library was reviewed against the EU Cyber Resilience Act (Annex I: secure by default, input validation, minimal attack surface, exploit mitigation, resilience) and FIPS 140-3 style code hygiene (no unsafe or unbounded functions, checked return values, no undefined behaviour).

**Dialog programs (Linux)**:

- zenity and kdialog are started directly with **posix_spawn** and an argument vector, never through a shell: the title is one argument (**--title=...**), shell metacharacters have no meaning.
- The programs are searched only in the trusted directories **/usr/bin**, **/bin** and **/usr/local/bin** (in this order), not via the **PATH** environment variable. A program is used only if it is an executable regular file and the file and its directory can be changed only by root or by the user of the process (owner root or the user, no write permission for the group and other users). The list of directories can be changed only at compile time (macro **SIMPLE_FILE_DIALOG_TRUSTED_DIRS**, a comma-separated list of string literals, for example for distributions with other directories or for tests), never by the environment.
- The programs get a filtered environment: **PATH=/usr/bin:/bin:/usr/local/bin** and only the variables needed by a GUI program (**DISPLAY**, **WAYLAND_DISPLAY**, **XAUTHORITY**, **DBUS_SESSION_BUS_ADDRESS**, **HOME**, **USER**, **LOGNAME**, **LANG**, **LANGUAGE**, **DESKTOP_SESSION**, **XMODIFIERS**, **GDK_BACKEND**, **GDK_SCALE**, **GDK_DPI_SCALE**, **XCURSOR_THEME**, **XCURSOR_SIZE** and the variables starting with **XDG_**, **LC_**, **GTK_**, **QT_**, **KDE_**, **GNOME_**). Variables which make GTK or Qt load code from a given directory (**GTK_MODULES**, **GTK_PATH**, **GTK_EXE_PREFIX**, **GTK_IM_MODULE_FILE**, **QT_PLUGIN_PATH**, **QT_QPA_PLATFORM_PLUGIN_PATH**), **LD_PRELOAD**, **LD_LIBRARY_PATH** and all other variables are not passed.
- Standard input and error of the program are **/dev/null**, its standard output is a pipe. Descriptors of the library are close-on-exec, with glibc 2.34 or newer all other descriptors of the application are closed in the child too. Signal dispositions are reset to the default and no signal is blocked in the child.
- The output is read up to PATH_MAX bytes: a longer output is never returned cut as if it were a valid path, the program is stopped and the backend counts as failed. System calls interrupted by signals are repeated, the child process is always reaped (no zombie processes), no descriptor is leaked.

**XDG Desktop Portal (Linux)**:

- Each call uses its own D-Bus connection and a unique request token. The response is accepted only from the unique bus name which answered the **OpenFile** call and only on the request object path: a **Response** signal sent directly to the application by another client of the bus is ignored.
- The method does not wait forever if the portal leaves the bus (crash, restart) or if the D-Bus connection fails; old portals which ignore the request token are supported without a race.
- The response is parsed strictly (signature, response code, exactly one URI of type **as**, bounded URI size) and the URI is decoded strictly (see [Behaviour on cancel and failure](#behaviour-on-cancel-and-failure)).

**Windows**:

- The dialog object is created in-process only (**CLSCTX_INPROC_SERVER**). Options **FOS_FORCEFILESYSTEM** (only file system items), **FOS_FILEMUSTEXIST**, **FOS_PATHMUSTEXIST** and **FOS_NOCHANGEDIR** (the working directory of the application is not changed) are set.
- All COM objects and the memory returned by COM are released on every path (RAII), COM initialization and uninitialization are balanced.

**General**:

- No exception leaves the method (it is **noexcept**, out of memory gives an empty string), no exception passes through the C callbacks of sd-bus.
- Zero compiler warnings with MSVC **/W4** and **/analyze**, gcc 13 and clang 18 with **-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-qual** and more (see **src/CMakeLists.txt**), with and without libsystemd.
- Exploit mitigations of the library target: MSVC **/sdl /GS /guard:cf** (and **/DYNAMICBASE /NXCOMPAT /guard:cf** for a dynamic library), gcc / clang **-fstack-protector-strong**, **-D_FORTIFY_SOURCE=2** (optimized builds), **-fstack-clash-protection**, **-fcf-protection=full** (x86) or **-mbranch-protection=standard** (ARM) if supported, full RELRO and non-executable stack.
- Automatic tests (unit tests, fake dialog programs, fake portal, COM life cycle), AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer and fuzzing (libFuzzer) of the URI decoder, the path check, the title sanitizing and the environment filter.



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

File **3rdparty/CMakeLists.txt** adds folder **SimpleFileDialog** to your project and excludes test applications (SimpleFileDialog class test applications) from compiling (by default test applications are excluded from compiling if SimpleFileDialog included as sub-repository). Your repository new structure will be:

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



# Tests

The test applications are built if the library is built as a stand-alone repository. **SimpleFileDialogTest** (**test/main.cpp**) shows the dialog and prints the chosen file. The automatic tests need no user interaction and return 0 if all checks passed; they compile the library source with test-only settings and do not link the library:

- **SimpleFileDialogUnitTest** (**test/UnitTest.cpp**): title sanitizing, UTF-8 checks, path check, environment filter, bounded output reading and file URI decoding. Linux: fake zenity and kdialog programs (shell scripts) written by the test to its own trusted directory (exit codes, cancel, fallback, long, missing or invalid output, special characters, signals, hostile **PATH** and **LD_PRELOAD**, arguments, environment, descriptors, signal masks, interrupted system calls, calls from several threads, leaks). Windows: COM life cycle with the dialog object created but not shown (the interactive **Show** call is replaced at compile time, macro **SIMPLE_FILE_DIALOG_TEST_SHOW**), UTF-8 / UTF-16 conversions.
- **SimpleFileDialogPortalTest** (**test/PortalTest.cpp**, Linux with libsystemd): fake XDG Desktop Portal on a private D-Bus session bus (success, cancel, errors, invalid responses and URIs, forged responses, old portals, portal leaving the bus, concurrent requests, call timeout). Run it with a private bus, the D-Bus daemon (package **dbus-daemon**) is needed:

```bash
dbus-run-session -- ./bin/SimpleFileDialogPortalTest
```
