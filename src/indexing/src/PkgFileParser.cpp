#include "gth/indexing/PkgFileParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <regex>

namespace gth::indexing {

namespace {

// Presents an ordered set of files (a package's split parts, each a
// contiguous slice of the original unsplit file) as one virtual stream, so
// PkgFileParser's format-parsing logic can read an absolute offset without
// caring which physical part it actually falls in. Opens a part fresh for
// each read rather than holding N file handles open - reads here are few
// (FIH header, CNT header, entry table, names, param.json) and small
// relative to part sizes, so the reopen cost is negligible.
class MultiPartReader {
public:
    explicit MultiPartReader(std::vector<std::pair<std::string, long long>> parts) : parts_(std::move(parts)) {
        long long offset = 0;
        partStart_.reserve(parts_.size());
        for (const auto& [path, size] : parts_) {
            partStart_.push_back(offset);
            offset += size;
        }
        totalSize_ = offset;
    }

    long long totalSize() const { return totalSize_; }

    bool readAt(long long virtualOffset, unsigned char* buffer, size_t size) {
        if (virtualOffset < 0 || virtualOffset + static_cast<long long>(size) > totalSize_) return false;

        long long curOffset = virtualOffset;
        size_t bufOffset = 0;
        long long remaining = static_cast<long long>(size);

        while (remaining > 0) {
            const size_t partIdx = findPart(curOffset);
            if (partIdx >= parts_.size()) return false;

            const long long offsetInPart = curOffset - partStart_[partIdx];
            const long long availableInPart = parts_[partIdx].second - offsetInPart;
            const long long toRead = std::min(remaining, availableInPart);
            if (toRead <= 0) return false;

            std::ifstream part(parts_[partIdx].first, std::ios::binary);
            if (!part.is_open()) return false;
            part.seekg(static_cast<std::streamoff>(offsetInPart));
            part.read(reinterpret_cast<char*>(buffer + bufOffset), static_cast<std::streamsize>(toRead));
            if (part.gcount() != toRead) return false;

            bufOffset += static_cast<size_t>(toRead);
            curOffset += toRead;
            remaining -= toRead;
        }
        return true;
    }

private:
    size_t findPart(long long offset) const {
        for (size_t i = 0; i < parts_.size(); ++i) {
            if (offset >= partStart_[i] && offset < partStart_[i] + parts_[i].second) return i;
        }
        return parts_.size();
    }

    std::vector<std::pair<std::string, long long>> parts_;
    std::vector<long long> partStart_;
    long long totalSize_ = 0;
};

}  // namespace

namespace {

constexpr uint32_t kMagicPs3 = 0x7F504B47;  // "\x7fPKG"
constexpr uint32_t kMagicPs4Cnt = 0x7F434E54;  // "\x7fCNT"
constexpr uint32_t kMagicPs5Fih = 0x7F464948;  // "\x7fFIH"

constexpr size_t kPs4HeaderSize = 0xA0;
constexpr size_t kPs4EntrySize = 0x20;
constexpr uint32_t kPs4ParamSfoId = 0x1000;
constexpr uint32_t kEntryNamesId = 0x0200;
// PS4 package entry IDs are fixed (unlike PS5's, resolved by name below) - cross-checked against two
// independent public references (SeregonWar/PkgToolBox's pkg_entry.py and maxton/LibOrbisPkg's
// PKG/Enums.cs), which agree exactly on this value.
constexpr uint32_t kIcon0PngId = 0x1200;

constexpr size_t kPs5FihHeaderSize = 0x10000;
constexpr size_t kPs5CntHeaderSize = 0x5A0;
constexpr size_t kPs5EntrySize = 0x20;

constexpr uint32_t kMaxEntryCount = 10000;
constexpr size_t kMaxMetadataSize = 4 * 1024 * 1024;  // param.sfo/param.json are always a few KB
constexpr size_t kMaxPosterSize = 4 * 1024 * 1024;  // ICON0.PNG is typically well under 500 KB

uint32_t be32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t be64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

uint32_t le32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) | p[0];
}

uint64_t le64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

bool readExact(std::ifstream& file, long long offset, unsigned char* buffer, size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));
    if (!file.good()) return false;
    file.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<size_t>(file.gcount()) == size;
}

bool readExact(std::ifstream& file, long long offset, std::string& out, size_t size) {
    out.resize(size);
    return readExact(file, offset, reinterpret_cast<unsigned char*>(out.data()), size);
}

std::string decodeCString(const unsigned char* data, size_t len) {
    size_t end = 0;
    while (end < len && data[end] != 0) ++end;
    std::string s(reinterpret_cast<const char*>(data), end);
    while (!s.empty() && static_cast<unsigned char>(s.back()) < 0x20) s.pop_back();
    return s;
}

// "EP0006-PPSA02343_00-NUTSEU0000000000" -> "PPSA02343"
std::string titleIdFromContentIdImpl(const std::string& contentId) {
    const auto dash = contentId.find('-');
    if (dash == std::string::npos) return {};
    const auto rest = contentId.substr(dash + 1);
    const auto underscore = rest.find('_');
    std::string titleId = (underscore == std::string::npos) ? rest : rest.substr(0, underscore);
    std::transform(titleId.begin(), titleId.end(), titleId.begin(), [](unsigned char c) { return std::toupper(c); });
    return titleId;
}

// "EP0006-PPSA02343_00-NUTSEU0000000000" -> "NUTSEU0000000000"
std::string labelFromContentIdImpl(const std::string& contentId) {
    const auto lastDash = contentId.find_last_of('-');
    if (lastDash == std::string::npos) return {};
    return contentId.substr(lastDash + 1);
}

}  // namespace

std::optional<PkgMetadata> PkgFileParser::parseFile(const std::string& pkgFilePath) {
    std::ifstream file(pkgFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    const long long fileSize = file.tellg();
    if (fileSize < 4) return parseFromFilename(pkgFilePath, fileSize);

    std::array<unsigned char, 4> magicBuf;
    if (!readExact(file, 0, magicBuf.data(), magicBuf.size())) return parseFromFilename(pkgFilePath, fileSize);
    const uint32_t magic = be32(magicBuf.data());

    std::optional<PkgMetadata> metadata;
    if (magic == kMagicPs3) {
        metadata = parsePs3(file, fileSize);
    } else if (magic == kMagicPs4Cnt) {
        metadata = parsePs4(file, fileSize);
    } else if (magic == kMagicPs5Fih) {
        metadata = parsePs5(file, fileSize);
    }

    // Either an unrecognized magic, or a recognized container whose real
    // metadata isn't reachable in this file (most commonly: a split-part
    // PS5 package - see parsePs5). The filename still carries a usable
    // Content ID for every part in that case.
    if (!metadata.has_value()) metadata = parseFromFilename(pkgFilePath, fileSize);

    // The package's own metadata is the authority on its version; a version
    // tag in the file name is only a fallback when that isn't reachable.
    if (metadata.has_value() && metadata->version.empty()) {
        metadata->version = versionFromFilename(std::filesystem::path(pkgFilePath).filename().string());
    }
    return metadata;
}

bool PkgFileParser::isMetadataPieceName(const std::string& fileName) {
    static constexpr char kSuffix[] = "_sc.pkg";
    constexpr size_t kLength = sizeof(kSuffix) - 1;
    if (fileName.size() <= kLength) return false;
    return std::equal(fileName.end() - kLength, fileName.end(), kSuffix,
                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

std::optional<PkgMetadata> PkgFileParser::parseMetadataPieceFileName(const std::string& pkgFilePath,
                                                                       long long fileSize) {
    return parseFromFilename(pkgFilePath, fileSize);
}

std::string PkgFileParser::versionFromFilename(const std::string& filename) {
    // Update packages are conventionally named "<content id>-A0110-V0100":
    // A#### is the app (patch) version - 01.10 - and V#### the master version.
    // The app version is the one a console shows as the game's version.
    static const std::regex versionTag(R"(-A(\d{2})(\d{2})-V\d{4})", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(filename, match, versionTag)) return {};
    return match[1].str() + "." + match[2].str();
}

std::optional<PkgMetadata> PkgFileParser::parsePs3(std::ifstream& file, long long fileSize) {
    constexpr size_t kContentIdOffset = 0x30;
    constexpr size_t kContentIdSize = 0x30;
    if (fileSize < static_cast<long long>(kContentIdOffset + kContentIdSize)) return std::nullopt;

    std::array<unsigned char, kContentIdSize> buf;
    if (!readExact(file, kContentIdOffset, buf.data(), buf.size())) return std::nullopt;
    const std::string contentId = decodeCString(buf.data(), buf.size());
    if (contentId.empty()) return std::nullopt;

    PkgMetadata metadata;
    metadata.sizeBytes = fileSize;
    metadata.contentId = contentId;
    metadata.titleId = titleIdFromContentIdImpl(contentId);
    metadata.title = bestEffortTitle(contentId, metadata.titleId);
    metadata.platform = "PS3";
    metadata.region = detectRegion(metadata.contentId);
    // Version stays empty (unknown) unless the package or its file name says
    // otherwise - a made-up default would be indistinguishable from a real one.
    return metadata;
}

std::optional<PkgMetadata> PkgFileParser::parsePs4(std::ifstream& file, long long fileSize) {
    if (fileSize < static_cast<long long>(kPs4HeaderSize)) return std::nullopt;

    std::array<unsigned char, kPs4HeaderSize> header;
    if (!readExact(file, 0, header.data(), header.size())) return std::nullopt;

    const uint32_t entryCount = be32(header.data() + 0x10);
    const uint32_t tableOffset = be32(header.data() + 0x18);
    if (entryCount == 0 || entryCount > kMaxEntryCount) return std::nullopt;
    if (tableOffset < kPs4HeaderSize) return std::nullopt;
    const long long tableEnd =
        static_cast<long long>(tableOffset) + static_cast<long long>(entryCount) * kPs4EntrySize;
    if (tableEnd > fileSize) return std::nullopt;

    const std::string contentId = decodeCString(header.data() + 0x40, 36);

    PkgMetadata metadata;
    metadata.sizeBytes = fileSize;
    metadata.contentId = contentId;
    metadata.titleId = titleIdFromContentIdImpl(contentId);
    metadata.title = bestEffortTitle(contentId, metadata.titleId);
    metadata.platform = "PS4";
    // Version stays empty (unknown) unless the package or its file name says
    // otherwise - a made-up default would be indistinguishable from a real one.

    // Walk the entry table looking for PARAM_SFO_ID (0x1000) - the real
    // human-readable title lives there, not in the opaque content ID.
    std::array<unsigned char, kPs4EntrySize> entry;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const long long entryOffset = static_cast<long long>(tableOffset) + static_cast<long long>(i) * kPs4EntrySize;
        if (!readExact(file, entryOffset, entry.data(), entry.size())) break;

        const uint32_t fileId = be32(entry.data() + 0);
        if (fileId != kPs4ParamSfoId) continue;

        const uint32_t dataOffset = be32(entry.data() + 16);
        const uint32_t size = be32(entry.data() + 20);
        if (size == 0 || size > kMaxMetadataSize) break;
        if (static_cast<long long>(dataOffset) + size > fileSize) break;

        std::string sfoData;
        if (!readExact(file, dataOffset, sfoData, size)) break;

        if (auto entries = parseSfo(sfoData); entries.has_value()) {
            // APP_VER is the patch level a console shows as the game's version
            // (01.05); VERSION is the content/master version and only a
            // fallback for packages that lack APP_VER.
            std::string sfoVersion;
            for (const auto& [key, value] : *entries) {
                if (key == "TITLE_ID" && !value.empty()) {
                    metadata.titleId = value;
                } else if ((key == "TITLE" || key == "TITLE_00") && !value.empty()) {
                    metadata.title = value;
                    metadata.titleIsVerified = true;
                } else if (key == "APP_VER" && !value.empty()) {
                    metadata.version = value;
                } else if (key == "VERSION" && !value.empty()) {
                    sfoVersion = value;
                }
            }
            if (metadata.version.empty()) metadata.version = sfoVersion;
        }
        break;
    }

    metadata.region = detectRegion(metadata.contentId);
    return metadata;
}

std::optional<PkgMetadata> PkgFileParser::parsePs5(std::ifstream& file, long long fileSize) {
    ReadAtFn readAt = [&file](long long offset, unsigned char* buffer, size_t size) {
        return readExact(file, offset, buffer, size);
    };
    return parsePs5Core(readAt, fileSize);
}

std::optional<PkgMetadata> PkgFileParser::parseSplitPs5Parts(
    const std::vector<std::pair<std::string, long long>>& orderedParts) {
    if (orderedParts.empty()) return std::nullopt;

    auto reader = std::make_shared<MultiPartReader>(orderedParts);
    ReadAtFn readAt = [reader](long long offset, unsigned char* buffer, size_t size) {
        return reader->readAt(offset, buffer, size);
    };
    return parsePs5Core(readAt, reader->totalSize());
}

std::optional<PkgMetadata> PkgFileParser::parsePs5Core(const ReadAtFn& readAt, long long totalSize) {
    if (totalSize < 0x100) return std::nullopt;

    std::array<unsigned char, 0x60> fih;
    if (!readAt(0, fih.data(), fih.size())) return std::nullopt;
    if (be32(fih.data()) != kMagicPs5Fih) return std::nullopt;

    const uint64_t pfsOffset = le64(fih.data() + 0x10);
    if (pfsOffset != kPs5FihHeaderSize) return std::nullopt;  // not a finalized PS5 image we understand
    const uint64_t sc_offset = le64(fih.data() + 0x58);

    // The embedded CNT header (and everything under it - entry table,
    // param.json) sits *after* the encrypted game image, which is normally
    // the bulk of the package. For a single unsplit file, that's almost
    // always beyond the part being parsed here; parseSplitPs5Parts() reads
    // across every part of the original file precisely to still reach it.
    if (sc_offset > static_cast<uint64_t>(totalSize) ||
        static_cast<uint64_t>(totalSize) - sc_offset < kPs5CntHeaderSize) {
        return std::nullopt;
    }

    std::array<unsigned char, kPs5CntHeaderSize> cnt;
    if (!readAt(static_cast<long long>(sc_offset), cnt.data(), cnt.size())) return std::nullopt;
    if (be32(cnt.data()) != kMagicPs4Cnt) return std::nullopt;  // embedded CNT uses the same \x7fCNT magic

    const uint32_t entryCount = be32(cnt.data() + 0x10);
    const uint32_t entryTableRelative = be32(cnt.data() + 0x18);
    if (entryCount == 0 || entryCount > kMaxEntryCount) return std::nullopt;
    const long long entryTableOffset = static_cast<long long>(sc_offset) + entryTableRelative;
    const long long entryTableEnd = entryTableOffset + static_cast<long long>(entryCount) * kPs5EntrySize;
    if (entryTableRelative < kPs5CntHeaderSize || entryTableEnd > totalSize) return std::nullopt;

    const std::string contentId = decodeCString(cnt.data() + 0x40, 48);

    PkgMetadata metadata;
    metadata.sizeBytes = totalSize;
    metadata.contentId = contentId;
    metadata.titleId = titleIdFromContentIdImpl(contentId);
    metadata.title = bestEffortTitle(contentId, metadata.titleId);
    metadata.platform = "PS5";
    // Version stays empty (unknown) unless the package or its file name says
    // otherwise - a made-up default would be indistinguishable from a real one.

    // Find "param.json" by name (PS5 entries have no fixed ID for it, unlike
    // PS4's PARAM_SFO_ID) - two passes: collect entries, then resolve names
    // from the ENTRY_NAMES table so we can match on the actual filename.
    struct Entry {
        uint32_t nameOffset;
        long long dataOffset;
        uint32_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(entryCount);
    long long namesOffset = -1;
    uint32_t namesSize = 0;

    std::array<unsigned char, kPs5EntrySize> entry;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (!readAt(entryTableOffset + static_cast<long long>(i) * kPs5EntrySize, entry.data(), entry.size())) {
            break;
        }
        const uint32_t fileId = be32(entry.data() + 0);
        const uint32_t nameOffset = be32(entry.data() + 4);
        const uint32_t relativeOffset = be32(entry.data() + 16);
        const uint32_t size = be32(entry.data() + 20);
        const long long absoluteOffset = static_cast<long long>(sc_offset) + relativeOffset;
        if (absoluteOffset < static_cast<long long>(sc_offset) || absoluteOffset + size > totalSize) continue;

        if (fileId == kEntryNamesId) {
            namesOffset = absoluteOffset;
            namesSize = size;
        }
        entries.push_back({nameOffset, absoluteOffset, size});
    }

    if (namesOffset >= 0 && namesSize > 0 && namesSize <= kMaxMetadataSize) {
        std::string names;
        names.resize(namesSize);
        if (readAt(namesOffset, reinterpret_cast<unsigned char*>(names.data()), namesSize)) {
            for (const auto& e : entries) {
                if (e.nameOffset == 0 || e.nameOffset >= names.size()) continue;
                const auto end = names.find('\0', e.nameOffset);
                if (end == std::string::npos) continue;
                std::string name = names.substr(e.nameOffset, end - e.nameOffset);
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                                [](unsigned char c) { return std::tolower(c); });
                // The package's own param.json - not the "origin-param.json"
                // / "target-param.json" copies a metadata block also carries
                // for the versions an update goes from and to.
                if (lower != "param.json" && (lower.size() < 11 || lower.compare(lower.size() - 11, 11, "/param.json") != 0)) {
                    continue;
                }

                if (e.size == 0 || e.size > kMaxMetadataSize) break;
                std::string json;
                json.resize(e.size);
                if (!readAt(e.dataOffset, reinterpret_cast<unsigned char*>(json.data()), e.size)) break;

                if (auto titleId = extractJsonStringField(json, "titleId");
                    titleId.has_value() && !titleId->empty()) {
                    metadata.titleId = *titleId;
                }
                if (auto contentIdJson = extractJsonStringField(json, "contentId");
                    contentIdJson.has_value() && !contentIdJson->empty()) {
                    metadata.contentId = *contentIdJson;
                }
                if (auto titleName = extractJsonStringField(json, "titleName");
                    titleName.has_value() && !titleName->empty()) {
                    metadata.title = *titleName;
                    metadata.titleIsVerified = true;
                }
                if (auto contentVersion = extractJsonStringField(json, "contentVersion");
                    contentVersion.has_value() && !contentVersion->empty()) {
                    metadata.version = *contentVersion;
                } else if (auto masterVersion = extractJsonStringField(json, "masterVersion");
                           masterVersion.has_value() && !masterVersion->empty()) {
                    metadata.version = *masterVersion;
                }
                break;
            }
        }
    }

    metadata.region = detectRegion(metadata.contentId);
    return metadata;
}

std::optional<std::string> PkgFileParser::extractPoster(const std::string& pkgFilePath) {
    std::ifstream file(pkgFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    const long long fileSize = file.tellg();
    if (fileSize < 4) return std::nullopt;

    std::array<unsigned char, 4> magicBuf;
    if (!readExact(file, 0, magicBuf.data(), magicBuf.size())) return std::nullopt;
    const uint32_t magic = be32(magicBuf.data());

    if (magic == kMagicPs4Cnt) return extractPs4Poster(file, fileSize);
    if (magic == kMagicPs5Fih) {
        ReadAtFn readAt = [&file](long long offset, unsigned char* buffer, size_t size) {
            return readExact(file, offset, buffer, size);
        };
        return extractPs5PosterCore(readAt, fileSize);
    }
    return std::nullopt;  // PS3, or a magic this doesn't recognize at all
}

std::optional<std::string> PkgFileParser::extractPosterFromParts(
    const std::vector<std::pair<std::string, long long>>& orderedParts) {
    if (orderedParts.empty()) return std::nullopt;

    auto reader = std::make_shared<MultiPartReader>(orderedParts);
    ReadAtFn readAt = [reader](long long offset, unsigned char* buffer, size_t size) {
        return reader->readAt(offset, buffer, size);
    };
    return extractPs5PosterCore(readAt, reader->totalSize());
}

std::optional<std::string> PkgFileParser::extractPs4Poster(std::ifstream& file, long long fileSize) {
    if (fileSize < static_cast<long long>(kPs4HeaderSize)) return std::nullopt;

    std::array<unsigned char, kPs4HeaderSize> header;
    if (!readExact(file, 0, header.data(), header.size())) return std::nullopt;

    const uint32_t entryCount = be32(header.data() + 0x10);
    const uint32_t tableOffset = be32(header.data() + 0x18);
    if (entryCount == 0 || entryCount > kMaxEntryCount) return std::nullopt;
    if (tableOffset < kPs4HeaderSize) return std::nullopt;
    const long long tableEnd =
        static_cast<long long>(tableOffset) + static_cast<long long>(entryCount) * kPs4EntrySize;
    if (tableEnd > fileSize) return std::nullopt;

    std::array<unsigned char, kPs4EntrySize> entry;
    for (uint32_t i = 0; i < entryCount; ++i) {
        const long long entryOffset = static_cast<long long>(tableOffset) + static_cast<long long>(i) * kPs4EntrySize;
        if (!readExact(file, entryOffset, entry.data(), entry.size())) break;

        const uint32_t fileId = be32(entry.data() + 0);
        if (fileId != kIcon0PngId) continue;

        const uint32_t dataOffset = be32(entry.data() + 16);
        const uint32_t size = be32(entry.data() + 20);
        if (size == 0 || size > kMaxPosterSize) return std::nullopt;
        if (static_cast<long long>(dataOffset) + size > fileSize) return std::nullopt;

        std::string data;
        if (!readExact(file, dataOffset, data, size)) return std::nullopt;
        return data;
    }
    return std::nullopt;
}

std::optional<std::string> PkgFileParser::extractPs5PosterCore(const ReadAtFn& readAt, long long totalSize) {
    if (totalSize < 0x100) return std::nullopt;

    std::array<unsigned char, 0x60> fih;
    if (!readAt(0, fih.data(), fih.size())) return std::nullopt;
    if (be32(fih.data()) != kMagicPs5Fih) return std::nullopt;

    const uint64_t pfsOffset = le64(fih.data() + 0x10);
    if (pfsOffset != kPs5FihHeaderSize) return std::nullopt;
    const uint64_t sc_offset = le64(fih.data() + 0x58);
    if (sc_offset > static_cast<uint64_t>(totalSize) ||
        static_cast<uint64_t>(totalSize) - sc_offset < kPs5CntHeaderSize) {
        return std::nullopt;
    }

    std::array<unsigned char, kPs5CntHeaderSize> cnt;
    if (!readAt(static_cast<long long>(sc_offset), cnt.data(), cnt.size())) return std::nullopt;
    if (be32(cnt.data()) != kMagicPs4Cnt) return std::nullopt;

    const uint32_t entryCount = be32(cnt.data() + 0x10);
    const uint32_t entryTableRelative = be32(cnt.data() + 0x18);
    if (entryCount == 0 || entryCount > kMaxEntryCount) return std::nullopt;
    const long long entryTableOffset = static_cast<long long>(sc_offset) + entryTableRelative;
    const long long entryTableEnd = entryTableOffset + static_cast<long long>(entryCount) * kPs5EntrySize;
    if (entryTableRelative < kPs5CntHeaderSize || entryTableEnd > totalSize) return std::nullopt;

    struct Entry {
        uint32_t nameOffset;
        long long dataOffset;
        uint32_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(entryCount);
    long long namesOffset = -1;
    uint32_t namesSize = 0;

    std::array<unsigned char, kPs5EntrySize> entry;
    for (uint32_t i = 0; i < entryCount; ++i) {
        if (!readAt(entryTableOffset + static_cast<long long>(i) * kPs5EntrySize, entry.data(), entry.size())) break;

        const uint32_t fileId = be32(entry.data() + 0);
        const uint32_t nameOffset = be32(entry.data() + 4);
        const uint32_t relativeOffset = be32(entry.data() + 16);
        const uint32_t size = be32(entry.data() + 20);
        const long long absoluteOffset = static_cast<long long>(sc_offset) + relativeOffset;
        if (absoluteOffset < static_cast<long long>(sc_offset) || absoluteOffset + size > totalSize) continue;

        if (fileId == kEntryNamesId) {
            namesOffset = absoluteOffset;
            namesSize = size;
        }
        entries.push_back({nameOffset, absoluteOffset, size});
    }

    if (namesOffset < 0 || namesSize == 0 || namesSize > kMaxMetadataSize) return std::nullopt;
    std::string names;
    names.resize(namesSize);
    if (!readAt(namesOffset, reinterpret_cast<unsigned char*>(names.data()), namesSize)) return std::nullopt;

    for (const auto& e : entries) {
        if (e.nameOffset == 0 || e.nameOffset >= names.size()) continue;
        const auto end = names.find('\0', e.nameOffset);
        if (end == std::string::npos) continue;
        std::string name = names.substr(e.nameOffset, end - e.nameOffset);
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        // The package's own icon0.png - not "origin-icon0.png"/"target-icon0.png", the same-shaped copies
        // an update's metadata piece also carries for the versions it goes from and to.
        const bool isIcon0 =
            lower == "icon0.png" || (lower.size() >= 10 && lower.compare(lower.size() - 10, 10, "/icon0.png") == 0);
        if (!isIcon0) continue;

        if (e.size == 0 || e.size > kMaxPosterSize) return std::nullopt;
        std::string data;
        data.resize(e.size);
        if (!readAt(e.dataOffset, reinterpret_cast<unsigned char*>(data.data()), e.size)) return std::nullopt;
        return data;
    }
    return std::nullopt;
}

std::optional<std::vector<std::pair<std::string, std::string>>> PkgFileParser::parseSfo(const std::string& data) {
    if (data.size() < 0x14) return std::nullopt;
    if (static_cast<unsigned char>(data[0]) != 0x00 || data[1] != 'P' || data[2] != 'S' || data[3] != 'F') {
        return std::nullopt;
    }

    const auto* d = reinterpret_cast<const unsigned char*>(data.data());
    const uint32_t keyOffset = le32(d + 8);
    const uint32_t dataOffset = le32(d + 12);
    const uint32_t count = le32(d + 16);
    if (count > 4096) return std::nullopt;

    const size_t indexEnd = 0x14 + static_cast<size_t>(count) * 0x10;
    if (keyOffset < indexEnd || dataOffset < keyOffset || dataOffset > data.size()) return std::nullopt;

    std::vector<std::pair<std::string, std::string>> result;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t recordOffset = 0x14 + static_cast<size_t>(i) * 0x10;
        if (recordOffset + 0x10 > data.size()) break;

        const uint16_t keyRelative = static_cast<uint16_t>(d[recordOffset]) | (static_cast<uint16_t>(d[recordOffset + 1]) << 8);
        const uint16_t format = (static_cast<uint16_t>(d[recordOffset + 2]) << 8) | d[recordOffset + 3];
        const uint32_t length = le32(d + recordOffset + 4);
        const uint32_t maxLength = le32(d + recordOffset + 8);
        const uint32_t valueRelative = le32(d + recordOffset + 12);
        if (maxLength != 0 && length > maxLength) continue;

        const size_t keyAbsolute = static_cast<size_t>(keyOffset) + keyRelative;
        if (keyAbsolute >= dataOffset || keyAbsolute >= data.size()) continue;
        const auto keyEnd = data.find('\0', keyAbsolute);
        if (keyEnd == std::string::npos || keyEnd > dataOffset) continue;
        const std::string key = data.substr(keyAbsolute, keyEnd - keyAbsolute);

        const size_t valueAbsolute = static_cast<size_t>(dataOffset) + valueRelative;
        if (valueAbsolute + length > data.size()) continue;

        std::string value;
        if (format == 0x0204) {  // TEXT
            const auto valueEnd = data.find('\0', valueAbsolute);
            const size_t actualEnd =
                (valueEnd == std::string::npos || valueEnd > valueAbsolute + length) ? valueAbsolute + length : valueEnd;
            value = data.substr(valueAbsolute, actualEnd - valueAbsolute);
        } else if (format == 0x0404) {  // INTEGER
            if (length < 4) continue;
            value = std::to_string(le32(d + valueAbsolute));
        } else {
            continue;  // binary value - nothing this cares about needs it
        }
        result.emplace_back(key, value);
    }
    return result;
}

std::optional<std::string> PkgFileParser::extractJsonStringField(const std::string& json, const std::string& key) {
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

std::optional<PkgMetadata> PkgFileParser::parseFromFilename(const std::string& pkgFilePath, long long fileSize) {
    const std::string filename = std::filesystem::path(pkgFilePath).filename().string();

    // Sony Content ID: XX####-YYYYNNNNN_VV-LLLLLLLLLLLLLLLL
    //   XX####  = 2-letter category + 4-digit distributor code
    //   YYYYNNNNN = 4-letter + 5-digit Title ID
    //   VV      = 2-digit app version
    //   L x16   = label
    // Split-part tools append "_<index>" after the extension-less name, so
    // this is matched anywhere in the filename rather than anchored to it.
    static const std::regex contentIdPattern(
        R"(([A-Za-z]{2}\d{4})-([A-Za-z]{4}\d{5})_(\d{2})-([A-Za-z0-9]{16}))");

    std::smatch match;
    if (!std::regex_search(filename, match, contentIdPattern)) return std::nullopt;

    PkgMetadata metadata;
    metadata.sizeBytes = fileSize;
    metadata.contentId = match[0].str();
    metadata.titleId = match[2].str();
    std::transform(metadata.titleId.begin(), metadata.titleId.end(), metadata.titleId.begin(),
                    [](unsigned char c) { return std::toupper(c); });

    metadata.platform = detectPlatform(metadata.titleId);
    metadata.region = detectRegion(metadata.contentId);
    // The file name is all there is on this path, so its version tag (if any)
    // is the only source. No tag means unknown (empty) - a made-up default
    // would be indistinguishable from a real one.
    metadata.version = versionFromFilename(filename);
    metadata.title = bestEffortTitle(metadata.contentId, metadata.titleId);

    return metadata;
}

std::string PkgFileParser::detectPlatform(const std::string& titleId) {
    if (titleId.empty()) return "Unknown";

    if (titleId.find("PPSA") == 0 || titleId.find("PPSS") == 0) {
        return "PS5";
    } else if (titleId.find("CUS") == 0 || titleId.find("BUS") == 0 || titleId.find("PCAS") == 0 ||
               titleId.find("PCJS") == 0 || titleId.find("PCSF") == 0 || titleId.find("PCSB") == 0 ||
               titleId.find("PCSC") == 0 || titleId.find("PCSD") == 0) {
        return "PS4";
    } else if (titleId.find("NPUA") == 0 || titleId.find("NPEA") == 0 || titleId.find("NPJA") == 0) {
        return "PS3";
    } else if (titleId.find("PPKA") == 0 || titleId.find("NPPA") == 0) {
        return "PSVita";
    }

    return "Unknown";
}

std::string PkgFileParser::detectRegion(const std::string& contentId) {
    // A content ID's first segment (before the first '-') is a Sony-issued
    // distributor/publisher code, e.g. "EP0006" or "UP0006" - format is one
    // region letter + 'P' (for "Publisher") + a 4-digit per-publisher
    // number (see psdevwiki's Publisher ID page). Unlike the raw PS5 title
    // ID prefix (always "PPSA" - see below), this code genuinely differs
    // per regional release, confirmed against real PlayStation Store
    // listings for the exact titles this app was tested against: "It Takes
    // Two" is EP0006-PPSA02343 on the EU store, while EA's US-issued titles
    // (e.g. EA SPORTS FC 25, UP0006-PPSA20049) use UP0006 - same publisher,
    // different regional entity, different code. CD Projekt's Cyberpunk
    // 2077 confirms the same pattern across three regions at once: en-us is
    // UP4497-PPSA03974, en-sa is EP4497-PPSA04027, en-hk/en-id is
    // HP4497-PPSA04028 - same publisher number (4497), region letter and
    // title ID both change per store. Only letters with real verified
    // evidence are mapped; anything else falls through rather than guess.
    static const std::vector<std::pair<char, std::string>> kRegionByDistributorLetter = {
        {'E', "Europe"},
        {'U', "North America"},
        {'H', "Asia"},
        {'J', "Japan"},
    };
    const auto dash = contentId.find('-');
    if (dash != std::string::npos && dash > 0) {
        const char firstLetter = static_cast<char>(std::toupper(static_cast<unsigned char>(contentId[0])));
        for (const auto& [letter, region] : kRegionByDistributorLetter) {
            if (firstLetter == letter) return region;
        }
    }

    // Fall back to the legacy PS3/PS4 title-ID-prefix table for anything
    // the distributor code above didn't confidently resolve (e.g. a
    // content ID that failed to parse, or a distributor letter with no
    // verified mapping yet).
    const std::string titleId = titleIdFromContentIdImpl(contentId);
    if (titleId.empty()) return "Unknown";
    std::string prefix = titleId.substr(0, 4);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return std::toupper(c); });

    // PS5 deliberately isn't covered by this table: Sony assigns "PPSA" to
    // every digital PS5 title regardless of region (confirmed: Cyberpunk
    // 2077's Japan release is PPSA-04026, its Asia release is PPSA-04028 -
    // same prefix, only the number differs), so it's the distributor code
    // above, not this raw title-ID prefix, that actually carries PS5
    // region.
    static const std::vector<std::pair<std::string, std::string>> kRegionByTitleIdPrefix = {
        {"CUSA", "North America / Europe"}, {"PCAS", "Asia"},   {"PCJS", "Japan"},
        {"PCSF", "Europe"},                 {"PCSB", "Europe"}, {"PCSC", "Asia"},
        {"PCSD", "Asia"},
        {"ECAS", "Asia"},                   {"ELAS", "Asia"},   {"ELJM", "Japan"},
    };
    for (const auto& [candidatePrefix, region] : kRegionByTitleIdPrefix) {
        if (prefix == candidatePrefix) return region;
    }
    return "Unknown";
}

bool PkgFileParser::looksLikeOpaqueLabel(const std::string& label) {
    if (label.size() < 4) return true;

    const bool allDigits = std::all_of(label.begin(), label.end(),
                                        [](unsigned char c) { return std::isdigit(c) != 0; });
    if (allDigits) return true;

    size_t distinctChars = 0;
    bool seen[256] = {};
    for (unsigned char c : label) {
        if (!seen[c]) {
            seen[c] = true;
            ++distinctChars;
        }
    }
    if (distinctChars <= 3) return true;  // e.g. mostly one repeated/padding character

    size_t longestRun = 1;
    size_t currentRun = 1;
    for (size_t i = 1; i < label.size(); ++i) {
        if (label[i] == label[i - 1]) {
            ++currentRun;
            longestRun = std::max(longestRun, currentRun);
        } else {
            currentRun = 1;
        }
    }
    if (longestRun >= 6) return true;  // e.g. "...0000000000" padding

    return false;
}

std::string PkgFileParser::humanizeLabel(const std::string& label) {
    std::string result;
    result.reserve(label.size() + 4);
    for (size_t i = 0; i < label.size(); ++i) {
        if (i > 0) {
            const bool prevDigit = std::isdigit(static_cast<unsigned char>(label[i - 1])) != 0;
            const bool curDigit = std::isdigit(static_cast<unsigned char>(label[i])) != 0;
            if (prevDigit != curDigit) result += ' ';
        }
        result += label[i];
    }
    return result;
}

std::string PkgFileParser::bestEffortTitle(const std::string& contentId, const std::string& titleId) {
    const std::string label = labelFromContentIdImpl(contentId);
    if (!label.empty() && !looksLikeOpaqueLabel(label)) return humanizeLabel(label);
    return titleId.empty() ? contentId : titleId;
}

}  // namespace gth::indexing
