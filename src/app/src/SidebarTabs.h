#pragma once

#include <algorithm>
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

inline constexpr int kSidebarGroupCount = 5;

enum class SidebarGroup : int { Workspace = 0, Devices = 1, LibraryData = 2, Administration = 3, Settings = 4 };

inline constexpr SidebarGroup SidebarGroupForLogicalIndex(int index) {
    switch (index) {
        case 0:  // Dashboard
        case 1:  // Games
        case 2:  // JB
        case 3:  // Updates
        case 4:  // Downloads
        case 7:  // Transfers
            return SidebarGroup::Workspace;
        case 6:  // Consoles
        case 8:  // FTP
        case 9:  // Network
            return SidebarGroup::Devices;
        case 5:   // Storage
        case 13:  // Backups
            return SidebarGroup::LibraryData;
        case 10:  // Logs
        case 11:  // Reports
        case 12:  // Enterprise
            return SidebarGroup::Administration;
        case 14:  // Settings
        default:
            return SidebarGroup::Settings;
    }
}

inline constexpr int SidebarGroupRowValue(SidebarGroup group) { return -1 - static_cast<int>(group); }

inline constexpr bool IsSidebarGroupRow(int rowValue) { return rowValue < 0 && rowValue >= -kSidebarGroupCount; }

// Build the rows shown by the owner-drawn navigation list. Group rows are
// encoded as negative values; page rows keep their stable logical tab index.
// This keeps visual disclosure independent from the app's routing indices.
inline std::vector<int> BuildSidebarNavigationRows(const std::vector<int>& visibleTabs,
                                                   const std::array<bool, kSidebarGroupCount>& expandedGroups) {
    std::vector<int> rows;
    rows.reserve(visibleTabs.size() + kSidebarGroupCount);

    for (int groupIndex = 0; groupIndex < kSidebarGroupCount; ++groupIndex) {
        const auto group = static_cast<SidebarGroup>(groupIndex);
        const bool hasVisibleTabs = std::any_of(visibleTabs.begin(), visibleTabs.end(), [group](int logicalIndex) {
            return SidebarGroupForLogicalIndex(logicalIndex) == group;
        });
        if (!hasVisibleTabs) continue;

        rows.push_back(SidebarGroupRowValue(group));
        if (!expandedGroups[static_cast<size_t>(groupIndex)]) continue;

        for (int logicalIndex : visibleTabs) {
            if (SidebarGroupForLogicalIndex(logicalIndex) == group) rows.push_back(logicalIndex);
        }
    }

    return rows;
}

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
