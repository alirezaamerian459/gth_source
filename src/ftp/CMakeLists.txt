# libcurl (FTP/FTPS) and libssh2 (SFTP), both fetched from their official
# GitHub release tags and built as static libraries - no OpenSSL anywhere in
# the chain: libcurl uses its native WinSSL/Schannel backend, libssh2 is
# built against Windows' native crypto (WinCNG) instead of OpenSSL/mbedTLS.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_HTTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CMAKE_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_11_1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(curl)

set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libssh2
    GIT_REPOSITORY https://github.com/libssh2/libssh2.git
    GIT_TAG libssh2-1.11.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libssh2)

add_library(gth_ftp STATIC
    src/CurlFtpClient.cpp
    src/Libssh2SftpClient.cpp
    src/FtpTransferProvider.cpp
    src/FtpClientFactory.cpp
)

target_include_directories(gth_ftp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gth_ftp PUBLIC gth_core libcurl libssh2)
target_compile_features(gth_ftp PUBLIC cxx_std_20)
