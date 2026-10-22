#include "JbGamesView.h"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <set>

#include "gth/core/entities/Storage.h"
#include "gth/core/enums/StorageType.h"

namespace {
constexpr int kTopAreaHeight = 88;
constexpr int kMargin = 24;
constexpr UINT_PTR kScanPollTimerId = 1;
constexpr UINT kScanPollIntervalMs = 200;

bool equalsIgnoreCase(const std::wstring& left, const wchar_t* right) {
    if (left.size() != wcslen(right)) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i])) return false;
    }
    return true;
}

std::wstring lowerCopy(std::wstring value) {
    for (wchar_t& c : value) c = std::towlower(c);
    return value;
}

bool containsIgnoreCase(const std::wstring& value, const wchar_t* needle) {
    return lowerCopy(value).find(lowerCopy(needle)) != std::wstring::npos;
}

bool isJbLibraryFolder(const std::wstring& name) {
    return equalsIgnoreCase(name, L"hacked") || equalsIgnoreCase(name, L"p30day");
}

bool isLocalDrive(gth::core::StorageType type) {
    return type == gth::core::StorageType::LocalHdd || type == gth::core::StorageType::LocalSsd ||
           type == gth::core::StorageType::ExternalHdd || type == gth::core::StorageType::Usb;
}

std::string titleIdFromName(const std::wstring& name) {
    // PS title IDs are four letters followed by five digits (for example
    // CUSA... or PPSA...). Looking for the shape keeps this local scanner
    // independent of any package parser and works with ordinary folder names.
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

bool isGameArchive(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    for (wchar_t& c : extension) c = std::towlower(c);
    return extension == L".pkg" || extension == L".fpkg" || extension == L".zip" || extension == L".7z" ||
           extension == L".rar" || extension == L".iso" || extension == L".bin" || extension == L".dec" ||
           extension == L".dump";
}

struct ArtifactSummary {
    int packages = 0;
    int fpkg = 0;
    int dumps = 0;
    int patches = 0;
    int dlc = 0;
};

void addArtifact(const std::filesystem::path& path, bool directory, ArtifactSummary& summary) {
    const std::wstring name = lowerCopy(path.filename().wstring());
    const std::wstring extension = lowerCopy(path.extension().wstring());

    if (directory) {
        if (name == L"dump" || containsIgnoreCase(name, L"dump")) {
            ++summary.dumps;
        } else if (containsIgnoreCase(name, L"dlc")) {
            ++summary.dlc;
        } else if (containsIgnoreCase(name, L"patch") || containsIgnoreCase(name, L"update")) {
            ++summary.patches;
        }
        return;
    }

    if (containsIgnoreCase(name, L"dlc")) {
        ++summary.dlc;
    } else if (containsIgnoreCase(name, L"patch") || containsIgnoreCase(name, L"update")) {
        ++summary.patches;
    } else if (extension == L".fpkg") {
        ++summary.fpkg;
    } else if (extension == L".pkg") {
        ++summary.packages;
    } else if (extension == L".dump" || extension == L".iso" || extension == L".bin" ||
               extension == L".dec") {
        ++summary.dumps;
    }
}

std::wstring artifactDetails(const ArtifactSummary& summary, bool exfat) {
    std::vector<std::wstring> details;
    if (exfat) details.push_back(L"exFAT");
    if (summary.packages) details.push_back(L"PKG " + std::to_wstring(summary.packages));
    if (summary.fpkg) details.push_back(L"FPKG " + std::to_wstring(summary.fpkg));
    if (summary.dumps) details.push_back(L"Dump " + std::to_wstring(summary.dumps));
    if (summary.patches) details.push_back(L"Patch " + std::to_wstring(summary.patches));
    if (summary.dlc) details.push_back(L"DLC " + std::to_wstring(summary.dlc));
    if (details.empty()) details.push_back(L"No package metadata detected");

    std::wstring result;
    for (const auto& detail : details) {
        if (!result.empty()) result += L" • ";
        result += detail;
    }
    return result;
}

std::wstring volumeRootForInfo(const std::wstring& rawRoot) {
    std::wstring root = rawRoot;
    if (root.rfind(L"\\\\?\\", 0) == 0) root.erase(0, 4);
    if (root.size() >= 2 && root[1] == L':') return root.substr(0, 3);
    return root;
}

bool isExfatVolume(const std::wstring& rawRoot) {
    const std::wstring volumeRoot = volumeRootForInfo(rawRoot);
    wchar_t fileSystemName[MAX_PATH] = {};
    return GetVolumeInformationW(volumeRoot.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fileSystemName,
                                 MAX_PATH) != FALSE &&
           equalsIgnoreCase(fileSystemName, L"exFAT");
}

std::filesystem::path extendedPath(const std::wstring& raw) {
    if (raw.rfind(L"\\\\?\\", 0) == 0) return std::filesystem::path(raw);
    if (raw.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + raw.substr(2));
    return std::filesystem::path(L"\\\\?\\" + raw);
}
}  // namespace

LRESULT JbGamesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 360, 58);
    titleLabel_.Create(m_hWnd, titleRect, L"JB games", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect scanRect(kMargin + 380, 28, kMargin + 560, 60);
    scanButton_.Create(m_hWnd, scanRect, L"Scan Local HDDs", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                       IDC_JB_SCAN_BUTTON);
    scanButton_.SetFont(theme::BodyFont());
    scanButton_.setKind(ModernButton::Kind::Primary);

    CRect statusRect(kMargin + 580, 18, 1100, 70);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL,
                     0, IDC_JB_GAMES_LISTVIEW);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);
    listView_.SetIconSpacing(PosterCache::kTileWidth + 24, PosterCache::kTileHeight + 52);
    if (posterCache_) posterCache_->subscribe(m_hWnd);

    renderEntries();
    return 0;
}

LRESULT JbGamesView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kScanPollTimerId);
    if (scanThread_.joinable()) scanThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT JbGamesView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (statusLabel_.IsWindow()) {
        statusLabel_.MoveWindow(kMargin + 580, 12, std::max(200, width - kMargin - 580), 70);
    }
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                             std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT JbGamesView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) {
    return HandleCtlColorStatic(wParam);
}

void JbGamesView::refresh() {
    if (!listView_.IsWindow()) return;
    renderEntries();
    if (!hasScanned_ && !scanInProgress_.load()) startScan();
}

void JbGamesView::renderEntries() {
    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();

    if (posterImageList_.IsNull()) {
        posterImageList_.Create(PosterCache::kTileWidth, PosterCache::kTileHeight, ILC_COLOR32, 0,
                                static_cast<int>(entries_.size()) + 1);
    } else {
        posterImageList_.RemoveAll();
    }
    listView_.SetImageList(posterImageList_, LVSIL_NORMAL);

    int row = 0;
    for (const auto& entry : entries_) {
        const std::wstring label = entry.title + L"\r\n" + Utf8ToWide(entry.platform) + L" • " + entry.details;
        const int imageIndex = posterCache_
            ? posterImageList_.Add(posterCache_->posterFor(entry.titleId, WideToUtf8(entry.title), entry.platform,
                                                          posterService_, ""))
            : -1;
        listView_.InsertItem(row++, label.c_str(), imageIndex);
    }

    scanButton_.EnableWindow(scanInProgress_.load() ? FALSE : TRUE);
    if (scanInProgress_.load()) {
        statusLabel_.SetWindowText(L"Scanning local drives for JB folders named 'hacked' or 'p30day'...");
    } else if (!hasScanned_) {
        statusLabel_.SetWindowText(L"No scan has run yet.");
    } else {
        std::wstring status = L"Found " + std::to_wstring(entries_.size()) + L" game(s) in " +
                               std::to_wstring(libraryFolderCount_) + L" JB folder(s) across " +
                               std::to_wstring(storageCount_) + L" local drive(s), including " +
                               std::to_wstring(exfatStorageCount_) + L" exFAT drive(s).";
        if (!scanError_.empty()) status += L"\r\n" + scanError_;
        statusLabel_.SetWindowText(status.c_str());
    }
}

LRESULT JbGamesView::OnScanClick(WORD, WORD, HWND, BOOL&) {
    if (!scanInProgress_.load()) startScan();
    return 0;
}

void JbGamesView::startScan() {
    if (!storageService_ || scanInProgress_.exchange(true)) return;
    if (scanThread_.joinable()) scanThread_.join();

    entries_.clear();
    libraryFolderCount_ = 0;
    storageCount_ = 0;
    exfatStorageCount_ = 0;
    scanError_.clear();
    renderEntries();

    scanThread_ = std::thread([this] {
        std::vector<Entry> found;
        std::set<std::wstring> seenPaths;
        int libraryFolders = 0;
        int drives = 0;
        int exfatDrives = 0;
        std::wstring error;

        try {
            // Keep the drive list current, just like the Games scan does.
            storageService_->detectAndSyncLocalStorages();
            const auto storages = storageService_->getAllStorages();
            for (const auto& storage : storages) {
                if (!storage.isAvailable || !isLocalDrive(storage.type)) continue;
                ++drives;

                const std::wstring rawRoot = Utf8ToWide(storage.rootPath);
                const bool exfat = isExfatVolume(rawRoot);
                if (exfat) ++exfatDrives;

                std::error_code ec;
                const auto root = extendedPath(rawRoot);
                if (!std::filesystem::is_directory(root, ec)) continue;

                const auto options = std::filesystem::directory_options::skip_permission_denied;
                for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; it != end;
                     it.increment(ec)) {
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    if (!it->is_directory(ec)) continue;
                    if (!isJbLibraryFolder(it->path().filename().wstring())) continue;

                    ++libraryFolders;
                    for (std::filesystem::directory_iterator child(it->path(), options, ec), childEnd; child != childEnd;
                         child.increment(ec)) {
                        if (ec) {
                            ec.clear();
                            continue;
                        }
                        const auto childPath = child->path();
                        const bool gameDirectory = child->is_directory(ec);
                        const bool gameFile = child->is_regular_file(ec) && isGameArchive(childPath);
                        if (!gameDirectory && !gameFile) continue;
                        const auto key = childPath.wstring();
                        if (!seenPaths.insert(key).second) continue;

                        Entry entry;
                        entry.title = childPath.stem().wstring();
                        if (gameDirectory) entry.title = childPath.filename().wstring();
                        entry.titleId = titleIdFromName(entry.title);
                        entry.platform = platformFromTitleId(entry.titleId);
                        entry.path = childPath;

                        ArtifactSummary artifacts;
                        addArtifact(childPath, gameDirectory, artifacts);
                        if (gameDirectory) {
                            for (std::filesystem::recursive_directory_iterator artifactIt(
                                     childPath, options, ec), artifactEnd;
                                 artifactIt != artifactEnd; artifactIt.increment(ec)) {
                                if (ec) {
                                    ec.clear();
                                    continue;
                                }
                                const bool artifactDirectory = artifactIt->is_directory(ec);
                                if (ec) {
                                    ec.clear();
                                    continue;
                                }
                                addArtifact(artifactIt->path(), artifactDirectory, artifacts);
                            }
                        }
                        entry.details = artifactDetails(artifacts, exfat);
                        found.push_back(std::move(entry));
                    }

                    // The parent iterator only needs to find JB library roots;
                    // each game directory is inspected separately for local
                    // package, dump, patch, and DLC markers.
                    it.disable_recursion_pending();
                }
            }
        } catch (const std::exception& ex) {
            error = Utf8ToWide(ex.what());
        }

        std::sort(found.begin(), found.end(), [](const Entry& left, const Entry& right) {
            if (left.title != right.title) return left.title < right.title;
            return left.path.wstring() < right.path.wstring();
        });

        entries_ = std::move(found);
        libraryFolderCount_ = libraryFolders;
        storageCount_ = drives;
        exfatStorageCount_ = exfatDrives;
        scanError_ = std::move(error);
        hasScanned_ = true;
        scanInProgress_.store(false);
    });
    SetTimer(kScanPollTimerId, kScanPollIntervalMs);
}

LRESULT JbGamesView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != kScanPollTimerId || scanInProgress_.load()) return 0;
    KillTimer(kScanPollTimerId);
    if (scanThread_.joinable()) scanThread_.join();
    renderEntries();
    return 0;
}

