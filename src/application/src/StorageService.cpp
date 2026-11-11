#include "gth/application/storage/StorageService.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <format>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "gth/core/Clock.h"

namespace gth::application {

using core::DetectedStorageInfo;
using core::Storage;
using core::StorageStatus;
using core::StorageType;

namespace {

// Types the local detector is responsible for. Storages of other types
// (NAS, SMB, network shares) are out of scope for a local scan and must
// never be silently marked offline by it.
bool isLocallyDetectableType(StorageType type) {
    return type == StorageType::LocalHdd || type == StorageType::LocalSsd ||
           type == StorageType::ExternalHdd || type == StorageType::Usb;
}

// Above this, a drive is flagged regardless of what its own reported
// HealthStatus says - SMART health enums lag real thermal risk, so this
// check runs independently of the health-percent comparison below.
using core::kOverheatThresholdCelsius;

std::string normalizePath(std::string path) {
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
        path.pop_back();
    }
    std::transform(path.begin(), path.end(), path.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    while (value.size() > 1 && (value.back() == '\\' || value.back() == '/')) value.pop_back();
    // A bare drive letter needs its trailing backslash back - GetDiskFreeSpaceExW
    // and friends treat "D:" (no backslash) as "current directory on D:", not its root.
    if (value.size() == 2 && value[1] == ':') value += '\\';
    return value;
}

std::string deriveStorageName(const std::string& path) {
    const auto filename = std::filesystem::path(path).filename().string();
    return filename.empty() ? path : filename;
}

bool isRemoteStorageType(StorageType type) {
    return type == StorageType::Nas || type == StorageType::SmbShare || type == StorageType::NetworkShare;
}

bool refreshRemoteCapacity(Storage& storage) {
    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    if (!::GetDiskFreeSpaceExW(utf8ToWide(storage.rootPath).c_str(), &freeBytes, &totalBytes, nullptr)) return false;

    storage.totalCapacityBytes = static_cast<long long>(std::bit_cast<std::uint64_t>(totalBytes));
    storage.freeSpaceBytes = static_cast<long long>(std::bit_cast<std::uint64_t>(freeBytes));
    storage.usedSpaceBytes = std::max<long long>(0, storage.totalCapacityBytes - storage.freeSpaceBytes);
    storage.status = StorageStatus::Online;
    storage.isAvailable = true;
    storage.lastScan = core::Clock::utcNowIso8601();
    storage.updatedAt = core::Clock::utcNowIso8601();
    return true;
}

// `disksOut` receives the per-disk readings when the provider can tell disks apart (else it is left empty).
void applyDetection(Storage& storage, const DetectedStorageInfo& info, core::IStorageHealthProvider* healthProvider,
                    std::vector<core::PhysicalDiskHealth>& disksOut) {
    // Captured before this scan's values overwrite it below - the baseline
    // "what did the last scan see" a changed-health alert compares against.
    // For a brand-new storage this is nullopt, so a first reading can never
    // itself count as a "change".
    const auto previousHealthPercent = storage.healthPercent;

    storage.name = info.name;
    storage.type = info.type;
    storage.totalCapacityBytes = info.totalCapacityBytes;
    storage.freeSpaceBytes = info.freeSpaceBytes;
    storage.usedSpaceBytes = std::max<long long>(0, info.totalCapacityBytes - info.freeSpaceBytes);
    storage.fileSystem = info.fileSystem;
    storage.isAvailable = info.isAvailable;
    storage.status = info.isAvailable ? StorageStatus::Online : StorageStatus::Offline;
    storage.lastScan = core::Clock::utcNowIso8601();
    storage.updatedAt = core::Clock::utcNowIso8601();

    storage.healthPercent.reset();
    storage.temperatureCelsius.reset();
    storage.healthAlertReason.reset();

    if (healthProvider != nullptr && info.isAvailable) {
        // One reading per physical disk when the provider can give them: a drive letter over several disks is
        // then as healthy as its worst disk and as hot as its hottest, instead of "unknown".
        core::StorageHealthInfo health;
        if (auto disks = healthProvider->queryDisks(info.rootPath); disks.has_value()) {
            health = core::summarizeDisks(*disks);
            disksOut = std::move(*disks);
        } else {
            health = healthProvider->queryHealth(info.rootPath);
        }
        storage.healthPercent = health.healthPercent;
        storage.temperatureCelsius = health.temperatureCelsius;

        std::string reason;
        if (storage.healthPercent && *storage.healthPercent < core::kHealthAlertBelowPercent) {
            reason = std::format("Health {}%", *storage.healthPercent);
            if (health.healthDetail) reason += ": " + *health.healthDetail;
        }
        if (previousHealthPercent && storage.healthPercent && *previousHealthPercent != *storage.healthPercent) {
            if (!reason.empty()) reason += "; ";
            reason += std::format("Health changed from {}% to {}%", *previousHealthPercent,
                                  *storage.healthPercent);
        }
        if (storage.temperatureCelsius && *storage.temperatureCelsius > kOverheatThresholdCelsius) {
            if (!reason.empty()) reason += "; ";
            reason += std::format("Temperature {}C exceeds {}C", *storage.temperatureCelsius,
                                  kOverheatThresholdCelsius);
        }
        if (!reason.empty()) storage.healthAlertReason = reason;
    }
}

}  // namespace

StorageService::StorageService(core::IStorageRepository& repository, core::IStorageDetector& detector,
                                core::IStorageHealthProvider* healthProvider)
    : repository_(repository), detector_(detector), healthProvider_(healthProvider) {}

std::vector<Storage> StorageService::getAllStorages() { return repository_.getAll(); }

void StorageService::rememberDisks(const std::string& normalisedRoot, std::vector<core::PhysicalDiskHealth> disks) {
    std::lock_guard lock(disksMutex_);
    disksByRoot_[normalisedRoot] = std::move(disks);
}

std::vector<core::PhysicalDiskHealth> StorageService::getPhysicalDisks(const std::string& rootPath) {
    std::lock_guard lock(disksMutex_);
    const auto found = disksByRoot_.find(normalizePath(rootPath));
    return found == disksByRoot_.end() ? std::vector<core::PhysicalDiskHealth>{} : found->second;
}

StorageSyncResult StorageService::detectAndSyncLocalStorages() {
    const auto detected = detector_.detect();
    const auto existing = repository_.getAll();

    std::unordered_map<std::string, Storage> existingByPath;
    for (const auto& storage : existing) {
        existingByPath.emplace(normalizePath(storage.rootPath), storage);
    }

    std::unordered_set<std::string> seenPaths;
    StorageSyncResult result;

    for (const auto& info : detected) {
        const auto key = normalizePath(info.rootPath);
        seenPaths.insert(key);

        auto it = existingByPath.find(key);
        if (it != existingByPath.end()) {
            Storage storage = it->second;
            std::vector<core::PhysicalDiskHealth> disks;
            applyDetection(storage, info, healthProvider_, disks);
            rememberDisks(key, std::move(disks));
            repository_.update(storage);
            result.updated++;
        } else {
            Storage storage;
            storage.rootPath = info.rootPath;
            std::vector<core::PhysicalDiskHealth> disks;
            applyDetection(storage, info, healthProvider_, disks);
            rememberDisks(key, std::move(disks));
            repository_.add(storage);
            result.added++;
        }
    }

    // Local-drive detection deliberately excludes network paths. Refresh
    // reachable NAS/SMB capacity separately so used/free values on the
    // Storage page remain current without changing the existing offline
    // reconciliation rules for local disks.
    for (const auto& storage : existing) {
        if (!isRemoteStorageType(storage.type)) continue;
        Storage refreshed = storage;
        if (!refreshRemoteCapacity(refreshed)) continue;
        repository_.update(refreshed);
        result.updated++;
    }

    for (const auto& storage : existing) {
        if (!isLocallyDetectableType(storage.type)) continue;
        if (seenPaths.contains(normalizePath(storage.rootPath))) continue;

        // A locally-detectable drive the detector no longer reports is
        // normally just unplugged/asleep - mark it offline rather than
        // deleting its history. But if the root path is still physically
        // there and reachable, the detector didn't miss it, it deliberately
        // excluded it (e.g. the Windows system drive, excluded so games
        // scans don't walk C:\Program Files\... treating every folder as a
        // game - see LocalStorageDetector). "Offline" wouldn't stop that:
        // the path still trivially exists, so a scan would run against it
        // anyway. Remove it outright instead, so a storage from before this
        // exclusion existed self-heals on the next Rescan instead of
        // lingering forever.
        std::error_code ec;
        if (std::filesystem::exists(storage.rootPath, ec) && !ec) {
            repository_.remove(storage.storageId);
            result.removed++;
            continue;
        }

        if (storage.status == StorageStatus::Offline && !storage.isAvailable) continue;

        Storage updated = storage;
        updated.status = StorageStatus::Offline;
        updated.isAvailable = false;
        updated.updatedAt = core::Clock::utcNowIso8601();
        repository_.update(updated);
        result.markedOffline++;
    }

    return result;
}

AddStorageResult StorageService::addManualStorage(const std::string& path) {
    AddStorageResult result;

    const std::string trimmed = trim(path);
    if (trimmed.empty()) {
        result.errorMessage = "Path cannot be empty.";
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::exists(trimmed, ec) || !std::filesystem::is_directory(trimmed, ec)) {
        result.errorMessage = std::format("Path doesn't exist or isn't reachable: {}", trimmed);
        return result;
    }

    if (repository_.getByRootPath(trimmed).has_value()) {
        result.errorMessage = "This storage is already registered.";
        return result;
    }

    Storage storage;
    storage.rootPath = trimmed;
    storage.name = deriveStorageName(trimmed);
    storage.type = trimmed.rfind("\\\\", 0) == 0 ? StorageType::SmbShare : StorageType::ExternalHdd;
    storage.status = StorageStatus::Online;
    storage.isAvailable = true;
    storage.lastScan = core::Clock::utcNowIso8601();
    storage.updatedAt = core::Clock::utcNowIso8601();

    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    if (::GetDiskFreeSpaceExW(utf8ToWide(trimmed).c_str(), &freeBytes, &totalBytes, nullptr)) {
        storage.totalCapacityBytes = static_cast<long long>(std::bit_cast<std::uint64_t>(totalBytes));
        storage.freeSpaceBytes = static_cast<long long>(std::bit_cast<std::uint64_t>(freeBytes));
        storage.usedSpaceBytes = std::max<long long>(0, storage.totalCapacityBytes - storage.freeSpaceBytes);
    }

    repository_.add(storage);

    result.success = true;
    result.storageId = storage.storageId;
    return result;
}

StorageSummary StorageService::getSummary() {
    const auto all = repository_.getAll();

    StorageSummary summary;
    summary.totalStorages = static_cast<int>(all.size());
    for (const auto& storage : all) {
        if (storage.status == StorageStatus::Online) {
            summary.onlineStorages++;
        } else {
            summary.offlineStorages++;
        }
        summary.totalCapacityBytes += storage.totalCapacityBytes;
        summary.freeSpaceBytes += storage.freeSpaceBytes;
    }
    return summary;
}

}  // namespace gth::application
