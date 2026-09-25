cmake_minimum_required(VERSION 3.13)



################################################################################
## LIBRARY-PROJECT
## name and version
################################################################################
project(SimpleFileDialog VERSION 1.1.0 LANGUAGES CXX)



################################################################################
## SETTINGS
## basic project settings before use
################################################################################
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Enabling export of all symbols to create a dynamic library
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# creating output directory architecture in accordance with GNU guidelines
set(BINARY_DIR "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BINARY_DIR}/lib")
file (GLOB_RECURSE IN_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.h.in)
configure_file(${IN_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}Version.h)



################################################################################
## TARGET
## create target and add include path
################################################################################
# create glob files for *.h, *.cpp
file (GLOB_RECURSE H_FILES   ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
file (GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
# concatenate the results (glob files) to variable
set  (SOURCES ${CPP_FILES} ${H_FILES})
# create lib from src
if (NOT TARGET ${PROJECT_NAME})
    add_library(${PROJECT_NAME} ${SOURCES})
endif()
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})



################################################################################
## LINK LIBRARIES
## optional libsystemd (sd-bus) for XDG Desktop Portal file dialog in Linux,
## SIMPLE_FILE_DIALOG_SD_BUS = 1 if found (zenity and kdialog are fallback), 0 otherwise (only zenity)
################################################################################
if (UNIX)
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)
    endif()
    if (SYSTEMD_FOUND)
        message(STATUS "${PROJECT_NAME}: libsystemd ${SYSTEMD_VERSION} found, XDG Desktop Portal is used")
        target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::SYSTEMD)
        target_compile_definitions(${PROJECT_NAME} PRIVATE SIMPLE_FILE_DIALOG_SD_BUS=1)
    else()
        message(WARNING "${PROJECT_NAME}: libsystemd not found, zenity and kdialog are used")
        target_compile_definitions(${PROJECT_NAME} PRIVATE SIMPLE_FILE_DIALOG_SD_BUS=0)
    endif()
endif()
