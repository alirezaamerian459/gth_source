#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/CdnUrl.h"

namespace gth::application {

class ICdnUrlService {
public:
    virtual ~ICdnUrlService() = default;

    virtual void captureUrl(const std::string& url, const std::string& host, bool isCdn) = 0;
    virtual void updateLocalPath(const std::string& cdnUrlId, const std::string& localPath) = 0;
    virtual void updateMark(const std::string& cdnUrlId, const std::string& markText) = 0;
    virtual void updateLixianUrl(const std::string& cdnUrlId, const std::string& lixianUrl) = 0;
    virtual void enableLixian(const std::string& cdnUrlId, bool enable) = 0;
    virtual std::vector<gth::core::CdnUrl> getAll() = 0;
    virtual std::vector<gth::core::CdnUrl> getRecent(int limit) = 0;
    virtual void removeUrl(const std::string& cdnUrlId) = 0;
};

}  // namespace gth::application
