#include "gth/network/SonyLinkImport.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace gth::network {

namespace {

// Everything needed sits in the first 64 KB of a metadata piece (entry table, names and
// param.json are all within the first ~20 KB of the real ones).
constexpr size_t kHeadBytes = 64 * 1024;

constexpr uint32_t kMagicCnt = 0x7F434E54;  // "\x7fCNT"
constexpr size_t kCntHeaderSize = 0x5A0;
constexpr size_t kEntrySize = 0x20;
constexpr uint32_t kEntryNamesId = 0x0200;
constexpr uint32_t kMaxEntries = 10000;
constexpr uint32_t kMaxParamJsonBytes = 1024 * 1024;

constexpr int kTransportFailuresBeforeStopping = 3;
constexpr size_t kMetadataPiecesTriedPerTitle = 3;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string uppered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint32_t be32(const std::string& bytes, size_t at) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data()) + at;
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// The characters that end an address in pasted text.
bool endsAddress(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u <= 0x20 || u == 0x7F || c == '"' || c == '\'' || c == '<' || c == '>' || c == ')' || c == ']' || c == '}' || c == ',' ||
           c == ';' || c == '|' || c == '\\';
}

bool isSonyPackageHost(const std::string& lowerHost) { return lowerHost == "sgst.prod.dl.playstation.net" || lowerHost == "gst.prod.dl.playstation.net"; }

// "PPSA02343" out of ".../PPSA02343_00/...", the first place a title ID appears in an address.
std::string titleIdInAddress(const std::string& url) {
    const std::string low = lowered(url);
    for (size_t at = low.find("ppsa"); at != std::string::npos; at = low.find("ppsa", at + 1)) {
        if (at + 9 > low.size() || at == 0 || low[at - 1] != '/') continue;
        bool digits = true;
        for (size_t k = 4; k < 9; ++k) digits = digits && std::isdigit(static_cast<unsigned char>(low[at + k])) != 0;
        if (!digits) continue;
        if (at + 9 < low.size() && std::isdigit(static_cast<unsigned char>(low[at + 9]))) continue;
        return uppered(url.substr(at, 9));
    }
    return {};
}

std::optional<SonyAddress> classify(const std::string& url) {
    const std::string titleId = titleIdInAddress(url);
    if (titleId.empty()) return std::nullopt;

    const std::string low = lowered(url);
    SonyAddress address;
    address.titleId = titleId;
    address.url = url;

    if (endsWith(low, "-version.xml")) {
        address.kind = SonyAddressKind::VersionFile;
        // Sony serves these over HTTPS; a link copied as plain http still leads to the same file.
        if (low.rfind("http://", 0) == 0) address.url = "https://" + url.substr(7);
        return address;
    }
    if (low.find("/app/info/") != std::string::npos) {
        address.kind = SonyAddressKind::MetadataPiece;
        // The manifest and the .crc list sit in the metadata piece's own folder, named after the same Content ID.
        if (endsWith(low, ".json")) {
            address.url = url.substr(0, url.size() - 5) + "_sc.pkg";
        } else if (endsWith(low, ".crc")) {
            address.url = url.substr(0, url.size() - 4) + "_sc.pkg";
        } else if (!endsWith(low, ".pkg")) {
            return std::nullopt;
        }
        return address;
    }
    if (low.find("/app/pkg/") != std::string::npos && endsWith(low, ".pkg")) {
        address.kind = SonyAddressKind::GamePiece;
        return address;
    }
    return std::nullopt;
}

// "value" of `"key": "value"` in flat JSON text; only the escapes Sony's files use are understood.
std::optional<std::string> jsonStringField(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    for (size_t at = json.find(needle); at != std::string::npos; at = json.find(needle, at + 1)) {
        size_t i = at + needle.size();
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != ':') continue;
        ++i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != '"') continue;
        ++i;

        std::string value;
        for (; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] == '\\' && i + 1 < json.size() && json[i + 1] == '/') {
                value += '/';
                ++i;
            } else {
                value += json[i];
            }
        }
        if (i >= json.size()) return std::nullopt;  // unterminated

        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::nullopt;
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }
    return std::nullopt;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

std::vector<SonyAddress> ExtractSonyAddresses(const std::string& text) {
    std::vector<SonyAddress> found;
    std::set<std::pair<int, std::string>> seen;
    const std::string low = lowered(text);

    size_t at = 0;
    while ((at = low.find("http", at)) != std::string::npos) {
        size_t schemeLength = 0;
        if (low.compare(at, 7, "http://") == 0) schemeLength = 7;
        else if (low.compare(at, 8, "https://") == 0) schemeLength = 8;
        if (schemeLength == 0) {
            ++at;
            continue;
        }

        const size_t hostStart = at + schemeLength;
        const size_t hostEnd = low.find('/', hostStart);
        if (hostEnd == std::string::npos || !isSonyPackageHost(low.substr(hostStart, hostEnd - hostStart))) {
            at = hostStart;
            continue;
        }

        size_t end = hostEnd;
        while (end < text.size() && !endsAddress(text[end])) ++end;
        std::string url = text.substr(at, end - at);
        while (!url.empty() && (url.back() == '.' || url.back() == ':')) url.pop_back();
        at = end;

        if (auto address = classify(url)) {
            if (seen.emplace(static_cast<int>(address->kind), address->url).second) found.push_back(std::move(*address));
        }
    }
    return found;
}

std::optional<std::string> ReadParamJsonFromMetadataPiece(const std::string& firstBytes) {
    if (firstBytes.size() < kCntHeaderSize || be32(firstBytes, 0) != kMagicCnt) return std::nullopt;

    const uint32_t count = be32(firstBytes, 0x10);
    const uint32_t table = be32(firstBytes, 0x18);
    if (count == 0 || count > kMaxEntries || table < kCntHeaderSize) return std::nullopt;
    if (static_cast<uint64_t>(table) + static_cast<uint64_t>(count) * kEntrySize > firstBytes.size()) return std::nullopt;

    struct Entry {
        uint32_t id, nameOffset, offset, size;
    };
    std::vector<Entry> entries;
    entries.reserve(count);
    std::optional<Entry> names;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = table + static_cast<size_t>(i) * kEntrySize;
        const Entry entry{be32(firstBytes, at), be32(firstBytes, at + 4), be32(firstBytes, at + 16), be32(firstBytes, at + 20)};
        if (static_cast<uint64_t>(entry.offset) + entry.size > firstBytes.size()) continue;  // beyond what was read
        if (entry.id == kEntryNamesId) names = entry;
        entries.push_back(entry);
    }
    if (!names || names->size == 0) return std::nullopt;

    for (const auto& entry : entries) {
        if (entry.nameOffset == 0 || entry.nameOffset >= names->size) continue;
        const size_t nameStart = names->offset + entry.nameOffset;
        const size_t nameEnd = firstBytes.find('\0', nameStart);
        if (nameEnd == std::string::npos || nameEnd > static_cast<size_t>(names->offset) + names->size) continue;

        const std::string name = lowered(firstBytes.substr(nameStart, nameEnd - nameStart));
        // The package's own param.json - not the origin-/target- copies kept for the versions an update goes between.
        if (name != "param.json" && !endsWith(name, "/param.json")) continue;
        if (entry.size == 0 || entry.size > kMaxParamJsonBytes) return std::nullopt;
        return firstBytes.substr(entry.offset, entry.size);
    }
    return std::nullopt;
}

std::optional<std::string> VersionFileUriFromParamJson(const std::string& paramJson) { return jsonStringField(paramJson, "versionFileUri"); }

std::optional<std::string> TitleIdFromParamJson(const std::string& paramJson) { return jsonStringField(paramJson, "titleId"); }

bool IsSonyVersionFileUrl(const std::string& url, const std::string& titleId) {
    if (titleId.empty()) return false;
    const std::string low = lowered(url);
    if (low.rfind("https://sgst.prod.dl.playstation.net/", 0) != 0) return false;
    if (!endsWith(low, "-version.xml")) return false;
    if (low.find(lowered(titleId)) == std::string::npos) return false;
    if (low.find("..") != std::string::npos) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7F || c == '"' || c == '\\'; });
}

SonyLinkImporter::SonyLinkImporter(std::filesystem::path linksFile, std::vector<std::filesystem::path> knownFiles, SonyHeadFetcher fetcher)
    : linksFile_(std::move(linksFile)), knownFiles_(std::move(knownFiles)), fetcher_(std::move(fetcher)) {}

SonyLinkImportResult SonyLinkImporter::import(const std::string& text, const std::function<bool()>& shouldCancel,
                                              const std::function<void(const std::string&)>& onProgress) {
    SonyLinkImportResult result;
    const auto addresses = ExtractSonyAddresses(text);
    result.addressesFound = addresses.size();

    struct Group {
        std::string directLink;
        std::vector<std::string> metadataPieces;
        bool sawGamePiece = false;
    };
    std::map<std::string, Group> groups;
    std::vector<std::string> order;
    for (const auto& address : addresses) {
        if (!groups.contains(address.titleId)) order.push_back(address.titleId);
        Group& group = groups[address.titleId];
        switch (address.kind) {
            case SonyAddressKind::VersionFile: group.directLink = address.url; break;
            case SonyAddressKind::MetadataPiece: group.metadataPieces.push_back(address.url); break;
            case SonyAddressKind::GamePiece: group.sawGamePiece = true; break;
        }
    }

    std::map<std::string, std::string> known;
    auto readKnown = [&known](const std::filesystem::path& file) {
        for (const auto& [id, link] : ParseSonyVersionLinks(readTextFile(file))) known[id] = link;
    };
    for (const auto& file : knownFiles_) readKnown(file);
    readKnown(linksFile_);

    std::string learnedLines;
    int transportFailures = 0;
    size_t position = 0;
    for (const auto& titleId : order) {
        ++position;
        if (shouldCancel && shouldCancel()) {
            result.cancelled = true;
            break;
        }
        const Group& group = groups[titleId];

        std::string link;
        std::string why;
        if (!group.directLink.empty()) {
            if (IsSonyVersionFileUrl(group.directLink, titleId)) link = group.directLink;
            else why = "that isn't a Sony version file link for this title";
        } else if (known.contains(titleId)) {
            ++result.alreadyKnown;
            continue;
        } else if (group.metadataPieces.empty()) {
            // Only the game's own pieces were pasted - they're in a different Sony folder from the metadata piece,
            // and nothing in them names it. The page that lists the pieces also has an SC (and a CRC) link.
            ++result.gamePiecesOnly;
            result.notes.push_back(titleId + ": only game-piece addresses were given. Copy the SC link (or the CRC / manifest link) as well - "
                                             "the piece addresses can't lead to the version file.");
            continue;
        } else {
            if (onProgress) onProgress("Reading " + titleId + " (" + std::to_string(position) + " of " + std::to_string(order.size()) + ")...");
            const size_t tries = std::min(group.metadataPieces.size(), kMetadataPiecesTriedPerTitle);
            for (size_t i = 0; i < tries && link.empty(); ++i) {
                const auto response = fetcher_(group.metadataPieces[i], kHeadBytes);
                if (response.status == 0) {
                    ++transportFailures;
                    why = "no answer from Sony (" + response.failure + ")";
                    continue;
                }
                transportFailures = 0;
                if (response.status != 200 && response.status != 206) {
                    why = "Sony answered HTTP " + std::to_string(response.status) + " for that address - it may have expired, or it isn't a metadata piece";
                    continue;
                }
                const auto paramJson = ReadParamJsonFromMetadataPiece(response.body);
                if (!paramJson) {
                    why = "that address isn't a metadata piece (no param.json in its first 64 KB)";
                    continue;
                }
                const auto paramTitle = TitleIdFromParamJson(*paramJson);
                if (paramTitle && uppered(*paramTitle) != titleId) {
                    why = "its param.json is for " + *paramTitle + ", not " + titleId;
                    continue;
                }
                const auto uri = VersionFileUriFromParamJson(*paramJson);
                if (!uri) {
                    why = "its param.json has no versionFileUri";
                    continue;
                }
                if (!IsSonyVersionFileUrl(*uri, titleId)) {
                    why = "its versionFileUri isn't a Sony version file link for this title";
                    continue;
                }
                link = *uri;
            }
        }

        if (link.empty()) {
            ++result.failed;
            result.notes.push_back(titleId + ": " + why);
            if (transportFailures >= kTransportFailuresBeforeStopping) {
                result.notes.push_back("Stopped early - Sony isn't answering (no internet, or a firewall is blocking the app).");
                break;
            }
            continue;
        }

        if (const auto existing = known.find(titleId); existing != known.end() && existing->second == link) {
            ++result.alreadyKnown;
            continue;
        }
        known[titleId] = link;
        learnedLines += titleId + "\t" + link + "\n";
        result.titleIdsLearned.push_back(titleId);
    }

    if (!learnedLines.empty()) {
        const auto merged = MergeSonyVersionLinks(linksFile_, learnedLines);
        if (merged.written) {
            result.learned = result.titleIdsLearned.size();
        } else {
            result.saveFailed = true;
            result.titleIdsLearned.clear();
        }
    }
    return result;
}

LocalLinkLearnResult LearnLinksFromLocalMetadata(const std::vector<std::pair<std::string, std::string>>& pieces,
                                                 const std::filesystem::path& linksFile,
                                                 const std::vector<std::filesystem::path>& knownFiles) {
    LocalLinkLearnResult result;

    std::map<std::string, std::string> known;
    for (const auto& file : knownFiles) {
        for (const auto& [id, link] : ParseSonyVersionLinks(readTextFile(file))) known[id] = link;
    }
    for (const auto& [id, link] : ParseSonyVersionLinks(readTextFile(linksFile))) known[id] = link;

    std::string learnedLines;
    for (const auto& [rawTitleId, path] : pieces) {
        const std::string titleId = uppered(rawTitleId);
        if (known.contains(titleId)) {
            ++result.alreadyKnown;
            continue;
        }

        ++result.looked;
        std::string head(kHeadBytes, '\0');
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                ++result.unreadable;
                result.notes.push_back(titleId + ": couldn't read " + path);
                continue;
            }
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<size_t>(in.gcount()));
        }

        // The same checks a link copied from a web page goes through: it has to be this title's own param.json
        // naming a real Sony version file for this title.
        const auto paramJson = ReadParamJsonFromMetadataPiece(head);
        if (!paramJson) {
            ++result.unreadable;
            result.notes.push_back(titleId + ": " + path + " has no param.json in its first 64 KB");
            continue;
        }
        const auto paramTitle = TitleIdFromParamJson(*paramJson);
        if (paramTitle && uppered(*paramTitle) != titleId) {
            ++result.unreadable;
            result.notes.push_back(titleId + ": its param.json is for " + *paramTitle);
            continue;
        }
        const auto uri = VersionFileUriFromParamJson(*paramJson);
        if (!uri || !IsSonyVersionFileUrl(*uri, titleId)) {
            ++result.unreadable;
            result.notes.push_back(titleId + ": its param.json has no usable versionFileUri");
            continue;
        }

        known[titleId] = *uri;
        learnedLines += titleId + "\t" + *uri + "\n";
        result.titleIdsLearned.push_back(titleId);
    }

    if (!learnedLines.empty()) {
        const auto merged = MergeSonyVersionLinks(linksFile, learnedLines);
        if (merged.written) {
            result.learned = result.titleIdsLearned.size();
        } else {
            result.saveFailed = true;
            result.titleIdsLearned.clear();
        }
    }
    return result;
}

}  // namespace gth::network
