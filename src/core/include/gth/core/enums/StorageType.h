#pragma once

#include <string>
#include <string_view>

namespace gth::core {

// Physical or logical kind of a storage location known to the archive.
enum class StorageType {
    Unknown = 0,
    LocalHdd = 1,
    LocalSsd = 2,
    ExternalHdd = 3,
    Usb = 4,
    Nas = 5,
    SmbShare = 6,
    NetworkShare = 7
};

// Stored as text in the database (readable, stable across enum reordering),
// mirroring how the value would be persisted in any SQL schema.
std::string toString(StorageType type);
StorageType storageTypeFromString(std::string_view text);

}  // namespace gth::core
