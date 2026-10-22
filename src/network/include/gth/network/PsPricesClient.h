#pragma once

#include <memory>
#include <optional>
#include <string>

#include "gth/core/network/ICoverArtClient.h"

namespace gth::network {

// Returns the first PlayStation cover URL found in a PSPrices game-search result page.  The
// helper is intentionally independent of WinHTTP so the parser can be tested against captured
// markup and can fail closed when PSPrices changes its HTML.
std::optional<std::string> ExtractFirstPsPricesCoverUrl(const std::string& htmlBody);

// Uses PSPrices' public regional game search page as the poster source.  PSPrices pages expose
// the official PlayStation image CDN URL for each result, so GTH downloads the image bytes and
// keeps using PosterCache's existing disk cache and background queue.
class PsPricesClient : public gth::core::ICoverArtClient {
public:
    PsPricesClient();
    ~PsPricesClient() override;

    std::optional<std::string> fetchPosterByTitle(const std::string& gameTitle) override;

private:
    struct Connections;
    std::unique_ptr<Connections> connections_;
};

}  // namespace gth::network
