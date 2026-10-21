#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "gth/core/index/ITitleLookupService.h"

namespace gth::network {

// Looks up a PS4/PS5 game's real title by title ID against the community
// title-tracking sites the PKG scene already relies on for this - there's
// no official Sony API for it. PS5 (PPSA...) goes to prosperopatches.com,
// PS4 (CUSA...) to orbispatches.com; any other platform returns nullopt
// without making a request. Every failure mode (offline, timeout, no
// match, unexpected response) returns nullopt rather than throwing, so a
// scan never breaks or hangs just because the machine has no network.
class OnlineTitleLookupService : public core::ITitleLookupService {
public:
    // Hosts are overridable only so tests can aim at an unreachable one and
    // exercise the failure path without depending on the real sites.
    explicit OnlineTitleLookupService(std::wstring ps5Host = L"prosperopatches.com",
                                       std::wstring ps4Host = L"orbispatches.com");
    ~OnlineTitleLookupService() override;
    OnlineTitleLookupService(const OnlineTitleLookupService&) = delete;
    OnlineTitleLookupService& operator=(const OnlineTitleLookupService&) = delete;

    std::optional<std::string> lookupTitle(const std::string& platform, const std::string& titleId) override;
    std::string lastFailureReason() const override;

private:
    std::wstring ps5Host_;
    std::wstring ps4Host_;
    std::string lastFailureReason_;
    int consecutiveFailures_ = 0;
    std::chrono::steady_clock::time_point pausedUntil_{};
    std::string pauseReason_;

    // One secure connection per site, kept open across lookups: a scan of a
    // big library asks about every game in turn, and setting up a fresh TLS
    // connection for each one (about 1.2 s) dominated the scan time.
    struct Connections;
    std::unique_ptr<Connections> connections_;
};

// Pulls the title out of one of these sites' `/api/internal/search` JSON
// responses, e.g. {"success":true,"results":[{"titleid":"PPSA20050",
// "name":"EA SPORTS FC 25",...}]}. Only trusts the first result, and only
// when its titleid matches queriedTitleId (case-insensitively) - the same
// endpoint also serves fuzzy name search, and trusting a fuzzy match here
// would silently mislabel the wrong game. Exposed standalone (no network
// involved) so it can be unit-tested against canned response bodies.
std::optional<std::string> ParseTitleLookupResponse(const std::string& jsonBody, const std::string& queriedTitleId);

}  // namespace gth::network
