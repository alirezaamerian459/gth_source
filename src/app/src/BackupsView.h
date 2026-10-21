#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include <gth/application/backup/BackupService.h>
#include <gth/application/indexing/IGameLibraryService.h>
#include <gth/core/credentials/ICredentialStore.h>
#include <gth/network/TelegramBackupTransport.h>

class BackupsView : public CWindowImpl<BackupsView> {
public:
    DECLARE_WND_CLASS(L"GthBackupsView")

    BEGIN_MSG_MAP(BackupsView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColorEdit)
        COMMAND_ID_HANDLER(IDC_BACKUPS_SAVE_CONFIG_BUTTON, OnSaveConfig)
        COMMAND_ID_HANDLER(IDC_BACKUPS_TEST_BUTTON, OnTestClick)
        COMMAND_ID_HANDLER(IDC_BACKUPS_SELECTED_BUTTON, OnBackupSelected)
        COMMAND_ID_HANDLER(IDC_BACKUPS_ALL_BUTTON, OnBackupAll)
        COMMAND_ID_HANDLER(IDC_BACKUPS_CANCEL_BUTTON, OnCancelClick)
        COMMAND_ID_HANDLER(IDC_BACKUPS_REFRESH_BUTTON, OnRefreshClick)
        COMMAND_ID_HANDLER(IDC_BACKUPS_BROWSE_BUTTON, OnBrowseClick)
        COMMAND_ID_HANDLER(IDC_BACKUPS_RESTORE_BUTTON, OnRestoreClick)
        COMMAND_ID_HANDLER(IDC_BACKUPS_COPY_ID_BUTTON, OnCopyIdClick)
    END_MSG_MAP()

    void init(gth::application::BackupService& backupService, gth::application::IGameLibraryService& library,
              gth::network::TelegramBackupTransport& transport, gth::core::ICredentialStore& credentialStore,
              std::filesystem::path chatIdPath, std::filesystem::path defaultRestoreRoot);
    void refresh();

private:
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&);
    LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }
    LRESULT OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }
    LRESULT OnSaveConfig(WORD, WORD, HWND, BOOL&);
    LRESULT OnTestClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnBackupSelected(WORD, WORD, HWND, BOOL&);
    LRESULT OnBackupAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnCancelClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnRefreshClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnBrowseClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnRestoreClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyIdClick(WORD, WORD, HWND, BOOL&);

    bool saveConfiguration(std::wstring& message);
    void configureTransport();
    void setBusy(bool busy);
    void setStatus(const std::wstring& text);
    void startWorker(std::function<std::wstring()> operation);
    std::vector<gth::application::BackupGame> activeGames() const;

    gth::application::BackupService* backupService_ = nullptr;
    gth::application::IGameLibraryService* library_ = nullptr;
    gth::network::TelegramBackupTransport* transport_ = nullptr;
    gth::core::ICredentialStore* credentialStore_ = nullptr;
    std::filesystem::path chatIdPath_;
    std::filesystem::path defaultRestoreRoot_;
    std::vector<gth::application::GameLibraryEntry> games_;
    std::vector<gth::application::BackupManifestRef> backups_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::mutex statusMutex_;
    std::wstring workerStatus_;
    std::wstring workerResult_;

    CStatic titleLabel_;
    CStatic setupLabel_;
    CStatic tokenCaption_;
    CEdit tokenEdit_;
    CStatic chatCaption_;
    CEdit chatEdit_;
    ModernButton saveButton_, testButton_, backupSelectedButton_, backupAllButton_;
    ModernButton cancelButton_, refreshButton_, browseButton_, restoreButton_, copyIdButton_;
    CStatic gamesCaption_, backupsCaption_, restorePathCaption_, manifestIdCaption_, statusLabel_;
    CEdit restorePathEdit_, manifestIdEdit_;
    CListViewCtrl gamesList_, backupsList_;
};
