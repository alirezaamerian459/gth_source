#include "BackupsView.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <sstream>

#include "gth/core/enums/GameVariantStatus.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr int kMargin = 24;
constexpr int kTimerId = 1;
constexpr UINT kTimerIntervalMs = 250;
constexpr const char* kCredentialTarget = "telegram-backup-bot-token";

std::wstring displayBytes(long long bytes) {
    constexpr long long kMiB = 1024LL * 1024;
    if (bytes < 0) return L"";
    std::wostringstream out;
    if (bytes >= kMiB) out << (bytes / kMiB) << L" MiB";
    else out << bytes << L" bytes";
    return out.str();
}

std::wstring platformStatus(gth::core::GameVariantStatus status) {
    return status == gth::core::GameVariantStatus::Active ? L"Available" : L"Missing";
}

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    return value.substr(start);
}
}  // namespace

void BackupsView::init(gth::application::BackupService& backupService,
                       gth::application::IGameLibraryService& library,
                       gth::network::TelegramBackupTransport& transport,
                       gth::core::ICredentialStore& credentialStore,
                       std::filesystem::path chatIdPath, std::filesystem::path defaultRestoreRoot) {
    backupService_ = &backupService;
    library_ = &library;
    transport_ = &transport;
    credentialStore_ = &credentialStore;
    chatIdPath_ = std::move(chatIdPath);
    defaultRestoreRoot_ = std::move(defaultRestoreRoot);
}

LRESULT BackupsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    HFONT body = theme::BodyFont();
    CRect placeholderRect(0, 0, 100, 30);
    titleLabel_.Create(m_hWnd, placeholderRect, L"Backups", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());
    setupLabel_.Create(m_hWnd, placeholderRect,
        L"Use a private Telegram channel. Create a bot with @BotFather, add it as a channel administrator, then enter its token and the channel @username or numeric chat ID.",
        WS_CHILD | WS_VISIBLE | SS_LEFT);
    setupLabel_.SetFont(body);
    tokenCaption_.Create(m_hWnd, placeholderRect, L"Telegram bot token", WS_CHILD | WS_VISIBLE);
    tokenCaption_.SetFont(body);
    tokenEdit_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL);
    tokenEdit_.SetFont(body);
    chatCaption_.Create(m_hWnd, placeholderRect, L"Private channel username or chat ID", WS_CHILD | WS_VISIBLE);
    chatCaption_.SetFont(body);
    chatEdit_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    chatEdit_.SetFont(body);

    auto makeButton = [&](ModernButton& button, const wchar_t* text, UINT id, ModernButton::Kind kind) {
        button.Create(m_hWnd, placeholderRect, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, id);
        button.SetFont(body);
        button.setKind(kind);
    };
    makeButton(saveButton_, L"Save Telegram settings", IDC_BACKUPS_SAVE_CONFIG_BUTTON, ModernButton::Kind::Primary);
    makeButton(testButton_, L"Test connection", IDC_BACKUPS_TEST_BUTTON, ModernButton::Kind::Secondary);
    makeButton(backupSelectedButton_, L"Back up selected", IDC_BACKUPS_SELECTED_BUTTON, ModernButton::Kind::Primary);
    makeButton(backupAllButton_, L"Back up all available games", IDC_BACKUPS_ALL_BUTTON, ModernButton::Kind::Secondary);
    makeButton(cancelButton_, L"Cancel", IDC_BACKUPS_CANCEL_BUTTON, ModernButton::Kind::Danger);
    makeButton(refreshButton_, L"Refresh", IDC_BACKUPS_REFRESH_BUTTON, ModernButton::Kind::Subtle);
    makeButton(browseButton_, L"Browse...", IDC_BACKUPS_BROWSE_BUTTON, ModernButton::Kind::Subtle);
    makeButton(restoreButton_, L"Restore backup", IDC_BACKUPS_RESTORE_BUTTON, ModernButton::Kind::Primary);
    makeButton(copyIdButton_, L"Copy selected restore ID", IDC_BACKUPS_COPY_ID_BUTTON, ModernButton::Kind::Subtle);

    gamesCaption_.Create(m_hWnd, placeholderRect, L"Games in this GTH library", WS_CHILD | WS_VISIBLE);
    gamesCaption_.SetFont(theme::SectionFont());
    backupsCaption_.Create(m_hWnd, placeholderRect, L"Backups available to restore", WS_CHILD | WS_VISIBLE);
    backupsCaption_.SetFont(theme::SectionFont());
    restorePathCaption_.Create(m_hWnd, placeholderRect, L"Restore into a new folder under", WS_CHILD | WS_VISIBLE);
    restorePathCaption_.SetFont(body);
    manifestIdCaption_.Create(m_hWnd, placeholderRect, L"Telegram manifest file ID (paste a saved key to restore on this install)", WS_CHILD | WS_VISIBLE);
    manifestIdCaption_.SetFont(body);
    manifestIdEdit_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    manifestIdEdit_.SetFont(body);
    restorePathEdit_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    restorePathEdit_.SetFont(body);
    statusLabel_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | SS_LEFT);
    statusLabel_.SetFont(body);

    gamesList_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    backupsList_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    gamesList_.SetFont(body);
    backupsList_.SetFont(body);
    theme::ApplyListViewTheme(gamesList_);
    theme::ApplyListViewTheme(backupsList_);
    gamesList_.InsertColumn(0, L"Game", LVCFMT_LEFT, 220, 0);
    gamesList_.InsertColumn(1, L"Platform", LVCFMT_LEFT, 90, 1);
    gamesList_.InsertColumn(2, L"Version", LVCFMT_LEFT, 90, 2);
    gamesList_.InsertColumn(3, L"Status", LVCFMT_LEFT, 90, 3);
    backupsList_.InsertColumn(0, L"Game", LVCFMT_LEFT, 200, 0);
    backupsList_.InsertColumn(1, L"Platform", LVCFMT_LEFT, 80, 1);
    backupsList_.InsertColumn(2, L"Version", LVCFMT_LEFT, 80, 2);
    backupsList_.InsertColumn(3, L"Date", LVCFMT_LEFT, 130, 3);
    backupsList_.InsertColumn(4, L"Size", LVCFMT_RIGHT, 80, 4);

    if (credentialStore_) {
        const auto credential = credentialStore_->read(kCredentialTarget);
        if (credential && !credential->secret.empty()) {
            statusLabel_.SetWindowText(L"A bot token is saved in Windows Credential Manager. Leave the token field blank to keep it.");
        }
    }
    if (!chatIdPath_.empty()) {
        std::ifstream chatFile(chatIdPath_, std::ios::binary);
        std::string chatId;
        std::getline(chatFile, chatId);
        chatId = trim(std::move(chatId));
        if (!chatId.empty()) chatEdit_.SetWindowText(Utf8ToWide(chatId).c_str());
    }
    restorePathEdit_.SetWindowText(defaultRestoreRoot_.wstring().c_str());
    configureTransport();
    ::SetTimer(m_hWnd, kTimerId, kTimerIntervalMs, nullptr);
    refresh();
    return 0;
}

LRESULT BackupsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    ::KillTimer(m_hWnd, kTimerId);
    cancel_.store(true);
    if (worker_.joinable()) worker_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT BackupsView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    const int usable = std::max(300, width - 2 * kMargin);
    const int gap = 20;
    const int half = std::max(140, (usable - gap) / 2);
    titleLabel_.MoveWindow(kMargin, 18, 320, 34);
    setupLabel_.MoveWindow(kMargin, 55, usable, 42);
    tokenCaption_.MoveWindow(kMargin, 102, 160, 20);
    tokenEdit_.MoveWindow(kMargin, 124, std::max(200, usable / 2 - 8), 25);
    chatCaption_.MoveWindow(kMargin + half + gap, 102, half, 20);
    chatEdit_.MoveWindow(kMargin + half + gap, 124, half, 25);
    saveButton_.MoveWindow(kMargin, 156, 180, 28);
    testButton_.MoveWindow(kMargin + 190, 156, 140, 28);
    backupSelectedButton_.MoveWindow(kMargin + 350, 156, 150, 28);
    backupAllButton_.MoveWindow(kMargin + 510, 156, 210, 28);
    cancelButton_.MoveWindow(kMargin + 730, 156, 90, 28);
    refreshButton_.MoveWindow(kMargin + 830, 156, 100, 28);

    const int listsTop = 224;
    const int restoreHeight = 136;
    const int listHeight = std::max(100, height - listsTop - restoreHeight - 62);
    gamesCaption_.MoveWindow(kMargin, 194, half, 24);
    backupsCaption_.MoveWindow(kMargin + half + gap, 194, half, 24);
    gamesList_.MoveWindow(kMargin, listsTop, half, listHeight);
    backupsList_.MoveWindow(kMargin + half + gap, listsTop, half, listHeight);
    manifestIdCaption_.MoveWindow(kMargin, listsTop + listHeight + 7, std::max(300, usable - half), 20);
    manifestIdEdit_.MoveWindow(kMargin, listsTop + listHeight + 29, half, 25);
    copyIdButton_.MoveWindow(kMargin + half + gap, listsTop + listHeight + 29, 180, 25);
    restorePathCaption_.MoveWindow(kMargin, listsTop + listHeight + 61, 300, 20);
    restorePathEdit_.MoveWindow(kMargin, listsTop + listHeight + 83, std::max(200, usable - 330), 25);
    browseButton_.MoveWindow(kMargin + std::max(200, usable - 320), listsTop + listHeight + 83, 90, 25);
    restoreButton_.MoveWindow(kMargin + std::max(300, usable - 220), listsTop + listHeight + 83, 150, 25);
    statusLabel_.MoveWindow(kMargin, height - 29, usable, 24);
    return 0;
}

LRESULT BackupsView::OnTimer(UINT, WPARAM timerId, LPARAM, BOOL&) {
    if (timerId != kTimerId) return 0;
    std::wstring progress;
    {
        std::lock_guard lock(statusMutex_);
        progress = workerStatus_;
    }
    if (running_.load()) {
        if (!progress.empty()) statusLabel_.SetWindowText(progress.c_str());
        return 0;
    }
    if (worker_.joinable()) {
        worker_.join();
        std::wstring result;
        {
            std::lock_guard lock(statusMutex_);
            result = workerResult_;
            workerResult_.clear();
            workerStatus_.clear();
        }
        setBusy(false);
        if (!result.empty()) statusLabel_.SetWindowText(result.c_str());
        refresh();
    }
    return 0;
}

LRESULT BackupsView::OnSaveConfig(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) return 0;
    std::wstring message;
    if (!saveConfiguration(message)) {
        setStatus(message);
        return 0;
    }
    tokenEdit_.SetWindowText(L"");
    setStatus(message);
    return 0;
}

LRESULT BackupsView::OnTestClick(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) return 0;
    std::wstring saved;
    if (!saveConfiguration(saved)) {
        setStatus(saved);
        return 0;
    }
    configureTransport();
    startWorker([this] {
        std::string error;
        if (transport_->testConnection(error)) return std::wstring(L"Telegram connection works. The bot can access this channel.");
        return Utf8ToWide(error);
    });
    return 0;
}

LRESULT BackupsView::OnBackupSelected(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) return 0;
    const int selected = gamesList_.GetSelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(games_.size())) {
        setStatus(L"Select an available game first.");
        return 0;
    }
    const auto& entry = games_[static_cast<size_t>(selected)];
    if (entry.variantStatus != gth::core::GameVariantStatus::Active) {
        setStatus(L"That game is marked missing from its storage.");
        return 0;
    }
    std::wstring saved;
    if (!saveConfiguration(saved)) { setStatus(saved); return 0; }
    configureTransport();
    const gth::application::BackupGame game{entry.gameVariantId, entry.title, entry.platform, entry.currentVersion};
    startWorker([this, game] {
        gth::application::BackupCallbacks callbacks;
        callbacks.shouldCancel = [this] { return cancel_.load(); };
        callbacks.onProgress = [this](const std::string& stage, long long done, long long total) {
            std::wstring message = Utf8ToWide(stage);
            if (total > 0) message += L" - " + displayBytes(done) + L" / " + displayBytes(total);
            std::lock_guard lock(statusMutex_);
            workerStatus_ = std::move(message);
        };
        auto result = backupService_->createBackup(game, callbacks);
        if (result.success) return L"Backup complete: " + Utf8ToWide(game.title) + L" (" + displayBytes(result.bytesTransferred) + L").";
        return Utf8ToWide(result.error.empty() ? "Backup failed." : result.error);
    });
    return 0;
}

LRESULT BackupsView::OnBackupAll(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) return 0;
    auto games = activeGames();
    if (games.empty()) { setStatus(L"No available games are in this GTH library."); return 0; }
    std::wstring saved;
    if (!saveConfiguration(saved)) { setStatus(saved); return 0; }
    configureTransport();
    startWorker([this, games = std::move(games)] {
        gth::application::BackupCallbacks callbacks;
        callbacks.shouldCancel = [this] { return cancel_.load(); };
        callbacks.onProgress = [this](const std::string& stage, long long done, long long total) {
            std::wstring message = Utf8ToWide(stage);
            if (total > 0) message += L" - " + displayBytes(done) + L" / " + displayBytes(total);
            std::lock_guard lock(statusMutex_);
            workerStatus_ = std::move(message);
        };
        const auto batch = backupService_->createBackups(games, callbacks);
        std::wstring message = L"Backup run finished: " + std::to_wstring(batch.backedUp) + L" complete, " +
            std::to_wstring(batch.failed) + L" failed.";
        if (batch.cancelled) message += L" Cancelled.";
        for (const auto& item : batch.games) {
            if (!item.success && !item.error.empty()) { message += L"\r\n" + Utf8ToWide(item.error); break; }
        }
        return message;
    });
    return 0;
}

LRESULT BackupsView::OnCancelClick(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) {
        cancel_.store(true);
        setStatus(L"Cancelling after the current network operation...");
    }
    return 0;
}

LRESULT BackupsView::OnRefreshClick(WORD, WORD, HWND, BOOL&) { refresh(); return 0; }

LRESULT BackupsView::OnBrowseClick(WORD, WORD, HWND, BOOL&) {
    const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool initializedHere = SUCCEEDED(result);
    std::array<wchar_t, MAX_PATH> display{};
    BROWSEINFOW info{};
    info.hwndOwner = m_hWnd;
    info.pszDisplayName = display.data();
    info.lpszTitle = L"Choose a restore destination folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST item = ::SHBrowseForFolderW(&info);
    if (item) {
        std::array<wchar_t, MAX_PATH> path{};
        if (::SHGetPathFromIDListW(item, path.data())) restorePathEdit_.SetWindowText(path.data());
        ::CoTaskMemFree(item);
    }
    if (initializedHere) ::CoUninitialize();
    return 0;
}

LRESULT BackupsView::OnRestoreClick(WORD, WORD, HWND, BOOL&) {
    if (running_.load()) return 0;
    const int idLength = manifestIdEdit_.GetWindowTextLength();
    std::wstring manifestIdText(static_cast<size_t>(idLength) + 1, L'\0');
    manifestIdEdit_.GetWindowText(manifestIdText.data(), idLength + 1);
    manifestIdText.resize(static_cast<size_t>(idLength));
    std::string manifestId = trim(WideToUtf8(manifestIdText));
    if (manifestId.empty()) {
        const int selected = backupsList_.GetSelectedIndex();
        if (selected < 0 || selected >= static_cast<int>(backups_.size())) {
            setStatus(L"Select a Telegram backup or paste its manifest file ID.");
            return 0;
        }
        manifestId = backups_[static_cast<size_t>(selected)].objectId;
    }
    const std::wstring destinationText = [&] {
        const int length = restorePathEdit_.GetWindowTextLength();
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        restorePathEdit_.GetWindowText(text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }();
    if (destinationText.empty()) { setStatus(L"Choose a restore destination folder."); return 0; }
    std::wstring saved;
    if (!saveConfiguration(saved)) { setStatus(saved); return 0; }
    configureTransport();
    const std::filesystem::path destination(destinationText);
    startWorker([this, manifestId = std::move(manifestId), destination] {
        gth::application::BackupCallbacks callbacks;
        callbacks.shouldCancel = [this] { return cancel_.load(); };
        callbacks.onProgress = [this](const std::string& stage, long long done, long long total) {
            std::wstring message = Utf8ToWide(stage);
            if (total > 0) message += L" - " + displayBytes(done) + L" / " + displayBytes(total);
            std::lock_guard lock(statusMutex_);
            workerStatus_ = std::move(message);
        };
        const auto result = backupService_->restoreBackup(manifestId, destination, callbacks);
        if (result.success) return std::wstring(L"Restore complete. GTH created a new verified folder under the selected destination.");
        return Utf8ToWide(result.error.empty() ? "Restore failed." : result.error);
    });
    return 0;
}

LRESULT BackupsView::OnCopyIdClick(WORD, WORD, HWND, BOOL&) {
    const int selected = backupsList_.GetSelectedIndex();
    if (selected < 0 || selected >= static_cast<int>(backups_.size())) {
        setStatus(L"Select a backup first.");
        return 0;
    }
    const std::wstring id = Utf8ToWide(backups_[static_cast<size_t>(selected)].objectId);
    manifestIdEdit_.SetWindowText(id.c_str());
    if (!::OpenClipboard(m_hWnd)) { setStatus(L"Restore ID filled in; Windows clipboard is busy."); return 0; }
    ::EmptyClipboard();
    const SIZE_T bytes = (id.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        auto* target = static_cast<wchar_t*>(::GlobalLock(memory));
        if (target) {
            std::copy(id.begin(), id.end(), target);
            target[id.size()] = L'\0';
            ::GlobalUnlock(memory);
            if (!::SetClipboardData(CF_UNICODETEXT, memory)) ::GlobalFree(memory);
        } else {
            ::GlobalFree(memory);
        }
    }
    ::CloseClipboard();
    setStatus(L"Restore ID copied. Save it somewhere you can reach if this PC or its local catalog is lost.");
    return 0;
}

bool BackupsView::saveConfiguration(std::wstring& message) {
    if (!credentialStore_) { message = L"The Windows credential store is unavailable."; return false; }
    const int tokenLength = tokenEdit_.GetWindowTextLength();
    std::wstring tokenText(static_cast<size_t>(tokenLength) + 1, L'\0');
    tokenEdit_.GetWindowText(tokenText.data(), tokenLength + 1);
    tokenText.resize(static_cast<size_t>(tokenLength));
    const int chatLength = chatEdit_.GetWindowTextLength();
    std::wstring chatText(static_cast<size_t>(chatLength) + 1, L'\0');
    chatEdit_.GetWindowText(chatText.data(), chatLength + 1);
    chatText.resize(static_cast<size_t>(chatLength));
    const std::string token = WideToUtf8(tokenText);
    const std::string chatId = trim(WideToUtf8(chatText));
    const auto existing = credentialStore_->read(kCredentialTarget);
    if (token.empty() && (!existing || existing->secret.empty())) {
        message = L"Enter a Telegram bot token first.";
        return false;
    }
    if (chatId.empty()) { message = L"Enter the private channel username or chat ID."; return false; }
    if (!token.empty()) {
        credentialStore_->save(kCredentialTarget, "", token);
        const auto saved = credentialStore_->read(kCredentialTarget);
        if (!saved || saved->secret != token) {
            message = L"Windows Credential Manager couldn't save the bot token for this Windows account.";
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(chatIdPath_.parent_path(), ec);
    if (ec) { message = L"Couldn't create the GTH settings folder."; return false; }
    const auto tempPath = std::filesystem::path(chatIdPath_.wstring() + L".tmp");
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        output << chatId << "\n";
        if (!output) { message = L"Couldn't save the Telegram channel ID."; return false; }
    }
    if (!::MoveFileExW(tempPath.c_str(), chatIdPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tempPath, ec);
        message = L"Couldn't finish saving the Telegram channel ID.";
        return false;
    }
    configureTransport();
    message = L"Telegram settings saved. The bot token is stored in Windows Credential Manager.";
    return true;
}

void BackupsView::configureTransport() {
    if (!transport_ || !credentialStore_) return;
    const auto credential = credentialStore_->read(kCredentialTarget);
    std::string chatId;
    if (!chatIdPath_.empty()) {
        std::ifstream input(chatIdPath_, std::ios::binary);
        std::getline(input, chatId);
        chatId = trim(std::move(chatId));
    }
    transport_->configure(credential ? credential->secret : std::string{}, std::move(chatId));
}

void BackupsView::setBusy(bool busy) {
    const BOOL enabled = busy ? FALSE : TRUE;
    saveButton_.EnableWindow(enabled);
    testButton_.EnableWindow(enabled);
    backupSelectedButton_.EnableWindow(enabled);
    backupAllButton_.EnableWindow(enabled);
    refreshButton_.EnableWindow(enabled);
    restoreButton_.EnableWindow(enabled);
    browseButton_.EnableWindow(enabled);
    cancelButton_.EnableWindow(busy ? TRUE : FALSE);
}

void BackupsView::setStatus(const std::wstring& text) { statusLabel_.SetWindowText(text.c_str()); }

void BackupsView::startWorker(std::function<std::wstring()> operation) {
    if (worker_.joinable()) worker_.join();
    cancel_.store(false);
    running_.store(true);
    {
        std::lock_guard lock(statusMutex_);
        workerStatus_ = L"Starting...";
        workerResult_.clear();
    }
    setBusy(true);
    worker_ = std::thread([this, operation = std::move(operation)]() mutable {
        std::wstring result;
        try { result = operation(); }
        catch (const std::exception& ex) { result = Utf8ToWide(ex.what()); }
        catch (...) { result = L"The backup operation failed with an unexpected error."; }
        {
            std::lock_guard lock(statusMutex_);
            workerResult_ = std::move(result);
        }
        running_.store(false);
    });
}

std::vector<gth::application::BackupGame> BackupsView::activeGames() const {
    std::vector<gth::application::BackupGame> result;
    for (const auto& entry : games_) {
        if (entry.variantStatus != gth::core::GameVariantStatus::Active || entry.currentVersion.empty()) continue;
        result.push_back({entry.gameVariantId, entry.title, entry.platform, entry.currentVersion});
    }
    return result;
}

void BackupsView::refresh() {
    if (!library_ || !gamesList_.IsWindow() || running_.load()) return;
    games_ = library_->getAllEntries();
    backups_ = backupService_ ? backupService_->listBackups() : std::vector<gth::application::BackupManifestRef>{};
    gamesList_.DeleteAllItems();
    for (size_t i = 0; i < games_.size(); ++i) {
        const auto& entry = games_[i];
        gamesList_.InsertItem(static_cast<int>(i), Utf8ToWide(entry.title).c_str());
        gamesList_.SetItemText(static_cast<int>(i), 1, Utf8ToWide(entry.platform).c_str());
        gamesList_.SetItemText(static_cast<int>(i), 2, Utf8ToWide(entry.currentVersion).c_str());
        gamesList_.SetItemText(static_cast<int>(i), 3, platformStatus(entry.variantStatus).c_str());
    }
    backupsList_.DeleteAllItems();
    for (size_t i = 0; i < backups_.size(); ++i) {
        const auto& backup = backups_[i];
        backupsList_.InsertItem(static_cast<int>(i), Utf8ToWide(backup.title).c_str());
        backupsList_.SetItemText(static_cast<int>(i), 1, Utf8ToWide(backup.platform).c_str());
        backupsList_.SetItemText(static_cast<int>(i), 2, Utf8ToWide(backup.version).c_str());
        backupsList_.SetItemText(static_cast<int>(i), 3, Utf8ToWide(backup.createdAt).c_str());
        backupsList_.SetItemText(static_cast<int>(i), 4, displayBytes(backup.totalBytes).c_str());
    }
    if (games_.empty()) setStatus(L"No games are indexed yet. Scan a storage in the Games tab first.");
    else if (!credentialStore_ || !credentialStore_->read(kCredentialTarget)) setStatus(L"Configure Telegram above to start backing up games.");
}

