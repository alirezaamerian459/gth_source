#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::core {

struct CdnUrl {
    std::string cdnUrlId = Guid::newGuid();

    std::string url;              // PSN/CDN URL
    std::string localPath;        // Local file path replacement
    std::string markText;         // User comment/mark
    std::string lixianUrl;        // Offline/cached URL
    bool isLixian = false;        // Whether in offline mode
    bool isCdn = false;           // Whether it's a CDN URL
    std::string host;             // Host for ping

    std::string capturedAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
