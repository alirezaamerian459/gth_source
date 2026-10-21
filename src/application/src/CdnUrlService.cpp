#include "gth/application/ftp/CdnUrlService.h"

#include "gth/application/storage/ILocalFileIndex.h"
#include "gth/core/Clock.h"

namespace gth::application {

void CdnUrlService::captureUrl(const std::string& url, const std::string& host, bool isCdn) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto existing = repo_.getAll();
    for (const auto& e : existing) {
        if (e.url == url) {
            gth::core::CdnUrl updated = e;
            updated.updatedAt = gth::core::Clock::utcNowIso8601();
            updated.host = host;
            updated.isCdn = isCdn;
            repo_.update(updated);
            return;
        }
    }

    gth::core::CdnUrl newUrl;
    newUrl.url = url;
    newUrl.host = host;
    newUrl.isCdn = isCdn;
    newUrl.isLixian = false;

    // Already have this exact file on a local drive? Then pre-fill it, so the log shows
    // "Local" and the console is served from disk instead of the network.
    if (localFileIndex_) {
        const std::string fileName = packageFileNameFromUrl(url);
        if (isServablePackageName(fileName)) {
            if (const auto found = localFileIndex_->findByFileName(fileName)) {
                const auto u8 = found->u8string();
                newUrl.localPath = std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
                newUrl.markText = "Local: " + fileName;
            }
        }
    }

    repo_.insert(newUrl);
}

void CdnUrlService::updateLocalPath(const std::string& cdnUrlId, const std::string& localPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto all = repo_.getAll();
    for (auto& url : all) {
        if (url.cdnUrlId == cdnUrlId) {
            url.localPath = localPath;
            url.updatedAt = gth::core::Clock::utcNowIso8601();
            repo_.update(url);
            return;
        }
    }
}

void CdnUrlService::updateMark(const std::string& cdnUrlId, const std::string& markText) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto all = repo_.getAll();
    for (auto& url : all) {
        if (url.cdnUrlId == cdnUrlId) {
            url.markText = markText;
            url.updatedAt = gth::core::Clock::utcNowIso8601();
            repo_.update(url);
            return;
        }
    }
}

void CdnUrlService::updateLixianUrl(const std::string& cdnUrlId, const std::string& lixianUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto all = repo_.getAll();
    for (auto& url : all) {
        if (url.cdnUrlId == cdnUrlId) {
            url.lixianUrl = lixianUrl;
            url.updatedAt = gth::core::Clock::utcNowIso8601();
            repo_.update(url);
            return;
        }
    }
}

void CdnUrlService::enableLixian(const std::string& cdnUrlId, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto all = repo_.getAll();
    for (auto& url : all) {
        if (url.cdnUrlId == cdnUrlId) {
            url.isLixian = enable;
            url.updatedAt = gth::core::Clock::utcNowIso8601();
            repo_.update(url);
            return;
        }
    }
}

std::vector<gth::core::CdnUrl> CdnUrlService::getAll() {
    return repo_.getAll();
}

std::vector<gth::core::CdnUrl> CdnUrlService::getRecent(int limit) {
    return repo_.getRecent(limit);
}

void CdnUrlService::removeUrl(const std::string& cdnUrlId) {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_.remove(cdnUrlId);
}

}  // namespace gth::application
