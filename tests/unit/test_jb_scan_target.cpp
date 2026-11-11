#include <gtest/gtest.h>

#include "JbScanTarget.h"

using gth::app::jb::isSelectableStorage;
using gth::app::jb::matchesTarget;
using gth::core::Storage;
using gth::core::StorageType;

TEST(JbScanTargetTest, AllLocalDrivesAcceptsAvailableLocalStorage) {
    Storage storage;
    storage.storageId = "drive-1";
    storage.type = StorageType::LocalHdd;
    storage.isAvailable = true;

    EXPECT_TRUE(isSelectableStorage(storage));
    EXPECT_TRUE(matchesTarget(storage, {}));
}

TEST(JbScanTargetTest, SelectedTargetOnlyAcceptsTheMatchingDrive) {
    Storage first;
    first.storageId = "drive-1";
    first.type = StorageType::ExternalHdd;
    first.isAvailable = true;

    Storage second = first;
    second.storageId = "drive-2";

    EXPECT_TRUE(matchesTarget(first, "drive-1"));
    EXPECT_FALSE(matchesTarget(second, "drive-1"));
}

TEST(JbScanTargetTest, UnavailableStoragesAreExcludedButReachableSharesAreSelectable) {
    Storage offline;
    offline.storageId = "offline";
    offline.type = StorageType::LocalHdd;
    offline.isAvailable = false;

    Storage network;
    network.storageId = "network";
    network.type = StorageType::Nas;
    network.isAvailable = true;

    EXPECT_FALSE(isSelectableStorage(offline));
    EXPECT_TRUE(isSelectableStorage(network));
    EXPECT_FALSE(matchesTarget(offline, {}));
    EXPECT_TRUE(matchesTarget(network, "network"));
}
