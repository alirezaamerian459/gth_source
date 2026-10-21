#include "gth/core/devices/Console.h"

namespace gth::core {

std::string toString(ConsoleProtocol protocol) {
    switch (protocol) {
        case ConsoleProtocol::Ftp: return "Ftp";
        case ConsoleProtocol::Smb: return "Smb";
        case ConsoleProtocol::Custom: return "Custom";
        case ConsoleProtocol::Http: return "Http";
        case ConsoleProtocol::Unknown:
        default: return "Unknown";
    }
}

ConsoleProtocol consoleProtocolFromString(std::string_view text) {
    if (text == "Ftp") return ConsoleProtocol::Ftp;
    if (text == "Smb") return ConsoleProtocol::Smb;
    if (text == "Custom") return ConsoleProtocol::Custom;
    if (text == "Http") return ConsoleProtocol::Http;
    return ConsoleProtocol::Unknown;
}

std::string toString(ConsoleStatus status) {
    switch (status) {
        case ConsoleStatus::Online: return "Online";
        case ConsoleStatus::Offline: return "Offline";
        case ConsoleStatus::Unknown:
        default: return "Unknown";
    }
}

ConsoleStatus consoleStatusFromString(std::string_view text) {
    if (text == "Online") return ConsoleStatus::Online;
    if (text == "Offline") return ConsoleStatus::Offline;
    return ConsoleStatus::Unknown;
}

}  // namespace gth::core
