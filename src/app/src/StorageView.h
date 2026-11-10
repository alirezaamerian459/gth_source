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

class StorageView;

class StorageBoardWindow : public CWindowImpl<StorageBoardWindow> {
public:
    DECLARE_WND_CLASS(L"GthStorageBoard")

    BEGIN_MSG_MAP(StorageBoardWindow)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
        MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
    END_MSG_MAP()

    void init(StorageView& owner) { owner_ = &owner; }
    void setContentExtent(int width, int height);
    void refresh() { Invalidate(FALSE); }
    int horizontalPosition() const { return horizontalPosition_; }
    int verticalPosition() const { return verticalPosition_; }

private:
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnHScroll(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);

    void updateScrollBars();
    void scrollTo(int horizontal, int vertical);
    int maxHorizontal() const;
    int maxVertical() const;

    StorageView* owner_ = nullptr;
    int contentWidth_ = 0;
    int contentHeight_ = 0;
    int horizontalPosition_ = 0;
    int verticalPosition_ = 0;
};

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
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColorStatic)
        COMMAND_ID_HANDLER(IDC_RESCAN_BUTTON, OnRescanClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_HEALTH_DETAILS_BUTTON, OnHealthDetailsClick)
        COMMAND_ID_HANDLER(IDC_STORAGE_CATEGORY_CREATE_BUTTON, OnCreateCategoryClick)
    END_MSG_MAP()

    void init(gth::application::IStorageService& service) { service_ = &service; }
    void refresh();

private:
    friend class StorageBoardWindow;

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRescanClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnHealthDetailsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCreateCategoryClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    void layoutControls();
    void startRescan();
    int categoryCardWidth() const;
    void updateBoardExtent();
    void paintCategoryBoard(HDC hdc, const RECT& clientRect, int horizontalOffset, int verticalOffset);
    void showCategoryContextMenu(POINT screenPoint);

    gth::application::IStorageService* service_ = nullptr;

    struct StorageCard {
        gth::core::Storage storage;
        std::vector<gth::core::PhysicalDiskHealth> disks;
        bool flagged = false;
    };

    int storageCardHeight(const StorageCard& card) const;

    struct CategoryCard {
        std::string categoryId;
        std::wstring name;
        bool uncategorized = false;
        std::vector<StorageCard> storages;
    };

    struct CategorySummary {
        std::wstring name;
        long long capacityBytes = 0;
        long long usedBytes = 0;
    };

    std::vector<CategoryCard> categoryCards_;
    std::vector<CategorySummary> categorySummaries_;
    long long totalCapacityBytes_ = 0;
    long long totalUsedBytes_ = 0;
    std::jthread rescanThread_;
    std::atomic<bool> rescanInProgress_{false};

    CStatic titleLabel_;
    ModernButton rescanButton_;
    CEdit categoryNameEdit_;
    ModernButton createCategoryButton_;
    ModernButton healthDetailsButton_;
    StorageBoardWindow board_;
    bool healthDetailsVisible_ = false;
};
