#pragma once

#include <array>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace gth::network {

namespace detail {

class WinsockScope {
public:
    WinsockScope() {
        WSADATA data{};
        started_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockScope() {
        if (started_) ::WSACleanup();
    }

    bool started() const noexcept { return started_; }

private:
    bool started_ = false;
};

class AddressInfoList {
public:
    explicit AddressInfoList(ADDRINFOA* addresses) : addresses_(addresses) {}
    ~AddressInfoList() {
        if (addresses_) ::freeaddrinfo(addresses_);
    }

    ADDRINFOA* get() const noexcept { return addresses_; }

private:
    ADDRINFOA* addresses_ = nullptr;
};

inline std::vector<in_addr> localIpv4Addresses() {
    std::array<char, 256> hostname{};
    if (::gethostname(hostname.data(), static_cast<int>(hostname.size())) == SOCKET_ERROR) return {};
    hostname.back() = '\0';

    ADDRINFOA hints{};
    hints.ai_family = AF_INET;
    ADDRINFOA* rawAddresses = nullptr;
    if (::getaddrinfo(hostname.data(), nullptr, &hints, &rawAddresses) != 0) return {};

    const AddressInfoList addresses(rawAddresses);
    std::vector<in_addr> result;
    for (const ADDRINFOA* current = addresses.get(); current; current = current->ai_next) {
        if (current->ai_addrlen < sizeof(sockaddr_in)) continue;
        const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
        result.push_back(socketAddress->sin_addr);
    }
    return result;
}

inline std::string ipv4ToString(const in_addr& address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    return ::InetNtopA(AF_INET, &address, buffer.data(), static_cast<DWORD>(buffer.size()))
               ? std::string(buffer.data())
               : std::string();
}

}  // namespace detail

class NetworkInfoHelper {
public:
    struct NetworkInfo {
        std::wstring ipAddress;
        int port = 0;
        std::wstring connectionUrl;  // "ip:port"
        bool isValid = false;
    };

    /// Gets the local machine's IPv4 address.
    static NetworkInfo GetLocalNetworkInfo(int servicePort = 5000) {
        NetworkInfo info;
        info.port = servicePort;

        detail::WinsockScope winsock;
        if (!winsock.started()) return info;

        const auto addresses = detail::localIpv4Addresses();
        if (addresses.empty()) return info;

        auto selected = addresses.begin();
        for (auto current = addresses.begin(); current != addresses.end(); ++current) {
            const auto hostAddress = ntohl(current->s_addr);
            if ((hostAddress >> 24) != 127) {
                selected = current;
                break;
            }
        }

        const std::string ip = detail::ipv4ToString(*selected);
        if (ip.empty()) return info;
        info.ipAddress.assign(ip.begin(), ip.end());
        info.isValid = true;

        std::array<wchar_t, 256> buffer{};
        swprintf_s(buffer.data(), buffer.size(), L"%s:%d", info.ipAddress.c_str(), servicePort);
        info.connectionUrl = buffer.data();
        return info;
    }

    /// Gets the first local IPv4 address, including loopback as before.
    static std::wstring GetLocalIPAlternative() {
        detail::WinsockScope winsock;
        if (!winsock.started()) return L"";

        const auto addresses = detail::localIpv4Addresses();
        if (addresses.empty()) return L"";
        const std::string ip = detail::ipv4ToString(addresses.front());
        return std::wstring(ip.begin(), ip.end());
    }
};

}  // namespace gth::network
