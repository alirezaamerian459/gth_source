#include <gtest/gtest.h>

#include "FtpLayout.h"
#include "SidebarTabs.h"

using namespace ftplayout;

// The LAN Console Proxy is for everyone. It lives on the FTP tab - which a signed-out (regular) user does see - and
// the FTP tab's admin-only sections above it (connections, host, mirror list) are hidden for that user, so the
// proxy moves up under the page header. This pins the fact both layouts (FtpView::OnCreate's baselines) depend on.

TEST(FtpLayoutTest, TheFtpTabIsOnTheRegularUsersListSoTheProxyOnItIsReachable) {
    const auto visible = VisibleSidebarTabIndices({}, /*adminSignedIn=*/false);
    bool ftpShown = false;
    for (int index : visible) ftpShown = ftpShown || std::wstring(kSidebarTabs[static_cast<size_t>(index)]) == L"FTP";
    EXPECT_TRUE(ftpShown) << "a signed-out user must be able to open the FTP tab, where the LAN Console Proxy is";
}

// The section holds two field rows (IP/Port/Start/Stop, then Cache Folder/Browse/Clear Cache) between its caption
// and its status block - the height must be exactly the sum of every row, or a later row gets clipped/overlapped.
TEST(FtpLayoutTest, TheSectionHeightIsTheSumOfItsRows) {
    EXPECT_EQ(kProxySectionHeight, 26 + 18 + 24 + 8 + 18 + 24 + 8 + 72);
    EXPECT_EQ(kProxySectionHeight, 198);
}

// The status text (address, running state, cache hit/miss counts) is the tallest thing at the bottom of the
// section - the section must be tall enough to hold every row including it.
TEST(FtpLayoutTest, EveryRowFitsInsideTheSection) {
    const int row1Top = kProxyCaptionRow;
    const int row1EditTop = row1Top + kFieldCaptionHeight;
    const int row2Top = row1EditTop + kFieldEditHeight + kProxyRowGap;
    const int row2EditTop = row2Top + kFieldCaptionHeight;
    const int statusTop = row2EditTop + kFieldEditHeight + kProxyFieldGap;
    EXPECT_EQ(statusTop + kProxyStatusHeight, kProxySectionHeight);
}
