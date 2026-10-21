#pragma once

#include <optional>
#include <string>
#include <vector>

// Pure HTTP request-line/header/target-URL parsing used by WinsockCachingProxyServer.cpp, lifted out of its
// anonymous namespace into a named one so a unit test can exercise this logic directly, without a socket or a
// window - see tests/unit/test_winsock_caching_proxy_logic.cpp.
namespace gth::acceleratorserver::internal {

std::string toLower(std::string s);

struct HeaderLine {
    std::string name;
    std::string value;
};

struct ParsedRequest {
    std::string method;
    std::string target;
    std::string version;
    std::vector<HeaderLine> headers;
    std::string leftoverBody;  // bytes already read past the header block's blank line
};

std::optional<std::string> findHeader(const std::vector<HeaderLine>& headers, const std::string& nameLower);
bool parseRequestLine(const std::string& line, ParsedRequest& req);
void parseHeaderLines(const std::string& block, std::vector<HeaderLine>& headers);

// "http://host[:port]/path" -> host/port/path. Falls back to the request's
// own Host header (origin-form request, the shape a transparent proxy
// config typically produces) when the target isn't absolute-form.
struct TargetUrl {
    std::string host;
    int port = 80;
    std::string pathAndQuery = "/";
};

bool resolveTargetUrl(const ParsedRequest& req, TargetUrl& out);
std::string absoluteUrl(const TargetUrl& t);

// Headers that describe this specific hop, never meaningful to forward.
bool isHopByHop(const std::string& nameLower);

}  // namespace gth::acceleratorserver::internal
