#include "gth/network/IcmpHostPinger.h"

#include <winsock2.h>

#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>

#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace gth::network {

std::optional<int> IcmpHostPinger::pingMs(const std::string& ip) {
    IN_ADDR addr{};
    if (::InetPtonA(AF_INET, ip.c_str(), &addr) != 1) return std::nullopt;

    const HANDLE icmpHandle = ::IcmpCreateFile();
    if (icmpHandle == INVALID_HANDLE_VALUE) return std::nullopt;

    const char payload[] = "gth-accelerator-ping";
    const DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8;
    std::vector<BYTE> replyBuffer(replySize);

    const DWORD result = ::IcmpSendEcho(icmpHandle, addr.S_un.S_addr, const_cast<char*>(payload), sizeof(payload),
                                         nullptr, replyBuffer.data(), replySize, 1000);
    ::IcmpCloseHandle(icmpHandle);

    if (result == 0) return std::nullopt;

    const auto* reply = reinterpret_cast<PICMP_ECHO_REPLY>(replyBuffer.data());
    if (reply->Status != IP_SUCCESS) return std::nullopt;
    return static_cast<int>(reply->RoundTripTime);
}

}  // namespace gth::network
