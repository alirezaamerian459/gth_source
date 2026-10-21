#include "gth/acceleratorserver/WinsockCachingProxyServerInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace gth::acceleratorserver::internal {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<std::string> findHeader(const std::vector<HeaderLine>& headers, const std::string& nameLower) {
    for (const auto& h : headers) {
        if (toLower(h.name) == nameLower) return h.value;
    }
    return std::nullopt;
}

bool parseRequestLine(const std::string& line, ParsedRequest& req) {
    std::istringstream iss(line);
    return static_cast<bool>(iss >> req.method >> req.target >> req.version);
}

void parseHeaderLines(const std::string& block, std::vector<HeaderLine>& headers) {
    std::istringstream iss(block);
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            continue;  // request/status line, handled separately
        }
        if (line.empty()) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        HeaderLine h;
        h.name = line.substr(0, colon);
        size_t valueStart = colon + 1;
        while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
        h.value = line.substr(valueStart);
        headers.push_back(std::move(h));
    }
}

bool resolveTargetUrl(const ParsedRequest& req, TargetUrl& out) {
    std::string target = req.target;
    if (toLower(target).rfind("http://", 0) == 0) {
        target = target.substr(7);
        const auto slash = target.find('/');
        std::string hostPort = slash == std::string::npos ? target : target.substr(0, slash);
        out.pathAndQuery = slash == std::string::npos ? "/" : target.substr(slash);
        const auto colon = hostPort.find(':');
        out.host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
        out.port = colon == std::string::npos ? 80 : std::atoi(hostPort.substr(colon + 1).c_str());
        return !out.host.empty();
    }

    const auto hostHeader = findHeader(req.headers, "host");
    if (!hostHeader) return false;
    const auto colon = hostHeader->find(':');
    out.host = colon == std::string::npos ? *hostHeader : hostHeader->substr(0, colon);
    out.port = colon == std::string::npos ? 80 : std::atoi(hostHeader->substr(colon + 1).c_str());
    out.pathAndQuery = req.target.empty() ? "/" : req.target;
    return !out.host.empty();
}

std::string absoluteUrl(const TargetUrl& t) {
    std::string url = "http://" + t.host;
    if (t.port != 80) url += ":" + std::to_string(t.port);
    url += t.pathAndQuery;
    return url;
}

bool isHopByHop(const std::string& nameLower) {
    return nameLower == "proxy-connection" || nameLower == "proxy-authorization" || nameLower == "connection" ||
           nameLower == "keep-alive";
}

}  // namespace gth::acceleratorserver::internal
