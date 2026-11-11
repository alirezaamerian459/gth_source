#include "gth/infrastructure/ServiceContainer.h"

#include <windows.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

#include "gth/core/Guid.h"
#include "gth/database/Migrator.h"

namespace gth::infrastructure {

namespace {

std::filesystem::path localAppDataDir() {
    size_t len = 0;
    char* buffer = nullptr;
    if (_dupenv_s(&buffer, &len, "LOCALAPPDATA") == 0 && buffer != nullptr) {
        std::filesystem::path path(buffer);
        free(buffer);
        return path;
    }
    return std::filesystem::current_path();
}

}  // namespace

std::filesystem::path ServiceContainer::defaultDataDirectory() { return localAppDataDir() / "GameTransferHub"; }

std::filesystem::path ServiceContainer::defaultDatabasePath() { return defaultDataDirectory() / "gth.db"; }

std::filesystem::path ServiceContainer::defaultUpdateManifestPath() {
    return defaultDataDirectory() / "update-manifest.txt";
}

std::filesystem::path ServiceContainer::defaultUpdateTempDirectory() { return defaultDataDirectory() / ".updates"; }

std::filesystem::path ServiceContainer::defaultLogDirectory() { return defaultDataDirectory() / "logs"; }

std::filesystem::path ServiceContainer::defaultNodeIdPath() { return defaultDataDirectory() / "node-id.txt"; }
std::filesystem::path ServiceContainer::defaultBackupWorkingDirectory() { return defaultDataDirectory() / ".backups"; }
std::filesystem::path ServiceContainer::defaultTelegramChatIdPath() {
    return defaultDataDirectory() / "telegram-backup-chat-id.txt";
}

std::filesystem::path ServiceContainer::defaultSonyLinksPath() { return defaultDataDirectory() / "sony-version-links.tsv"; }

std::filesystem::path ServiceContainer::defaultSonyLinksNeededPath() { return defaultDataDirectory() / "sony-links-needed.tsv"; }

std::filesystem::path ServiceContainer::defaultSonyAutomationPath() { return defaultDataDirectory() / "sony-automation.txt"; }

std::vector<std::filesystem::path> ServiceContainer::sonyLinkFiles() {
    std::vector<std::filesystem::path> files;

    std::array<wchar_t, MAX_PATH> exePath{};
    if (::GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size())) > 0) {
        files.push_back(std::filesystem::path(exePath.data()).parent_path() / "sony-version-links.tsv");
    }
    files.push_back(defaultSonyLinksPath());
    files.push_back(defaultSonyLinksNeededPath());  // the list of games still needing a link, filled in by hand
    return files;
}

namespace {
// The Agent's identity must survive process restarts (so repeated reports
// from the same machine upsert the same RemoteNodes row instead of
// accumulating a new one every launch), but has no reason to live in the
// database itself - it's local machine identity, not archive data. A plain
// text file next to the log/database files is the simplest thing that
// works.
std::string loadOrCreateLocalNodeIdFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());

    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        std::string id;
        std::getline(in, id);
        if (!id.empty()) return id;
    }

    const std::string id = gth::core::Guid::newGuid();
    std::ofstream out(path, std::ios::trunc);
    out << id;
    return id;
}
}  // namespace

std::string ServiceContainer::loadOrCreateLocalNodeId() { return loadOrCreateLocalNodeIdFile(defaultNodeIdPath()); }

namespace {
// sqlite3_open() fails to create the .db file if its parent directory
// doesn't exist yet, and that call happens as part of the Database member's
// constructor - i.e. before ServiceContainer's own constructor body runs.
// Ensuring the directory exists has to happen in the member-initializer
// expression itself, not after it.
std::string ensureParentDirectoryAndReturnPath(const std::filesystem::path& databasePath) {
    std::filesystem::create_directories(databasePath.parent_path());
    return databasePath.string();
}
}  // namespace

ServiceContainer::MigrationRunner::MigrationRunner(database::Database& db) { database::Migrator::applyMigrations(db); }

ServiceContainer::ServiceContainer(const std::filesystem::path& databasePath)
    : database_(ensureParentDirectoryAndReturnPath(databasePath)),
      migrationRunner_(database_),
      storageRepository_(database_),
      storageService_(storageRepository_, storageDetector_, &storageHealthProvider_),
      jbLibraryService_(storageService_),
      gameRepository_(database_),
      gameVariantRepository_(database_),
      gameVersionRepository_(database_),
      gameFileRepository_(database_),
      scanJobRepository_(database_),
      firewallService_(defaultDataDirectory() / "firewall-auto-allow-off.txt"),
      storageScanner_(indexing::FilesystemStorageScanner(), &titleLookupService_),
      gameIndexer_(storageRepository_, storageScanner_, gameRepository_, gameVariantRepository_,
                   gameVersionRepository_, gameFileRepository_, scanJobRepository_),
      gameLibraryService_(gameRepository_, gameVariantRepository_, gameVersionRepository_, &storageRepository_),
      localGameBackupSource_(gameVariantRepository_, storageRepository_),
      gamePosterService_(gameVariantRepository_, gameVersionRepository_, gameFileRepository_, storageRepository_),
      sonyUpdateService_(sonyLinkFiles()),
      updateCheckService_(gameVariantRepository_, gameVersionRepository_, gameFileRepository_, sonyUpdateService_),
      gameSearchService_(gameRepository_, gameVariantRepository_, gameVersionRepository_, storageRepository_),
      localFileIndex_(storageRepository_, gameVariantRepository_, gameVersionRepository_, gameFileRepository_),
      updateInfoRepository_(database_),
      updateJobRepository_(database_),
      updateHistoryRepository_(database_),
      localMetadataSource_(defaultUpdateManifestPath().string()),
      updateDetector_(gameVariantRepository_, gameVersionRepository_, updateInfoRepository_,
                       std::vector<core::IUpdateSource*>{&localMetadataSource_}),
      updateJobService_(updateJobRepository_),
      updateScheduler_(updateDetector_, updateJobService_),
      downloadManager_(updateJobRepository_),
      telegramBackupTransport_(),
      backupService_(localGameBackupSource_, telegramBackupTransport_, fileVerifier_, defaultBackupWorkingDirectory()),
      packageSyncService_(storageRepository_, gameVariantRepository_, gameVersionRepository_, gameFileRepository_,
                          sonyUpdateService_, sonyPieceDownloader_, fileVerifier_, storageScanner_),
      archiveUpdater_(updateJobRepository_, gameVariantRepository_, gameVersionRepository_, storageRepository_),
      updateManager_(updateJobRepository_, gameVariantRepository_, updateHistoryRepository_, downloadManager_,
                      fileVerifier_, archiveUpdater_, defaultUpdateTempDirectory().string()),
      consoleRepository_(database_),
      consoleManager_(consoleRepository_, reachabilityChecker_),
      transferJobRepository_(database_),
      transferItemRepository_(database_),
      transferQueue_(transferJobRepository_, transferItemRepository_, localFileTransferProvider_, 2),
      reportService_(storageRepository_, gameRepository_, gameVariantRepository_, gameVersionRepository_,
                      updateInfoRepository_, updateJobRepository_, consoleRepository_, transferJobRepository_),
      remoteNodeRepository_(database_),
      centralServerService_(remoteNodeRepository_, centralServerListener_),
      agentService_(reportService_, tcpRemoteReportSender_, loadOrCreateLocalNodeId()),
      ftpConnectionRepository_(database_),
      ftpConnectionService_(ftpConnectionRepository_, credentialStore_),
      ftpTransferService_(ftpConnectionRepository_, credentialStore_, ftpClientFactory_),
      ftpServerService_(winsockFtpServer_, networkInterfaceDetector_, &jbLibraryService_),
      cdnUrlRepository_(database_),
      cdnUrlService_(cdnUrlRepository_, &localFileIndex_),
      localGameServer_(std::make_shared<network::LocalGameServer>(&cdnUrlService_, &localFileIndex_, &jbLibraryService_)),
      cdnMirrorRepository_(database_),
      downloadAcceleratorServer_([this](const std::string& host) { return downloadAcceleratorService_.resolveMirrorIp(host); },
                                 consoleManager_, cdnUrlService_, localGameServer_),
      downloadAcceleratorService_(cdnMirrorRepository_, downloadAcceleratorServer_, icmpHostPinger_,
                                   networkInterfaceDetector_),
      downloadAcceleratorServerFactory_(
          [this](const std::string& host) { return downloadAcceleratorService_.resolveMirrorIp(host); },
          consoleManager_, cdnUrlService_, localGameServer_),
      appSettingsRepository_(database_),
      appSettingsService_(appSettingsRepository_),
      adminAuthService_(credentialStore_) {
    transferQueue_.start();
}

}  // namespace gth::infrastructure
