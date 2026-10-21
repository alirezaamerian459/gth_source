#pragma once

#include <optional>
#include <string>

namespace gth::core {

// Best-effort online lookup of a game's real title from its platform +
// title ID, for the (rare) cases a package's own embedded metadata can't
// supply one (see EnhancedStorageScanner's title-enrichment pass). There's
// no official Sony API for this; implementations are expected to use
// community title-tracking sites instead. Implementations must never throw
// and must return nullopt promptly on any failure - offline, no match,
// timeout, unsupported platform - since scanning must keep working
// correctly with no network at all.
class ITitleLookupService {
public:
    virtual ~ITitleLookupService() = default;
    virtual std::optional<std::string> lookupTitle(const std::string& platform, const std::string& titleId) = 0;

    // Why the most recent lookupTitle() call returned nullopt when it
    // couldn't even get an answer (network down, blocked, timed out) - empty
    // when the call succeeded or the site simply had no such title.
    virtual std::string lastFailureReason() const { return {}; }
};

}  // namespace gth::core
