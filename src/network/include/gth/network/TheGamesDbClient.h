#pragma once

#include <memory>
#include <optional>
#include <string>

#include "gth/core/network/ICoverArtClient.h"

namespace gth::network {

// The first box art image URL in a GET .../search.php?name={term} response body, or nullopt if
// the page has no result with a cover at all. Exposed standalone (no network involved) so it can
// be unit-tested against a canned response body - see TheGamesDbClient.cpp for the exact HTML
// shape this expects (verified against the real, live page).
std::optional<std::string> ExtractFirstBoxArtUrl(const std::string& htmlBody);

// thegamesdb.net's own public search page (plain server-rendered HTML, no login, no API key -
// verified live: a plain HTTP GET to it returns the exact same box art <img> tag a browser
// renders) - platform-agnostic by design (every PlayStation generation is its own first-class
// platform there), unlike a Steam-centric art database, so it actually covers a title that never
// left PlayStation. See TheGamesDbClient.cpp for exactly what shape that page is expected to have.
class TheGamesDbClient : public gth::core::ICoverArtClient {
public:
    TheGamesDbClient();
    ~TheGamesDbClient() override;

    std::optional<std::string> fetchPosterByTitle(const std::string& gameTitle) override;

private:
    struct Connections;
    std::unique_ptr<Connections> connections_;
};

}  // namespace gth::network
