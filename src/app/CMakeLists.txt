# WTL is header-only, fetched from its official SourceForge distribution
# (not a third-party GitHub mirror). DOWNLOAD_NAME forces a ".zip" filename
# since the SourceForge URL's last path segment is "download", not
# "WTL10_01_Release.zip" - without it, CMake's extraction step can't tell
# what archive format was downloaded.
include(FetchContent)
FetchContent_Declare(
    wtl
    URL https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.01%20Release/WTL10_01_Release.zip/download
    URL_HASH SHA256=1a62ea728d088c7c5c7cfc76db445e5c9f04923f92cadcf27b5d7678d85826a2
    DOWNLOAD_NAME WTL10_01_Release.zip
)
FetchContent_MakeAvailable(wtl)

add_library(wtl INTERFACE)
target_include_directories(wtl INTERFACE ${wtl_SOURCE_DIR}/Include)

add_executable(GameTransferHubApp WIN32
    src/WinMain.cpp
    src/MainFrame.cpp
    src/DashboardView.cpp
    src/GamesView.cpp
    src/UpdatesView.cpp
    src/PosterCache.cpp
    src/DownloadsView.cpp
    src/StorageView.cpp
    src/NetworkView.cpp
    src/ConsolesView.cpp
    src/TransfersView.cpp
    src/FtpView.cpp
    src/LogsView.cpp
    src/ReportsView.cpp
    src/EnterpriseView.cpp
    src/SettingsView.cpp
    src/BackupsView.cpp
    src/ComingSoonView.cpp
    resources/AppResources.rc
)

target_link_libraries(GameTransferHubApp PRIVATE
    gth_infrastructure
    gth_logging
    wtl
    comctl32
    msimg32
)

target_compile_definitions(GameTransferHubApp PRIVATE UNICODE _UNICODE)
target_compile_features(GameTransferHubApp PRIVATE cxx_std_20)

set_target_properties(GameTransferHubApp PROPERTIES OUTPUT_NAME "GameTransferHub")

# Storage-tab drive temperature comes from MSFT_StorageReliabilityCounter
# (WMI) and, as a fallback, a direct ATA SMART passthrough - both are gated
# by Windows to admin processes only (the same restriction PowerShell's own
# Get-StorageReliabilityCounter cmdlet hits unelevated). Requiring
# administrator here, rather than degrading to "Unknown" silently, is what
# makes that feature actually work: Windows shows the UAC prompt on every
# launch and elevates the whole process.
set_target_properties(GameTransferHubApp PROPERTIES
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
)
