#pragma once

namespace gth::application {

struct StorageSyncResult {
    int added = 0;
    int updated = 0;
    int markedOffline = 0;
    int removed = 0;
};

}  // namespace gth::application
