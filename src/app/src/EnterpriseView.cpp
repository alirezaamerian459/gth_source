#include "EnterpriseView.h"

#include <algorithm>

namespace {
constexpr int kTopAreaHeight = 300;
constexpr int kMargin = 24;
}  // namespace

LRESULT EnterpriseView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    HFONT g_sectionFont = theme::SectionFont();
    HFONT g_normalFont = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 260, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Enterprise", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    const int captionHeight = 18;
    const int editHeight = 24;

    // --- Central Server section ---
    CRect serverSectionRect(kMargin, 64, kMargin + 400, 84);
    serverSectionLabel_.Create(m_hWnd, serverSectionRect, L"Central Server", WS_CHILD | WS_VISIBLE);
    serverSectionLabel_.SetFont(g_sectionFont);

    int x = kMargin;
    const int serverCaptionTop = 88;
    const int serverEditTop = serverCaptionTop + captionHeight;

    CRect portCapRect(x, serverCaptionTop, x + 100, serverCaptionTop + captionHeight);
    serverPortCaption_.Create(m_hWnd, portCapRect, L"Port", WS_CHILD | WS_VISIBLE);
    serverPortCaption_.SetFont(g_normalFont);
    CRect portEditRect(x, serverEditTop, x + 100, serverEditTop + editHeight);
    serverPortEdit_.Create(m_hWnd, portEditRect, L"6900", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    serverPortEdit_.SetFont(g_normalFont);
    x += 112;

    CRect serverToggleRect(x, serverEditTop, x + 140, serverEditTop + editHeight);
    serverToggleButton_.Create(m_hWnd, serverToggleRect, L"Start Server", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, IDC_SERVER_TOGGLE_BUTTON);
    serverToggleButton_.SetFont(g_normalFont);
    serverToggleButton_.setKind(ModernButton::Kind::Primary);
    x += 152;

    CRect serverStatusRect(x, serverEditTop + 3, x + 400, serverEditTop + editHeight);
    serverStatusLabel_.Create(m_hWnd, serverStatusRect, L"Not running.", WS_CHILD | WS_VISIBLE);
    serverStatusLabel_.SetFont(g_normalFont);

    // --- Agent section ---
    CRect agentSectionRect(kMargin, 132, kMargin + 400, 152);
    agentSectionLabel_.Create(m_hWnd, agentSectionRect, L"Agent (report to a Central Server)", WS_CHILD | WS_VISIBLE);
    agentSectionLabel_.SetFont(g_sectionFont);

    x = kMargin;
    const int agentCaptionTop = 156;
    const int agentEditTop = agentCaptionTop + captionHeight;

    auto makeAgentField = [&](CStatic& caption, CEdit& edit, const wchar_t* label, const wchar_t* defaultText,
                               int width) {
        CRect capRect(x, agentCaptionTop, x + width, agentCaptionTop + captionHeight);
        caption.Create(m_hWnd, capRect, label, WS_CHILD | WS_VISIBLE);
        caption.SetFont(g_normalFont);

        CRect editRect(x, agentEditTop, x + width, agentEditTop + editHeight);
        edit.Create(m_hWnd, editRect, defaultText, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
        edit.SetFont(g_normalFont);

        x += width + 12;
    };

    makeAgentField(agentAddressCaption_, agentAddressEdit_, L"Server Address", L"", 160);
    makeAgentField(agentPortCaption_, agentPortEdit_, L"Port", L"6900", 80);
    makeAgentField(agentNameCaption_, agentNameEdit_, L"Node Name", L"", 140);
    makeAgentField(agentIntervalCaption_, agentIntervalEdit_, L"Interval (s)", L"300", 90);

    CRect agentToggleRect(x, agentEditTop, x + 150, agentEditTop + editHeight);
    agentToggleButton_.Create(m_hWnd, agentToggleRect, L"Start Reporting", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, IDC_AGENT_TOGGLE_BUTTON);
    agentToggleButton_.SetFont(g_normalFont);
    agentToggleButton_.setKind(ModernButton::Kind::Primary);

    CRect agentStatusRect(kMargin, agentEditTop + editHeight + 6, kMargin + 700, agentEditTop + editHeight + 26);
    agentStatusLabel_.Create(m_hWnd, agentStatusRect, L"Not running.", WS_CHILD | WS_VISIBLE);
    agentStatusLabel_.SetFont(g_normalFont);

    // --- Known nodes list ---
    CRect nodesLabelRect(kMargin, 244, kMargin + 200, 264);
    nodesLabel_.Create(m_hWnd, nodesLabelRect, L"Known Nodes", WS_CHILD | WS_VISIBLE);
    nodesLabel_.SetFont(g_sectionFont);

    CRect refreshRect(kMargin + 160, 240, kMargin + 260, 264);
    refreshNodesButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_REFRESH_NODES_BUTTON);
    refreshNodesButton_.SetFont(g_normalFont);
    refreshNodesButton_.setKind(ModernButton::Kind::Subtle);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    nodesListView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                           IDC_REMOTE_NODES_LISTVIEW);
    nodesListView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    nodesListView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(nodesListView_);

    nodesListView_.InsertColumn(0, L"Node Id", LVCFMT_LEFT, 220);
    nodesListView_.InsertColumn(1, L"Name", LVCFMT_LEFT, 140);
    nodesListView_.InsertColumn(2, L"Address", LVCFMT_LEFT, 120);
    nodesListView_.InsertColumn(3, L"Last Seen", LVCFMT_LEFT, 160);
    nodesListView_.InsertColumn(4, L"Summary", LVCFMT_LEFT, 400);

    refresh();
    return 0;
}

LRESULT EnterpriseView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (nodesListView_.IsWindow()) {
        nodesListView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                                   std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT EnterpriseView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    HWND ctrl = reinterpret_cast<HWND>(lParam);
    ::SetBkMode(hdc, TRANSPARENT);
    const bool muted = ctrl == serverStatusLabel_.m_hWnd || ctrl == agentStatusLabel_.m_hWnd ||
                        ctrl == serverPortCaption_.m_hWnd || ctrl == agentAddressCaption_.m_hWnd ||
                        ctrl == agentPortCaption_.m_hWnd || ctrl == agentNameCaption_.m_hWnd ||
                        ctrl == agentIntervalCaption_.m_hWnd;
    ::SetTextColor(hdc, muted ? theme::TextSecondary() : theme::TextPrimary());
    HBRUSH pageBrush = theme::ContentBgBrush();
    return reinterpret_cast<LRESULT>(pageBrush);
}

LRESULT EnterpriseView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::wstring EnterpriseView::getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

void EnterpriseView::refreshNodesList() {
    if (!serverService_ || !nodesListView_.IsWindow()) return;

    theme::ApplyListViewTheme(nodesListView_);
    nodesListView_.DeleteAllItems();
    int row = 0;
    for (const auto& node : serverService_->getAllNodes()) {
        nodesListView_.InsertItem(row, Utf8ToWide(node.nodeId).c_str());
        nodesListView_.SetItemText(row, 1, Utf8ToWide(node.name).c_str());
        nodesListView_.SetItemText(row, 2, Utf8ToWide(node.address).c_str());
        nodesListView_.SetItemText(row, 3, node.lastSeenAt ? Utf8ToWide(*node.lastSeenAt).c_str() : L"-");
        nodesListView_.SetItemText(row, 4, node.lastReportSummary ? Utf8ToWide(*node.lastReportSummary).c_str() : L"-");
        row++;
    }
}

void EnterpriseView::updateToggleLabelsAndEnabledState() {
    if (!serverService_ || !agentService_) return;

    const bool serverRunning = serverService_->isRunning();
    serverToggleButton_.SetWindowText(serverRunning ? L"Stop Server" : L"Start Server");
    serverToggleButton_.setKind(serverRunning ? ModernButton::Kind::Danger : ModernButton::Kind::Primary);
    serverToggleButton_.Invalidate();
    serverPortEdit_.EnableWindow(!serverRunning);
    serverStatusLabel_.SetWindowText(serverRunning ? L"Running." : L"Not running.");

    const bool agentRunning = agentService_->isRunning();
    agentToggleButton_.SetWindowText(agentRunning ? L"Stop Reporting" : L"Start Reporting");
    agentToggleButton_.setKind(agentRunning ? ModernButton::Kind::Danger : ModernButton::Kind::Primary);
    agentToggleButton_.Invalidate();
    agentAddressEdit_.EnableWindow(!agentRunning);
    agentPortEdit_.EnableWindow(!agentRunning);
    agentNameEdit_.EnableWindow(!agentRunning);
    agentIntervalEdit_.EnableWindow(!agentRunning);

    std::wstring status = agentRunning ? L"Running. " : L"Not running. ";
    status += L"Local node id: " + Utf8ToWide(agentService_->localNodeId());
    agentStatusLabel_.SetWindowText(status.c_str());
}

void EnterpriseView::refresh() {
    updateToggleLabelsAndEnabledState();
    refreshNodesList();
}

LRESULT EnterpriseView::OnServerToggleClick(WORD, WORD, HWND, BOOL&) {
    if (!serverService_) return 0;

    if (serverService_->isRunning()) {
        serverService_->stop();
    } else {
        int port = 6900;
        try {
            port = std::stoi(getEditText(serverPortEdit_));
        } catch (...) {
            serverStatusLabel_.SetWindowText(L"Port must be a number.");
            return 0;
        }
        serverService_->start(port);
    }

    refresh();
    return 0;
}

LRESULT EnterpriseView::OnAgentToggleClick(WORD, WORD, HWND, BOOL&) {
    if (!agentService_) return 0;

    if (agentService_->isRunning()) {
        agentService_->stop();
        refresh();
        return 0;
    }

    const auto address = getEditText(agentAddressEdit_);
    const auto name = getEditText(agentNameEdit_);
    if (address.empty() || name.empty()) {
        agentStatusLabel_.SetWindowText(L"Server address and node name are required.");
        return 0;
    }

    int port = 6900;
    int intervalSeconds = 300;
    try {
        port = std::stoi(getEditText(agentPortEdit_));
        intervalSeconds = std::stoi(getEditText(agentIntervalEdit_));
    } catch (...) {
        agentStatusLabel_.SetWindowText(L"Port and interval must be numbers.");
        return 0;
    }

    const bool succeeded = agentService_->start(WideToUtf8(address), port, WideToUtf8(name), intervalSeconds);
    refresh();
    if (!succeeded) {
        agentStatusLabel_.SetWindowText((L"First report failed - check the address/port. Local node id: " +
                                          Utf8ToWide(agentService_->localNodeId()))
                                             .c_str());
    }
    return 0;
}

LRESULT EnterpriseView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}
