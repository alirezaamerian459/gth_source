#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gth/application/indexing/GameIndexerService.h"
#include "gth/application/indexing/GameLibraryService.h"
#include "gth/application/indexing/GamePosterService.h"
#include "gth/application/backup/BackupService.h"
#include "gth/application/indexing/IGameIndexer.h"
#include "gth/application/indexing/IGameLibraryService.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/application/storage/GameSearchService.h"
#include "gth/application/storage/IGameSearchService.h"
#include "gth/application/storage/LocalFileIndexService.h"
#include "gth/application/storage/IStorageService.h"
#include "gth/application/storage/StorageService.h"
#include "gth/application/updates/ArchiveUpdater.h"
#include "gth/application/updates/DownloadManager.h"
#include "gth/application/updates/IArchiveUpdater.h"
#include "gth/application/updates/IDownloadManager.h"
#include "gth/application/updates/IUpdateDetector.h"
#include "gth/application/updates/IUpdateJobService.h"
#include "gth/application/updates/IUpdateManager.h"
#include "gth/application/updates/IUpdateScheduler.h"
#include "gth/application/updates/UpdateDetectorService.h"
#include "gth/application/updates/UpdateJobService.h"
#include "gth/application/updates/UpdateManagerService.h"
#include "gth/application/updates/UpdateSchedulerService.h"
#include "gth/application/devices/ConsoleManagerService.h"
#include "gth/application/devices/IConsoleManager.h"
#include "gth/application/enterprise/AgentService.h"
#include "gth/application/enterprise/CentralServerService.h"
#include "gth/application/enterprise/IAgentService.h"
#include "gth/application/enterprise/ICentralServerService.h"
#include "gth/application/reports/IReportService.h"
#include "gth/application/reports/ReportService.h"
#include "gth/application/transfer/ITransferQueueService.h"
#include "gth/application/transfer/TransferQueueService.h"
#include "gth/application/ftp/IFtpConnectionService.h"
#include "gth/application/ftp/FtpConnectionService.h"
#include "gth/application/ftp/IFtpTransferService.h"
#include "gth/application/ftp/FtpTransferService.h"
#include "gth/application/ftp/IFtpServerService.h"
#include "gth/application/ftp/FtpServerService.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/application/ftp/CdnUrlService.h"
#include "gth/application/accelerator/IDownloadAcceleratorService.h"
#include "gth/application/accelerator/DownloadAcceleratorService.h"
#include "gth/application/settings/IAppSettingsService.h"
#include "gth/application/settings/AppSettingsService.h"
#include "gth/application/auth/IAdminAuthService.h"
#include "gth/application/auth/AdminAuthService.h"
#include "gth/credentials/WindowsCredentialStore.h"
#include "gth/database/AppSettingsRepository.h"
#include "gth/database/CdnMirrorRepository.h"
#include "gth/database/CdnUrlRepository.h"
#include "gth/database/FtpConnectionRepository.h"
#include "gth/ftp/FtpClientFactory.h"
#include "gth/ftpserver/WinsockFtpServer.h"
#include "gth/acceleratorserver/WinsockCachingProxyServer.h"
#include "gth/acceleratorserver/IDownloadAcceleratorServerFactory.h"
#include "gth/acceleratorserver/DownloadAcceleratorServerFactory.h"
#include "gth/network/IcmpHostPinger.h"
#include "gth/database/Database.h"
#include "gth/database/GameFileRepository.h"
#include "gth/database/GameRepository.h"
#include "gth/database/GameVariantRepository.h"
#include "gth/database/GameVersionRepository.h"
#include "gth/database/ScanJobRepository.h"
#include "gth/database/StorageRepository.h"
#include "gth/database/UpdateHistoryRepository.h"
#include "gth/database/UpdateInfoRepository.h"
#include "gth/database/UpdateJobRepository.h"
#include "gth/database/ConsoleRepository.h"
#include "gth/database/RemoteNodeRepository.h"
#include "gth/database/TransferItemRepository.h"
#include "gth/database/TransferJobRepository.h"
#include "gth/indexing/EnhancedStorageScanner.h"
#include "gth/network/CentralServerListener.h"
#include "gth/network/TcpReachabilityChecker.h"
#include "gth/network/TcpRemoteReportSender.h"
#include "gth/network/Win32NetworkInterfaceDetector.h"
#include "gth/network/LocalGameServer.h"
#include "gth/application/indexing/IPackageSync.h"
#include "gth/application/indexing/IUpdateChecker.h"
#include "gth/application/indexing/PackageSyncService.h"
#include "gth/application/indexing/UpdateCheckService.h"
#include "gth/network/OnlineTitleLookupService.h"
#include "gth/network/TelegramBackupTransport.h"
#include "gth/infrastructure/LocalGameBackupSource.h"
#include "gth/network/SonyPieceDownloader.h"
#include "gth/network/SonyUpdateService.h"
#include "gth/network/TheGamesDbClient.h"
#include "gth/network/WindowsFirewallService.h"
#include "gth/transfer/LocalFileTransferProvider.h"
#include "gth/storage/LocalStorageDetector.h"
#include "gth/storage/WindowsStorageHealthProvider.h"
#include "gth/updates/LocalMetadataSource.h"
#include "gth/verification/FileVerifier.h"

namespace gth::infrastructure {

// Composition root: the only place that knows how the Application-layer
// abstractions are actually implemented (SQLite, local drive detection, ...).
// Owns every concrete instance for the lifetime of the process; the UI
// resolves everything it needs through the IStorageService&/IGameIndexer&/
// etc. accessors, never touching Database/StorageRepository/
// LocalStorageDetector/FilesystemStorageScanner/LocalMetadataSource directly.
class ServiceContainer {
public:
    explicit ServiceContainer(const std::filesystem::path& databasePath);

    application::IStorageService& storageService() { return storageService_; }
    application::IGameIndexer& gameIndexer() { return gameIndexer_; }
    application::IGameLibraryService& gameLibraryService() { return gameLibraryService_; }
    application::IGamePosterService& gamePosterService() { return gamePosterService_; }
    core::ICoverArtClient& coverArtClient() { return coverArtClient_; }
    application::IGameSearchService& gameSearchService() { return gameSearchService_; }
    application::IUpdateDetector& updateDetector() { return updateDetector_; }
    application::IUpdateJobService& updateJobService() { return updateJobService_; }
    application::IUpdateScheduler& updateScheduler() { return updateScheduler_; }
    application::IDownloadManager& downloadManager() { return downloadManager_; }
    application::IUpdateManager& updateManager() { return updateManager_; }
    core::IUpdateSource& defaultUpdateSource() { return localMetadataSource_; }
    core::INetworkInterfaceDetector& networkInterfaceDetector() { return networkInterfaceDetector_; }
    application::IConsoleManager& consoleManager() { return consoleManager_; }
    application::ITransferQueueService& transferQueue() { return transferQueue_; }
    application::IReportService& reportService() { return reportService_; }
    application::ICentralServerService& centralServerService() { return centralServerService_; }
    application::IAgentService& agentService() { return agentService_; }
    application::IFtpConnectionService& ftpConnectionService() { return ftpConnectionService_; }
    application::IFtpTransferService& ftpTransferService() { return ftpTransferService_; }
    application::IFtpServerService& ftpServerService() { return ftpServerService_; }
    application::ICdnUrlService& cdnUrlService() { return cdnUrlService_; }
    application::IDownloadAcceleratorService& downloadAcceleratorService() { return downloadAcceleratorService_; }
    acceleratorserver::IDownloadAcceleratorServerFactory& downloadAcceleratorServerFactory() {
        return downloadAcceleratorServerFactory_;
    }
    application::IAppSettingsService& appSettingsService() { return appSettingsService_; }
    application::IAdminAuthService& adminAuthService() { return adminAuthService_; }
    core::IFirewallService& firewallService() { return firewallService_; }
    core::ITitleLookupService& titleLookupService() { return titleLookupService_; }
    application::IUpdateChecker& updateChecker() { return updateCheckService_; }
    application::IPackageSync& packageSync() { return packageSyncService_; }
    application::BackupService& backupService() { return backupService_; }
    network::TelegramBackupTransport& telegramBackupTransport() { return telegramBackupTransport_; }
    core::ICredentialStore& credentialStore() { return credentialStore_; }

    // Where PS5 games' Sony version-file links are kept, one "<title ID> <tab>
    // <link>" per line. The one in the data folder wins over one beside the exe.
    static std::filesystem::path defaultSonyLinksPath();
    // Games still needing a link, listed for filling in; read as a links file too.
    static std::filesystem::path defaultSonyLinksNeededPath();
    // What the Sony automation does by itself (see SonyAutomationSettings).
    static std::filesystem::path defaultSonyAutomationPath();
    static std::vector<std::filesystem::path> sonyLinkFiles();

    static std::filesystem::path defaultDataDirectory();
    static std::filesystem::path defaultDatabasePath();
    static std::filesystem::path defaultUpdateManifestPath();
    static std::filesystem::path defaultUpdateTempDirectory();
    static std::filesystem::path defaultLogDirectory();
    static std::filesystem::path defaultNodeIdPath();
    static std::filesystem::path defaultBackupWorkingDirectory();
    static std::filesystem::path defaultTelegramChatIdPath();

private:
    // Runs pending migrations as soon as database_ exists, before any other
    // member (e.g. transferQueue_, whose constructor queries the database
    // for crash recovery) is constructed. Must stay declared immediately
    // after database_: members are constructed in declaration order, not
    // member-initializer-list order.
    struct MigrationRunner {
        explicit MigrationRunner(database::Database& db);
    };

    database::Database database_;
    MigrationRunner migrationRunner_;
    database::StorageRepository storageRepository_;
    storage::LocalStorageDetector storageDetector_;
    storage::WindowsStorageHealthProvider storageHealthProvider_;
    application::StorageService storageService_;

    database::GameRepository gameRepository_;
    database::GameVariantRepository gameVariantRepository_;
    database::GameVersionRepository gameVersionRepository_;
    database::GameFileRepository gameFileRepository_;
    database::ScanJobRepository scanJobRepository_;
    network::WindowsFirewallService firewallService_;
    network::OnlineTitleLookupService titleLookupService_;
    indexing::EnhancedStorageScanner storageScanner_;
    application::GameIndexerService gameIndexer_;
    application::GameLibraryService gameLibraryService_;
    LocalGameBackupSource localGameBackupSource_;
    application::GamePosterService gamePosterService_;
    network::TheGamesDbClient coverArtClient_;
    network::SonyUpdateService sonyUpdateService_;
    application::UpdateCheckService updateCheckService_;
    application::GameSearchService gameSearchService_;
    application::LocalFileIndexService localFileIndex_;

    database::UpdateInfoRepository updateInfoRepository_;
    database::UpdateJobRepository updateJobRepository_;
    database::UpdateHistoryRepository updateHistoryRepository_;
    updates::LocalMetadataSource localMetadataSource_;
    application::UpdateDetectorService updateDetector_;
    application::UpdateJobService updateJobService_;
    application::UpdateSchedulerService updateScheduler_;
    application::DownloadManager downloadManager_;
    verification::FileVerifier fileVerifier_;
    network::TelegramBackupTransport telegramBackupTransport_;
    application::BackupService backupService_;
    network::SonyPieceDownloader sonyPieceDownloader_;
    application::PackageSyncService packageSyncService_;
    application::ArchiveUpdater archiveUpdater_;
    application::UpdateManagerService updateManager_;

    network::Win32NetworkInterfaceDetector networkInterfaceDetector_;

    database::ConsoleRepository consoleRepository_;
    network::TcpReachabilityChecker reachabilityChecker_;
    application::ConsoleManagerService consoleManager_;

    database::TransferJobRepository transferJobRepository_;
    database::TransferItemRepository transferItemRepository_;
    transfer::LocalFileTransferProvider localFileTransferProvider_;
    application::TransferQueueService transferQueue_;

    application::ReportService reportService_;

    database::RemoteNodeRepository remoteNodeRepository_;
    network::CentralServerListener centralServerListener_;
    network::TcpRemoteReportSender tcpRemoteReportSender_;
    application::CentralServerService centralServerService_;
    application::AgentService agentService_;

    database::FtpConnectionRepository ftpConnectionRepository_;
    credentials::WindowsCredentialStore credentialStore_;
    ftp::FtpClientFactory ftpClientFactory_;
    application::FtpConnectionService ftpConnectionService_;
    application::FtpTransferService ftpTransferService_;

    ftpserver::WinsockFtpServer winsockFtpServer_;
    application::FtpServerService ftpServerService_;

    database::CdnUrlRepository cdnUrlRepository_;
    application::CdnUrlService cdnUrlService_;
    // Shared by every proxy instance below (the original one and every
    // "+ Add Console" one the factory creates) - there is only one game
    // library on this PC regardless of how many consoles are connected.
    std::shared_ptr<network::LocalGameServer> localGameServer_;

    database::CdnMirrorRepository cdnMirrorRepository_;
    network::IcmpHostPinger icmpHostPinger_;
    // Constructed with a lambda that only captures `this` and defers into
    // downloadAcceleratorService_ - safe despite that member not existing
    // yet at this point in the initializer list, because the lambda isn't
    // actually invoked until a proxy request arrives, long after the whole
    // ServiceContainer (and therefore downloadAcceleratorService_) is built.
    acceleratorserver::WinsockCachingProxyServer downloadAcceleratorServer_;
    application::DownloadAcceleratorService downloadAcceleratorService_;
    // Builds every extra "+ Add Console" listener with the same
    // console/CDN/library wiring as downloadAcceleratorServer_ above - see
    // IDownloadAcceleratorServerFactory.
    acceleratorserver::DownloadAcceleratorServerFactory downloadAcceleratorServerFactory_;

    database::AppSettingsRepository appSettingsRepository_;
    application::AppSettingsService appSettingsService_;
    application::AdminAuthService adminAuthService_;

    static std::string loadOrCreateLocalNodeId();
};

}  // namespace gth::infrastructure
