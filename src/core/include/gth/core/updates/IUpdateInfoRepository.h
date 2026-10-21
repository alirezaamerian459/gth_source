#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/UpdateInfo.h"

namespace gth::core {

class IUpdateInfoRepository {
public:
    virtual ~IUpdateInfoRepository() = default;

    virtual std::vector<UpdateInfo> getAll() = 0;
    virtual std::optional<UpdateInfo> getByVariantId(const std::string& gameVariantId) = 0;

    // Upsert by GameVariantId: a re-check always replaces the previous
    // snapshot for that variant rather than accumulating rows.
    virtual void upsert(const UpdateInfo& info) = 0;
};

}  // namespace gth::core
