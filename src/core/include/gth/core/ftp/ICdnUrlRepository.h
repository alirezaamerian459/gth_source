#pragma once

#include <string>
#include <vector>

#include "gth/core/ftp/CdnUrl.h"

namespace gth::core {

class ICdnUrlRepository {
public:
    virtual ~ICdnUrlRepository() = default;

    virtual void insert(const CdnUrl& cdnUrl) = 0;
    virtual void update(const CdnUrl& cdnUrl) = 0;
    virtual void remove(const std::string& cdnUrlId) = 0;
    virtual void removeByUrl(const std::string& url) = 0;

    virtual std::vector<CdnUrl> getAll() = 0;
    virtual std::vector<CdnUrl> getRecent(int limit) = 0;
};

}  // namespace gth::core
