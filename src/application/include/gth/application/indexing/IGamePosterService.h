#pragma once

#include <optional>
#include <string>

namespace gth::application {

// Reads a variant's current-version box art (ICON0.PNG) straight out of its own package
// files - no network, nothing beyond what the archive already has on disk. Backs the
// poster tiles the Games and Updates tabs show; nothing else needs this.
class IGamePosterService {
public:
    virtual ~IGamePosterService() = default;

    // Raw PNG bytes of `gameVariantId`'s current version's ICON0.PNG, or nullopt when there's no
    // Active current version, no reachable package file for it, or (PS3, or a package this can't
    // parse) no such entry to read. Never throws. Reads a handful of small header-sized chunks off
    // disk - cheap enough to call from the UI thread, but callers still cache the result rather
    // than calling this on every repaint.
    virtual std::optional<std::string> extractPosterPng(const std::string& gameVariantId) = 0;
};

}  // namespace gth::application
