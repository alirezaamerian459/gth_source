#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include "gth/application/storage/IStorageService.h"
#include "gth/core/entities/Storage.h"
#include "gth/core/storage/PhysicalDiskHealth.h"

class StorageView : public CWindowImpl<StorageView> {
public:
    DECLARE_WND_CLASS(L"GthStorageView")

    BEGIN_MSG_MAP(StorageView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        COMMAND_ID_HANDLER(IDC_RESCAN_BUTTON, OnRescanClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_ADD_BUTTON, OnAddStorageClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_COMBO, OnCategorySelectionChanged)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_CREATE_BUTTON, OnCreateCategoryClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_RENAME_BUTTON, OnRenameCategoryClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_DELETE_BUTTON, OnDeleteCategoryClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_STORAGE_COMBO, OnCategoryStorageSelectionChanged)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_ADD_STORAGE_BUTTON, OnAddStorageToCategoryClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_REMOVE_STORAGE_BUTTON, OnRemoveStorageFromCategoryClick)
        NOTIFY_CODE_HANDLER(NM_CUSTOMDRAW, OnListViewCustomDraw)
    END_MSG_MAP()

    void init(gth::application::IStorageService& service) { service_ = &service; }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRescanClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddStorageClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCategorySelectionChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCreateCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRenameCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnDeleteCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCategoryStorageSelectionChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddStorageToCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRemoveStorageFromCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnListViewCustomDraw(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

    // Starts a background rescan if one isn't already running; shared by the
    // Rescan button and the 5-second auto-refresh timer.
    void startRescan();

    gth::application::IStorageService* service_ = nullptr;

    // Parallel to the ListView's rows so custom-draw can look up whether a
    // given row index is flagged without round-tripping through the service.
    // One entry per list row - a storage, or one of its physical disks listed directly under it - so a
    // drive spread over several disks shows each disk's health and temperature without any clicking.
    struct RowInfo {
        bool flagged = false;
        std::string storageId;
    };
    std::vector<RowInfo> lastRows_;
    std::vector<std::string> categoryIds_;
    std::vector<std::string> categoryStorageIds_;
    std::string selectedCategoryId_;

    // detectAndSyncLocalStorages() calls into Win32/WMI drive probes that -
    // even with LocalStorageDetector/WindowsStorageHealthProvider's own
    // per-drive timeouts - can still take several seconds across multiple
    // drives. Running it on the UI thread would leave the window unable to
    // pump messages for that whole stretch, which is exactly what Windows
    // flags as "Not Responding". Run it on this background thread instead;
    // a short-lived poll timer (started only while a scan is in flight)
    // notices completion and refreshes the list on the UI thread.
    std::thread rescanThread_;
    std::atomic<bool> rescanInProgress_{false};

    CStatic titleLabel_;
    ModernButton rescanButton_;
    CEdit addPathEdit_;
    ModernButton addPathButton_;
    CStatic addStatusLabel_;
    CStatic categoryLabel_;
    CStatic categoryStorageLabel_;
    CComboBox categoryCombo_;
    CEdit categoryNameEdit_;
    ModernButton createCategoryButton_;
    ModernButton renameCategoryButton_;
    ModernButton deleteCategoryButton_;
    CComboBox categoryStorageCombo_;
    ModernButton addToCategoryButton_;
    ModernButton removeFromCategoryButton_;
    CStatic categoryStatusLabel_;
    CListViewCtrl listView_;
};
