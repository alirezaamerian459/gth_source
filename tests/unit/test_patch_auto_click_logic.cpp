#include <gtest/gtest.h>

#include "PatchAutoClickLogic.h"

using namespace gth::captcha;

TEST(PatchAutoClickLogicTest, ParsesPlainPatchHeading) {
    std::wstring version;
    EXPECT_TRUE(ParsePatchHeading(L"Patch 01.000.028", version));
    EXPECT_EQ(version, L"01.000.028");
}

TEST(PatchAutoClickLogicTest, RejectsNonPatchText) {
    std::wstring version;
    EXPECT_FALSE(ParsePatchHeading(L"Download DP", version));
    EXPECT_FALSE(ParsePatchHeading(L"Patch notes", version));
    EXPECT_FALSE(ParsePatchHeading(L"Patch", version));
    EXPECT_FALSE(ParsePatchHeading(L"Patch 01.000.028 PKG Size 54.5 GB", version));
}

TEST(PatchAutoClickLogicTest, RejectsMalformedVersionNumbers) {
    std::wstring version;
    EXPECT_FALSE(ParsePatchHeading(L"Patch .028", version));    // leading dot
    EXPECT_FALSE(ParsePatchHeading(L"Patch 01..028", version));  // double dot
    EXPECT_FALSE(ParsePatchHeading(L"Patch 01.028.", version));  // trailing dot
}

TEST(PatchAutoClickLogicTest, PicksHighestVersionNotFirstListed) {
    const std::vector<std::wstring> names = {L"Patch 01.000.010", L"Patch 01.000.028", L"Patch 01.000.005"};
    const auto idx = IndexOfNewestPatch(names);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 1u);
}

TEST(PatchAutoClickLogicTest, IgnoresUnrelatedTextMixedIn) {
    const std::vector<std::wstring> names = {L"Download", L"SC", L"Patch 01.000.028", L"CRC"};
    const auto idx = IndexOfNewestPatch(names);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 2u);
}

TEST(PatchAutoClickLogicTest, NoPatchesReturnsNullopt) {
    EXPECT_FALSE(IndexOfNewestPatch({L"Download", L"SC"}).has_value());
    EXPECT_FALSE(IndexOfNewestPatch({}).has_value());
}

TEST(PatchAutoClickLogicTest, TitleIdIsPrefixBeforeColon) {
    EXPECT_EQ(TitleIdFromWindowTitle(L"PPSA20050: EA SPORTS FC 25 - Google Chrome"), L"PPSA20050");
    EXPECT_EQ(TitleIdFromWindowTitle(L"New Tab - Google Chrome"), L"");
}
