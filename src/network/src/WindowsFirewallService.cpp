#include "gth/network/WindowsFirewallService.h"

#include <windows.h>
#include <netfw.h>
#include <oleauto.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <fstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace gth::network {

namespace {

constexpr wchar_t kInboundRuleName[] = L"Dr Game Transfer Hub - incoming from local network";
constexpr wchar_t kOutboundRuleName[] = L"Dr Game Transfer Hub - outgoing";

std::wstring currentExePath() {
    std::wstring buffer(1024, L'\0');
    for (;;) {
        const DWORD len = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) return {};
        if (len < buffer.size()) {
            buffer.resize(len);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                          nullptr);
    std::string out(static_cast<size_t>((std::max)(len, 0)), '\0');
    if (len > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr,
                              nullptr);
    }
    return out;
}

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>((std::max)(len, 0)), L'\0');
    if (len > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    }
    return out;
}

std::string hexOf(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
    return buf;
}

// The firewall API refuses changes from a process that isn't elevated; the
// app is normally launched as administrator, so this mostly means the exe
// was started some other way.
std::string describeFailure(HRESULT hr) {
    if (hr == E_ACCESSDENIED) {
        return "Windows refused the change - the firewall can only be changed by an administrator. Close the app "
               "and start it with \"Run as administrator\".";
    }
    return "Windows Firewall returned an error (" + hexOf(hr) + ").";
}

// COM must be initialised on the calling thread. The UI thread may already
// have done it (in a different apartment mode), which is fine to run under.
class ComApartment {
public:
    ComApartment() {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owns_ = SUCCEEDED(hr);
        ok_ = owns_ || hr == RPC_E_CHANGED_MODE;
    }
    ~ComApartment() {
        if (owns_) ::CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    bool ok() const { return ok_; }

private:
    bool owns_ = false;
    bool ok_ = false;
};

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put() {
        reset();
        return &ptr_;
    }
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    T* ptr_ = nullptr;
};

class Bstr {
public:
    explicit Bstr(const std::wstring& value) : bstr_(::SysAllocStringLen(value.c_str(), static_cast<UINT>(value.size()))) {}
    ~Bstr() { ::SysFreeString(bstr_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return bstr_; }

private:
    BSTR bstr_;
};

HRESULT createPolicy(ComPtr<INetFwPolicy2>& policy) {
    return ::CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2),
                              reinterpret_cast<void**>(policy.put()));
}

// True when the rule named `name` exists, is enabled, allows, points at
// `exePath`, and runs in `inbound`'s direction.
bool ruleIsInPlace(INetFwRules* rules, const wchar_t* name, const std::wstring& exePath, bool inbound) {
    ComPtr<INetFwRule> rule;
    if (FAILED(rules->Item(Bstr(name), rule.put())) || !rule) return false;

    BSTR app = nullptr;
    const bool pathMatches = SUCCEEDED(rule->get_ApplicationName(&app)) && app != nullptr &&
                              SameFirewallPath(app, exePath);
    ::SysFreeString(app);
    if (!pathMatches) return false;

    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_IN;
    NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
    if (FAILED(rule->get_Enabled(&enabled)) || FAILED(rule->get_Direction(&direction)) ||
        FAILED(rule->get_Action(&action))) {
        return false;
    }
    return enabled != VARIANT_FALSE && action == NET_FW_ACTION_ALLOW &&
           direction == (inbound ? NET_FW_RULE_DIR_IN : NET_FW_RULE_DIR_OUT);
}

// Rules are matched by name, but a portable app that's been extracted to a
// new folder leaves an older rule with the same name pointing at the old
// path - remove every rule of that name so the fresh one is the only one.
HRESULT removeRulesNamed(INetFwRules* rules, const wchar_t* name) {
    for (int guard = 0; guard < 50; ++guard) {
        ComPtr<INetFwRule> existing;
        if (FAILED(rules->Item(Bstr(name), existing.put())) || !existing) return S_OK;

        const HRESULT hr = rules->Remove(Bstr(name));
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

// Every enabled rule that blocks this exe, as AddRef'd pointers the caller
// must release. Collected first and acted on afterwards, so no rule is
// changed in the middle of walking the collection.
std::vector<INetFwRule*> collectBlockingRules(INetFwRules* rules, const std::wstring& exePath) {
    std::vector<INetFwRule*> found;

    ComPtr<IUnknown> unknown;
    if (FAILED(rules->get__NewEnum(unknown.put())) || !unknown) return found;
    ComPtr<IEnumVARIANT> enumerator;
    if (FAILED(unknown->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(enumerator.put()))) ||
        !enumerator) {
        return found;
    }

    for (;;) {
        VARIANT item;
        ::VariantInit(&item);
        ULONG fetched = 0;
        if (enumerator->Next(1, &item, &fetched) != S_OK || fetched == 0) break;

        if (item.vt == VT_DISPATCH && item.pdispVal != nullptr) {
            ComPtr<INetFwRule> rule;
            if (SUCCEEDED(item.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(rule.put()))) &&
                rule) {
                NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
                VARIANT_BOOL enabled = VARIANT_FALSE;
                BSTR app = nullptr;
                const bool readOk = SUCCEEDED(rule->get_Action(&action)) && SUCCEEDED(rule->get_Enabled(&enabled)) &&
                                    SUCCEEDED(rule->get_ApplicationName(&app));
                if (readOk && app != nullptr &&
                    IsBlockingRuleForApp(action == NET_FW_ACTION_BLOCK, enabled != VARIANT_FALSE, app, exePath)) {
                    rule.get()->AddRef();
                    found.push_back(rule.get());
                }
                ::SysFreeString(app);
            }
        }
        ::VariantClear(&item);
    }
    return found;
}

void releaseAll(std::vector<INetFwRule*>& rules) {
    for (INetFwRule* rule : rules) rule->Release();
    rules.clear();
}

HRESULT addRule(INetFwRules* rules, const FirewallRuleSpec& spec) {
    ComPtr<INetFwRule> rule;
    HRESULT hr = ::CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
                                    reinterpret_cast<void**>(rule.put()));
    if (FAILED(hr)) return hr;

    if (FAILED(hr = rule->put_Name(Bstr(spec.name)))) return hr;
    if (FAILED(hr = rule->put_Description(Bstr(spec.description)))) return hr;
    if (FAILED(hr = rule->put_ApplicationName(Bstr(spec.applicationPath)))) return hr;
    if (FAILED(hr = rule->put_Protocol(spec.protocol))) return hr;
    if (!spec.remoteAddresses.empty() && FAILED(hr = rule->put_RemoteAddresses(Bstr(spec.remoteAddresses)))) {
        return hr;
    }
    if (FAILED(hr = rule->put_Direction(spec.inbound ? NET_FW_RULE_DIR_IN : NET_FW_RULE_DIR_OUT))) return hr;
    if (FAILED(hr = rule->put_Action(NET_FW_ACTION_ALLOW))) return hr;
    if (FAILED(hr = rule->put_Profiles(NET_FW_PROFILE2_ALL))) return hr;
    if (FAILED(hr = rule->put_Enabled(VARIANT_TRUE))) return hr;

    return rules->Add(rule.get());
}

}  // namespace

bool SameFirewallPath(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

bool IsBlockingRuleForApp(bool actionIsBlock, bool enabled, const std::wstring& ruleApplicationPath,
                          const std::wstring& exePath) {
    return actionIsBlock && enabled && !ruleApplicationPath.empty() && SameFirewallPath(ruleApplicationPath, exePath);
}

std::vector<FirewallRuleSpec> BuildFirewallRuleSpecs(const std::wstring& exePath) {
    FirewallRuleSpec inbound;
    inbound.name = kInboundRuleName;
    inbound.description =
        L"Lets other devices on your local network (consoles, other PCs) connect to Dr Game Transfer Hub's FTP "
        L"server, proxy and accelerator. Limited to the local subnet - nothing from the internet.";
    inbound.inbound = true;
    inbound.protocol = 6;  // TCP
    inbound.remoteAddresses = L"LocalSubnet";
    inbound.applicationPath = exePath;

    FirewallRuleSpec outbound;
    outbound.name = kOutboundRuleName;
    outbound.description = L"Lets Dr Game Transfer Hub connect out to consoles, FTP servers and the internet.";
    outbound.inbound = false;
    outbound.protocol = 256;  // any
    outbound.applicationPath = exePath;

    return {inbound, outbound};
}

WindowsFirewallService::WindowsFirewallService(std::filesystem::path optOutMarkerPath)
    : optOut_(std::move(optOutMarkerPath)) {}

core::FirewallStatus WindowsFirewallService::status() {
    core::FirewallStatus result;

    ComApartment com;
    if (!com.ok()) {
        result.detail = "Couldn't start the Windows component needed to read the firewall.";
        return result;
    }

    ComPtr<INetFwPolicy2> policy;
    const HRESULT hr = createPolicy(policy);
    if (FAILED(hr) || !policy) {
        result.detail = "Windows Firewall isn't available on this PC (" + hexOf(hr) + ").";
        return result;
    }
    result.available = true;

    bool anyProfileRead = false;
    bool anyProfileOn = false;
    for (const NET_FW_PROFILE_TYPE2 profile :
         {NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC}) {
        VARIANT_BOOL enabled = VARIANT_FALSE;
        if (SUCCEEDED(policy->get_FirewallEnabled(profile, &enabled))) {
            anyProfileRead = true;
            if (enabled != VARIANT_FALSE) anyProfileOn = true;
        }
    }
    // If no profile could be read, assume it's on rather than claim nothing
    // is being blocked.
    result.firewallEnabled = anyProfileRead ? anyProfileOn : true;

    ComPtr<INetFwRules> rules;
    if (SUCCEEDED(policy->get_Rules(rules.put())) && rules) {
        const std::wstring exe = currentExePath();
        if (!exe.empty()) {
            auto blockers = collectBlockingRules(rules.get(), exe);
            result.blockingRules = static_cast<int>(blockers.size());
            releaseAll(blockers);

            result.appAllowed = result.blockingRules == 0 && ruleIsInPlace(rules.get(), kInboundRuleName, exe, true) &&
                                ruleIsInPlace(rules.get(), kOutboundRuleName, exe, false);
        }
    }
    return result;
}

core::FirewallResult WindowsFirewallService::allowThisApp() {
    core::FirewallResult result;

    const std::wstring exe = currentExePath();
    if (exe.empty()) {
        result.message = "Couldn't work out where this program is running from.";
        return result;
    }

    int switchedOff = 0;
    {
        ComApartment com;
        if (!com.ok()) {
            result.message = "Couldn't start the Windows component needed to change the firewall.";
            return result;
        }

        ComPtr<INetFwPolicy2> policy;
        HRESULT hr = createPolicy(policy);
        ComPtr<INetFwRules> rules;
        if (SUCCEEDED(hr) && policy) hr = policy->get_Rules(rules.put());
        if (FAILED(hr) || !rules) {
            result.message = "Windows Firewall isn't available on this PC (" + hexOf(hr) + ").";
            return result;
        }

        for (const wchar_t* name : {kInboundRuleName, kOutboundRuleName}) {
            hr = removeRulesNamed(rules.get(), name);
            if (FAILED(hr)) {
                result.message = describeFailure(hr);
                return result;
            }
        }

        // Windows' own "gametransferhub.exe" rules, created when its "allow
        // this app?" prompt was dismissed, block this exe on Public networks
        // and win over any allow rule. Switch them off (not delete them) so
        // nothing else about the user's firewall changes and it's reversible.
        auto blockers = collectBlockingRules(rules.get(), exe);
        HRESULT switchOffFailure = S_OK;
        for (INetFwRule* blocker : blockers) {
            const HRESULT switched = blocker->put_Enabled(VARIANT_FALSE);
            if (SUCCEEDED(switched)) {
                ++switchedOff;
            } else if (SUCCEEDED(switchOffFailure)) {
                switchOffFailure = switched;
            }
        }
        releaseAll(blockers);
        if (FAILED(switchOffFailure)) {
            result.message = describeFailure(switchOffFailure);
            return result;
        }

        for (const auto& spec : BuildFirewallRuleSpecs(exe)) {
            hr = addRule(rules.get(), spec);
            if (FAILED(hr)) {
                result.message = describeFailure(hr);
                return result;
            }
        }
    }

    const auto after = status();
    if (!after.appAllowed) {
        result.message = after.blockingRules > 0
                             ? "Windows still has " + std::to_string(after.blockingRules) +
                                   " firewall rule(s) blocking this app that couldn't be switched off. In Windows "
                                   "Defender Firewall with Advanced Security, disable the inbound rules named "
                                   "\"gametransferhub.exe\"."
                             : "The rules were added but Windows doesn't show them as active - check Windows "
                               "Defender Firewall with Advanced Security.";
        return result;
    }

    clearOptOut();
    result.success = true;
    result.message = "Dr Game Transfer Hub is now allowed through Windows Firewall for devices on your local network.";
    if (switchedOff > 0) {
        result.message += " Also switched off " + std::to_string(switchedOff) +
                          " older Windows rule(s) that were blocking it.";
    }
    return result;
}

std::optional<core::FirewallResult> WindowsFirewallService::allowThisAppIfNeeded() {
    const auto current = status();
    if (!current.available || current.appAllowed || optedOut()) return std::nullopt;
    return allowThisApp();
}

core::FirewallResult WindowsFirewallService::removeAppRules() {
    core::FirewallResult result;

    ComApartment com;
    if (!com.ok()) {
        result.message = "Couldn't start the Windows component needed to change the firewall.";
        return result;
    }

    ComPtr<INetFwPolicy2> policy;
    HRESULT hr = createPolicy(policy);
    ComPtr<INetFwRules> rules;
    if (SUCCEEDED(hr) && policy) hr = policy->get_Rules(rules.put());
    if (FAILED(hr) || !rules) {
        result.message = "Windows Firewall isn't available on this PC (" + hexOf(hr) + ").";
        return result;
    }

    for (const wchar_t* name : {kInboundRuleName, kOutboundRuleName}) {
        hr = removeRulesNamed(rules.get(), name);
        if (FAILED(hr)) {
            result.message = describeFailure(hr);
            return result;
        }
    }
    // The user asked for these to go, so don't quietly put them back on the
    // next launch.
    recordOptOut();
    result.success = true;
    result.message = "Removed Dr Game Transfer Hub's firewall rules. They won't be added again automatically for "
                     "this copy of the app - use Allow through Firewall to turn that back on.";
    return result;
}

bool FirewallOptOutList::contains(const std::wstring& exePath) const {
    if (exePath.empty()) return false;

    std::ifstream in(file_);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (SameFirewallPath(widen(line), exePath)) return true;
    }
    return false;
}

void FirewallOptOutList::add(const std::wstring& exePath) const {
    if (exePath.empty() || contains(exePath)) return;

    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::ofstream out(file_, std::ios::app);
    out << narrow(exePath) << "\n";
}

void FirewallOptOutList::remove(const std::wstring& exePath) const {
    if (exePath.empty() || !contains(exePath)) return;

    std::vector<std::string> keep;
    {
        std::ifstream in(file_);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && !SameFirewallPath(widen(line), exePath)) keep.push_back(line);
        }
    }

    std::ofstream out(file_, std::ios::trunc);
    for (const auto& line : keep) out << line << "\n";
}

bool WindowsFirewallService::optedOut() const { return optOut_.contains(currentExePath()); }
void WindowsFirewallService::recordOptOut() const { optOut_.add(currentExePath()); }
void WindowsFirewallService::clearOptOut() const { optOut_.remove(currentExePath()); }

}  // namespace gth::network
