#pragma once

#include "gth/core/accelerator/ICdnMirrorRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class CdnMirrorRepository : public core::ICdnMirrorRepository {
public:
    explicit CdnMirrorRepository(Database& db);

    std::vector<core::CdnMirror> getAll() override;
    std::optional<core::CdnMirror> getById(const std::string& mirrorId) override;

    void add(const core::CdnMirror& mirror) override;
    void updateRtt(const std::string& mirrorId, std::optional<int> rttMs) override;
    void remove(const std::string& mirrorId) override;

private:
    Database& db_;
};

}  // namespace gth::database
