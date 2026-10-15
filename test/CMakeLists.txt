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

# Warnings and exploit mitigation of every executable (the same as in src)
include(CheckCXXCompilerFlag)
foreach (TARGET_NAME ${TEST_TARGETS})
    if (MSVC)
        # All warnings, standard conformance, security checks, stack cookies
        # and control flow guard
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4 /permissive- /sdl /GS /guard:cf)
        # ASLR, DEP and control flow guard for an executable
        target_link_options(${TARGET_NAME} PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf)
    else()
        # All warnings
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wold-style-cast -Wcast-qual -Wformat=2 -Wnon-virtual-dtor
            -Woverloaded-virtual -Wdouble-promotion -Wimplicit-fallthrough
            -Wmissing-declarations -Wundef -Wextra-semi
            -Wzero-as-null-pointer-constant)
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TARGET_NAME} PRIVATE -Wdocumentation)
        endif()
        if (NOT WIN32)
            # Stack protector and hardened libc calls (optimized builds only)
            target_compile_options(${TARGET_NAME} PRIVATE -fstack-protector-strong
                "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>")
            # Stack clash protection and control flow protection (x86 CET or ARM
            # pointer authentication and branch target identification), if the
            # compiler supports them for the target
            set(HARDENING_FLAGS -fstack-clash-protection -fcf-protection=full)
            if (CMAKE_SYSTEM_PROCESSOR MATCHES "^([Aa][Aa][Rr][Cc][Hh]64|[Aa][Rr][Mm])")
                # Clang on x86 accepts this option in the check but warns on every use
                list(APPEND HARDENING_FLAGS -mbranch-protection=standard)
            endif()
            foreach (FLAG ${HARDENING_FLAGS})
                string(MAKE_C_IDENTIFIER "HAS${FLAG}" FLAG_VAR)
                check_cxx_compiler_flag(${FLAG} ${FLAG_VAR})
                if (${FLAG_VAR})
                    target_compile_options(${TARGET_NAME} PRIVATE ${FLAG})
                endif()
            endforeach()
        endif()
        if (UNIX AND NOT APPLE)
            # Full RELRO and non-executable stack for an executable
            target_link_options(${TARGET_NAME} PRIVATE
                -Wl,-z,relro,-z,now,-z,noexecstack)
        endif()
    endif()
endforeach()
