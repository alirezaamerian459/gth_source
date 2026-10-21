#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/CdnUrl.h"
#include "gth/core/ftp/ICdnUrlRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class CdnUrlRepository : public gth::core::ICdnUrlRepository {
public:
    explicit CdnUrlRepository(Database& db) : db_(db) {}

    void insert(const gth::core::CdnUrl& cdnUrl) override;
    void update(const gth::core::CdnUrl& cdnUrl) override;
    void remove(const std::string& cdnUrlId) override;
    void removeByUrl(const std::string& url) override;

    std::vector<gth::core::CdnUrl> getAll() override;
    std::vector<gth::core::CdnUrl> getRecent(int limit) override;

private:
    gth::core::CdnUrl fromRow(const Statement& stmt);

    Database& db_;
};

}  // namespace gth::database
