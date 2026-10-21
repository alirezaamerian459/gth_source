#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Current health/reachability state of a storage location.
enum class StorageStatus {
    Unknown = 0,
    Online = 1,
    Offline = 2,
    Error = 3
};

std::string toString(StorageStatus status);
StorageStatus storageStatusFromString(std::string_view text);

}  // namespace gth::core
