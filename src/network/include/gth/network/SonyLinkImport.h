#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "gth/network/SonyUpdateService.h"

namespace gth::network {

// A PS5 game's version-file link ("versionFileUri") is written into the game's
// own param.json - which lives in the small "_sc.pkg" metadata piece Sony
// serves next to the game's pieces. So anything that points at that piece
// leads to the link: the piece's own address, the manifest (".json") or the
// ".crc" file beside it, or the version file itself. This turns whatever a
// person copies from a tracker page, or a download tool exports, into links.
//
// Only Sony's own package hosts are ever recognised, and only the first 64 KB
// of the metadata piece is read - never the game.

enum class SonyAddressKind {
    VersionFile,    // ".../np/<title>_00/<id>-version.xml" - the link itself
    MetadataPiece,  // ".../app/info/.../<content id>_sc.pkg" - its param.json states the link
    GamePiece,      // ".../app/pkg/.../<content id>_N.pkg" - part of the game; says nothing about the link
};

struct SonyAddress {
    SonyAddressKind kind = SonyAddressKind::GamePiece;
    std::string titleId;  // "PPSA02343"
    // For a MetadataPiece, the metadata piece's address - even when what was pasted was the manifest or the
    // ".crc" file that sits in the same folder.
    std::string url;
};

// Every Sony package-host address in free text (a pasted list, a tool's export,
// a page copied whole), in order, without repeats. Text that isn't such an
// address is ignored.
std::vector<SonyAddress> ExtractSonyAddresses(const std::string& text);

// param.json's text out of the first bytes of a metadata piece ("\x7fCNT" block
// with its entry table, names and the plain-text param.json). Nothing here
// needs the piece beyond its first ~64 KB.
std::optional<std::string> ReadParamJsonFromMetadataPiece(const std::string& firstBytes);

// The "versionFileUri" value in a param.json, trimmed (Sony pads the value with spaces).
std::optional<std::string> VersionFileUriFromParamJson(const std::string& paramJson);

// The "titleId" value in a param.json.
std::optional<std::string> TitleIdFromParamJson(const std::string& paramJson);

// Whether `url` is a genuine Sony version file link for `titleId`: HTTPS on
// sgst.prod.dl.playstation.net, ending "-version.xml", with the title ID in its path.
bool IsSonyVersionFileUrl(const std::string& url, const std::string& titleId);

// Fetches at most the first `maxBytes` of a URL (a Range request; a server that
// ignores Range still gets cut off after `maxBytes`). Injectable for tests.
using SonyHeadFetcher = std::function<SonyHttpResponse(const std::string& url, size_t maxBytes)>;
SonyHeadFetcher MakeSonyHeadFetcher();

struct SonyLinkImportResult {
    size_t addressesFound = 0;   // Sony addresses in the text
    size_t learned = 0;          // titles whose link was newly saved (or changed)
    size_t alreadyKnown = 0;     // titles that already had that link
    size_t gamePiecesOnly = 0;   // titles seen only as game pieces - nothing to learn from those
    size_t failed = 0;           // titles a metadata piece was found for, but no link could be read
    bool cancelled = false;
    bool saveFailed = false;
    std::vector<std::string> titleIdsLearned;
    std::vector<std::string> notes;  // one line per title that didn't work, saying why
};

class SonyLinkImporter {
public:
    // `linksFile`: where learned links are saved (merged into what is there).
    // `knownFiles`: every links file the app reads - a title that already has a
    // link there isn't looked up again.
    SonyLinkImporter(std::filesystem::path linksFile, std::vector<std::filesystem::path> knownFiles,
                     SonyHeadFetcher fetcher = MakeSonyHeadFetcher());

    // `onProgress` gets a short line before each title; `shouldCancel` is asked between titles.
    SonyLinkImportResult importLinks(const std::string& text, const std::function<bool()>& shouldCancel = {},
                                     const std::function<void(const std::string&)>& onProgress = {});

private:
    std::filesystem::path linksFile_;
    std::vector<std::filesystem::path> knownFiles_;
    SonyHeadFetcher fetcher_;
};

// The same link, learned with no network and no help: a PS5 game whose archive already holds its "_sc.pkg"
// metadata piece carries its own version link in that file's param.json. `pieces` are (title ID, path of the
// piece on disk); titles that already have a link in `knownFiles` / `linksFile` aren't read again.
struct LocalLinkLearnResult {
    size_t looked = 0;        // pieces read
    size_t learned = 0;       // links newly saved
    size_t alreadyKnown = 0;  // titles that already had a link
    size_t unreadable = 0;    // pieces that couldn't be read or hold no usable link
    bool saveFailed = false;
    std::vector<std::string> titleIdsLearned;
    std::vector<std::string> notes;
};
LocalLinkLearnResult LearnLinksFromLocalMetadata(const std::vector<std::pair<std::string, std::string>>& pieces,
                                                 const std::filesystem::path& linksFile,
                                                 const std::vector<std::filesystem::path>& knownFiles);

}  // namespace gth::network
