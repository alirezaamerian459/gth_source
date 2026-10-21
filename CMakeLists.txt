cmake_minimum_required(VERSION 3.24)

# Static VC++ runtime (/MT, /MTd for Debug) so a Release build is a single
# portable .exe that runs on another PC without that machine needing the
# Visual C++ Redistributable installed first - every third-party dependency
# here (curl, libssh2, sqlite3, spdlog, googletest) is already built as a
# static library, so the CRT was the only remaining dynamic dependency.
# Must be set before project() - CMake bakes the runtime-library choice
# into its MSVC ABI detection at that point.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(GameTransferHub LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Game Transfer Hub targets Windows only (Win32/WTL UI, Windows storage APIs).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GTH_BUILD_TESTS "Build the unit test suite" ON)

add_subdirectory(src/core)
add_subdirectory(src/application)
add_subdirectory(src/storage_detection)
add_subdirectory(src/indexing)
add_subdirectory(src/updates)
add_subdirectory(src/verification)
add_subdirectory(src/network)
add_subdirectory(src/captcha)
add_subdirectory(src/transfer)
add_subdirectory(src/database)
add_subdirectory(src/logging)
add_subdirectory(src/credentials)
add_subdirectory(src/ftp)
add_subdirectory(src/ftpserver)
add_subdirectory(src/acceleratorserver)
add_subdirectory(src/infrastructure)
add_subdirectory(src/app)

if(GTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()
