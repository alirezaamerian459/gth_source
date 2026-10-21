#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include "gth/application/backup/BackupService.h"

namespace gth::network {

// Telegram Bot API transport for a private channel. A bot token is supplied
// at runtime and must be kept in the Windows Credential Manager by the UI.
class TelegramBackupTransport final : public application::IBackupTransport {
public:
    TelegramBackupTransport();
    ~TelegramBackupTransport() override;

    TelegramBackupTransport(const TelegramBackupTransport&) = delete;
    TelegramBackupTransport& operator=(const TelegramBackupTransport&) = delete;

    void configure(std::string botToken, std::string chatId);
    bool testConnection(std::string& error);

    std::optional<std::string> uploadObject(const std::string& name, const std::filesystem::path& localPath,
                                             long long sizeBytes, const application::BackupCallbacks& callbacks,
                                             std::string& error) override;
    bool downloadObject(const std::string& objectId, const std::filesystem::path& destination,
                        const application::BackupCallbacks& callbacks, std::string& error) override;

private:
    bool ready(std::string& error) const;

    void* session_ = nullptr;
    std::string botToken_;
    std::string chatId_;
    std::chrono::steady_clock::time_point lastMessageAt_{};
};

}  // namespace gth::network
