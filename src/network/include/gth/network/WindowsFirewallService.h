#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "gth/core/network/IFirewallService.h"

namespace gth::network {

// One Windows Firewall rule this app wants for itself.
struct FirewallRuleSpec {
    std::wstring name;
    std::wstring description;
    bool inbound = true;
    long protocol = 256;               // NET_FW_IP_PROTOCOL_ANY; 6 = TCP
    std::wstring remoteAddresses;      // "LocalSubnet", or empty for any
    std::wstring applicationPath;
};

// The rules the app installs for the exe at `exePath`. Program-scoped rather
// than port-scoped on purpose: FTP passive data connections, the HTTP proxy
// and the accelerator all use ports that vary, and a program rule covers
// them without opening those ports to any other software. Inbound is limited
// to the local subnet, so consoles and PCs on the same network can connect
// but nothing from the wider internet can.
std::vector<FirewallRuleSpec> BuildFirewallRuleSpecs(const std::wstring& exePath);

// Windows paths compare case-insensitively.
bool SameFirewallPath(const std::wstring& a, const std::wstring& b);

// Whether an existing firewall rule is one that stops this exe from working:
// enabled, a Block action, and aimed at this exact program. Windows itself
// creates these (e.g. "gametransferhub.exe", blocked on Public networks) when
// its "allow this app?" prompt is dismissed, and a block rule always beats an
// allow rule - so an allow rule alone can't fix that situation.
bool IsBlockingRuleForApp(bool actionIsBlock, bool enabled, const std::wstring& ruleApplicationPath,
                          const std::wstring& exePath);

// Program locations whose firewall rules the user removed on purpose, kept in
// a small text file (one path per line) so a later launch doesn't quietly put
// them back.
class FirewallOptOutList {
public:
    explicit FirewallOptOutList(std::filesystem::path file) : file_(std::move(file)) {}

    bool contains(const std::wstring& exePath) const;
    void add(const std::wstring& exePath) const;
    void remove(const std::wstring& exePath) const;

private:
    std::filesystem::path file_;
};

class WindowsFirewallService : public core::IFirewallService {
public:
    // `optOutMarkerPath` is the file backing a FirewallOptOutList.
    explicit WindowsFirewallService(std::filesystem::path optOutMarkerPath);

    core::FirewallStatus status() override;
    core::FirewallResult allowThisApp() override;
    core::FirewallResult removeAppRules() override;
    std::optional<core::FirewallResult> allowThisAppIfNeeded() override;

private:
    bool optedOut() const;
    void recordOptOut() const;
    void clearOptOut() const;

    FirewallOptOutList optOut_;
};

}  // namespace gth::network
