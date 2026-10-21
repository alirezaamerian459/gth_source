#pragma once

#include <optional>
#include <string>

namespace gth::core {

struct FirewallStatus {
    // False when Windows Firewall couldn't be queried at all (its service is
    // stopped, or another firewall product manages the machine) - callers
    // should stay quiet rather than guess.
    bool available = false;
    // On for at least one network profile (Domain/Private/Public).
    bool firewallEnabled = false;
    // This app's allow rules exist and point at the copy that's running right
    // now, AND nothing enabled in Windows Firewall blocks this exe.
    bool appAllowed = false;
    // Enabled block rules aimed at this exact exe. Windows creates these when
    // its "allow this app?" prompt is dismissed (typically: blocked on Public
    // networks), and a block rule beats any allow rule.
    int blockingRules = 0;
    std::string detail;  // why `available` is false, when it is
};

struct FirewallResult {
    bool success = false;
    std::string message;
};

// Lets the app get itself through Windows Firewall instead of the user
// switching the firewall off: a program-scoped allow rule for this exe,
// limited to devices on the local network, plus switching off (never
// deleting) any rule that blocks this exact exe. Nothing here ever turns the
// firewall itself on or off, or touches any other program's rules.
class IFirewallService {
public:
    virtual ~IFirewallService() = default;

    virtual FirewallStatus status() = 0;

    // Adds the allow rules and switches off rules blocking this exe, then
    // checks the result. Clears any earlier opt-out.
    virtual FirewallResult allowThisApp() = 0;

    // Removes this app's own allow rules, and remembers (for this program
    // location) that the user doesn't want them re-added automatically.
    virtual FirewallResult removeAppRules() = 0;

    // Called at startup. Runs allowThisApp() only when it would help - the
    // firewall can be queried, this copy isn't allowed yet, and the user
    // hasn't removed the rules for it before. Returns nullopt when there was
    // nothing to do.
    virtual std::optional<FirewallResult> allowThisAppIfNeeded() = 0;
};

}  // namespace gth::core
