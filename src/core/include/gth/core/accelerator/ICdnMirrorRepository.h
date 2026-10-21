#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/accelerator/CdnMirror.h"

namespace gth::core {

class ICdnMirrorRepository {
public:
    virtual ~ICdnMirrorRepository() = default;

    virtual std::vector<CdnMirror> getAll() = 0;
    virtual std::optional<CdnMirror> getById(const std::string& mirrorId) = 0;

    virtual void add(const CdnMirror& mirror) = 0;
    // Persists a freshly-measured RTT for one mirror - the only field a
    // "measure" pass ever changes.
    virtual void updateRtt(const std::string& mirrorId, std::optional<int> rttMs) = 0;
    virtual void remove(const std::string& mirrorId) = 0;
};

}  // namespace gth::core
