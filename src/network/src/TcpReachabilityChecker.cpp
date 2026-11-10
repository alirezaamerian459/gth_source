#include "gth/network/TcpReachabilityChecker.h"

#include <winsock2.h>

#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace gth::network {

bool TcpReachabilityChecker::isReachable(const std::string& ipAddress, int port, int timeoutMilliseconds) {
    if (WSADATA wsaData; ::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    bool reachable = false;
    const SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != INVALID_SOCKET) {
        u_long nonBlocking = 1;
        ::ioctlsocket(sock, FIONBIO, &nonBlocking);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(static_cast<u_short>(port));

        if (::InetPtonA(AF_INET, ipAddress.c_str(), &addr.sin_addr) == 1) {
            const int connectResult = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            if (connectResult == 0) {
                reachable = true;
            } else if (::WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set writeSet;
                FD_ZERO(&writeSet);
                FD_SET(sock, &writeSet);

                timeval tv{};
                tv.tv_sec = timeoutMilliseconds / 1000;
                tv.tv_usec = (timeoutMilliseconds % 1000) * 1000;

                if (::select(0, nullptr, &writeSet, nullptr, &tv) > 0) {
                    int soError = 0;
                    int soErrorLen = sizeof(soError);
                    if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soErrorLen) ==
                            0 &&
                        soError == 0) {
                        reachable = true;
                    }
                }
            }
        }

        ::closesocket(sock);
    }

    ::WSACleanup();
    return reachable;
}

}  // namespace gth::network
