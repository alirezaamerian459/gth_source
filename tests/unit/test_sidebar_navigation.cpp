#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "SidebarTabs.h"

TEST(SidebarNavigationTest, ShowsAllSectionHeadingsButOnlyExpandedPages) {
    const std::array<bool, kSidebarGroupCount> expanded{{true, false, false, false, false}};
    const auto rows = BuildSidebarNavigationRows(VisibleSidebarTabIndices({}, true), expanded);

    EXPECT_EQ(rows, (std::vector<int>{SidebarGroupRowValue(SidebarGroup::Workspace), kSidebarDashboardIndex, 1, 2,
                                      3, 4, SidebarGroupRowValue(SidebarGroup::Devices),
                                      SidebarGroupRowValue(SidebarGroup::LibraryData),
                                      SidebarGroupRowValue(SidebarGroup::Administration),
                                      SidebarGroupRowValue(SidebarGroup::Settings)}));
}

TEST(SidebarNavigationTest, ExpandingAnotherSectionAddsItsPagesWithoutChangingRouteIndices) {
    const std::array<bool, kSidebarGroupCount> expanded{{true, true, false, false, false}};
    const auto rows = BuildSidebarNavigationRows(VisibleSidebarTabIndices({}, true), expanded);

    EXPECT_NE(std::find(rows.begin(), rows.end(), SidebarGroupRowValue(SidebarGroup::Devices)), rows.end());
    EXPECT_NE(std::find(rows.begin(), rows.end(), 5), rows.end());  // Storage
    EXPECT_NE(std::find(rows.begin(), rows.end(), 6), rows.end());  // Consoles
    EXPECT_NE(std::find(rows.begin(), rows.end(), 8), rows.end());  // FTP
    EXPECT_NE(std::find(rows.begin(), rows.end(), 9), rows.end());  // Network
    EXPECT_FALSE(IsSidebarGroupRow(8));
    EXPECT_TRUE(IsSidebarGroupRow(SidebarGroupRowValue(SidebarGroup::Devices)));
}

TEST(SidebarNavigationTest, UsesTheAlreadyFilteredPageSetForSignedOutUsers) {
    const std::array<bool, kSidebarGroupCount> expanded{{true, true, true, true, true}};
    const auto rows = BuildSidebarNavigationRows(VisibleSidebarTabIndices({}, false), expanded);

    EXPECT_EQ(std::find(rows.begin(), rows.end(), SidebarGroupRowValue(SidebarGroup::Administration)), rows.end());
    EXPECT_EQ(std::find(rows.begin(), rows.end(), 7), rows.end());   // Transfers
    EXPECT_EQ(std::find(rows.begin(), rows.end(), 10), rows.end());  // Logs
    EXPECT_NE(std::find(rows.begin(), rows.end(), 5), rows.end());   // Storage
    EXPECT_NE(std::find(rows.begin(), rows.end(), 13), rows.end());  // Backups
}
