#include "WtlCommon.h"
#include "MainFrame.h"
#include "SplashWindow.h"

#include <commctrl.h>
#include <exception>

#include "gth/infrastructure/ServiceContainer.h"
#include "gth/logging/Logging.h"

#pragma comment(lib, "comctl32.lib")

// Embeds the ComCtl32 v6 manifest dependency directly at link time, so
// buttons/listviews get current visual styles without a separate .rc/
// manifest resource file.
#pragma comment(linker,                                                         \
                 "\"/manifestdependency:type='win32' "                          \
                 "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "  \
                 "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                 "language='*'\"")

WTL::CAppModule _Module;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    gth::logging::init(gth::infrastructure::ServiceContainer::defaultLogDirectory());
    GTH_LOG_INFO("Application started");

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    ::InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);

    // Game-launcher-style intro (Ubisoft Connect and similar): a borderless
    // splash with the product mark, held for a minimum stretch of wall
    // time regardless of how fast init actually finishes underneath it -
    // startup here is normally near-instant (DB migration is a few ms), so
    // without a floor the splash would just flash and be gone.
    SplashWindow splash;
    splash.CreateSplash();
    splash.ShowWindow(SW_SHOWNORMAL);
    splash.UpdateWindow();
    const DWORD splashStartTick = ::GetTickCount();

    int exitCode = 0;
    try {
        gth::infrastructure::ServiceContainer container(gth::infrastructure::ServiceContainer::defaultDatabasePath());
        GTH_LOG_INFO("Database ready and migrated");

        MainFrame frame(container.storageService(), container.gameIndexer(), container.gameLibraryService(),
                         container.gamePosterService(),
                         container.updateDetector(), container.updateJobService(),
                         container.networkInterfaceDetector(), container.consoleManager(),
                         container.transferQueue(), container.reportService(),
                         gth::infrastructure::ServiceContainer::defaultLogDirectory(),
                         container.centralServerService(), container.agentService(),
                         container.ftpConnectionService(), container.ftpTransferService(),
                         container.ftpServerService(), container.cdnUrlService(),
                         container.downloadAcceleratorService(),
                         container.appSettingsService(), container.adminAuthService(),
                         container.downloadAcceleratorServerFactory(), container.firewallService(),
                         container.titleLookupService(), container.updateChecker(),
                         gth::infrastructure::ServiceContainer::defaultSonyLinksPath(), container.packageSync(),
                         gth::infrastructure::ServiceContainer::defaultSonyAutomationPath(),
                         container.coverArtClient(), container.backupService(), container.telegramBackupTransport(),
                         container.credentialStore(),
                         gth::infrastructure::ServiceContainer::defaultTelegramChatIdPath(),
                         gth::infrastructure::ServiceContainer::defaultDataDirectory() / "restored-games");
        if (frame.CreateEx() == nullptr) {
            GTH_LOG_ERROR("Main window creation failed");
            splash.DestroyWindow();
            exitCode = 1;
        } else {
            // Hold the splash for whatever's left of kMinSplashMs, pumping
            // its own messages (WM_PAINT chiefly) so it stays responsive/
            // repainted instead of just sitting there looking frozen for
            // up to two seconds.
            const DWORD splashDeadline = splashStartTick + SplashWindow::kHoldMs;
            MSG splashMsg;
            while (::GetTickCount() < splashDeadline) {
                if (::PeekMessage(&splashMsg, nullptr, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&splashMsg);
                    ::DispatchMessage(&splashMsg);
                } else {
                    ::Sleep(10);
                }
            }
            splash.DestroyWindow();

            // Some launchers (including PowerShell's Start-Process) pass
            // SW_HIDE even when the user explicitly launched the desktop
            // application. The splash is shown explicitly above, but using
            // that hidden flag here would leave the real window running with
            // no visible UI. Preserve normal/minimized launch states while
            // treating a hidden startup request as an accidental launcher
            // default rather than hiding the product from the user.
            const int showCommand = nCmdShow == SW_HIDE ? SW_SHOWNORMAL : nCmdShow;
            frame.ShowWindow(showCommand);
            frame.UpdateWindow();

            MSG msg;
            while (::GetMessage(&msg, nullptr, 0, 0) > 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
            exitCode = static_cast<int>(msg.wParam);
        }
    } catch (const std::exception& ex) {
        GTH_LOG_ERROR("Fatal error: {}", ex.what());
        if (splash.IsWindow()) splash.DestroyWindow();
        ::MessageBoxA(nullptr, ex.what(), "Dr Game Transfer Hub - Fatal Error", MB_ICONERROR);
        exitCode = 1;
    }

    GTH_LOG_INFO("Application exiting with code {}", exitCode);
    _Module.Term();
    return exitCode;
}
