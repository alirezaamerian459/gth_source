#include "gth/network/TcpRemoteReportSender.h"

#include <winsock2.h>

#include <ws2tcpip.h>

#include <array>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace gth::network {

bool TcpRemoteReportSender::send(const std::string& serverAddress, int port, const std::string& nodeId,
                                  const std::string& nodeName, const std::string& summaryLine) {
    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    bool success = false;
    const SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != INVALID_SOCKET) {
        DWORD timeoutMs = 5000;
        ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(static_cast<u_short>(port));

        // Accept a hostname as well as a dotted-quad: try InetPton first
        // (the common case, no DNS lookup needed), fall back to
        // getaddrinfo for a hostname like "nas01.local".
        if (::InetPtonA(AF_INET, serverAddress.c_str(), &addr.sin_addr) != 1) {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* resolved = nullptr;
            if (::getaddrinfo(serverAddress.c_str(), nullptr, &hints, &resolved) == 0 && resolved != nullptr) {
                addr.sin_addr = reinterpret_cast<sockaddr_in*>(resolved->ai_addr)->sin_addr;
                ::freeaddrinfo(resolved);
            } else {
                ::closesocket(sock);
                ::WSACleanup();
                return false;
            }
        }

        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            const std::string line = nodeId + "\t" + nodeName + "\t" + summaryLine + "\n";
            if (::send(sock, line.c_str(), static_cast<int>(line.size()), 0) > 0) {
                std::array<char, 64> buffer;
                const int received = ::recv(sock, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
                success = received > 0 && std::strncmp(buffer.data(), "OK", 2) == 0;
            }
        }

        ::closesocket(sock);
    }

    ::WSACleanup();
    return success;
}

}  // namespace gth::network
