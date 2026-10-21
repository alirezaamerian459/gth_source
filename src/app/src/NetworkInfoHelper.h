#pragma once

#include <string>
#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace gth::network {

class NetworkInfoHelper {
public:
    struct NetworkInfo {
        std::wstring ipAddress;
        int port;
        std::wstring connectionUrl;  // "ip:port"
        bool isValid;
    };

    /// Gets the local machine's IPv4 address
    static NetworkInfo GetLocalNetworkInfo(int servicePort = 5000) {
        NetworkInfo info;
        info.port = servicePort;
        info.isValid = false;

        // gethostname()/gethostbyname() are Winsock calls - they fail
        // (SOCKET_ERROR / null) until WSAStartup has been called at least
        // once for this process. RAII guard so every return path below
        // (including the early ones) cleans up via WSACleanup automatically
        // instead of needing it repeated at each return.
        struct WinsockScope {
            bool ok;
            WinsockScope() {
                WSADATA wsaData;
                ok = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
            }
            ~WinsockScope() {
                if (ok) WSACleanup();
            }
        } winsock;
        if (!winsock.ok) return info;

        // Get hostname
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
            return info;
        }

        // Get IP from hostname
        struct hostent* he = gethostbyname(hostname);
        if (!he || he->h_addrtype != AF_INET) {
            return info;
        }

        // Convert IP address
        struct in_addr** addr_list = (struct in_addr**)he->h_addr_list;
        if (!addr_list[0]) {
            return info;
        }

        // Get first IPv4 address (skip loopback if possible)
        for (int i = 0; addr_list[i]; i++) {
            std::string ipStr = inet_ntoa(*addr_list[i]);

            // Skip loopback
            if (ipStr.find("127.") != 0) {
                info.ipAddress = std::wstring(ipStr.begin(), ipStr.end());
                info.isValid = true;
                break;
            }
        }

        // Fallback to first address if all were loopback
        if (!info.isValid && addr_list[0]) {
            std::string ipStr = inet_ntoa(*addr_list[0]);
            info.ipAddress = std::wstring(ipStr.begin(), ipStr.end());
            info.isValid = true;
        }

        // Build connection URL
        if (info.isValid) {
            wchar_t buf[256];
            swprintf_s(buf, L"%s:%d", info.ipAddress.c_str(), servicePort);
            info.connectionUrl = buf;
        }

        return info;
    }

    /// Gets local IP using alternative method (Winsock2)
    static std::wstring GetLocalIPAlternative() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return L"";
        }

        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
            WSACleanup();
            return L"";
        }

        struct hostent* pHost = gethostbyname(hostname);
        if (!pHost) {
            WSACleanup();
            return L"";
        }

        struct in_addr ipaddr;
        ipaddr.s_addr = *(u_long*)pHost->h_addr_list[0];
        std::string ipStr = inet_ntoa(ipaddr);

        WSACleanup();

        return std::wstring(ipStr.begin(), ipStr.end());
    }
};

}  // namespace gth::network
