#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/core/ftp/ICdnUrlRepository.h"

namespace gth::application {

class ILocalFileIndex;

class CdnUrlService : public ICdnUrlService {
public:
    // localFileIndex (optional) lets a newly captured URL be matched to a file already on disk
    // by the file name it asks for - see ILocalFileIndex.
    explicit CdnUrlService(gth::core::ICdnUrlRepository& repo, ILocalFileIndex* localFileIndex = nullptr)
        : repo_(repo), localFileIndex_(localFileIndex) {}

    void captureUrl(const std::string& url, const std::string& host = "", bool isCdn = true) override;
    void updateLocalPath(const std::string& cdnUrlId, const std::string& localPath) override;
    void updateMark(const std::string& cdnUrlId, const std::string& markText) override;
    void updateLixianUrl(const std::string& cdnUrlId, const std::string& lixianUrl) override;
    void enableLixian(const std::string& cdnUrlId, bool enable) override;

    std::vector<gth::core::CdnUrl> getAll() override;
    std::vector<gth::core::CdnUrl> getRecent(int limit) override;

    void removeUrl(const std::string& cdnUrlId) override;

private:
    gth::core::ICdnUrlRepository& repo_;
    ILocalFileIndex* localFileIndex_;
    // captureUrl() is the hot path: every proxy connection thread calls it,
    // and consoles routinely hold several connections open at once. Without
    // this, two threads' "does this URL already exist" getAll() scans can
    // both miss each other's in-flight insert (classic check-then-act race),
    // producing duplicate rows for the same URL.
    std::mutex mutex_;
};

}  // namespace gth::application
