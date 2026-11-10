#include "gth/ftpserver/WinsockFtpServer.h"

#include <winsock2.h>

#include <ws2tcpip.h>

#include <cctype>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace gth::ftpserver {

namespace fs = std::filesystem;

namespace {

// True FTP-style forward-slash virtual path resolution, deliberately kept
// separate from std::filesystem's own path-joining: `operator/=` silently
// replaces the *entire* left-hand path when the right-hand side looks
// absolute (a drive letter, a UNC prefix, ...), which is exactly the kind of
// trap a traversal attempt could exploit. Segments are resolved by hand
// instead: ".." pops the last segment, and if there is nothing left to pop
// (i.e. the request tries to climb above the shared root) resolution fails
// outright rather than being silently clamped back to the root.
std::optional<std::vector<std::string>> normalizeVirtualPath(const std::vector<std::string>& currentSegments,
                                                               const std::string& argument) {
    // Backslashes are not separators in this virtual scheme - reject them
    // outright instead of letting them be reinterpreted as separators later.
    if (argument.find('\\') != std::string::npos) return std::nullopt;

    std::vector<std::string> segments =
        (!argument.empty() && argument.front() == '/') ? std::vector<std::string>{} : currentSegments;

    std::stringstream stream(argument);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") continue;
        // A colon anywhere in a segment ("C:", "..:") would be an attempt to
        // name an absolute OS location rather than something inside the
        // shared root.
        if (part.find(':') != std::string::npos) return std::nullopt;
        if (part == "..") {
            if (segments.empty()) return std::nullopt;
            segments.pop_back();
            continue;
        }
        segments.push_back(part);
    }
    return segments;
}

fs::path toFilesystemPath(const fs::path& root, const std::vector<std::string>& segments) {
    fs::path result = root;
    for (const auto& segment : segments) result /= segment;
    return result;
}

std::string toVirtualPathString(const std::vector<std::string>& segments) {
    std::string path = "/";
    for (size_t i = 0; i < segments.size(); ++i) {
        path += segments[i];
        if (i + 1 < segments.size()) path += "/";
    }
    return path;
}

bool sendAll(SOCKET socket, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        const int n = ::send(socket, data + sent, length - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool sendLine(SOCKET socket, const std::string& line) {
    const std::string withCrlf = line + "\r\n";
    return sendAll(socket, withCrlf.data(), static_cast<int>(withCrlf.size()));
}

// One control-connection session: command parsing/dispatch plus the
// short-lived PASV data connection each transfer command opens. Lives
// entirely inside this .cpp - WinsockFtpServer::handleClient is the only
// thing that constructs one.
class ClientSession {
public:
    ClientSession(SOCKET controlSocket, fs::path root) : controlSocket_(controlSocket), root_(std::move(root)) {}

    ~ClientSession() {
        closeDataListenSocket();
        ::closesocket(controlSocket_);
    }

    void run() {
        DWORD timeoutMs = 15000;  // generous - one client thread per control connection, not shared
        ::setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs),
                     sizeof(timeoutMs));

        sendLine(controlSocket_, "220 Game Transfer Hub FTP server ready.");

        std::string line;
        while (readLine(line)) {
            if (!handleCommand(line)) break;
        }
    }

private:
    bool readLine(std::string& outLine) {
        auto newlinePos = buffer_.find('\n');
        while (newlinePos == std::string::npos) {
            std::array<char, 512> chunk{};
            const int received = ::recv(controlSocket_, chunk.data(), static_cast<int>(chunk.size()), 0);
            if (received <= 0) return false;
            buffer_.append(chunk.data(), static_cast<size_t>(received));
            if (buffer_.size() > 16384) return false;  // runaway line - bail out rather than grow forever
            newlinePos = buffer_.find('\n');
        }

        outLine = buffer_.substr(0, newlinePos);
        buffer_.erase(0, newlinePos + 1);
        if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
        return true;
    }

    static void splitCommand(const std::string& line, std::string& verb, std::string& argument) {
        const auto spacePos = line.find(' ');
        if (spacePos == std::string::npos) {
            verb = line;
            argument.clear();
        } else {
            verb = line.substr(0, spacePos);
            argument = line.substr(spacePos + 1);
        }
        for (auto& c : verb) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    }

    // Returns false to close the connection (QUIT or an unreadable line).
    bool handleCommand(const std::string& line) {
        std::string verb, argument;
        splitCommand(line, verb, argument);

        if (verb == "USER") {
            sendLine(controlSocket_, "331 User name okay, need password.");
        } else if (verb == "PASS") {
            sendLine(controlSocket_, "230 Logged in.");
        } else if (verb == "SYST") {
            sendLine(controlSocket_, "215 UNIX Type: L8");
        } else if (verb == "TYPE") {
            sendLine(controlSocket_, "200 Type set to " + (argument.empty() ? std::string("I") : argument) + ".");
        } else if (verb == "PWD" || verb == "XPWD") {
            sendLine(controlSocket_, "257 \"" + toVirtualPathString(currentSegments_) + "\" is the current directory.");
        } else if (verb == "CWD" || verb == "XCWD") {
            doCwd(argument);
        } else if (verb == "CDUP" || verb == "XCUP") {
            doCwd("..");
        } else if (verb == "PASV") {
            doPasv();
        } else if (verb == "LIST" || verb == "NLST") {
            doList(argument, verb == "NLST");
        } else if (verb == "RETR") {
            doRetr(argument);
        } else if (verb == "STOR") {
            doStor(argument);
        } else if (verb == "NOOP") {
            sendLine(controlSocket_, "200 OK.");
        } else if (verb == "QUIT") {
            sendLine(controlSocket_, "221 Goodbye.");
            return false;
        } else {
            sendLine(controlSocket_, "502 Command not implemented.");
        }
        return true;
    }

    void doCwd(const std::string& argument) {
        const auto resolved = normalizeVirtualPath(currentSegments_, argument);
        if (!resolved) {
            sendLine(controlSocket_, "550 Failed to change directory.");
            return;
        }

        std::error_code ec;
        const fs::path target = toFilesystemPath(root_, *resolved);
        if (!fs::is_directory(target, ec) || ec) {
            sendLine(controlSocket_, "550 Failed to change directory.");
            return;
        }

        currentSegments_ = *resolved;
        sendLine(controlSocket_, "250 Directory changed.");
    }

    void doPasv() {
        closeDataListenSocket();

        dataListenSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (dataListenSocket_ == INVALID_SOCKET) {
            sendLine(controlSocket_, "425 Can't open data connection.");
            return;
        }

        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_addr.s_addr = INADDR_ANY;
        bindAddr.sin_port = 0;  // let the OS pick an ephemeral port
        if (::bind(dataListenSocket_, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0 ||
            ::listen(dataListenSocket_, 1) != 0) {
            closeDataListenSocket();
            sendLine(controlSocket_, "425 Can't open data connection.");
            return;
        }

        sockaddr_in boundAddr{};
        int boundLen = sizeof(boundAddr);
        ::getsockname(dataListenSocket_, reinterpret_cast<sockaddr*>(&boundAddr), &boundLen);
        const int dataPort = ::ntohs(boundAddr.sin_port);

        // Report the address the client is already talking to us on (the
        // control socket's local endpoint), not INADDR_ANY - the machine may
        // have several LAN interfaces.
        sockaddr_in localAddr{};
        int localLen = sizeof(localAddr);
        ::getsockname(controlSocket_, reinterpret_cast<sockaddr*>(&localAddr), &localLen);
        const auto* ipBytes = reinterpret_cast<const unsigned char*>(&localAddr.sin_addr);

        std::array<char, 128> response{};
        std::snprintf(response.data(), response.size(), "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).", ipBytes[0],
                      ipBytes[1], ipBytes[2], ipBytes[3], (dataPort >> 8) & 0xFF, dataPort & 0xFF);
        sendLine(controlSocket_, response.data());
    }

    // Accepts exactly one data connection on the PASV listening socket (with
    // a timeout so a client that never connects can't hang the thread
    // forever), then closes the listening socket - a fresh PASV is required
    // per transfer, matching real FTP servers.
    SOCKET acceptDataConnection() {
        if (dataListenSocket_ == INVALID_SOCKET) return INVALID_SOCKET;

        u_long nonBlocking = 1;
        ::ioctlsocket(dataListenSocket_, FIONBIO, &nonBlocking);

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(dataListenSocket_, &readSet);
        timeval tv{};
        tv.tv_sec = 15;
        tv.tv_usec = 0;

        SOCKET accepted = INVALID_SOCKET;
        if (::select(0, &readSet, nullptr, nullptr, &tv) > 0) {
            accepted = ::accept(dataListenSocket_, nullptr, nullptr);
            if (accepted != INVALID_SOCKET) {
                // Same inherited-non-blocking-mode pitfall as the control
                // socket (see acceptLoop): the transfer send/recv loops
                // below assume blocking semantics.
                u_long blockingMode = 0;
                ::ioctlsocket(accepted, FIONBIO, &blockingMode);
            }
        }
        closeDataListenSocket();
        return accepted;
    }

    void closeDataListenSocket() {
        if (dataListenSocket_ != INVALID_SOCKET) {
            ::closesocket(dataListenSocket_);
            dataListenSocket_ = INVALID_SOCKET;
        }
    }

    void doList(const std::string& argument, bool namesOnly) {
        const auto resolved = normalizeVirtualPath(currentSegments_, argument);
        if (!resolved) {
            sendLine(controlSocket_, "550 Failed to list directory.");
            return;
        }

        std::error_code ec;
        const fs::path target = toFilesystemPath(root_, *resolved);
        if (!fs::is_directory(target, ec) || ec) {
            sendLine(controlSocket_, "550 Failed to list directory.");
            return;
        }

        sendLine(controlSocket_, "150 Opening data connection for directory listing.");
        const SOCKET dataSocket = acceptDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            sendLine(controlSocket_, "425 Can't open data connection.");
            return;
        }

        std::string listing;
        for (const auto& entry : fs::directory_iterator(target, ec)) {
            if (ec) break;
            std::error_code entryEc;
            const bool isDir = entry.is_directory(entryEc);
            const std::string name = entry.path().filename().string();
            if (namesOnly) {
                listing += name + "\r\n";
                continue;
            }
            const long long size = isDir ? 0 : static_cast<long long>(entry.file_size(entryEc));
            std::array<char, 768> lineBuf{};
            std::snprintf(lineBuf.data(), lineBuf.size(), "%crwxr-xr-x 1 owner group %lld Jan 01 00:00 %s\r\n",
                          isDir ? 'd' : '-', size, name.c_str());
            listing += lineBuf.data();
        }

        sendAll(dataSocket, listing.data(), static_cast<int>(listing.size()));
        ::closesocket(dataSocket);
        sendLine(controlSocket_, "226 Transfer complete.");
    }

    void doRetr(const std::string& argument) {
        const auto resolved = normalizeVirtualPath(currentSegments_, argument);
        if (!resolved || resolved->empty()) {
            sendLine(controlSocket_, "550 File not found.");
            return;
        }

        std::error_code ec;
        const fs::path target = toFilesystemPath(root_, *resolved);
        if (!fs::is_regular_file(target, ec) || ec) {
            sendLine(controlSocket_, "550 File not found.");
            return;
        }

        std::ifstream file(target, std::ios::binary);
        if (!file) {
            sendLine(controlSocket_, "550 File not found.");
            return;
        }

        sendLine(controlSocket_, "150 Opening data connection for file transfer.");
        const SOCKET dataSocket = acceptDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            sendLine(controlSocket_, "425 Can't open data connection.");
            return;
        }

        std::array<char, 8192> chunk{};
        bool ok = true;
        while (true) {
            file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const std::streamsize count = file.gcount();
            if (count <= 0) break;
            if (!sendAll(dataSocket, chunk.data(), static_cast<int>(count))) {
                ok = false;
                break;
            }
            if (!file) break;
        }

        ::closesocket(dataSocket);
        sendLine(controlSocket_, ok ? "226 Transfer complete." : "426 Connection closed; transfer aborted.");
    }

    void doStor(const std::string& argument) {
        const auto resolved = normalizeVirtualPath(currentSegments_, argument);
        if (!resolved || resolved->empty()) {
            sendLine(controlSocket_, "550 Permission denied.");
            return;
        }

        std::error_code ec;
        const fs::path target = toFilesystemPath(root_, *resolved);
        // STOR doesn't implicitly create directories - the parent must
        // already exist under the shared root.
        if (!fs::is_directory(target.parent_path(), ec) || ec) {
            sendLine(controlSocket_, "550 Permission denied.");
            return;
        }

        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        if (!file) {
            sendLine(controlSocket_, "550 Permission denied.");
            return;
        }

        sendLine(controlSocket_, "150 Opening data connection for file transfer.");
        const SOCKET dataSocket = acceptDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            sendLine(controlSocket_, "425 Can't open data connection.");
            return;
        }

        std::array<char, 8192> chunk{};
        int received;
        while ((received = ::recv(dataSocket, chunk.data(), static_cast<int>(chunk.size()), 0)) > 0) {
            file.write(chunk.data(), received);
        }

        file.close();
        ::closesocket(dataSocket);
        sendLine(controlSocket_, "226 Transfer complete.");
    }

    SOCKET controlSocket_;
    fs::path root_;
    std::vector<std::string> currentSegments_;
    std::string buffer_;
    SOCKET dataListenSocket_ = INVALID_SOCKET;
};

}  // namespace

WinsockFtpServer::~WinsockFtpServer() { stop(); }

void WinsockFtpServer::start(const core::FtpServerConfig& config) {
    if (running_.exchange(true)) return;  // already running
    acceptThread_ = std::jthread(&WinsockFtpServer::acceptLoop, this, config);
}

void WinsockFtpServer::stop() {
    if (!running_.exchange(false)) return;  // wasn't running
    if (acceptThread_.joinable()) acceptThread_.join();
    port_.store(0);
}

void WinsockFtpServer::acceptLoop(core::FtpServerConfig config) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::path(config.rootPath), ec);
    if (ec) {
        running_.store(false);
        return;
    }
    if (!fs::is_directory(root, ec) || ec) {
        running_.store(false);
        return;
    }

    if (WSADATA wsaData; ::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        running_.store(false);
        return;
    }

    const SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        ::WSACleanup();
        running_.store(false);
        return;
    }

    // SO_REUSEADDR lets a restarted server rebind immediately instead of
    // waiting out the previous socket's TIME_WAIT.
    BOOL reuse = TRUE;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = ::htons(static_cast<u_short>(config.port));

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listenSocket, SOMAXCONN) != 0) {
        ::closesocket(listenSocket);
        ::WSACleanup();
        running_.store(false);
        return;
    }

    sockaddr_in boundAddr{};
    int boundLen = sizeof(boundAddr);
    ::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&boundAddr), &boundLen);
    port_.store(::ntohs(boundAddr.sin_port));

    u_long nonBlocking = 1;
    ::ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

    std::vector<std::jthread> connectionThreads;
    const std::string rootString = root.string();

    while (running_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // 200ms poll - short enough that stop() feels responsive

        if (::select(0, &readSet, nullptr, nullptr, &tv) > 0) {
            const SOCKET clientSocket = ::accept(listenSocket, nullptr, nullptr);
            if (clientSocket != INVALID_SOCKET) {
                // On Windows, a socket returned by accept() inherits the
                // listening socket's non-blocking (FIONBIO) mode - but
                // ClientSession::readLine() relies on blocking recv()
                // semantics (it treats any negative return as "connection
                // closed"). Without resetting this, the very first recv()
                // races the client's next line and fails with
                // WSAEWOULDBLOCK, which reads as an immediate disconnect.
                u_long blockingMode = 0;
                ::ioctlsocket(clientSocket, FIONBIO, &blockingMode);
                connectionThreads.emplace_back(&WinsockFtpServer::handleClient, this,
                                                static_cast<std::uintptr_t>(clientSocket), rootString);
            }
        }
    }

    for (auto& t : connectionThreads) {
        if (t.joinable()) t.join();
    }

    ::closesocket(listenSocket);
    ::WSACleanup();
}

void WinsockFtpServer::handleClient(std::uintptr_t clientSocketHandle, std::string rootPath) {
    const SOCKET clientSocket = static_cast<SOCKET>(clientSocketHandle);
    ClientSession session(clientSocket, fs::path(rootPath));
    session.run();
}

}  // namespace gth::ftpserver
