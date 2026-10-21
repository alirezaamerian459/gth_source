#pragma once

namespace gth::application {

// Aggregate figures shown on the Dashboard's storage tile.
struct StorageSummary {
    int totalStorages = 0;
    int onlineStorages = 0;
    int offlineStorages = 0;
    long long totalCapacityBytes = 0;
    long long freeSpaceBytes = 0;
};

}  // namespace gth::application
