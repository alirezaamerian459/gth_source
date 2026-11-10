#include "gth/indexing/EnhancedStorageScanner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "gth/indexing/PkgFileParser.h"

namespace gth::indexing {

namespace fs = std::filesystem;
using core::ScannedFile;
using core::ScannedVariant;
using core::ScannedVersion;

namespace {

// std::filesystem on Windows enforces the legacy ~260 char MAX_PATH unless
// the path is prefixed with \\?\ (or \\?\UNC\ for network shares), which
// switches the Win32 layer to the extended-length path API (up to ~32767
// chars). Without this, a single deeply-nested file/folder name - common
// once a library reaches multi-TB size - throws a filesystem_error deep in
// the middle of the scan.
fs::path toLongPath(const std::string& rawPath) {
    std::string normalized = rawPath;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    if (normalized.rfind("\\\\?\\", 0) == 0) return fs::path(normalized);
    if (normalized.rfind("\\\\", 0) == 0) return fs::path("\\\\?\\UNC\\" + normalized.substr(2));
    return fs::path("\\\\?\\" + normalized);
}

}  // namespace

EnhancedStorageScanner::EnhancedStorageScanner(const FilesystemStorageScanner& dirScanner,
                                                core::ITitleLookupService* titleLookup)
    : dirScanner_(dirScanner), titleLookup_(titleLookup) {}

core::ScanDiagnostics EnhancedStorageScanner::lastScanDiagnostics() const { return diagnostics_; }

std::vector<ScannedVariant> EnhancedStorageScanner::scan(const std::string& rootPath) {
    diagnostics_ = {};

    // First, scan for traditional directory structure. Wrapped so a
    // filesystem error while walking the Platform/Title/Region/Version
    // layout (e.g. an inaccessible or malformed branch) can't prevent the
    // .pkg scan below from running - the two scan strategies are
    // independent and either one failing shouldn't sink the other.
    std::vector<ScannedVariant> results;
    try {
        results = dirScanner_.scan(rootPath);
    } catch (const std::exception&) {
        results.clear();
    }

    // Then, scan for .pkg files
    auto pkgResults = scanForPkgFiles(rootPath);

    // Merge results: .pkg files that match existing variants should update them,
    // new .pkg files should be added as new variants
    std::unordered_map<std::string, size_t> variantMap;
    for (size_t i = 0; i < results.size(); ++i) {
        std::string key = results[i].platform + "|" + results[i].title + "|" + results[i].region;
        variantMap[key] = i;
    }

    for (auto& pkgVariant : pkgResults) {
        std::string key = pkgVariant.platform + "|" + pkgVariant.title + "|" + pkgVariant.region;

        auto it = variantMap.find(key);
        if (it != variantMap.end()) {
            // Merge PKG versions into existing variant
            for (auto& version : pkgVariant.versions) {
                results[it->second].versions.push_back(std::move(version));
            }
        } else {
            // Add as new variant
            results.push_back(std::move(pkgVariant));
        }
    }

    return results;
}

namespace {

std::string nowIso8601() {
    const auto secs = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", secs);
}

std::string fileLastModified(const fs::path& absolutePath) {
    std::error_code ec;
    auto lastWriteTime = fs::last_write_time(absolutePath, ec);
    if (ec) return "2000-01-01T00:00:00Z";

    // clock_cast is a pure, deterministic conversion between clock epochs -
    // unlike the "subtract file_clock::now(), add system_clock::now()" trick
    // this used to use, which samples two different clocks at two different
    // (always slightly different) instants. That made the computed string
    // for the *same unchanged file* jitter by a second across scans whenever
    // the gap between those two now() calls happened to straddle a second
    // boundary, and since reconcileFiles compares lastModified by exact
    // string equality, every such jitter looked like a real file change -
    // spuriously inflating "versions updated" on every rescan, worse over
    // higher-latency I/O (e.g. SMB) where that gap is larger and less
    // predictable.
    const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(lastWriteTime);
    const auto secs = std::chrono::floor<std::chrono::seconds>(systemTime);
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", secs);
}

struct PkgHit {
    PkgMetadata metadata;
    fs::path absolutePath;
    std::string relativePath;
};

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) out += c;  // drop other control chars
        }
    }
    return out;
}

// Every field this cares about is a flat "key": "value" pair, same as the
// param.json reader in PkgFileParser - a full JSON parser would be pure
// overhead for reading one field back out of a file this code itself wrote.
std::optional<std::string> readJsonStringField(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::string value;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            value += json[pos + 1];
            pos += 2;
        } else {
            value += json[pos];
            ++pos;
        }
    }
    return value;
}

struct SidecarField {
    std::string value;
    bool isManual;
};

// Applies the "auto vs manual" provenance rule for one field: a sidecar
// explicitly marked "manual" for this field is never touched again; one
// marked "auto" (or unmarked - a legacy sidecar, or one the user created by
// hand) is only treated as a manual edit when its stored value isn't just
// the given placeholder, since that's the one case a hand edit and a fresh
// automatic guess can't be told apart in any other way. This is what lets a
// later, better auto-guess (e.g. an unrecognized label getting humanized,
// or a param.json becoming reachable) win over a stale one, while a real
// edit stays locked in forever.
SidecarField resolveSidecarField(const std::optional<std::string>& existingValue,
                                  const std::optional<std::string>& existingSource, const std::string& placeholder,
                                  const std::string& freshValue) {
    if (existingValue.has_value() && !existingValue->empty()) {
        if (existingSource.has_value() && *existingSource == "manual") return {*existingValue, true};
        if (!existingSource.has_value() && *existingValue != placeholder) return {*existingValue, true};
    }
    return {freshValue, false};
}

// How far a saved title can be trusted, by where it came from. Sidecars written before sources were recorded in
// detail say only "auto" (or nothing at all), so "auto" is the lowest rank.
int titleRank(const std::string& source) {
    if (source == "manual") return 3;   // typed in by a person
    if (source == "package") return 2;  // the package's own param.json / PARAM.SFO
    if (source == "online") return 1;   // a tracker site knew the title ID
    return 0;                           // a guess made from the file names, or unknown
}

struct TitleChoice {
    std::string value;
    std::string source;  // manual | package | online | auto
};

// Which title a release keeps after a scan. A saved title is only replaced by one that is at least as trustworthy,
// and NEVER by the bare title ID: that is what a scan falls back to when it couldn't find a name this time (no
// network, a share that didn't answer, a tracker site that was down), and writing it over a name a scan found
// earlier is how a library of real names turns back into "PPSA02343"s. A better source still upgrades a weaker
// one (a guess becomes an online name, an online name becomes the package's own).
TitleChoice resolveSidecarTitle(const std::optional<std::string>& existingValue,
                                const std::optional<std::string>& existingSource, const std::string& titleId,
                                const std::string& freshValue, const std::string& freshSource) {
    if (existingValue.has_value() && !existingValue->empty()) {
        if (existingSource.has_value() && *existingSource == "manual") return {*existingValue, "manual"};
        // No provenance at all and not the placeholder: a value someone typed by hand.
        if (!existingSource.has_value() && *existingValue != titleId) return {*existingValue, "manual"};

        if (*existingValue != titleId) {
            const std::string keptSource = existingSource.has_value() ? *existingSource : std::string("auto");
            const bool freshIsBareId = freshValue.empty() || freshValue == titleId;
            if (freshIsBareId || titleRank(freshSource) < titleRank(keptSource)) return {*existingValue, keptSource};
        }
    }
    return {freshValue, freshSource};
}

// Whether the sidecar at `path` already holds a name worth keeping: not the title ID, and put there by a person, the
// package itself or a lookup that worked - not by a guess. The online lookup is skipped for such a release.
bool sidecarHoldsATrustedTitle(const fs::path& path, const std::string& titleId) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    std::string existing;
    {
        std::ifstream in(path, std::ios::binary);
        existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto title = readJsonStringField(existing, "title");
    const auto source = readJsonStringField(existing, "titleSource");
    if (!title.has_value() || title->empty() || *title == titleId || !source.has_value()) return false;
    return titleRank(*source) >= 1;
}

struct SidecarResult {
    std::string title;
    std::string region;
    std::string version;
};

// Writes (or refreshes) a small sidecar JSON next to a release's files, and
// returns the title/region that should actually be used - the whole point
// of this file existing is to be a place the user can hand-edit either
// field once (most usefully region, which PS5 doesn't encode in the title
// ID at all - see PkgFileParser::detectRegion) and have it stick.
SidecarResult syncTitleSidecar(const fs::path& directory, const std::string& contentId, const std::string& titleId,
                                const std::string& scannedTitle, const std::string& platform,
                                const std::string& scannedRegion, const std::string& version, long long sizeBytes,
                                size_t fileCount, const std::string& scannedTitleSource) {
    if (contentId.empty()) return {scannedTitle, scannedRegion, version};

    const fs::path sidecarPath = directory / (contentId + ".gth.json");

    TitleChoice title{scannedTitle, scannedTitleSource};
    SidecarField region{scannedRegion, false};
    SidecarField versionField{version, false};
    std::error_code ec;
    if (fs::exists(sidecarPath, ec)) {
        std::string existing;
        {
            std::ifstream in(sidecarPath, std::ios::binary);
            existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        title = resolveSidecarTitle(readJsonStringField(existing, "title"), readJsonStringField(existing, "titleSource"),
                                    titleId, scannedTitle, scannedTitleSource);

        auto existingRegion = readJsonStringField(existing, "region");
        auto existingRegionSource = readJsonStringField(existing, "regionSource");
        if (existingRegion.has_value() && !existingRegionSource.has_value()) {
            // Legacy sidecar from before regionSource existed (or before
            // this fix at all): for a PS5 title its region can only be
            // "Unknown" or this code's own since-corrected "North America /
            // Europe" guess (see PkgFileParser::detectRegion) - never
            // something a real user typed by hand - so treat either the
            // same as a fresh "auto" value rather than locking in a stale,
            // now-known-wrong guess forever.
            const bool looksLikeOldAutoGuess =
                existingRegion->empty() || *existingRegion == "Unknown" ||
                (platform == "PS5" && *existingRegion == "North America / Europe");
            if (looksLikeOldAutoGuess) existingRegionSource = std::string("auto");
        }
        region = resolveSidecarField(existingRegion, existingRegionSource, "Unknown", scannedRegion);

        // Sidecars written before the version was detected for real hold a
        // made-up "1.00" with no versionSource - that value is the
        // placeholder here, so it's replaced by whatever this scan found (or
        // nothing, which is the honest answer) instead of being mistaken for
        // a hand-typed version and locked in forever.
        versionField = resolveSidecarField(readJsonStringField(existing, "version"),
                                            readJsonStringField(existing, "versionSource"), "1.00", version);
    }

    std::ostringstream out;
    out << "{\n"
        << "  \"title\": \"" << jsonEscape(title.value) << "\",\n"
        << "  \"titleSource\": \"" << jsonEscape(title.source) << "\",\n"
        << "  \"titleId\": \"" << jsonEscape(titleId) << "\",\n"
        << "  \"contentId\": \"" << jsonEscape(contentId) << "\",\n"
        << "  \"platform\": \"" << jsonEscape(platform) << "\",\n"
        << "  \"region\": \"" << jsonEscape(region.value) << "\",\n"
        << "  \"regionSource\": \"" << (region.isManual ? "manual" : "auto") << "\",\n"
        << "  \"version\": \"" << jsonEscape(versionField.value) << "\",\n"
        << "  \"versionSource\": \"" << (versionField.isManual ? "manual" : "auto") << "\",\n"
        << "  \"sizeBytes\": " << sizeBytes << ",\n"
        << "  \"fileCount\": " << fileCount << ",\n"
        << "  \"lastScanned\": \"" << jsonEscape(nowIso8601()) << "\",\n"
        << "  \"_note\": \"Edit the title/region/version fields above to correct them, then rescan in Game "
           "Transfer Hub to pick it up. PS5 doesn't encode region in the title ID, so it usually shows as "
           "Unknown unless you set it here; the version is blank when the package and its file name don't "
           "state one - type it in and set its versionSource to manual.\"\n"
        << "}\n";

    std::ofstream file(sidecarPath, std::ios::binary | std::ios::trunc);
    file << out.str();

    return {title.value, region.value, versionField.value};
}

// Sets one flat string field in a sidecar's JSON text, in place: the value is
// swapped when the key is already there (keeping every other field and any
// hand-added ones exactly as they were), appended before the closing brace
// when it isn't. A key only counts when it's followed by a colon, so a title
// that happens to read "version" is never mistaken for the field.
void setJsonStringField(std::string& json, const std::string& key, const std::string& value) {
    const std::string needle = "\"" + key + "\"";
    const std::string quoted = "\"" + jsonEscape(value) + "\"";

    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        size_t cursor = pos + needle.size();
        while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\n' ||
                                        json[cursor] == '\r')) {
            ++cursor;
        }
        if (cursor >= json.size() || json[cursor] != ':') {
            pos += needle.size();
            continue;
        }
        ++cursor;
        while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t')) ++cursor;
        if (cursor >= json.size() || json[cursor] != '"') {
            pos += needle.size();
            continue;
        }
        size_t end = cursor + 1;
        while (end < json.size() && json[end] != '"') end += (json[end] == '\\' && end + 1 < json.size()) ? 2 : 1;
        if (end >= json.size()) break;  // unterminated string - fall through to the append below
        json.replace(cursor, end + 1 - cursor, quoted);
        return;
    }

    const size_t close = json.rfind('}');
    if (close == std::string::npos) {
        json = "{\n  " + needle + ": " + quoted + "\n}\n";
        return;
    }
    size_t last = close;
    while (last > 0 && (json[last - 1] == ' ' || json[last - 1] == '\t' || json[last - 1] == '\n' ||
                        json[last - 1] == '\r')) {
        --last;
    }
    const bool emptyObject = last > 0 && json[last - 1] == '{';
    json.replace(last, close - last, std::string(emptyObject ? "" : ",") + "\n  " + needle + ": " + quoted + "\n");
}

// The trailing "_<N>" part index from a split-part filename (e.g. 3 from
// "..._3.pkg"), or -1 if this isn't a numbered part (a standalone file).
// Parts must be fed to the multi-part reader in this numeric order, not
// filename/lexicographic order - "_10.pkg" sorts before "_2.pkg" as text.
int splitPartIndex(const std::string& relativePath) {
    // The metadata piece ("..._sc.pkg") is the release's last piece, whatever
    // number the last game piece has.
    if (PkgFileParser::isMetadataPieceName(relativePath)) return std::numeric_limits<int>::max();

    static const std::regex partPattern(R"(_(\d+)\.[Pp][Kk][Gg]$)");
    std::smatch match;
    if (!std::regex_search(relativePath, match, partPattern)) return -1;
    try {
        return std::stoi(match[1].str());
    } catch (const std::exception&) {
        return -1;
    }
}

// The key a release's pieces are grouped under: its containing folder plus its
// Content ID (or the file itself when it has none).
std::string versionKeyFor(const PkgHit& hit) {
    const fs::path parentDir = fs::path(hit.relativePath).parent_path();
    return !hit.metadata.contentId.empty() ? (parentDir / hit.metadata.contentId).string() : hit.relativePath;
}

std::vector<ScannedVariant> buildVariantsFromHits(const std::vector<PkgHit>& hits,
                                                    core::ITitleLookupService* titleLookup,
                                                    core::ScanDiagnostics& diagnostics) {
    std::vector<ScannedVariant> results;

    // variantKey (platform|title|region) -> index into results
    std::unordered_map<std::string, size_t> variantIndex;
    // variantKey -> (versionKey -> index into that variant's versions)
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> versionIndex;
    // variantKey -> every hit that fed into it, for the title-enrichment
    // pass below (needs each hit's absolute path and size, which aren't
    // kept on ScannedFile).
    std::unordered_map<std::string, std::vector<const PkgHit*>> hitsByVariantKey;
    // variantKey -> true if its title hasn't been verified from actual
    // embedded metadata (PARAM.SFO/param.json) - it's a title ID or a
    // best-effort label guess either way, so still worth trying to improve
    // (set once, at variant creation - every hit sharing a variantKey has
    // the same title/verification status by construction of the key).
    std::unordered_map<std::string, bool> needsTitleEnrichment;
    // variantKey -> where this scan's title came from (manual | package | online | auto), for the sidecar's
    // titleSource: only a verified name may replace a saved one, and nothing may replace it with a bare title ID.
    std::unordered_map<std::string, std::string> titleOrigin;

    for (const auto& hit : hits) {
        if (hit.metadata.title.empty()) continue;

        const std::string variantKey = hit.metadata.platform + "|" + hit.metadata.title + "|" + hit.metadata.region;
        hitsByVariantKey[variantKey].push_back(&hit);

        auto vIt = variantIndex.find(variantKey);
        size_t variantIdx;
        if (vIt == variantIndex.end()) {
            ScannedVariant variant;
            variant.title = hit.metadata.title;
            variant.platform = hit.metadata.platform;
            variant.region = hit.metadata.region;

            // Must be unique per (StorageId, RelativePath) - the DB enforces
            // that with IX_GameVariants_Storage_Path, and GameIndexerService
            // uses (StorageId, RelativePath) as this variant's stable
            // identity across rescans (see IGameVariantRepository::
            // getByStorageAndPath). The containing folder alone isn't
            // enough: multiple releases sitting directly in the same flat
            // folder (no per-game subfolder, which is normal for a pile of
            // loose .pkg files) would all compute the same "." parent and
            // collide into a single shared row. Disambiguate with the
            // title ID, not the display title: the title ID is fixed for a
            // release, while the display title is explicitly meant to
            // improve over time (humanization, online lookup) - folding in
            // the title instead would change relativePath every time the
            // title gets better, making GameIndexerService treat the
            // improved variant as brand new and silently orphan the old
            // row as "Missing" instead of updating it in place.
            const std::string& disambiguator = !hit.metadata.titleId.empty() ? hit.metadata.titleId : variant.title;
            const fs::path parentDir = fs::path(hit.relativePath).parent_path();
            const fs::path variantContainer = parentDir.empty() ? fs::path(disambiguator) : (parentDir / disambiguator);
            variant.relativePath = variantContainer.string();

            results.push_back(std::move(variant));
            variantIdx = results.size() - 1;
            variantIndex.emplace(variantKey, variantIdx);
            needsTitleEnrichment.emplace(variantKey, !hit.metadata.titleIsVerified);
            titleOrigin.emplace(variantKey, hit.metadata.titleIsVerified ? "package" : "auto");
        } else {
            variantIdx = vIt->second;
        }

        // A release split into multiple .pkg parts (e.g. "..._0.pkg",
        // "..._1.pkg", ...) shares one Content ID across every part - group
        // them into a single version with one file per part, instead of a
        // separate "game" per part. The grouping key is anchored to the
        // containing folder + Content ID so it stays stable regardless of
        // directory-iteration order (which part is seen "first").
        const fs::path parentDir = fs::path(hit.relativePath).parent_path();
        const std::string versionKey = !hit.metadata.contentId.empty()
                                            ? (parentDir / hit.metadata.contentId).string()
                                            : hit.relativePath;

        ScannedFile file;
        file.relativePath = hit.relativePath;
        file.sizeBytes = hit.metadata.sizeBytes;
        file.lastModified = fileLastModified(hit.absolutePath);

        auto& versionsForVariant = versionIndex[variantKey];
        auto verIt = versionsForVariant.find(versionKey);
        if (verIt == versionsForVariant.end()) {
            ScannedVersion version;
            version.version = hit.metadata.version;
            version.relativePath = versionKey;
            version.files.push_back(std::move(file));

            results[variantIdx].versions.push_back(std::move(version));
            versionsForVariant.emplace(versionKey, results[variantIdx].versions.size() - 1);
        } else {
            results[variantIdx].versions[verIt->second].files.push_back(std::move(file));
        }
    }

    // A PS5 release whose title is still just its title ID means the single-
    // file header parse in parseFile() couldn't reach the embedded CNT
    // header (see PkgFileParser::parsePs5) - almost always because the game
    // is split into transfer-sized parts and that data lives past the part
    // being read. Re-attempt across every part of the release as one
    // virtual stream: real, human-readable titles are worth this extra
    // pass, and it's only paid for releases that need it.
    //
    // The same read also settles the version: param.json's contentVersion is
    // the package's own statement of it, which beats a file-name tag and
    // beats leaving it unknown. It is done one release (one Content ID in one
    // folder) at a time, since parts of different releases must never be
    // read as one stream.
    for (const auto& [variantKey, versionsForVariant] : versionIndex) {
        const size_t variantIdx = variantIndex.at(variantKey);
        if (results[variantIdx].platform != "PS5") continue;

        auto hitsIt = hitsByVariantKey.find(variantKey);
        if (hitsIt == hitsByVariantKey.end()) continue;

        for (const auto& [versionKey, versionIdx] : versionsForVariant) {
            auto& version = results[variantIdx].versions[versionIdx];
            if (!needsTitleEnrichment[variantKey] && !version.version.empty()) continue;  // nothing left to learn

            std::vector<std::pair<const PkgHit*, int>> ordered;
            for (const auto* hit : hitsIt->second) {
                if (versionKeyFor(*hit) == versionKey) ordered.emplace_back(hit, splitPartIndex(hit->relativePath));
            }
            if (ordered.size() < 2) continue;  // nothing more to read
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });

            std::vector<std::pair<std::string, long long>> parts;
            parts.reserve(ordered.size());
            for (const auto& [hit, index] : ordered) parts.emplace_back(hit->absolutePath.string(), hit->metadata.sizeBytes);

            // Only take what the read found when it's actually verified (found
            // a real param.json) - parseSplitPs5Parts falls back to the same
            // best-effort label guess we already have when the CNT header is
            // reachable but param.json isn't, and that's not an improvement
            // worth overwriting a possibly-different existing guess with.
            const auto enriched = PkgFileParser::parseSplitPs5Parts(parts);
            if (!enriched || !enriched->titleIsVerified) continue;

            if (needsTitleEnrichment[variantKey] && !enriched->title.empty()) {
                results[variantIdx].title = enriched->title;
                needsTitleEnrichment[variantKey] = false;
                titleOrigin[variantKey] = "package";
            }
            if (version.version.empty() && !enriched->version.empty()) version.version = enriched->version;
        }
    }

    // Last resort for a release no local parsing could name: ask a
    // community title-tracking site (see OnlineTitleLookupService) by
    // title ID. Best-effort and entirely optional - titleLookup is null in
    // every offline/test context, and any failure (no network, no match)
    // just leaves the existing local guess in place. Runs for PS4 and PS5
    // only, since that's what those sites track.
    for (const auto& [variantKey, variantIdx] : variantIndex) {
        if (!needsTitleEnrichment[variantKey]) {
            ++diagnostics.releasesNamedFromFiles;
            continue;
        }
        const auto& platform = results[variantIdx].platform;
        auto hitsIt = hitsByVariantKey.find(variantKey);
        const bool hasTitleId =
            hitsIt != hitsByVariantKey.end() && !hitsIt->second.empty() && !hitsIt->second.front()->metadata.titleId.empty();
        if ((platform != "PS4" && platform != "PS5") || !hasTitleId) {
            ++diagnostics.releasesNotEligible;
            continue;
        }
        if (titleLookup == nullptr) continue;

        const std::string& titleId = hitsIt->second.front()->metadata.titleId;

        // A release an earlier scan already named - from its package, by a lookup that worked, or by hand - keeps that
        // name, and the site isn't asked again. Asking about every game on every scan is what let a slow or
        // unreachable site turn a whole library back into title IDs (the sidecar step below then keeps the saved
        // name instead of the bare ID this scan fell back to).
        const auto& front = *hitsIt->second.front();
        if (!front.metadata.contentId.empty() &&
            sidecarHoldsATrustedTitle(front.absolutePath.parent_path() / (front.metadata.contentId + ".gth.json"), titleId)) {
            ++diagnostics.releasesNamedFromFiles;
            continue;
        }

        ++diagnostics.titleLookupsAttempted;
        if (auto onlineTitle = titleLookup->lookupTitle(platform, titleId);
            onlineTitle.has_value() && !onlineTitle->empty()) {
            results[variantIdx].title = *onlineTitle;
            needsTitleEnrichment[variantKey] = false;
            titleOrigin[variantKey] = "online";
            ++diagnostics.titleLookupsResolved;
        } else if (const std::string failure = titleLookup->lastFailureReason(); !failure.empty()) {
            ++diagnostics.titleLookupsFailed;
            diagnostics.lastTitleLookupFailure = failure;
        } else {
            ++diagnostics.titleLookupsNotFound;
        }
    }

    // Write (or refresh) a small sidecar JSON next to each release's files -
    // a durable, user-editable place to correct the title when nothing this
    // scanner tried could read it from the package itself (see
    // parseSplitPs5Parts's doc comment: some dumps genuinely never included
    // that data). A prior sidecar's title, if the user has set one, takes
    // priority over anything determined above.
    for (const auto& [variantKey, versionsForVariant] : versionIndex) {
        const size_t variantIdx = variantIndex.at(variantKey);
        auto hitsIt = hitsByVariantKey.find(variantKey);
        if (hitsIt == hitsByVariantKey.end() || hitsIt->second.empty()) continue;

        for (const auto& [versionKey, versionIdx] : versionsForVariant) {
            // Any hit belonging to this version carries the same titleId/
            // contentId (that's what versionKey is grouped by); the first
            // one found also gives the containing directory.
            const PkgHit* representative = nullptr;
            for (const auto* hit : hitsIt->second) {
                const fs::path parentDir = fs::path(hit->relativePath).parent_path();
                const std::string thisVersionKey = !hit->metadata.contentId.empty()
                                                        ? (parentDir / hit->metadata.contentId).string()
                                                        : hit->relativePath;
                if (thisVersionKey == versionKey) {
                    representative = hit;
                    break;
                }
            }
            if (!representative || representative->metadata.contentId.empty()) continue;

            auto& version = results[variantIdx].versions[versionIdx];
            long long totalSize = 0;
            for (const auto& f : version.files) totalSize += f.sizeBytes;

            const auto sidecarResult = syncTitleSidecar(
                representative->absolutePath.parent_path(), representative->metadata.contentId,
                representative->metadata.titleId, results[variantIdx].title, results[variantIdx].platform,
                results[variantIdx].region, version.version, totalSize, version.files.size(),
                titleOrigin.contains(variantKey) ? titleOrigin.at(variantKey) : std::string("auto"));

            results[variantIdx].title = sidecarResult.title;
            results[variantIdx].region = sidecarResult.region;
            version.version = sidecarResult.version;
        }
    }

    return results;
}

}  // namespace

std::vector<ScannedVariant> EnhancedStorageScanner::scanForPkgFiles(const std::string& rootPath) {
    std::vector<ScannedVariant> results;

    std::error_code ec;
    const fs::path shortRoot(rootPath);
    if (!fs::exists(shortRoot, ec) || !fs::is_directory(shortRoot, ec)) {
        return results;
    }
    const fs::path root = toLongPath(rootPath);

    const auto opts = fs::directory_options::skip_permission_denied;

    std::vector<PkgHit> hits;
    std::vector<PkgHit> metadataPieces;

    // Recurse one directory at a time, each in its own try/catch, instead of
    // a single recursive_directory_iterator range-for over the whole tree.
    // The range-for's operator++ throws on the first problem entry anywhere
    // in a multi-TB library and aborts the *entire* scan with zero results;
    // isolating each branch means one bad folder only loses that folder.
    const std::function<void(const fs::path&)> scanDir = [&](const fs::path& dir) {
        std::error_code dirEc;
        for (const auto& entry : fs::directory_iterator(dir, opts, dirEc)) {
            std::error_code entryEc;

            // A symlink/junction to a directory must not be followed:
            // directory_iterator's is_directory() follows symlinks (unlike
            // recursive_directory_iterator, which only follows one when
            // follow_directory_symlink is set), so without this check a
            // reparse point that loops back on an ancestor - not uncommon
            // on external/NAS drives - would recurse forever.
            if (entry.is_symlink(entryEc)) continue;

            if (entry.is_directory(entryEc)) {
                try {
                    scanDir(entry.path());
                } catch (const std::exception&) {
                    // Skip this branch, keep scanning its siblings.
                }
                continue;
            }
            if (!entry.is_regular_file(entryEc)) continue;

            std::string ext = entry.path().extension().string();

            // Check if it's a .pkg file
            if (ext != ".pkg" && ext != ".PKG") continue;

            // A PS5 release's metadata piece ("<content id>_sc.pkg") opens with
            // a \x7fCNT header, which parseFile() would take for a PS4 game of
            // its own. It's the last piece of the release its Content ID
            // names, so it's set aside and attached to that release below.
            if (PkgFileParser::isMetadataPieceName(entry.path().filename().string())) {
                const auto pieceSize = static_cast<long long>(entry.file_size(entryEc));
                auto pieceMetadata = PkgFileParser::parseMetadataPieceFileName(entry.path().string(), pieceSize);
                auto pieceRelative = fs::relative(entry.path(), root, ec);
                if (pieceMetadata.has_value() && !ec && !entryEc) {
                    metadataPieces.push_back(PkgHit{std::move(*pieceMetadata), entry.path(), pieceRelative.string()});
                }
                continue;
            }

            // Try to parse the .pkg file
            auto metadata = PkgFileParser::parseFile(entry.path().string());
            if (!metadata.has_value()) continue;

            // Get relative path from root
            auto relativePath = fs::relative(entry.path(), root, ec);
            if (ec) continue;

            hits.push_back(PkgHit{std::move(*metadata), entry.path(), relativePath.string()});
        }
    };

    try {
        scanDir(root);
    } catch (const std::exception&) {
        // Root itself inaccessible - nothing to do.
    }

    // Attach each metadata piece to the release it completes: a piece takes
    // on its sibling's identity (title, region, ...) so it always lands in
    // the same variant and version, and only its own size is its own. One with
    // no other piece of its release beside it isn't a game and is left out.
    std::vector<PkgHit> attached;
    for (auto& piece : metadataPieces) {
        for (const auto& sibling : hits) {
            if (sibling.absolutePath.parent_path() != piece.absolutePath.parent_path()) continue;
            if (sibling.metadata.contentId.empty() || sibling.metadata.contentId != piece.metadata.contentId) continue;

            PkgMetadata metadata = sibling.metadata;
            metadata.sizeBytes = piece.metadata.sizeBytes;
            attached.push_back(PkgHit{std::move(metadata), piece.absolutePath, piece.relativePath});
            break;
        }
    }
    for (auto& piece : attached) hits.push_back(std::move(piece));

    return buildVariantsFromHits(hits, titleLookup_, diagnostics_);
}

bool EnhancedStorageScanner::pinReleaseVersion(const std::string& rootPath, const std::string& versionRelativePath,
                                                const std::string& version, std::string& error) {
    // A release the scanner couldn't read a Content ID for is keyed by its
    // .pkg file name, and no sidecar is ever written for it.
    const fs::path relative(versionRelativePath);
    const std::string leaf = relative.filename().string();
    if (leaf.empty() || relative.extension() == ".pkg" || relative.extension() == ".PKG") {
        error = "This package has no Content ID, so there's no place to save a version for it.";
        return false;
    }

    // The path ends in the Content ID, so the sidecar name is appended to the
    // whole string rather than swapped in as an "extension". \\?\ paths
    // aren't normalized by Windows, so separators have to be backslashes.
    std::string sidecarRelative = versionRelativePath + ".gth.json";
    std::replace(sidecarRelative.begin(), sidecarRelative.end(), '/', '\\');
    const fs::path sidecarPath = toLongPath(rootPath) / sidecarRelative;

    std::string json;
    std::error_code ec;
    if (fs::exists(sidecarPath, ec)) {
        std::ifstream in(sidecarPath, std::ios::binary);
        json.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else if (!fs::is_directory(sidecarPath.parent_path(), ec)) {
        error = "The game's folder isn't reachable right now (" + sidecarPath.parent_path().string() + ").";
        return false;
    }

    setJsonStringField(json, "version", version);
    setJsonStringField(json, "versionSource", version.empty() ? "auto" : "manual");

    std::ofstream file(sidecarPath, std::ios::binary | std::ios::trunc);
    file << json;
    file.flush();
    if (!file) {
        error = "Couldn't write next to the game files (read-only, or no permission): " + sidecarPath.string();
        return false;
    }
    return true;
}

}  // namespace gth::indexing
