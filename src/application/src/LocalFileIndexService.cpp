#include "gth/application/storage/LocalFileIndexService.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "gth/core/entities/Storage.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/index/IGameFileRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

namespace fs = std::filesystem;

namespace {

constexpr auto kMinRebuildInterval = std::chrono::seconds(15);

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void addCandidate(std::vector<fs::path>& candidates, fs::path candidate) {
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

std::optional<fs::path> firstExisting(const std::vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return std::nullopt;
}

}  // namespace

LocalFileIndexService::LocalFileIndexService(core::IStorageRepository& storageRepo,
                                             core::IGameVariantRepository& variantRepo,
                                             core::IGameVersionRepository& versionRepo,
                                             core::IGameFileRepository& fileRepo)
    : storageRepo_(storageRepo), variantRepo_(variantRepo), versionRepo_(versionRepo), fileRepo_(fileRepo) {}

std::shared_ptr<const LocalFileIndexService::Index> LocalFileIndexService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::shared_ptr<const LocalFileIndexService::Index> LocalFileIndexService::rebuild(
    const std::shared_ptr<const Index>& seen) {
    std::lock_guard<std::mutex> build(buildMutex_);

    // Another request thread may have rebuilt while this one waited for the build lock.
    if (auto current = snapshot(); current && current != seen) return current;

    std::unordered_map<std::string, std::string> rootByStorageId;
    for (const auto& storage : storageRepo_.getAll()) rootByStorageId.emplace(storage.storageId, storage.rootPath);

    auto index = std::make_shared<Index>();
    for (const auto& variant : variantRepo_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active) continue;
        const auto rootIt = rootByStorageId.find(variant.storageId);
        if (rootIt == rootByStorageId.end() || rootIt->second.empty()) continue;
        const fs::path root(rootIt->second);

        for (const auto& version : versionRepo_.getByVariantId(variant.gameVariantId)) {
            if (version.status != core::GameVariantStatus::Active) continue;

            for (const auto& file : fileRepo_.getByVersionId(version.gameVersionId)) {
                const fs::path relative(file.relativePath);
                const std::string name = toLower(relative.filename().string());
                if (name.empty()) continue;

                auto& candidates = index->byName[name];
                addCandidate(candidates, root / relative);
                addCandidate(candidates, root / fs::path(version.relativePath) / relative);
                addCandidate(candidates, root / fs::path(variant.relativePath) / fs::path(version.relativePath) / relative);
            }
        }
    }
    index->builtAt = std::chrono::steady_clock::now();

    std::shared_ptr<const Index> published = std::move(index);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = published;
    }
    return published;
}

std::optional<fs::path> LocalFileIndexService::findByFileName(const std::string& fileName) {
    if (fileName.empty()) return std::nullopt;
    const std::string key = toLower(fileName);

    auto current = snapshot();
    if (!current) current = rebuild(nullptr);

    auto lookup = [&](const Index& index) -> std::optional<fs::path> {
        const auto it = index.byName.find(key);
        return it == index.byName.end() ? std::nullopt : firstExisting(it->second);
    };

    if (auto hit = lookup(*current)) return hit;

    // A miss on an old snapshot: the library may have been scanned since. Rebuild (rate limited so
    // a burst of unrelated requests can't hammer the database) and look once more.
    if (std::chrono::steady_clock::now() - current->builtAt >= kMinRebuildInterval) {
        const auto fresh = rebuild(current);
        if (fresh != current) return lookup(*fresh);
    }
    return std::nullopt;
}

}  // namespace gth::application
