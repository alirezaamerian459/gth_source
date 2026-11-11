#include "gth/application/ftp/FtpServerService.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace gth::application {

namespace {

std::string safeVirtualComponent(std::string value) {
    for (char& c : value) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' ||
            c == '*') {
            c = '_';
        }
    }
    if (value.empty() || value == "." || value == "..") value = "Untitled";
    return value;
}

std::string uniqueGameLabel(const JbGameEntry& entry, std::set<std::string>& usedLabels) {
    std::string base = safeVirtualComponent(entry.title);
    if (base.empty()) base = "Untitled";
    std::string label = base;
    int suffix = 2;
    while (!usedLabels.insert(label).second) label = base + " (" + std::to_string(suffix++) + ")";
    return label;
}

}  // namespace

FtpServerService::FtpServerService(core::IFtpServer& server, core::INetworkInterfaceDetector& networkInterfaceDetector,
                                   IJbLibraryService* jbLibraryService)
    : server_(server), networkInterfaceDetector_(networkInterfaceDetector), jbLibraryService_(jbLibraryService) {}

void FtpServerService::start(const std::string& rootPath, int port) {
    start(rootPath, port, false);
}

void FtpServerService::start(const std::string& rootPath, int port, bool includeJbLibrary) {
    core::FtpServerConfig config;
    config.rootPath = rootPath;
    config.port = port;

    if (includeJbLibrary && jbLibraryService_) {
        const auto catalog = jbLibraryService_->scan();
        std::set<std::string> usedLabels;
        for (const auto& entry : catalog.entries) {
            std::error_code ec;
            if (!std::filesystem::exists(entry.path, ec) || ec) continue;
            const std::string label = uniqueGameLabel(entry, usedLabels);
            config.virtualMappings.push_back({"JB Library/" + label, entry.path.string(), true});
        }
    }

    server_.start(config);
}

void FtpServerService::stop() { server_.stop(); }

bool FtpServerService::isRunning() const { return server_.isRunning(); }

int FtpServerService::port() const { return server_.port(); }

std::vector<std::string> FtpServerService::listenAddresses() {
    std::vector<std::string> addresses;
    for (const auto& iface : networkInterfaceDetector_.detect()) {
        if (!iface.ipv4.empty() && iface.status == core::NetworkInterfaceStatus::Up) addresses.push_back(iface.ipv4);
    }
    return addresses;
}

}  // namespace gth::application
