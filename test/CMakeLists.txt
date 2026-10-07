cmake_minimum_required(VERSION 3.13)



################################################################################
## EXECUTABLE-PROJECT
## name and version
################################################################################
project(SimpleFileDialogTest LANGUAGES CXX)



################################################################################
## SETTINGS
## basic project settings before use
################################################################################
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Standard C++17 without compiler specific extensions
set(CMAKE_CXX_EXTENSIONS OFF)
# creating output directory architecture in accordance with GNU guidelines
set(BINARY_DIR "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BINARY_DIR}/lib")
find_package(Threads REQUIRED)



################################################################################
## TARGETS
## interactive test application (main.cpp) and automatic tests: the library
## source is compiled into the automatic tests with test-only settings (fake
## dialog programs, COM test seam), they do not link the library
################################################################################
if (NOT TARGET ${PROJECT_NAME})
    add_executable(${PROJECT_NAME} main.cpp)
endif()
target_link_libraries(${PROJECT_NAME} SimpleFileDialog)

set(TEST_TARGETS ${PROJECT_NAME} SimpleFileDialogUnitTest)
add_executable(SimpleFileDialogUnitTest UnitTest.cpp)
target_include_directories(SimpleFileDialogUnitTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(SimpleFileDialogUnitTest Threads::Threads)
if (WIN32)
    target_link_libraries(SimpleFileDialogUnitTest ole32 uuid)
elseif (UNIX)
    target_compile_definitions(SimpleFileDialogUnitTest PRIVATE
        "SIMPLE_FILE_DIALOG_TRUSTED_DIRS=\"${CMAKE_CURRENT_BINARY_DIR}/unit_trusted\"")
    # Same optional libsystemd as the library (see src/CMakeLists.txt)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD QUIET IMPORTED_TARGET libsystemd)
    endif()
    if (SYSTEMD_FOUND)
        target_compile_definitions(SimpleFileDialogUnitTest PRIVATE SIMPLE_FILE_DIALOG_SD_BUS=1)
        target_link_libraries(SimpleFileDialogUnitTest PkgConfig::SYSTEMD)
        # Fake XDG desktop portal on a private bus (run with dbus-run-session)
        add_executable(SimpleFileDialogPortalTest PortalTest.cpp)
        list(APPEND TEST_TARGETS SimpleFileDialogPortalTest)
        target_include_directories(SimpleFileDialogPortalTest PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src)
        target_compile_definitions(SimpleFileDialogPortalTest PRIVATE
            SIMPLE_FILE_DIALOG_SD_BUS=1
            "SIMPLE_FILE_DIALOG_TRUSTED_DIRS=\"${CMAKE_CURRENT_BINARY_DIR}/portal_trusted\"")
        target_link_libraries(SimpleFileDialogPortalTest PkgConfig::SYSTEMD Threads::Threads)
    else()
        target_compile_definitions(SimpleFileDialogUnitTest PRIVATE SIMPLE_FILE_DIALOG_SD_BUS=0)
    endif()
endif()

# All warnings
foreach (TARGET_NAME ${TEST_TARGETS})
    if (MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wold-style-cast)
    endif()
endforeach()
