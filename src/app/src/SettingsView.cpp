#include "SettingsView.h"

#include <ShlObj.h>

#include <array>
#include <bit>
#include <format>
#include <stdexcept>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr int kMargin = 28;
constexpr int kCheckboxColumnWidth = 220;
constexpr int kCheckboxRowHeight = 30;
constexpr int kCheckboxRows = 5;
}  // namespace

using gth::core::AppSettings;
using gth::core::ThemeMode;

LRESULT SettingsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    scroll_.attach(m_hWnd);
    HFONT body = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 300, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Settings & Preferences", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    // --- Appearance ---
    int y = 74;
    CRect appearanceCapRect(kMargin, y, kMargin + 300, y + 22);
    appearanceCaption_.Create(m_hWnd, appearanceCapRect, L"Appearance", WS_CHILD | WS_VISIBLE);
    appearanceCaption_.SetFont(theme::SectionFont());
    y += 30;

    CRect lightRect(kMargin, y, kMargin + 130, y + 32);
    lightButton_.Create(m_hWnd, lightRect, L"Light Mode", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                         IDC_SETTINGS_THEME_LIGHT_BUTTON);
    lightButton_.SetFont(body);

    CRect darkRect(kMargin + 140, y, kMargin + 270, y + 32);
    darkButton_.Create(m_hWnd, darkRect, L"Dark Mode", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                        IDC_SETTINGS_THEME_DARK_BUTTON);
    darkButton_.SetFont(body);
    y += 32 + 34;

    CRect moreRect(kMargin, 140, kMargin + 188, 168);
    moreOptionsButton_.Create(m_hWnd, moreRect, L"More settings  \x25B8",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                              IDC_SETTINGS_MORE_OPTIONS_BUTTON);
    moreOptionsButton_.SetFont(body);
    moreOptionsButton_.setKind(ModernButton::Kind::Subtle);

    // --- Sidebar Tabs ---
    CRect tabsCapRect(kMargin, y, kMargin + 300, y + 22);
    tabsCaption_.Create(m_hWnd, tabsCapRect, L"Sidebar Tabs", WS_CHILD | WS_VISIBLE);
    tabsCaption_.SetFont(theme::SectionFont());
    y += 26;

    CRect tabsHintRect(kMargin, y, kMargin + 700, y + 20);
    tabsHintCaption_.Create(m_hWnd, tabsHintRect,
                             L"Uncheck a tab to hide it from the sidebar. Dashboard and Settings are always visible.",
                             WS_CHILD | WS_VISIBLE);
    tabsHintCaption_.SetFont(body);
    y += 28;

    const int checkboxesTop = y;
    tabCheckboxes_.clear();
    // Reserve up front and construct/Create() each ModernCheckBox directly
    // in its final vector slot (never push_back a value that's already had
    // Create() called on it). ModernCheckBox is a CWindowImpl - WTL binds
    // its window's message-thunk to the specific object address Create()
    // was called on, baked into a small generated trampoline; copying that
    // object afterward (push_back's copy, or a vector reallocation moving
    // an already-created element to new storage) leaves the thunk pointing
    // at a stale/dead address, so the *next* message this checkbox
    // receives corrupts memory or jumps into garbage. reserve() up front +
    // constructing in place avoids both of those relocations entirely.
    tabCheckboxes_.reserve(static_cast<size_t>(kSidebarTabCount));
    int checkboxIndex = 0;
    for (int logicalIndex = 0; logicalIndex < kSidebarTabCount; ++logicalIndex) {
        if (!IsSidebarTabHideable(logicalIndex)) continue;

        const int col = checkboxIndex / kCheckboxRows;
        const int row = checkboxIndex % kCheckboxRows;
        CRect boxRect(kMargin + col * kCheckboxColumnWidth, checkboxesTop + row * kCheckboxRowHeight,
                      kMargin + col * kCheckboxColumnWidth + kCheckboxColumnWidth - 10,
                      checkboxesTop + row * kCheckboxRowHeight + kCheckboxRowHeight - 6);

        tabCheckboxes_.emplace_back();
        ModernCheckBox& box = tabCheckboxes_.back();
        box.Create(m_hWnd, boxRect, SidebarDisplayName(logicalIndex),
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,
                   IDC_SETTINGS_TAB_CHECKBOX_FIRST + checkboxIndex);
        box.SetFont(body);
        ++checkboxIndex;
    }
    y = checkboxesTop + kCheckboxRows * kCheckboxRowHeight + 20;

    // --- Defaults ---
    CRect defaultsCapRect(kMargin, y, kMargin + 300, y + 22);
    defaultsCaption_.Create(m_hWnd, defaultsCapRect, L"Defaults", WS_CHILD | WS_VISIBLE);
    defaultsCaption_.SetFont(theme::SectionFont());
    y += 30;

    CRect folderCapRect(kMargin, y, kMargin + 300, y + 18);
    folderCaption_.Create(m_hWnd, folderCapRect, L"Default Local Folder (used to seed FTP's local path)",
                           WS_CHILD | WS_VISIBLE);
    folderCaption_.SetFont(body);
    y += 22;

    CRect folderEditRect(kMargin, y, kMargin + 400, y + 24);
    folderEdit_.Create(m_hWnd, folderEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    folderEdit_.SetFont(body);

    CRect folderBrowseRect(kMargin + 410, y, kMargin + 500, y + 24);
    folderBrowseButton_.Create(m_hWnd, folderBrowseRect, L"Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, IDC_SETTINGS_FOLDER_BROWSE_BUTTON);
    folderBrowseButton_.SetFont(body);
    folderBrowseButton_.setKind(ModernButton::Kind::Subtle);

    CRect folderSaveRect(kMargin + 510, y, kMargin + 590, y + 24);
    folderSaveButton_.Create(m_hWnd, folderSaveRect, L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                              IDC_SETTINGS_FOLDER_SAVE_BUTTON);
    folderSaveButton_.SetFont(body);
    folderSaveButton_.setKind(ModernButton::Kind::Primary);
    y += 24 + 34;

    // --- Logging ---
    CRect loggingCapRect(kMargin, y, kMargin + 300, y + 22);
    loggingCaption_.Create(m_hWnd, loggingCapRect, L"Logging", WS_CHILD | WS_VISIBLE);
    loggingCaption_.SetFont(theme::SectionFont());
    y += 30;

    CRect logLevelCapRect(kMargin, y, kMargin + 150, y + 18);
    logLevelCaption_.Create(m_hWnd, logLevelCapRect, L"Verbosity", WS_CHILD | WS_VISIBLE);
    logLevelCaption_.SetFont(body);
    y += 22;

    CRect logLevelRect(kMargin, y, kMargin + 160, y + 120);
    logLevelCombo_.Create(m_hWnd, logLevelRect, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0,
                           IDC_SETTINGS_LOGLEVEL_COMBO);
    logLevelCombo_.SetFont(body);
    logLevelCombo_.AddString(L"Error");
    logLevelCombo_.AddString(L"Warning");
    logLevelCombo_.AddString(L"Info");
    logLevelCombo_.AddString(L"Debug");
    ::SetWindowTheme(logLevelCombo_.m_hWnd, L"DarkMode_CFD", nullptr);
    y += 24 + 34;

    // --- Windows Firewall ---
    CRect firewallCapRect(kMargin, y, kMargin + 300, y + 22);
    firewallCaption_.Create(m_hWnd, firewallCapRect, L"Windows Firewall", WS_CHILD | WS_VISIBLE);
    firewallCaption_.SetFont(theme::SectionFont());
    y += 30;

    CRect firewallStatusRect(kMargin, y, kMargin + 720, y + 40);
    firewallStatusLabel_.Create(m_hWnd, firewallStatusRect, L"", WS_CHILD | WS_VISIBLE);
    firewallStatusLabel_.SetFont(body);
    y += 46;

    CRect firewallAllowRect(kMargin, y, kMargin + 210, y + 24);
    firewallAllowButton_.Create(m_hWnd, firewallAllowRect, L"Allow through Firewall",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 IDC_SETTINGS_FIREWALL_ALLOW_BUTTON);
    firewallAllowButton_.SetFont(body);
    firewallAllowButton_.setKind(ModernButton::Kind::Primary);

    CRect firewallRemoveRect(kMargin + 220, y, kMargin + 340, y + 24);
    firewallRemoveButton_.Create(m_hWnd, firewallRemoveRect, L"Remove rule",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                  IDC_SETTINGS_FIREWALL_REMOVE_BUTTON);
    firewallRemoveButton_.SetFont(body);
    firewallRemoveButton_.setKind(ModernButton::Kind::Subtle);
    y += 24 + 8;

    CRect firewallHintRect(kMargin, y, kMargin + 720, y + 40);
    firewallHintLabel_.Create(m_hWnd, firewallHintRect, L"", WS_CHILD | WS_VISIBLE);
    firewallHintLabel_.SetFont(body);
    y += 40 + 34;

    // --- Admin Sign In ---
    // Two sub-states (signed-out form / signed-in + change-credentials
    // form) share this same vertical slot rather than reflowing the page -
    // toggled via SetControlsVisible in refreshSignInSection().
    CRect signInCapRect(kMargin, y, kMargin + 300, y + 22);
    signInCaption_.Create(m_hWnd, signInCapRect, L"Admin Sign In", WS_CHILD | WS_VISIBLE);
    signInCaption_.SetFont(theme::SectionFont());
    const int signInSectionTop = y + 30;

    // Signed-out form
    int sy = signInSectionTop;
    CRect signInUserCapRect(kMargin, sy, kMargin + 150, sy + 18);
    signInUserCaption_.Create(m_hWnd, signInUserCapRect, L"Username", WS_CHILD | WS_VISIBLE);
    signInUserCaption_.SetFont(body);
    CRect signInPassCapRect(kMargin + 220, sy, kMargin + 370, sy + 18);
    signInPassCaption_.Create(m_hWnd, signInPassCapRect, L"Password", WS_CHILD | WS_VISIBLE);
    signInPassCaption_.SetFont(body);
    sy += 22;

    CRect signInUserEditRect(kMargin, sy, kMargin + 200, sy + 24);
    signInUserEdit_.Create(m_hWnd, signInUserEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    signInUserEdit_.SetFont(body);
    CRect signInPassEditRect(kMargin + 220, sy, kMargin + 370, sy + 24);
    signInPassEdit_.Create(m_hWnd, signInPassEditRect, L"",
                            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD);
    signInPassEdit_.SetFont(body);
    CRect signInButtonRect(kMargin + 390, sy, kMargin + 470, sy + 24);
    signInButton_.Create(m_hWnd, signInButtonRect, L"Sign In", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                          IDC_SETTINGS_SIGNIN_BUTTON);
    signInButton_.SetFont(body);
    signInButton_.setKind(ModernButton::Kind::Primary);
    sy += 24 + 10;

    CRect signInStatusRect(kMargin, sy, kMargin + 500, sy + 20);
    signInStatusLabel_.Create(m_hWnd, signInStatusRect, L"", WS_CHILD | WS_VISIBLE);
    signInStatusLabel_.SetFont(body);
    signInStatusLabel_.SetWindowText(L"First use: enter the username and password you want to save.");

    // Signed-in form (occupies the same slot as the block above)
    sy = signInSectionTop;
    CRect signedInAsRect(kMargin, sy, kMargin + 400, sy + 20);
    signedInAsLabel_.Create(m_hWnd, signedInAsRect, L"", WS_CHILD | WS_VISIBLE);
    signedInAsLabel_.SetFont(body);
    CRect signOutRect(kMargin + 410, sy, kMargin + 490, sy + 24);
    signOutButton_.Create(m_hWnd, signOutRect, L"Sign Out", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_SETTINGS_SIGNOUT_BUTTON);
    signOutButton_.SetFont(body);
    signOutButton_.setKind(ModernButton::Kind::Danger);
    sy += 28;

    CRect changeCredCapRect(kMargin, sy, kMargin + 300, sy + 20);
    changeCredCaption_.Create(m_hWnd, changeCredCapRect, L"Change Username / Password", WS_CHILD | WS_VISIBLE);
    changeCredCaption_.SetFont(theme::BodyFontBold());
    sy += 24;

    CRect currentPassCapRect(kMargin, sy, kMargin + 150, sy + 18);
    currentPassCaption_.Create(m_hWnd, currentPassCapRect, L"Current Password", WS_CHILD | WS_VISIBLE);
    currentPassCaption_.SetFont(body);
    CRect newUserCapRect(kMargin + 220, sy, kMargin + 370, sy + 18);
    newUserCaption_.Create(m_hWnd, newUserCapRect, L"New Username (optional)", WS_CHILD | WS_VISIBLE);
    newUserCaption_.SetFont(body);
    CRect newPassCapRect(kMargin + 440, sy, kMargin + 590, sy + 18);
    newPassCaption_.Create(m_hWnd, newPassCapRect, L"New Password (optional)", WS_CHILD | WS_VISIBLE);
    newPassCaption_.SetFont(body);
    sy += 22;

    CRect currentPassEditRect(kMargin, sy, kMargin + 200, sy + 24);
    currentPassEdit_.Create(m_hWnd, currentPassEditRect, L"",
                             WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD);
    currentPassEdit_.SetFont(body);
    CRect newUserEditRect(kMargin + 220, sy, kMargin + 420, sy + 24);
    newUserEdit_.Create(m_hWnd, newUserEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    newUserEdit_.SetFont(body);
    CRect newPassEditRect(kMargin + 440, sy, kMargin + 590, sy + 24);
    newPassEdit_.Create(m_hWnd, newPassEditRect, L"",
                         WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD);
    newPassEdit_.SetFont(body);
    CRect changeCredButtonRect(kMargin + 610, sy, kMargin + 700, sy + 24);
    changeCredButton_.Create(m_hWnd, changeCredButtonRect, L"Update", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, IDC_SETTINGS_CHANGE_CREDENTIALS_BUTTON);
    changeCredButton_.SetFont(body);
    changeCredButton_.setKind(ModernButton::Kind::Primary);

    // Whichever sub-state is taller (the change-credentials form) sets how
    // far the shared y cursor advances past this section.
    y = sy + 24 + 34;

    // --- Console Limits (admin-only, same visibility toggle as the
    // signed-in half of Admin Sign In above) ---
    CRect consoleLimitsCapRect(kMargin, y, kMargin + 300, y + 22);
    consoleLimitsCaption_.Create(m_hWnd, consoleLimitsCapRect, L"Console Limits", WS_CHILD | WS_VISIBLE);
    consoleLimitsCaption_.SetFont(theme::SectionFont());
    y += 30;

    CRect maxConsolesCapRect(kMargin, y, kMargin + 300, y + 18);
    maxConsolesCaption_.Create(m_hWnd, maxConsolesCapRect,
                                L"Max consoles connectable (in addition to the first)", WS_CHILD | WS_VISIBLE);
    maxConsolesCaption_.SetFont(body);
    y += 22;

    CRect maxConsolesEditRect(kMargin, y, kMargin + 80, y + 24);
    maxConsolesEdit_.Create(m_hWnd, maxConsolesEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER);
    maxConsolesEdit_.SetFont(body);
    CRect maxConsolesSaveRect(kMargin + 90, y, kMargin + 170, y + 24);
    maxConsolesSaveButton_.Create(m_hWnd, maxConsolesSaveRect, L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, IDC_SETTINGS_MAXCONSOLES_SAVE_BUTTON);
    maxConsolesSaveButton_.SetFont(body);
    maxConsolesSaveButton_.setKind(ModernButton::Kind::Primary);
    y += 24 + 34;

    // --- Reset + status ---
    resetTop_ = y;
    CRect resetRect(kMargin, y, kMargin + 160, y + 30);
    resetButton_.Create(m_hWnd, resetRect, L"Reset to Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                         IDC_SETTINGS_RESET_BUTTON);
    resetButton_.SetFont(body);
    resetButton_.setKind(ModernButton::Kind::Danger);

    CRect statusRect(kMargin + 174, y + 3, kMargin + 700, y + 30);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(body);
    y += 30 + kMargin;

    expandedContentHeight_ = y;
    refresh();
    layoutOptions();
    return 0;
}

void SettingsView::layoutOptions() {
    if (!moreOptionsButton_.IsWindow()) return;
    moreOptionsButton_.SetWindowText(moreOptionsExpanded_ ? L"Hide other settings  \x25BE"
                                                         : L"More settings  \x25B8");
    const auto show = [this](CWindow& control, bool visible) {
        if (control.IsWindow()) control.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    };
    const bool expanded = moreOptionsExpanded_;
    for (CWindow* control : {static_cast<CWindow*>(&tabsCaption_), static_cast<CWindow*>(&tabsHintCaption_),
                             static_cast<CWindow*>(&defaultsCaption_), static_cast<CWindow*>(&folderCaption_),
                             static_cast<CWindow*>(&folderEdit_), static_cast<CWindow*>(&folderBrowseButton_),
                             static_cast<CWindow*>(&folderSaveButton_), static_cast<CWindow*>(&loggingCaption_),
                             static_cast<CWindow*>(&logLevelCaption_), static_cast<CWindow*>(&logLevelCombo_),
                             static_cast<CWindow*>(&firewallCaption_), static_cast<CWindow*>(&firewallStatusLabel_),
                             static_cast<CWindow*>(&firewallAllowButton_), static_cast<CWindow*>(&firewallRemoveButton_),
                             static_cast<CWindow*>(&firewallHintLabel_), static_cast<CWindow*>(&signInCaption_),
                             static_cast<CWindow*>(&signInUserCaption_), static_cast<CWindow*>(&signInPassCaption_),
                             static_cast<CWindow*>(&signInUserEdit_), static_cast<CWindow*>(&signInPassEdit_),
                             static_cast<CWindow*>(&signInButton_), static_cast<CWindow*>(&signInStatusLabel_),
                             static_cast<CWindow*>(&signedInAsLabel_), static_cast<CWindow*>(&signOutButton_),
                             static_cast<CWindow*>(&changeCredCaption_), static_cast<CWindow*>(&currentPassCaption_),
                             static_cast<CWindow*>(&newUserCaption_), static_cast<CWindow*>(&newPassCaption_),
                             static_cast<CWindow*>(&currentPassEdit_), static_cast<CWindow*>(&newUserEdit_),
                             static_cast<CWindow*>(&newPassEdit_), static_cast<CWindow*>(&changeCredButton_),
                             static_cast<CWindow*>(&consoleLimitsCaption_), static_cast<CWindow*>(&maxConsolesCaption_),
                             static_cast<CWindow*>(&maxConsolesEdit_), static_cast<CWindow*>(&maxConsolesSaveButton_)}) {
        show(*control, expanded);
    }
    for (auto& checkbox : tabCheckboxes_) show(checkbox, expanded);

    if (!expanded) {
        resetButton_.MoveWindow(kMargin, 188, 160, 30, FALSE);
        statusLabel_.MoveWindow(kMargin + 174, 191, 526, 24, FALSE);
        scroll_.setContentHeight(232);
    } else {
        resetButton_.MoveWindow(kMargin, resetTop_, 160, 30, FALSE);
        statusLabel_.MoveWindow(kMargin + 174, resetTop_ + 3, 526, 27, FALSE);
        scroll_.setContentHeight(expandedContentHeight_);
    }
}

LRESULT SettingsView::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    scroll_.onSize();
    return 0;
}

LRESULT SettingsView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    const auto hdc = std::bit_cast<HDC>(wParam);
    const auto ctrl = std::bit_cast<HWND>(lParam);
    ::SetBkMode(hdc, TRANSPARENT);
    const bool secondary = ctrl == tabsHintCaption_.m_hWnd || ctrl == folderCaption_.m_hWnd ||
                            ctrl == logLevelCaption_.m_hWnd || ctrl == signInUserCaption_.m_hWnd ||
                            ctrl == signInPassCaption_.m_hWnd || ctrl == currentPassCaption_.m_hWnd ||
                            ctrl == newUserCaption_.m_hWnd || ctrl == newPassCaption_.m_hWnd ||
                            ctrl == maxConsolesCaption_.m_hWnd;
    ::SetTextColor(hdc, secondary ? theme::TextSecondary() : theme::TextPrimary());
    return std::bit_cast<LRESULT>(theme::ContentBgBrush());
}

LRESULT SettingsView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::wstring SettingsView::getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

void SettingsView::showStatus(const std::wstring& text) {
    if (statusLabel_.IsWindow()) statusLabel_.SetWindowText(text.c_str());
}

void SettingsView::updateThemeButtons() {
    using enum ModernButton::Kind;
    const bool dark = current_.theme == ThemeMode::Dark;
    lightButton_.setKind(dark ? Secondary : Primary);
    darkButton_.setKind(dark ? Primary : Secondary);
    if (lightButton_.IsWindow()) lightButton_.Invalidate();
    if (darkButton_.IsWindow()) darkButton_.Invalidate();
}

void SettingsView::applySettings(const AppSettings& settings) {
    current_ = settings;
    if (!settingsService_) return;

    settingsService_->updateSettings(current_);
    theme::SetDarkMode(current_.theme == ThemeMode::Dark);
    if (onSettingsChanged_) onSettingsChanged_();
}

void SettingsView::refresh() {
    if (!settingsService_) return;
    current_ = settingsService_->getSettings();
    theme::SetDarkMode(current_.theme == ThemeMode::Dark);

    updateThemeButtons();

    int checkboxIndex = 0;
    for (int logicalIndex = 0; logicalIndex < kSidebarTabCount; ++logicalIndex) {
        if (!IsSidebarTabHideable(logicalIndex)) continue;
        if (checkboxIndex < static_cast<int>(tabCheckboxes_.size())) {
            const std::string label = WideToUtf8(kSidebarTabs[static_cast<size_t>(logicalIndex)]);
            const bool visible = !current_.hiddenTabs.contains(label);
            tabCheckboxes_[static_cast<size_t>(checkboxIndex)].SetCheck(visible ? BST_CHECKED : BST_UNCHECKED);
        }
        ++checkboxIndex;
    }

    if (folderEdit_.IsWindow()) folderEdit_.SetWindowText(Utf8ToWide(current_.defaultLocalFolder).c_str());

    if (logLevelCombo_.IsWindow()) {
        int sel = 2;  // Info
        if (current_.logLevel == "Error") sel = 0;
        else if (current_.logLevel == "Warning") sel = 1;
        else if (current_.logLevel == "Info") sel = 2;
        else if (current_.logLevel == "Debug") sel = 3;
        logLevelCombo_.SetCurSel(sel);
    }

    if (maxConsolesEdit_.IsWindow()) maxConsolesEdit_.SetWindowText(std::to_wstring(current_.maxConsoles).c_str());

    refreshSignInSection();
    refreshFirewallSection();
    layoutOptions();
}

LRESULT SettingsView::OnMoreOptionsClick(WORD, WORD, HWND, BOOL&) {
    moreOptionsExpanded_ = !moreOptionsExpanded_;
    layoutOptions();
    return 0;
}

namespace {
constexpr wchar_t kFirewallDefaultHint[] =
    L"Done automatically each time the app starts: two rules for this program only (incoming from your local network, "
    L"and outgoing), and any Windows rule blocking this app is switched off. It never turns the firewall itself on "
    L"or off.";
}  // namespace

void SettingsView::refreshFirewallSection() {
    if (!firewallService_ || !firewallStatusLabel_.IsWindow()) return;

    const auto status = firewallService_->status();

    std::wstring text;
    if (!status.available) {
        text = std::format(L"Windows Firewall can't be read on this PC, so nothing can be changed from here. {}",
                           Utf8ToWide(status.detail));
    } else if (status.appAllowed) {
        text = status.firewallEnabled
                   ? L"Allowed: devices on your local network can connect to this app while Windows Firewall is on."
                   : L"The rules are in place, but Windows Firewall is currently turned off. Turn it back on in "
                     L"Windows Security - this app will keep working.";
    } else if (status.blockingRules > 0) {
        text = std::format(
            L"{} Windows firewall rule(s) are blocking this app (Windows creates these when its \"allow this app?\" "
            L"prompt is closed without allowing it). Click Allow to switch them off.",
            status.blockingRules);
    } else {
        text = status.firewallEnabled
                   ? L"Not allowed yet: while Windows Firewall is on, other devices (consoles, other PCs) can be "
                     L"blocked from connecting to this app. Click Allow."
                   : L"Windows Firewall is turned off on this PC, so nothing is being blocked - but that leaves the "
                     L"PC unprotected. Click Allow, then turn the firewall back on.";
    }
    firewallStatusLabel_.SetWindowText(text.c_str());
    firewallHintLabel_.SetWindowText(kFirewallDefaultHint);
    firewallAllowButton_.EnableWindow(status.available && !status.appAllowed);
    firewallRemoveButton_.EnableWindow(status.available);
}

LRESULT SettingsView::OnFirewallAllowClick(WORD, WORD, HWND, BOOL&) {
    if (!firewallService_) return 0;
    const auto result = firewallService_->allowThisApp();
    refreshFirewallSection();
    firewallHintLabel_.SetWindowText(Utf8ToWide(result.message).c_str());
    return 0;
}

LRESULT SettingsView::OnFirewallRemoveClick(WORD, WORD, HWND, BOOL&) {
    if (!firewallService_) return 0;
    const auto result = firewallService_->removeAppRules();
    refreshFirewallSection();
    firewallHintLabel_.SetWindowText(Utf8ToWide(result.message).c_str());
    return 0;
}

void SettingsView::refreshSignInSection() {
    if (!adminAuthService_) return;

    const bool signedIn = adminAuthService_->isSignedIn();
    SetControlsVisible({&signInUserCaption_, &signInUserEdit_, &signInPassCaption_, &signInPassEdit_,
                         &signInButton_, &signInStatusLabel_},
                        !signedIn);
    SetControlsVisible({&signedInAsLabel_, &signOutButton_, &changeCredCaption_, &currentPassCaption_,
                         &currentPassEdit_, &newUserCaption_, &newUserEdit_, &newPassCaption_, &newPassEdit_,
                         &changeCredButton_, &consoleLimitsCaption_, &maxConsolesCaption_, &maxConsolesEdit_,
                         &maxConsolesSaveButton_},
                        signedIn);

    if (signedIn && signedInAsLabel_.IsWindow()) {
        signedInAsLabel_.SetWindowText(std::format(L"Signed in as {}", Utf8ToWide(adminAuthService_->currentUsername())).c_str());
    }

    // Never leave a typed password sitting in a field after a click.
    if (signInPassEdit_.IsWindow()) signInPassEdit_.SetWindowText(L"");
    if (currentPassEdit_.IsWindow()) currentPassEdit_.SetWindowText(L"");
    if (newUserEdit_.IsWindow()) newUserEdit_.SetWindowText(L"");
    if (newPassEdit_.IsWindow()) newPassEdit_.SetWindowText(L"");
}

LRESULT SettingsView::OnLightClick(WORD, WORD, HWND, BOOL&) {
    AppSettings updated = current_;
    updated.theme = ThemeMode::Light;
    applySettings(updated);
    updateThemeButtons();
    showStatus(L"Switched to Light mode.");
    return 0;
}

LRESULT SettingsView::OnDarkClick(WORD, WORD, HWND, BOOL&) {
    AppSettings updated = current_;
    updated.theme = ThemeMode::Dark;
    applySettings(updated);
    updateThemeButtons();
    showStatus(L"Switched to Dark mode.");
    return 0;
}

LRESULT SettingsView::OnTabCheckboxClick(WORD, WORD wID, HWND, BOOL&) {
    const int checkboxIndex = wID - IDC_SETTINGS_TAB_CHECKBOX_FIRST;
    if (checkboxIndex < 0 || checkboxIndex >= static_cast<int>(tabCheckboxes_.size())) return 0;

    // Hideable tabs are exactly kSidebarTabs[1..kSidebarTabCount-2] in
    // order (index 0 is Dashboard, the last is Settings - see
    // IsSidebarTabHideable), so checkbox i always maps to logical tab i+1.
    const int logicalIndex = checkboxIndex + 1;
    const std::string label = WideToUtf8(kSidebarTabs[static_cast<size_t>(logicalIndex)]);

    AppSettings updated = current_;
    const bool checked = tabCheckboxes_[static_cast<size_t>(checkboxIndex)].GetCheck() == BST_CHECKED;
    if (checked) {
        updated.hiddenTabs.erase(label);
    } else {
        updated.hiddenTabs.insert(label);
    }
    applySettings(updated);
    showStatus(L"Sidebar updated.");
    return 0;
}

LRESULT SettingsView::OnFolderBrowseClick(WORD, WORD, HWND, BOOL&) {
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = SUCCEEDED(comResult);

    std::array<wchar_t, MAX_PATH> displayName{};
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hWnd;
    bi.pszDisplayName = displayName.data();
    bi.lpszTitle = L"Choose a default local folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        if (::SHGetPathFromIDListW(pidl, path.data())) {
            folderEdit_.SetWindowText(path.data());
        }
        ::CoTaskMemFree(pidl);
    }

    if (comInitializedHere) ::CoUninitialize();
    return 0;
}

LRESULT SettingsView::OnFolderSaveClick(WORD, WORD, HWND, BOOL&) {
    AppSettings updated = current_;
    updated.defaultLocalFolder = WideToUtf8(getEditText(folderEdit_));
    applySettings(updated);
    showStatus(L"Default folder saved.");
    return 0;
}

LRESULT SettingsView::OnLogLevelChange(WORD, WORD, HWND, BOOL&) {
    const int sel = logLevelCombo_.GetCurSel();
    const std::array<const wchar_t*, 4> levels{L"Error", L"Warning", L"Info", L"Debug"};
    if (sel < 0 || sel >= 4) return 0;

    AppSettings updated = current_;
    updated.logLevel = WideToUtf8(levels[sel]);
    applySettings(updated);
    showStatus(L"Log verbosity updated.");
    return 0;
}

LRESULT SettingsView::OnResetClick(WORD, WORD, HWND, BOOL&) {
    applySettings(AppSettings{});
    refresh();
    showStatus(L"Settings reset to defaults.");
    return 0;
}

LRESULT SettingsView::OnSignInClick(WORD, WORD, HWND, BOOL&) {
    if (!adminAuthService_) return 0;

    const std::string username = WideToUtf8(getEditText(signInUserEdit_));
    const std::string password = WideToUtf8(getEditText(signInPassEdit_));
    if (adminAuthService_->signIn(username, password)) {
        refreshSignInSection();
        if (onSettingsChanged_) onSettingsChanged_();
    } else {
        if (signInStatusLabel_.IsWindow()) signInStatusLabel_.SetWindowText(L"Incorrect username or password.");
        if (signInPassEdit_.IsWindow()) signInPassEdit_.SetWindowText(L"");
    }
    return 0;
}

LRESULT SettingsView::OnSignOutClick(WORD, WORD, HWND, BOOL&) {
    if (!adminAuthService_) return 0;

    adminAuthService_->signOut();
    refreshSignInSection();
    if (onSettingsChanged_) onSettingsChanged_();
    return 0;
}

LRESULT SettingsView::OnChangeCredentialsClick(WORD, WORD, HWND, BOOL&) {
    if (!adminAuthService_) return 0;

    const std::string currentPassword = WideToUtf8(getEditText(currentPassEdit_));
    const std::wstring newUsernameW = getEditText(newUserEdit_);
    const std::wstring newPasswordW = getEditText(newPassEdit_);
    const std::optional<std::string> newUsername =
        newUsernameW.empty() ? std::nullopt : std::optional<std::string>(WideToUtf8(newUsernameW));
    const std::optional<std::string> newPassword =
        newPasswordW.empty() ? std::nullopt : std::optional<std::string>(WideToUtf8(newPasswordW));

    if (adminAuthService_->changeCredentials(currentPassword, newUsername, newPassword)) {
        refreshSignInSection();
        showStatus(L"Credentials updated.");
    } else {
        refreshSignInSection();
        showStatus(L"Current password is incorrect - credentials not changed.");
    }
    return 0;
}

LRESULT SettingsView::OnMaxConsolesSaveClick(WORD, WORD, HWND, BOOL&) {
    // Admin-only in practice (this whole section is hidden while signed
    // out), but guard anyway in case a signed-out user somehow still
    // reaches this handler (e.g. a stale message queued right as they
    // signed out).
    if (!adminAuthService_ || !adminAuthService_->isSignedIn()) return 0;

    const std::wstring text = getEditText(maxConsolesEdit_);
    int value = 0;
    try {
        value = std::stoi(text);
    } catch (const std::invalid_argument&) {
        showStatus(L"Enter a whole number.");
        return 0;
    } catch (const std::out_of_range&) {
        showStatus(L"Enter a whole number.");
        return 0;
    }
    if (value < 0 || value > 30) {
        showStatus(L"Must be between 0 and 30 (the technical maximum).");
        return 0;
    }

    AppSettings updated = current_;
    updated.maxConsoles = value;
    applySettings(updated);
    showStatus(L"Console limit saved.");
    return 0;
}
