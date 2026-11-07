add_library(gth_application STATIC
    src/StorageService.cpp
    src/StorageCategoryService.cpp
    src/GameIndexerService.cpp
    src/GameLibraryService.cpp
    src/UpdateCheckService.cpp
    src/PackageSyncService.cpp
    src/SonyAutomation.cpp
    src/DownloadHistory.cpp
    src/UpdateDetectorService.cpp
    src/UpdateJobService.cpp
    src/UpdateSchedulerService.cpp
    src/DownloadManager.cpp
    src/ArchiveUpdater.cpp
    src/UpdateManagerService.cpp
    src/ConsoleManagerService.cpp
    src/TransferQueueService.cpp
    src/ReportService.cpp
    src/CentralServerService.cpp
    src/AgentService.cpp
    src/FtpConnectionService.cpp
    src/FtpTransferService.cpp
    src/FtpServerService.cpp
    src/CdnUrlService.cpp
    src/DownloadAcceleratorService.cpp
    src/AppSettingsService.cpp
    src/AdminAuthService.cpp
    src/GameSearchService.cpp
    src/LocalFileIndexService.cpp
    src/GamePosterService.cpp
    src/BackupService.cpp
)

target_include_directories(gth_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# gth_indexing (PkgFileParser) has no dependency of its own on gth_application, so this doesn't
# introduce a cycle - GamePosterService is just the first thing here that needs to read a .pkg
# file's own bytes (its box art) rather than only the database rows a scan already produced.
target_link_libraries(gth_application PUBLIC gth_core gth_captcha gth_indexing)
target_compile_features(gth_application PUBLIC cxx_std_20)
