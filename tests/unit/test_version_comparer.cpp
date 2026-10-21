#include <gtest/gtest.h>

#include "gth/core/index/VersionComparer.h"

using namespace gth::core;

TEST(VersionComparerTest, DetectsSimpleIncrease) {
    EXPECT_LT(compareVersions("1.04", "1.06"), 0);
    EXPECT_GT(compareVersions("1.06", "1.04"), 0);
    EXPECT_EQ(compareVersions("1.06", "1.06"), 0);
}

TEST(VersionComparerTest, ComparesNumericallyNotLexicographically) {
    // A plain string compare would say "1.9" > "1.10" - wrong.
    EXPECT_LT(compareVersions("1.9", "1.10"), 0);
}

TEST(VersionComparerTest, HandlesDifferentSegmentCounts) {
    EXPECT_LT(compareVersions("1.4", "1.4.1"), 0);
    EXPECT_EQ(compareVersions("1.4.0", "1.4"), 0);
}
