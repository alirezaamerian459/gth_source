#include "gth/application/jb/JbLibraryService.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <set>
#include <string_view>

#include "gth/core/entities/Storage.h"
#include "gth/core/enums/StorageType.h"

namespace gth::application {

namespace {

namespace fs = std::filesystem;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::wstring lowerCopy(std::wstring value) {
    for (wchar_t& c : value) c = std::towlower(c);
    return value;
}

bool equalsIgnoreCase(const std::wstring& left, std::wstring_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) return false;
    }
    return true;
}

bool isJbLibraryFolder(const std::wstring& name) {
    return equalsIgnoreCase(name, L"hacked") || equalsIgnoreCase(name, L"p30day");
}

bool isLocalDrive(core::StorageType type) {
    return type == core::StorageType::LocalHdd || type == core::StorageType::LocalSsd ||
           type == core::StorageType::ExternalHdd || type == core::StorageType::Usb;
}

bool isGameArchive(const fs::path& path) {
    const std::wstring extension = lowerCopy(path.extension().wstring());
    return extension == L".pkg" || extension == L".fpkg" || extension == L".zip" || extension == L".7z" ||
           extension == L".rar" || extension == L".iso" || extension == L".bin" || extension == L".dec" ||
           extension == L".dump";
}

bool isServableJbFileName(const std::string& fileName) {
    const std::string lower = lowerCopy(fileName);
    for (const char* suffix : {".pkg", ".fpkg", ".pup", ".zip", ".7z", ".rar", ".iso", ".bin", ".dec", ".dump"}) {
        const std::string_view ending(suffix);
        if (lower.size() >= ending.size() && lower.ends_with(ending)) return true;
    }
    return false;
}

std::string titleIdFromName(const std::wstring& name) {
    for (size_t i = 0; i + 9 <= name.size(); ++i) {
        bool letters = true;
        for (size_t j = 0; j < 4; ++j) letters &= std::iswalpha(name[i + j]) != 0;
        bool digits = true;
        for (size_t j = 4; j < 9; ++j) digits &= std::iswdigit(name[i + j]) != 0;
        if (!letters || !digits) continue;

        std::wstring id = name.substr(i, 9);
        for (wchar_t& c : id) c = std::towupper(c);
        std::string result;
        result.reserve(id.size());
        for (wchar_t c : id) result.push_back(static_cast<char>(c));
        return result;
    }
    return {};
}

std::string platformFromTitleId(const std::string& titleId) {
    if (titleId.rfind("PPS", 0) == 0) return "PS5";
    if (titleId.rfind("CUSA", 0) == 0) return "PS4";
    return "PS";
}

fs::path pathFromUtf8(const std::string& value) {
    const auto utf8 = std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size());
    return fs::path(utf8);
}

fs::path extendedPath(const fs::path& rawPath) {
    const std::wstring raw = rawPath.wstring();
    if (raw.rfind(L"\\\\?\\", 0) == 0) return fs::path(raw);
    if (raw.rfind(L"\\\\", 0) == 0) return fs::path(L"\\\\?\\UNC\\" + raw.substr(2));
    return fs::path(L"\\\\?\\" + raw);
}

bool isExfatVolume(const fs::path& rawPath) {
    std::wstring root = rawPath.wstring();
    if (root.rfind(L"\\\\?\\", 0) == 0) root.erase(0, 4);
    if (root.size() >= 2 && root[1] == L':') root = root.substr(0, 3);

    std::array<wchar_t, MAX_PATH> fileSystemName{};
    return ::GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fileSystemName.data(),
                                   static_cast<DWORD>(fileSystemName.size())) != FALSE &&
           equalsIgnoreCase(std::wstring(fileSystemName.data()), L"exFAT");
}

using FileIndex = std::unordered_map<std::string, std::vector<fs::path>>;

void addCandidate(FileIndex& index, const fs::path& path) {
    const std::string fileName = path.filename().string();
    if (fileName.empty() || !isServableJbFileName(fileName)) return;
    auto& candidates = index[lowerCopy(fileName)];
    if (std::find(candidates.begin(), candidates.end(), path) == candidates.end()) candidates.push_back(path);
}

void indexFiles(const fs::path& path, FileIndex& index) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        addCandidate(index, path);
        return;
    }
    if (!fs::is_directory(path, ec) || ec) return;

    const auto options = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(path, options, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc) addCandidate(index, it->path());
    }
}

}  // namespace

JbLibraryService::JbLibraryService(IStorageService& storageService) : storageService_(storageService) {}

JbLibraryScanResult JbLibraryService::scan(const std::string& targetStorageId) {
    std::lock_guard<std::mutex> scanLock(scanMutex_);

    JbLibraryScanResult result;
    FileIndex fileIndex;
    std::set<std::wstring> seenPaths;

    try {
        storageService_.detectAndSyncLocalStorages();
        auto storages = storageService_.getAllStorages();
        std::sort(storages.begin(), storages.end(), [](const auto& left, const auto& right) {
            return left.rootPath < right.rootPath;
        });

        for (const auto& storage : storages) {
            if (!storage.isAvailable || !isLocalDrive(storage.type)) continue;
            if (!targetStorageId.empty() && storage.storageId != targetStorageId) continue;

            ++result.storageCount;
            const fs::path rawRoot = pathFromUtf8(storage.rootPath);
            if (isExfatVolume(rawRoot)) ++result.exfatStorageCount;

            std::error_code rootEc;
            const fs::path root = extendedPath(rawRoot);
            if (!fs::is_directory(root, rootEc) || rootEc) continue;

            const auto options = fs::directory_options::skip_permission_denied;
            for (fs::recursive_directory_iterator it(root, options, rootEc), end; it != end; it.increment(rootEc)) {
                if (rootEc) {
                    rootEc.clear();
                    continue;
                }
                std::error_code dirEc;
                if (!it->is_directory(dirEc) || dirEc || !isJbLibraryFolder(it->path().filename().wstring())) continue;

                ++result.libraryFolderCount;
                for (fs::directory_iterator child(it->path(), options, dirEc), childEnd; child != childEnd;
                     child.increment(dirEc)) {
                    if (dirEc) {
                        dirEc.clear();
                        continue;
                    }

                    const fs::path childPath = child->path();
                    std::error_code entryEc;
                    const bool gameDirectory = child->is_directory(entryEc);
                    const bool gameFile = child->is_regular_file(entryEc) && isGameArchive(childPath);
                    if (!gameDirectory && !gameFile) continue;
                    if (!seenPaths.insert(childPath.wstring()).second) continue;

                    JbGameEntry entry;
                    entry.title = gameDirectory ? childPath.filename().string() : childPath.stem().string();
                    entry.titleId = titleIdFromName(childPath.filename().wstring());
                    entry.platform = platformFromTitleId(entry.titleId);
                    entry.path = childPath;
                    entry.storageId = storage.storageId;
                    entry.storageName = storage.name;
                    entry.storageRootPath = storage.rootPath;
                    result.entries.push_back(std::move(entry));

                    indexFiles(childPath, fileIndex);
                }

                it.disable_recursion_pending();
            }
        }
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }

    std::sort(result.entries.begin(), result.entries.end(), [](const JbGameEntry& left, const JbGameEntry& right) {
        if (left.title != right.title) return left.title < right.title;
        return left.path.wstring() < right.path.wstring();
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = result;
        fileIndex_ = std::move(fileIndex);
        builtAt_ = std::chrono::steady_clock::now();
    }
    return result;
}

JbLibraryScanResult JbLibraryService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::optional<std::filesystem::path> JbLibraryService::findFileByName(const std::string& fileName) {
    if (fileName.empty() || !isServableJbFileName(fileName)) return std::nullopt;

    bool needsScan = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        needsScan = snapshot_.entries.empty() ||
                    std::chrono::steady_clock::now() - builtAt_ >= kMinRebuildInterval;
    }
    if (needsScan) scan();

    const std::string key = lowerCopy(fileName);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = fileIndex_.find(key);
    if (it == fileIndex_.end()) return std::nullopt;
    for (const auto& candidate : it->second) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
    }
    return std::nullopt;
}

}  // namespace gth::application
