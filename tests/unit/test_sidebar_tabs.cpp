#include <gtest/gtest.h>

#include <algorithm>

#include "SidebarTabs.h"

namespace {

// Logical indices, per kSidebarTabs: 0 Dashboard, 1 Games, 2 Updates, 3 Downloads,
// 4 Storage, 5 Consoles, 6 Transfers, 7 FTP, 8 Network, 9 Logs, 10 Reports,
// 11 Enterprise, 12 Backups, 13 Settings.
constexpr int kDashboard = 0, kGames = 1, kUpdates = 2, kDownloads = 3, kStorage = 4, kConsoles = 5, kTransfers = 6,
              kFtp = 7, kNetwork = 8, kEnterprise = 11, kBackups = 12, kSettings = 13;

bool contains(const std::vector<int>& indices, int index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
}

}  // namespace

// The Games tab is where a regular user scans and browses their library - it
// used to be admin-only, which left anyone who wasn't signed in unable to see
// their own games at all.
TEST(SidebarTabsTest, ASignedOutUserSeesTheGamesTab) {
    EXPECT_TRUE(contains(VisibleSidebarTabIndices({}, /*adminSignedIn=*/false), kGames));
}

// The Updates tab is where the Sony check, sync, automation and version tools
// live - the whole point of them is that a regular user can keep their own
// archive current, so it isn't an admin-only tab.
TEST(SidebarTabsTest, ASignedOutUserSeesTheUpdatesTab) {
    const auto visible = VisibleSidebarTabIndices({}, false);

    EXPECT_TRUE(contains(visible, kUpdates));
    EXPECT_EQ(visible, (std::vector<int>{kDashboard, kGames, kUpdates, kDownloads, kStorage, kConsoles, kFtp,
                                         kBackups, kSettings}));
}

// Downloads shows the same per-game download stats a regular user's own Update All Games
// presses produce - like Updates, there's no reason it should need an admin sign-in.
TEST(SidebarTabsTest, ASignedOutUserSeesTheDownloadsTab) {
    EXPECT_TRUE(contains(VisibleSidebarTabIndices({}, /*adminSignedIn=*/false), kDownloads));
}

TEST(SidebarTabsTest, ASignedOutUserCanAccessTheirOwnBackups) {
    EXPECT_TRUE(contains(VisibleSidebarTabIndices({}, /*adminSignedIn=*/false), kBackups));
}

TEST(SidebarTabsTest, APersonallyHiddenUpdatesTabStaysHidden) {
    EXPECT_FALSE(contains(VisibleSidebarTabIndices({"Updates"}, false), kUpdates));
    EXPECT_FALSE(contains(VisibleSidebarTabIndices({"Updates"}, true), kUpdates));
}

TEST(SidebarTabsTest, ASignedOutUserStillDoesNotSeeTheAdminOnlyTabs) {
    const auto visible = VisibleSidebarTabIndices({}, false);

    EXPECT_FALSE(contains(visible, kTransfers));
    EXPECT_FALSE(contains(visible, kNetwork));
    EXPECT_FALSE(contains(visible, kEnterprise));
}

TEST(SidebarTabsTest, AnAdminSeesEveryTab) {
    EXPECT_EQ(VisibleSidebarTabIndices({}, true).size(), static_cast<size_t>(kSidebarTabCount));
}

// The Settings checkboxes are a personal preference layered on top of the
// role rule, not replaced by it - hiding Games there hides it for everyone,
// admin or not, until it's ticked again.
TEST(SidebarTabsTest, APersonallyHiddenGamesTabStaysHiddenRegardlessOfRole) {
    EXPECT_FALSE(contains(VisibleSidebarTabIndices({"Games"}, false), kGames));
    EXPECT_FALSE(contains(VisibleSidebarTabIndices({"Games"}, true), kGames));
    EXPECT_TRUE(contains(VisibleSidebarTabIndices({"Games"}, true), kStorage));
}

TEST(SidebarTabsTest, DashboardAndSettingsCanNeverBeHidden) {
    const auto visible = VisibleSidebarTabIndices({"Dashboard", "Settings"}, false);

    EXPECT_TRUE(contains(visible, kDashboard));
    EXPECT_TRUE(contains(visible, kSettings));
}
