#pragma once

#include <functional>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gth::indexing {

struct PkgMetadata {
    std::string title;
    std::string titleId;
    std::string contentId;
    // The game's version as the package - or, failing that, its file name -
    // states it. EMPTY means unknown, never a made-up default.
    std::string version;
    std::string region;
    std::string platform;
    long long sizeBytes = 0;

    // True only when `title` came from an actual embedded metadata entry
    // (PARAM.SFO's TITLE, or PS5 param.json's titleName) - never for a
    // title ID or a best-effort guess derived from the Content ID's label
    // segment. Callers use this to decide whether a title is worth trying
    // to improve on (e.g. EnhancedStorageScanner's split-part re-read only
    // fires when this is false).
    bool titleIsVerified = false;
};

// Parses PS3/PS4/PS5 .pkg headers well enough to catalog a game - not a full
// extractor (no decryption, no PFS/entry-content reading beyond the small
// plaintext metadata entries every retail package carries: PARAM.SFO on
// PS3/PS4, param.json on PS5). Format reverse-engineered against SeregonWar's
// PkgToolBox (github.com/SeregonWar/PkgToolBox); see that project's
// packages/package_ps4.py, package_ps5.py and sfo.py for the authoritative
// reference this was checked against.
class PkgFileParser {
public:
    // Parse .pkg file and extract metadata
    // Returns metadata if successful, nullopt if parsing fails
    static std::optional<PkgMetadata> parseFile(const std::string& pkgFilePath);

    // A package split into transfer-sized parts (part "_0.pkg", "_1.pkg",
    // ...) has its FIH wrapper in part 0, but the embedded \x7fCNT header
    // (and therefore param.json, and the real title) sits *after* the
    // encrypted game image - for any game past a few GB, that's beyond part
    // 0's own bytes, often by tens of GB. parseFile() on part 0 alone
    // correctly can't reach it and falls back to the title ID. This reads
    // across the full ordered set of parts as one virtual stream (an
    // absolute offset from the FIH header may land in any of them) to reach
    // that data anyway. orderedParts must be every part belonging to one
    // release, in original-file order (part 0 first), as {absolutePath,
    // sizeBytes}; sizeBytes must be each part's real size, since that's what
    // maps a virtual offset to the right part.
    static std::optional<PkgMetadata> parseSplitPs5Parts(
        const std::vector<std::pair<std::string, long long>>& orderedParts);

    // The app version encoded in an update package's file name
    // ("...-A0110-V0100..." -> "01.10"), or empty when there's no such tag.
    static std::string versionFromFilename(const std::string& filename);

    // A PS5 package Sony serves in pieces has one extra final piece,
    // "<content id>_sc.pkg" (about 85 MB): the metadata block that holds
    // param.json - the real title and version - and everything else past the
    // encrypted image. It starts with a \x7fCNT header like a PS4 package but
    // is not a game of its own; it's the last piece of the release its Content
    // ID names, and is read together with that release's other pieces.
    static bool isMetadataPieceName(const std::string& fileName);

    // Metadata for such a piece taken from its file name alone (its Content
    // ID), so the caller can attach it to the release it completes.
    static std::optional<PkgMetadata> parseMetadataPieceFileName(const std::string& pkgFilePath, long long fileSize);

    // Extracts ICON0.PNG - the box-art tile every retail PS3/PS4/PS5 package is required to embed
    // unencrypted right next to its metadata (Sony's own console shows this same file in its home-screen
    // library grid) - as raw PNG bytes. `pkgFilePath` is the same single file parseFile() would be given:
    // works for a PS4 package, and for a PS5 package whose embedded CNT header (see parsePs5) lands inside
    // this one file. nullopt when the file isn't a package this can read, carries no such entry, or (a
    // split PS5 game, almost always) the header isn't reachable in this one file - see
    // extractPosterFromParts for that case. PS3 isn't supported here (its icon lives inside the encrypted
    // image, unlike PS4/PS5's cleartext asset table) - callers fall back to a placeholder for it.
    static std::optional<std::string> extractPoster(const std::string& pkgFilePath);

    // Same, reading across a release's ordered split parts (see parseSplitPs5Parts) - needed because a
    // split PS5 release's entry table normally lands past every numbered part, in its "_sc.pkg" metadata
    // piece (see isMetadataPieceName).
    static std::optional<std::string> extractPosterFromParts(
        const std::vector<std::pair<std::string, long long>>& orderedParts);

private:
    using ReadAtFn = std::function<bool(long long offset, unsigned char* buffer, size_t size)>;

    // PS3 packages (\x7fPKG, magic 0x7F504B47): content ID lives right in the
    // fixed header at 0x30, no further structure needed for cataloging.
    static std::optional<PkgMetadata> parsePs3(std::ifstream& file, long long fileSize);

    // PS4 packages (\x7fCNT, magic 0x7F434E54): fixed 0xA0-byte header gives
    // the entry table location and a raw content ID at 0x40; walking the
    // entry table to the PARAM_SFO_ID (0x1000) entry and parsing it as a
    // PARAM.SFO gets the real title instead of just the opaque content ID.
    static std::optional<PkgMetadata> parsePs4(std::ifstream& file, long long fileSize);

    // PS5 "finalized" packages (\x7fFIH, magic 0x7F464948): a 64KB wrapper
    // around the encrypted game image, with a second \x7fCNT header embedded
    // *after* that image (so, for a large game, potentially gigabytes into
    // the file - see FIH+0x58). Only reachable when that offset falls inside
    // this single file; for a package split into transfer-sized parts, the
    // embedded CNT (and therefore the real title) almost always lands in a
    // later part than the one being parsed, so this legitimately returns
    // nullopt for most split files - parseFromFilename() is the fallback for
    // exactly that case, not a bug in this parser. Both this single-file
    // entry point and parseSplitPs5Parts() share the same core logic
    // (parsePs5Core), just reading through a single file vs a virtual
    // multi-part stream.
    static std::optional<PkgMetadata> parsePs5(std::ifstream& file, long long fileSize);
    static std::optional<PkgMetadata> parsePs5Core(const ReadAtFn& readAt, long long totalSize);

    // ICON0.PNG's entry ID (PS4) is fixed, so this reruns parsePs4's own header/entry-table walk
    // looking for that one ID instead of PARAM_SFO_ID.
    static std::optional<std::string> extractPs4Poster(std::ifstream& file, long long fileSize);
    // PS5 entries have no fixed ID (same reason parsePs5Core resolves param.json by name, not ID) - this
    // reruns parsePs5Core's own CNT-header/entry-table walk looking up "icon0.png" in the names table.
    static std::optional<std::string> extractPs5PosterCore(const ReadAtFn& readAt, long long totalSize);

    // Reads and validates a PARAM.SFO/PSF blob (PS3/PS4), returning its
    // key/value entries as strings (binary values are dropped - nothing here
    // needs them).
    static std::optional<std::vector<std::pair<std::string, std::string>>> parseSfo(const std::string& data);

    // Pulls a handful of flat/shallow string fields out of a PS5 param.json
    // blob without a full JSON parser - every field this cares about
    // (titleId, contentId, a titleName under localizedParameters.*) is a
    // simple "key": "value" pair, so a full parser would be pure overhead
    // for what's read here.
    static std::optional<std::string> extractJsonStringField(const std::string& json, const std::string& key);

    // Fallback when the binary header doesn't match a known magic, or a
    // known-magic file's real metadata isn't reachable within it: many
    // real-world dumps are split into multiple parts for transfer/storage
    // (e.g. "EP0006-PPSA02343_00-NUTSEU0000000000_0.pkg" .. "_10.pkg") where
    // only the first part carries any header at all (PS4) or an unreachable
    // one (PS5, see parsePs5), and the rest are raw continuation bytes.
    // Every part's filename still carries Sony's standard Content ID
    // (distributor-titleId_appVersion-label), so we can recognize and group
    // them from the name alone.
    static std::optional<PkgMetadata> parseFromFilename(const std::string& pkgFilePath, long long fileSize);

    // Get platform from title ID
    static std::string detectPlatform(const std::string& titleId);

    // Get region from title ID (Sony's title-ID-prefix -> region convention,
    // not a positional-character guess - prefixes that aren't in the table
    // report "Unknown" rather than a wrong guess).
    static std::string detectRegion(const std::string& contentId);

    // Best-effort, unverified title when no embedded metadata gave one: the
    // Content ID's 16-character label segment is often a squashed-together
    // abbreviation of the real title (e.g. "EASPORTSFC2025BG" for EA Sports
    // FC 25), but it's also sometimes just padding (e.g.
    // "NUTSEU0000000000") - looksLikeOpaqueLabel() tells those apart, and
    // this returns the humanized label when it looks real, the title ID
    // otherwise. Never sets titleIsVerified - this is always a guess.
    static std::string bestEffortTitle(const std::string& contentId, const std::string& titleId);

    // A label with almost no distinct characters, a long run of one
    // repeated character (e.g. padding zeros), or that's entirely digits
    // isn't a squashed title - it's a placeholder/counter, and humanizing
    // it would just produce a plausible-looking wrong answer.
    static bool looksLikeOpaqueLabel(const std::string& label);

    // Inserts a space at each letter/digit boundary ("EASPORTSFC2025BG" ->
    // "EASPORTSFC 2025 BG") - as far as this can go without a dictionary of
    // real words to split "EASPORTSFC" itself on; still far more
    // recognizable than the bare title ID.
    static std::string humanizeLabel(const std::string& label);
};

}  // namespace gth::indexing
