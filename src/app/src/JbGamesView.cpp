#include "JbGamesView.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <format>
#include <set>
#include <string_view>

#include "JbScanTarget.h"
#include "gth/core/entities/Storage.h"

namespace {
constexpr int kTopAreaHeight = 112;
constexpr int kMargin = 24;
constexpr UINT_PTR kScanPollTimerId = 1;
constexpr UINT kScanPollIntervalMs = 200;

bool equalsIgnoreCase(const std::wstring& left, std::wstring_view right) {
    if (left.size() != right.size()) return false;
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

bool isJbLibraryFolder(const std::wstring& name, bool exfatVolume) {
    // Standard JB layouts use hacked/p30day. The exFAT layout used by the
    // external library drive exposes the same game folders below GAME.
    return equalsIgnoreCase(name, L"hacked") || equalsIgnoreCase(name, L"p30day") ||
           (exfatVolume && equalsIgnoreCase(name, L"GAME"));
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
           extension == L".dump" || extension == L".exfat";
}

struct ArtifactSummary {
    int packages = 0;
    int fpkg = 0;
    int dumps = 0;
    int exfat = 0;
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
    } else if (extension == L".exfat") {
        ++summary.exfat;
    }
}

std::wstring artifactDetails(const ArtifactSummary& summary, bool exfat) {
    std::vector<std::wstring> details;
    if (exfat) details.push_back(L"exFAT");
    if (summary.packages) details.push_back(std::format(L"PKG {}", summary.packages));
    if (summary.fpkg) details.push_back(std::format(L"FPKG {}", summary.fpkg));
    if (summary.exfat) details.push_back(std::format(L"exFAT file {}", summary.exfat));
    if (summary.dumps) details.push_back(std::format(L"Dump {}", summary.dumps));
    if (summary.patches) details.push_back(std::format(L"Patch {}", summary.patches));
    if (summary.dlc) details.push_back(std::format(L"DLC {}", summary.dlc));
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
    std::array<wchar_t, MAX_PATH> fileSystemName{};
    return GetVolumeInformationW(volumeRoot.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fileSystemName.data(),
                                 static_cast<DWORD>(fileSystemName.size())) != FALSE &&
           equalsIgnoreCase(std::wstring(fileSystemName.data()), L"exFAT");
}

std::filesystem::path extendedPath(const std::wstring& raw) {
    if (raw.rfind(L"\\\\?\\", 0) == 0) return std::filesystem::path(raw);
    if (raw.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + raw.substr(2));
    return std::filesystem::path(L"\\\\?\\" + raw);
}
}  // namespace

LRESULT JbGamesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 18, kMargin + 260, 52);
    titleLabel_.Create(m_hWnd, titleRect, L"JB Library", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect targetLabelRect(kMargin + 280, 22, kMargin + 370, 50);
    targetLabel_.Create(m_hWnd, targetLabelRect, L"Scan target:", WS_CHILD | WS_VISIBLE);
    targetLabel_.SetFont(theme::BodyFont());

    CRect storageComboRect(kMargin + 380, 18, kMargin + 630, 48);
    storageCombo_.Create(m_hWnd, storageComboRect, L"",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                         IDC_JB_STORAGE_COMBO);
    storageCombo_.SetFont(theme::BodyFont());
    ::SetWindowTheme(storageCombo_.m_hWnd, L"DarkMode_CFD", nullptr);

    CRect scanRect(kMargin + 650, 18, kMargin + 830, 50);
    scanButton_.Create(m_hWnd, scanRect, L"Scan all local drives", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                       IDC_JB_SCAN_BUTTON);
    scanButton_.SetFont(theme::BodyFont());
    scanButton_.setKind(ModernButton::Kind::Primary);

    CRect statusRect(kMargin + 280, 56, 1100, 96);
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
    if (targetLabel_.IsWindow()) targetLabel_.MoveWindow(kMargin + 280, 22, 90, 28);
    if (storageCombo_.IsWindow()) storageCombo_.MoveWindow(kMargin + 380, 18, 250, 30);
    if (scanButton_.IsWindow()) scanButton_.MoveWindow(kMargin + 650, 18, 180, 32);
    if (statusLabel_.IsWindow()) {
        statusLabel_.MoveWindow(kMargin + 280, 56, std::max(260, width - kMargin - 280), 44);
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

std::string JbGamesView::selectedTargetId() const {
    const int index = storageCombo_.GetCurSel();
    if (index < 0 || static_cast<size_t>(index) >= scanTargets_.size()) return {};
    return scanTargets_[static_cast<size_t>(index)].storageId;
}

std::wstring JbGamesView::selectedTargetLabel() const {
    const int index = storageCombo_.GetCurSel();
    if (index < 0 || static_cast<size_t>(index) >= scanTargets_.size()) return L"All local drives";
    return scanTargets_[static_cast<size_t>(index)].label;
}

void JbGamesView::refreshStorageTargets() {
    if (!storageService_ || !storageCombo_.IsWindow()) return;

    const std::string previousId = selectedTargetId();
    scanTargets_.clear();
    scanTargets_.push_back({{}, L"All local drives"});

    auto storages = storageService_->getAllStorages();
    std::sort(storages.begin(), storages.end(), [](const auto& left, const auto& right) {
        return left.rootPath < right.rootPath;
    });
    for (const auto& storage : storages) {
        if (!gth::app::jb::isSelectableStorage(storage)) continue;
        const std::wstring name = Utf8ToWide(storage.name.empty() ? storage.rootPath : storage.name);
        const std::wstring root = Utf8ToWide(storage.rootPath);
        scanTargets_.push_back({storage.storageId, std::format(L"{} ({})", name, root)});
    }

    storageCombo_.ResetContent();
    int selectedIndex = 0;
    for (size_t index = 0; index < scanTargets_.size(); ++index) {
        storageCombo_.AddString(scanTargets_[index].label.c_str());
        if (!previousId.empty() && scanTargets_[index].storageId == previousId) {
            selectedIndex = static_cast<int>(index);
        }
    }
    storageCombo_.SetCurSel(selectedIndex);
    if (scanButton_.IsWindow()) {
        scanButton_.SetWindowText(selectedIndex == 0 ? L"Scan all local drives" : L"Scan selected drive");
    }
}

void JbGamesView::refresh() {
    if (!listView_.IsWindow()) return;
    renderEntries();
    if (!hasScanned_ && !scanInProgress_.load()) startScan();
}

void JbGamesView::renderEntries() {
    refreshStorageTargets();
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
        const std::wstring label = std::format(L"{}\r\n{} • {}", entry.title, Utf8ToWide(entry.platform), entry.details);
        const int imageIndex = posterCache_
            ? posterImageList_.Add(posterCache_->posterFor(entry.titleId, WideToUtf8(entry.title), entry.platform,
                                                          posterService_, ""))
            : -1;
        listView_.InsertItem(row++, label.c_str(), imageIndex);
    }

    const bool scanInProgress = scanInProgress_.load();
    scanButton_.EnableWindow(scanInProgress ? FALSE : TRUE);
    storageCombo_.EnableWindow(scanInProgress ? FALSE : TRUE);
    if (scanInProgress) {
        statusLabel_.SetWindowText(
            std::format(L"Scanning {} for JB folders (hacked/p30day, or GAME on exFAT)...", scanScopeLabel_).c_str());
    } else if (!hasScanned_) {
        statusLabel_.SetWindowText(L"No scan has run yet.");
    } else {
        std::wstring status = std::format(
            L"Found {} game(s) in {} JB folder(s) across {} local drive(s), including {} exFAT drive(s). [{}]",
            entries_.size(), libraryFolderCount_, storageCount_, exfatStorageCount_, scanScopeLabel_);
        if (!scanError_.empty()) status += std::format(L"\r\n{}", scanError_);
        statusLabel_.SetWindowText(status.c_str());
    }
}

LRESULT JbGamesView::OnScanClick(WORD, WORD, HWND, BOOL&) {
    if (!scanInProgress_.load()) startScan();
    return 0;
}

LRESULT JbGamesView::OnTargetChange(WORD, WORD, HWND, BOOL&) {
    if (scanInProgress_.load()) return 0;
    const int index = storageCombo_.GetCurSel();
    scanButton_.SetWindowText(index <= 0 ? L"Scan all local drives" : L"Scan selected drive");
    statusLabel_.SetWindowText(std::format(L"Ready to scan {}.", selectedTargetLabel()).c_str());
    return 0;
}

void JbGamesView::startScan() {
    if (!storageService_ || scanInProgress_.exchange(true)) return;
    if (scanThread_.joinable()) scanThread_.join();

    const std::string targetStorageId = selectedTargetId();
    scanScopeLabel_ = selectedTargetLabel();

    entries_.clear();
    libraryFolderCount_ = 0;
    storageCount_ = 0;
    exfatStorageCount_ = 0;
    scanError_.clear();
    renderEntries();

    scanThread_ = std::jthread([this, targetStorageId] {
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
                if (!gth::app::jb::matchesTarget(storage, targetStorageId)) continue;
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
                    if (!isJbLibraryFolder(it->path().filename().wstring(), exfat)) continue;

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

