#include "gth/ftp/Libssh2SftpClient.h"

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#pragma comment(lib, "ws2_32.lib")

namespace gth::ftp {

namespace {
struct WsaGuard {
    WsaGuard() {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WsaGuard() { ::WSACleanup(); }
};

// One process-wide WSAStartup/WSACleanup pair, ref-counted implicitly by
// static initialization order - simplest way to guarantee Winsock is ready
// before any Libssh2SftpClient connects, without every call site worrying
// about it.
void ensureWinsockReady() { static WsaGuard guard; }
}  // namespace

Libssh2SftpClient::Libssh2SftpClient() { ensureWinsockReady(); }

Libssh2SftpClient::~Libssh2SftpClient() { disconnect(); }

core::FtpOperationResult Libssh2SftpClient::connect(const core::FtpConnectParams& params) {
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) return {false, "Failed to create socket."};

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(static_cast<u_short>(params.port));
    if (::inet_pton(AF_INET, params.host.c_str(), &sin.sin_addr) != 1) {
        // Not a dotted IPv4 literal - resolve as a hostname.
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (::getaddrinfo(params.host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            ::closesocket(socket_);
            return {false, "Failed to resolve host."};
        }
        sin.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        ::freeaddrinfo(result);
    }

    if (::connect(socket_, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0) {
        ::closesocket(socket_);
        return {false, "TCP connect failed."};
    }

    if (libssh2_init(0) != 0) {
        ::closesocket(socket_);
        return {false, "libssh2_init failed."};
    }

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        ::closesocket(socket_);
        return {false, "Failed to create libssh2 session."};
    }
    session_ = session;

    if (libssh2_session_handshake(session, static_cast<int>(socket_)) != 0) {
        libssh2_session_free(session);
        session_ = nullptr;
        ::closesocket(socket_);
        return {false, "SSH handshake failed."};
    }

    if (libssh2_userauth_password(session, params.username.c_str(), params.password.c_str()) != 0) {
        libssh2_session_disconnect(session, "auth failure");
        libssh2_session_free(session);
        session_ = nullptr;
        ::closesocket(socket_);
        return {false, "Authentication failed."};
    }

    LIBSSH2_SFTP* sftp = libssh2_sftp_init(session);
    if (!sftp) {
        libssh2_session_disconnect(session, "sftp init failure");
        libssh2_session_free(session);
        session_ = nullptr;
        ::closesocket(socket_);
        return {false, "Failed to initialize SFTP subsystem."};
    }
    sftp_ = sftp;

    connected_ = true;
    return {true, ""};
}

void Libssh2SftpClient::disconnect() {
    if (sftp_) {
        libssh2_sftp_shutdown(reinterpret_cast<LIBSSH2_SFTP*>(sftp_));
        sftp_ = nullptr;
    }
    if (session_) {
        libssh2_session_disconnect(reinterpret_cast<LIBSSH2_SESSION*>(session_), "bye");
        libssh2_session_free(reinterpret_cast<LIBSSH2_SESSION*>(session_));
        session_ = nullptr;
    }
    if (socket_ != 0) {
        ::closesocket(socket_);
        socket_ = 0;
    }
    connected_ = false;
}

std::vector<core::FtpDirectoryEntry> Libssh2SftpClient::listDirectory(const std::string& path) {
    std::vector<core::FtpDirectoryEntry> entries;
    if (!connected_) return entries;

    auto* sftp = reinterpret_cast<LIBSSH2_SFTP*>(sftp_);
    LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(sftp, path.c_str());
    if (!dir) return entries;

    char nameBuf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    while (libssh2_sftp_readdir(dir, nameBuf, sizeof(nameBuf), &attrs) > 0) {
        std::string name(nameBuf);
        if (name == "." || name == "..") continue;

        core::FtpDirectoryEntry entry;
        entry.name = name;
        entry.isDirectory = LIBSSH2_SFTP_S_ISDIR(attrs.permissions) != 0;
        entry.sizeBytes = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<long long>(attrs.filesize) : 0;
        entries.push_back(std::move(entry));
    }

    libssh2_sftp_closedir(dir);
    return entries;
}

core::FtpOperationResult Libssh2SftpClient::upload(const std::string& localPath, const std::string& remotePath,
                                                    const std::function<void(long long, long long)>& onProgress,
                                                    std::atomic<bool>* cancelFlag) {
    if (!connected_) return {false, "Not connected."};

    std::FILE* file = nullptr;
    if (fopen_s(&file, localPath.c_str(), "rb") != 0 || !file) return {false, "Could not open local file."};

    std::fseek(file, 0, SEEK_END);
    const long long totalSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    auto* sftp = reinterpret_cast<LIBSSH2_SFTP*>(sftp_);
    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(
        sftp, remotePath.c_str(), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!handle) {
        std::fclose(file);
        return {false, "Failed to open remote file for writing."};
    }

    char buffer[32768];
    long long transferred = 0;
    bool cancelled = false;
    size_t bytesRead;
    while ((bytesRead = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        char* ptr = buffer;
        size_t remaining = bytesRead;
        while (remaining > 0) {
            const ssize_t written = libssh2_sftp_write(handle, ptr, remaining);
            if (written < 0) {
                cancelled = true;
                break;
            }
            ptr += written;
            remaining -= static_cast<size_t>(written);
            transferred += written;
        }
        if (onProgress) onProgress(transferred, totalSize);
        if (cancelFlag && cancelFlag->load()) {
            cancelled = true;
            break;
        }
        if (cancelled) break;
    }

    libssh2_sftp_close(handle);
    std::fclose(file);

    if (cancelled) return {false, "Upload cancelled or failed."};
    return {true, ""};
}

core::FtpOperationResult Libssh2SftpClient::download(const std::string& remotePath, const std::string& localPath,
                                                      const std::function<void(long long, long long)>& onProgress,
                                                      std::atomic<bool>* cancelFlag) {
    if (!connected_) return {false, "Not connected."};

    auto* sftp = reinterpret_cast<LIBSSH2_SFTP*>(sftp_);
    LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(sftp, remotePath.c_str(), LIBSSH2_FXF_READ, 0);
    if (!handle) return {false, "Failed to open remote file for reading."};

    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    libssh2_sftp_fstat(handle, &attrs);
    const long long totalSize = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<long long>(attrs.filesize) : 0;

    std::FILE* file = nullptr;
    if (fopen_s(&file, localPath.c_str(), "wb") != 0 || !file) {
        libssh2_sftp_close(handle);
        return {false, "Could not open local file."};
    }

    char buffer[32768];
    long long transferred = 0;
    bool failed = false;
    while (true) {
        const ssize_t bytesRead = libssh2_sftp_read(handle, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            failed = true;
            break;
        }
        if (bytesRead == 0) break;

        std::fwrite(buffer, 1, static_cast<size_t>(bytesRead), file);
        transferred += bytesRead;
        if (onProgress) onProgress(transferred, totalSize);
        if (cancelFlag && cancelFlag->load()) {
            failed = true;
            break;
        }
    }

    libssh2_sftp_close(handle);
    std::fclose(file);

    if (failed) return {false, "Download cancelled or failed."};
    return {true, ""};
}

}  // namespace gth::ftp
