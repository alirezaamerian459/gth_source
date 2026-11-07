#include "gth/network/Win32NetworkInterfaceDetector.h"

#include <winsock2.h>

#include <iphlpapi.h>
#include <ws2tcpip.h>

#include <array>
#include <cstdio>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace gth::network {

using core::NetworkInterfaceInfo;
using core::NetworkInterfaceStatus;
using core::NetworkInterfaceType;

namespace {

std::string ipToString(const SOCKADDR* addr) {
    std::array<char, INET6_ADDRSTRLEN> buf{};
    if (addr->sa_family == AF_INET) {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        ::InetNtopA(AF_INET, const_cast<IN_ADDR*>(&addr4->sin_addr), buf.data(), static_cast<DWORD>(buf.size()));
    } else if (addr->sa_family == AF_INET6) {
        const auto* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        ::InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&addr6->sin6_addr), buf.data(), static_cast<DWORD>(buf.size()));
    }
    return buf.data();
}

std::string macToString(const BYTE* addr, ULONG len) {
    if (len < 6) return "";
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4],
                   addr[5]);
    return buf.data();
}

std::string wideToUtf8(const wchar_t* wide) {
    if (!wide) return "";
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

NetworkInterfaceType classifyType(IFTYPE ifType) {
    switch (ifType) {
        case IF_TYPE_ETHERNET_CSMACD: return NetworkInterfaceType::Ethernet;
        case IF_TYPE_IEEE80211: return NetworkInterfaceType::WiFi;
        case IF_TYPE_TUNNEL:
        case IF_TYPE_PROP_VIRTUAL: return NetworkInterfaceType::VirtualAdapter;
        default: return NetworkInterfaceType::Other;
    }
}

}  // namespace

std::vector<NetworkInterfaceInfo> Win32NetworkInterfaceDetector::detect() {
    std::vector<NetworkInterfaceInfo> results;

    ULONG bufferSize = 15000;
    std::vector<BYTE> buffer(bufferSize);
    auto* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    DWORD apiResult = ::GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &bufferSize);
    if (apiResult == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        apiResult = ::GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &bufferSize);
    }
    if (apiResult != NO_ERROR) return results;

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        NetworkInterfaceInfo info;
        info.name = wideToUtf8(adapter->FriendlyName);
        info.type = classifyType(adapter->IfType);
        info.status = adapter->OperStatus == IfOperStatusUp ? NetworkInterfaceStatus::Up : NetworkInterfaceStatus::Down;
        info.macAddress = macToString(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
        info.speedBitsPerSecond = static_cast<long long>(adapter->TransmitLinkSpeed);

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            auto* sockAddr = unicast->Address.lpSockaddr;
            if (sockAddr->sa_family == AF_INET && info.ipv4.empty()) {
                info.ipv4 = ipToString(sockAddr);

                ULONG mask = 0;
                ::ConvertLengthToIpv4Mask(unicast->OnLinkPrefixLength, &mask);
                IN_ADDR maskAddr{};
                maskAddr.S_un.S_addr = mask;
                std::array<char, INET_ADDRSTRLEN> maskBuf{};
                ::InetNtopA(AF_INET, &maskAddr, maskBuf.data(), static_cast<DWORD>(maskBuf.size()));
                info.subnetMask = maskBuf.data();
            } else if (sockAddr->sa_family == AF_INET6 && info.ipv6.empty()) {
                info.ipv6 = ipToString(sockAddr);
            }
        }

        if (adapter->FirstGatewayAddress) {
            info.gateway = ipToString(adapter->FirstGatewayAddress->Address.lpSockaddr);
        }

        results.push_back(std::move(info));
    }

    return results;
}

}  // namespace gth::network
