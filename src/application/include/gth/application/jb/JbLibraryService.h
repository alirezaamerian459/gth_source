#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gth/application/jb/IJbLibraryService.h"
#include "gth/application/storage/IStorageService.h"

namespace gth::application {

class JbLibraryService : public IJbLibraryService {
public:
    explicit JbLibraryService(IStorageService& storageService);

    JbLibraryScanResult scan(const std::string& targetStorageId = {}) override;
    JbLibraryScanResult snapshot() const override;
    std::optional<std::filesystem::path> findFileByName(const std::string& fileName) override;

private:
    using FileIndex = std::unordered_map<std::string, std::vector<std::filesystem::path>>;

    static constexpr std::chrono::seconds kMinRebuildInterval{15};

    IStorageService& storageService_;
    mutable std::mutex mutex_;
    std::mutex scanMutex_;
    JbLibraryScanResult snapshot_;
    FileIndex fileIndex_;
    std::chrono::steady_clock::time_point builtAt_{};
};

}  // namespace gth::application
