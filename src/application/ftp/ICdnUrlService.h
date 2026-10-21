#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/CdnUrl.h"

namespace gth::application {

class ICdnUrlService {
public:
    virtual ~ICdnUrlService() = default;

    // Capture/add a URL to the log
    virtual void captureUrl(const std::string& url, const std::string& host = "", bool isCdn = true) = 0;

    // Update URL metadata
    virtual void updateLocalPath(const std::string& cdnUrlId, const std::string& localPath) = 0;
    virtual void updateMark(const std::string& cdnUrlId, const std::string& markText) = 0;
    virtual void updateLixianUrl(const std::string& cdnUrlId, const std::string& lixianUrl) = 0;
    virtual void enableLixian(const std::string& cdnUrlId, bool enable) = 0;

    // Retrieve URLs
    virtual std::vector<gth::core::CdnUrl> getAll() = 0;
    virtual std::vector<gth::core::CdnUrl> getRecent(int limit) = 0;

    // Delete
    virtual void removeUrl(const std::string& cdnUrlId) = 0;
    virtual void removeByUrl(const std::string& url) = 0;
};

}  // namespace gth::application
