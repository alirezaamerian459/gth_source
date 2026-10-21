#pragma once

#include <optional>
#include <string>

namespace gth::core {

// Best-effort box art for a game, found by its plain title text alone - no account, no API key,
// nothing to configure. This is only ever a fallback for a game whose own package can't supply a
// picture locally (see PosterCache) - every implementation must never throw and must return
// nullopt promptly on any failure (no match, network/site error), never block indefinitely.
class ICoverArtClient {
public:
    virtual ~ICoverArtClient() = default;

    // Raw image bytes (whatever format the source serves) for the best match of `gameTitle`, or
    // nullopt.
    virtual std::optional<std::string> fetchPosterByTitle(const std::string& gameTitle) = 0;
};

}  // namespace gth::core
