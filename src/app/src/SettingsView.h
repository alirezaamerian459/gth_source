#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "ModernCheckBox.h"
#include "ScrollSupport.h"
#include "SidebarTabs.h"
#include "Theme.h"
#include "gth/application/settings/IAppSettingsService.h"
#include "gth/application/auth/IAdminAuthService.h"
#include "gth/core/network/IFirewallService.h"

// The "Settings" tab: appearance (light/dark, applied instantly and
// persisted immediately - no separate Save button, matching the Windows
// Settings app model this whole shell already imitates, see Theme.h),
// which sidebar tabs are visible, a default local folder used to seed
// FtpView's local-path field, and a log verbosity preference. Every control
// here commits straight to IAppSettingsService on change; onSettingsChanged
// (passed in via init) lets MainFrame know it needs to re-theme itself and
// rebuild the sidebar.
class SettingsView : public CWindowImpl<SettingsView> {
public:
    DECLARE_WND_CLASS(L"GthSettingsView")

    BEGIN_MSG_MAP(SettingsView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        COMMAND_ID_HANDLER(IDC_SETTINGS_THEME_LIGHT_BUTTON, OnLightClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_THEME_DARK_BUTTON, OnDarkClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_FOLDER_BROWSE_BUTTON, OnFolderBrowseClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_FOLDER_SAVE_BUTTON, OnFolderSaveClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_RESET_BUTTON, OnResetClick)
        COMMAND_RANGE_HANDLER(IDC_SETTINGS_TAB_CHECKBOX_FIRST, IDC_SETTINGS_TAB_CHECKBOX_LAST, OnTabCheckboxClick)
        COMMAND_HANDLER(IDC_SETTINGS_LOGLEVEL_COMBO, CBN_SELCHANGE, OnLogLevelChange)
        COMMAND_ID_HANDLER(IDC_SETTINGS_SIGNIN_BUTTON, OnSignInClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_SIGNOUT_BUTTON, OnSignOutClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_CHANGE_CREDENTIALS_BUTTON, OnChangeCredentialsClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_MAXCONSOLES_SAVE_BUTTON, OnMaxConsolesSaveClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_FIREWALL_ALLOW_BUTTON, OnFirewallAllowClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_FIREWALL_REMOVE_BUTTON, OnFirewallRemoveClick)
        COMMAND_ID_HANDLER(IDC_SETTINGS_MORE_OPTIONS_BUTTON, OnMoreOptionsClick)
    END_MSG_MAP()

    void init(gth::application::IAppSettingsService& settingsService,
              gth::application::IAdminAuthService& adminAuthService, gth::core::IFirewallService& firewallService,
              std::function<void()> onSettingsChanged) {
        settingsService_ = &settingsService;
        adminAuthService_ = &adminAuthService;
        firewallService_ = &firewallService;
        onSettingsChanged_ = std::move(onSettingsChanged);
    }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnVScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        return scroll_.onVScroll(uMsg, wParam, lParam, bHandled);
    }
    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        return scroll_.onMouseWheel(uMsg, wParam, lParam, bHandled);
    }
    LRESULT OnLightClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnDarkClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnFolderBrowseClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnFolderSaveClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnResetClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnTabCheckboxClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnLogLevelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnSignInClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnSignOutClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnChangeCredentialsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnMaxConsolesSaveClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnFirewallAllowClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnFirewallRemoveClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnMoreOptionsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    void applySettings(const gth::core::AppSettings& settings);
    void updateThemeButtons();
    void refreshSignInSection();
    void refreshFirewallSection();
    std::wstring getEditText(CEdit& edit);
    void showStatus(const std::wstring& text);
    void layoutOptions();

    gth::application::IAppSettingsService* settingsService_ = nullptr;
    gth::application::IAdminAuthService* adminAuthService_ = nullptr;
    gth::core::IFirewallService* firewallService_ = nullptr;
    std::function<void()> onSettingsChanged_;
    gth::core::AppSettings current_;

    CStatic titleLabel_;
    ModernButton moreOptionsButton_;
    bool moreOptionsExpanded_ = false;
    int resetTop_ = 0;
    int expandedContentHeight_ = 0;

    CStatic appearanceCaption_;
    ModernButton lightButton_, darkButton_;

    CStatic tabsCaption_, tabsHintCaption_;
    std::vector<ModernCheckBox> tabCheckboxes_;

    CStatic defaultsCaption_, folderCaption_;
    CEdit folderEdit_;
    ModernButton folderBrowseButton_, folderSaveButton_;

    CStatic loggingCaption_, logLevelCaption_;
    CComboBox logLevelCombo_;

    // Windows Firewall - lets the app add (or remove) its own program-scoped
    // allow rule so other devices can reach the FTP server/proxy/accelerator
    // without the user having to switch the whole firewall off.
    CStatic firewallCaption_, firewallStatusLabel_, firewallHintLabel_;
    ModernButton firewallAllowButton_, firewallRemoveButton_;

    ModernButton resetButton_;
    CStatic statusLabel_;

    // Admin Sign In - signed-out form and signed-in/change-credentials form
    // occupy the same vertical slot, toggled via SetControlsVisible rather
    // than reflowed (unlike FtpView, this page doesn't have large sections
    // stacked below it that would leave an awkward gap).
    CStatic signInCaption_;
    CStatic signInUserCaption_, signInPassCaption_;
    CEdit signInUserEdit_, signInPassEdit_;
    ModernButton signInButton_;
    CStatic signInStatusLabel_;

    CStatic signedInAsLabel_;
    ModernButton signOutButton_;
    CStatic changeCredCaption_, currentPassCaption_, newUserCaption_, newPassCaption_;
    CEdit currentPassEdit_, newUserEdit_, newPassEdit_;
    ModernButton changeCredButton_;

    // Console Limits - admin-only (same visibility toggle as the signed-in
    // half of Admin Sign In above): caps how many "Add Console" HTTP MITM
    // Proxy instances FtpView will let a user create at once.
    CStatic consoleLimitsCaption_, maxConsolesCaption_;
    CEdit maxConsolesEdit_;
    ModernButton maxConsolesSaveButton_;

    ScrollSupport scroll_;
};
