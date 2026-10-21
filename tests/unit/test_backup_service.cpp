#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gth/application/backup/BackupService.h"
#include "gth/core/Guid.h"
#include "gth/verification/FileVerifier.h"

namespace fs = std::filesystem;

namespace {

class FakeBackupSource : public gth::application::IBackupSource {
public:
    fs::path root;

    bool downloadGame(const gth::application::BackupGame&, const fs::path& destination,
                      const gth::application::BackupCallbacks&, std::string& error) override {
        std::error_code ec;
        fs::create_directories(destination, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        fs::copy(root, destination, fs::copy_options::recursive, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    }
};

class FakeBackupTransport : public gth::application::IBackupTransport {
public:
    void setRoot(fs::path root) {
        root_ = std::move(root);
        fs::create_directories(root_);
    }

    std::optional<std::string> uploadObject(const std::string& name, const fs::path& localPath, long long,
                                             const gth::application::BackupCallbacks&, std::string& error) override {
        if (failUploadAt && uploads.size() + 1 == *failUploadAt) {
            error = "injected upload error";
            return std::nullopt;
        }
        const std::string id = "object-" + std::to_string(nextId_++);
        std::error_code ec;
        fs::copy_file(localPath, root_ / id, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = ec.message();
            return std::nullopt;
        }
        uploads.push_back({id, name});
        return id;
    }

    bool downloadObject(const std::string& objectId, const fs::path& destination,
                        const gth::application::BackupCallbacks&, std::string& error) override {
        std::error_code ec;
        fs::copy_file(root_ / objectId, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    }

    void replace(const std::string& objectId, const std::string& contents) {
        std::ofstream out(root_ / objectId, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    struct Upload { std::string id; std::string name; };
    std::vector<Upload> uploads;
    std::optional<size_t> failUploadAt;

private:
    fs::path root_;
    size_t nextId_ = 1;
};

class BackupServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("gth-backup-test-" + gth::core::Guid::newGuid());
        fs::create_directories(root_ / "source" / "PS5" / "Game");
        fs::create_directories(root_ / "working");
        fs::create_directories(root_ / "telegram");
        transport_.setRoot(root_ / "telegram");
        source_.root = root_ / "source";
        write(root_ / "source" / "PS5" / "Game" / "eboot.bin", "hello package bytes and end");
        service_ = std::make_unique<gth::application::BackupService>(source_, transport_, hasher_, root_ / "working");
    }

    void TearDown() override {
        service_.reset();
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    static void write(const fs::path& path, const std::string& contents) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::string read(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    fs::path root_;
    FakeBackupSource source_;
    FakeBackupTransport transport_;
    gth::verification::FileVerifier hasher_;
    std::unique_ptr<gth::application::BackupService> service_;
};

TEST_F(BackupServiceTest, CreatesManifestLastAndRestoresVerifiedFiles) {
    const gth::application::BackupGame game{"variant-1", "Example Game", "PS5", "1.20"};
    const auto backup = service_->createBackup(game);

    ASSERT_TRUE(backup.success) << backup.error;
    ASSERT_EQ(transport_.uploads.size(), 2u);
    EXPECT_NE(transport_.uploads.back().name.find("-manifest.gth"), std::string::npos);
    EXPECT_EQ(backup.manifestObjectId, transport_.uploads.back().id);
    const auto catalog = service_->listBackups();
    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog.front().objectId, backup.manifestObjectId);
    EXPECT_EQ(catalog.front().title, game.title);

    const auto restored = service_->restoreBackup(backup.manifestObjectId, root_ / "restored");
    ASSERT_TRUE(restored.success) << restored.error;
    const fs::path restoredRoot = root_ / "restored" / ("Example Game-PS5-1.20-" + backup.backupId);
    const fs::path restoredFile = restoredRoot / "PS5" / "Game" / "eboot.bin";
    EXPECT_EQ(read(restoredFile), read(root_ / "source" / "PS5" / "Game" / "eboot.bin"));
}

TEST_F(BackupServiceTest, CorruptChunkIsRejectedAndRestoreIsNotInstalled) {
    const auto backup = service_->createBackup({"variant-1", "Example Game", "PS5", "1.20"});
    ASSERT_TRUE(backup.success) << backup.error;
    transport_.replace(transport_.uploads.front().id,
                       std::string(std::string("hello package bytes and end").size(), 'x'));

    const auto restored = service_->restoreBackup(backup.manifestObjectId, root_ / "restored");
    EXPECT_FALSE(restored.success);
    EXPECT_NE(restored.error.find("SHA-256"), std::string::npos);
    EXPECT_FALSE(fs::exists(root_ / "restored" / ("Example Game-PS5-1.20-" + backup.backupId)));
}

TEST_F(BackupServiceTest, DoesNotPublishManifestAfterChunkUploadFailure) {
    transport_.failUploadAt = 1;
    const auto backup = service_->createBackup({"variant-1", "Example Game", "PS5", "1.20"});

    EXPECT_FALSE(backup.success);
    EXPECT_TRUE(backup.manifestObjectId.empty());
    EXPECT_TRUE(transport_.uploads.empty());
}

TEST_F(BackupServiceTest, RejectsManifestPathTraversalBeforeDownloadingChunks) {
    // A valid empty-file digest with a path that escapes the restore tree.
    const std::string manifest =
        "GTH_BACKUP_V1\n"
        "G\t6964\t5469746c65\t505335\t31\t323032362d30312d30315430303a30303a30305a\n"
        "F\t2e2e2f6576696c\t0\te3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\t0\n";
    transport_.replace("malicious-manifest", manifest);

    const auto restored = service_->restoreBackup("malicious-manifest", root_ / "restored");
    EXPECT_FALSE(restored.success);
    EXPECT_NE(restored.error.find("unsafe file path"), std::string::npos);
}

}  // namespace
