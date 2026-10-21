#pragma once

#include "gth/core/ftp/IFtpConnectionRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class FtpConnectionRepository : public core::IFtpConnectionRepository {
public:
    explicit FtpConnectionRepository(Database& db);

    std::vector<core::FtpConnection> getAll() override;
    std::optional<core::FtpConnection> getById(const std::string& connectionId) override;

    void add(const core::FtpConnection& connection) override;
    void update(const core::FtpConnection& connection) override;
    void remove(const std::string& connectionId) override;

private:
    Database& db_;
};

}  // namespace gth::database
