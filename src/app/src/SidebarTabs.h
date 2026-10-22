#pragma once

#include <array>
#include <set>
#include <string>
#include <vector>

// Canonical sidebar tab list - shared between MainFrame (which renders the
// sidebar and owns each tab's logical index) and SettingsView (which lets
// the user hide any of the non-pinned ones). Index 0 (Dashboard) and the
// last index (Settings) are pinned: never offered as a checkbox, never
// filtered out of the sidebar, so there's always a way back into Settings
// and a home screen.
inline constexpr std::array<const wchar_t*, 15> kSidebarTabs = {
    L"Dashboard", L"Games", L"JB", L"Updates", L"Downloads", L"Storage", L"Consoles", L"Transfers",
    L"FTP", L"Network", L"Logs", L"Reports", L"Enterprise", L"Backups", L"Settings"};

inline constexpr int kSidebarTabCount = static_cast<int>(kSidebarTabs.size());
inline constexpr int kSidebarDashboardIndex = 0;
inline constexpr int kSidebarSettingsIndex = kSidebarTabCount - 1;

inline constexpr bool IsSidebarTabHideable(int logicalIndex) {
    return logicalIndex != kSidebarDashboardIndex && logicalIndex != kSidebarSettingsIndex;
}

// Sidebar tabs a signed-out (regular) user must never see, regardless of
// their own hiddenTabs preference - see MainFrame::populateSidebar's
// effective-hidden-tabs union and IAdminAuthService. Deliberately every
// label except the regular-user allowlist {Dashboard, Games, Storage,
// Consoles, FTP, Settings}; Dashboard/Settings don't need listing here since
// IsSidebarTabHideable already pins them regardless of this set.
inline constexpr std::array<const wchar_t*, 5> kNonAdminRestrictedTabs = {
    L"Transfers", L"Network", L"Logs", L"Reports", L"Enterprise"};

// Logical indices (into kSidebarTabs) of the tabs to show, in sidebar order:
// everything except the user's own hidden tabs and - for a signed-out
// (regular) user - the admin-only ones. A union, not a replace, so a personal
// hide preference survives signing in and out. Tab labels are plain ASCII, so
// widening/narrowing is a simple per-character copy.
inline std::vector<int> VisibleSidebarTabIndices(const std::set<std::string>& userHiddenTabs, bool adminSignedIn) {
    const auto narrow = [](const wchar_t* label) {
        std::string out;
        for (const wchar_t* c = label; *c; ++c) out += static_cast<char>(*c);
        return out;
    };

    std::set<std::string> hidden = userHiddenTabs;
    if (!adminSignedIn) {
        for (const wchar_t* restricted : kNonAdminRestrictedTabs) hidden.insert(narrow(restricted));
    }

    std::vector<int> visible;
    for (int i = 0; i < kSidebarTabCount; ++i) {
        if (IsSidebarTabHideable(i) && hidden.count(narrow(kSidebarTabs[static_cast<size_t>(i)])) > 0) continue;
        visible.push_back(i);
    }
    return visible;
}
