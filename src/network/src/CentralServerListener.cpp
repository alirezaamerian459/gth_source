#include "gth/network/CentralServerListener.h"

#include <winsock2.h>

#include <ws2tcpip.h>

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace gth::network {

namespace {
// Splits "<nodeId>\t<name>\t<summary...>" into exactly 3 parts - summary is
// everything after the second tab, so it may itself contain further
// structure without needing escaping.
bool splitReportLine(const std::string& line, std::string& nodeId, std::string& name, std::string& summary) {
    const auto firstTab = line.find('\t');
    if (firstTab == std::string::npos) return false;
    const auto secondTab = line.find('\t', firstTab + 1);
    if (secondTab == std::string::npos) return false;

    nodeId = line.substr(0, firstTab);
    name = line.substr(firstTab + 1, secondTab - firstTab - 1);
    summary = line.substr(secondTab + 1);
    return !nodeId.empty();
}

std::string peerAddressOf(SOCKET clientSocket) {
    sockaddr_in addr{};
    int addrLen = sizeof(addr);
    if (::getpeername(clientSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) return "unknown";

    char buffer[INET_ADDRSTRLEN]{};
    if (!::InetNtopA(AF_INET, &addr.sin_addr, buffer, sizeof(buffer))) return "unknown";
    return buffer;
}
}  // namespace

CentralServerListener::~CentralServerListener() { stop(); }

void CentralServerListener::start(int port, ReportHandler onReport) {
    if (running_.exchange(true)) return;  // already running
    onReport_ = std::move(onReport);
    acceptThread_ = std::thread(&CentralServerListener::acceptLoop, this, port);
}

void CentralServerListener::stop() {
    if (!running_.exchange(false)) return;  // wasn't running
    if (acceptThread_.joinable()) acceptThread_.join();
}

void CentralServerListener::acceptLoop(int port) {
    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
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
    addr.sin_port = ::htons(static_cast<u_short>(port));

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listenSocket, SOMAXCONN) != 0) {
        ::closesocket(listenSocket);
        ::WSACleanup();
        running_.store(false);
        return;
    }

    u_long nonBlocking = 1;
    ::ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

    std::vector<std::thread> connectionThreads;

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
                connectionThreads.emplace_back(&CentralServerListener::handleConnection, this,
                                                static_cast<std::uintptr_t>(clientSocket));
            }
        }
    }

    for (auto& t : connectionThreads) {
        if (t.joinable()) t.join();
    }

    ::closesocket(listenSocket);
    ::WSACleanup();
}

void CentralServerListener::handleConnection(std::uintptr_t clientSocketHandle) {
    const SOCKET clientSocket = static_cast<SOCKET>(clientSocketHandle);

    DWORD timeoutMs = 5000;
    ::setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    std::string line;
    char buffer[512];
    while (line.find('\n') == std::string::npos && line.size() < 8192) {
        const int received = ::recv(clientSocket, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        line.append(buffer, static_cast<size_t>(received));
    }

    const auto newline = line.find('\n');
    if (newline != std::string::npos) line.erase(newline);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string nodeId, name, summary;
    if (splitReportLine(line, nodeId, name, summary)) {
        const std::string peerAddress = peerAddressOf(clientSocket);
        if (onReport_) onReport_(nodeId, name, summary, peerAddress);

        const char* ack = "OK\n";
        ::send(clientSocket, ack, static_cast<int>(strlen(ack)), 0);
    } else {
        const char* err = "ERROR\tmalformed report\n";
        ::send(clientSocket, err, static_cast<int>(strlen(err)), 0);
    }

    ::closesocket(clientSocket);
}

}  // namespace gth::network
