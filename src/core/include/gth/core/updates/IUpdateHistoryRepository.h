#pragma once

#include <string>
#include <vector>

#include "gth/core/entities/UpdateHistory.h"

namespace gth::core {

class IUpdateHistoryRepository {
public:
    virtual ~IUpdateHistoryRepository() = default;

    virtual std::vector<UpdateHistory> getByVariantId(const std::string& gameVariantId) = 0;
    virtual void add(const UpdateHistory& history) = 0;
};

}  // namespace gth::core
