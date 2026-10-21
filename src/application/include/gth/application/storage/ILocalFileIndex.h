#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

namespace gth::application {

// Finds a file in the indexed game library by its exact file name.
//
// This is how the proxy recognises a download it can serve from disk: a console asks for a
// specific package file ("UP0001-CUSA00001_00-GAME0000000000000-A0100-V0100.pkg", "..._0.pkg", ...)
// and the same file - same name - is sitting on one of the user's HDDs. Package file names are
// unique per release/piece, so matching on the name is reliable in a way that guessing a game
// title out of the URL path is not.
class ILocalFileIndex {
public:
    virtual ~ILocalFileIndex() = default;

    // Case-insensitive. Returns a path that existed at the moment of the call (a drive that is
    // currently unplugged never produces a hit), or nullopt.
    virtual std::optional<std::filesystem::path> findByFileName(const std::string& fileName) = 0;
};

// The file name a request URL asks for: last path segment, query string and fragment removed.
inline std::string packageFileNameFromUrl(const std::string& url) {
    const std::string noQuery = url.substr(0, url.find_first_of("?#"));
    const auto slash = noQuery.find_last_of('/');
    return slash == std::string::npos ? std::string() : noQuery.substr(slash + 1);
}

// Only package/firmware downloads are ever matched automatically (the same "*.pkg*|*.pup*" rule
// PSX Download Helper ships with). Generic names such as "manifest.json" or "index.html" could
// otherwise collide with unrelated files somewhere in a multi-terabyte library.
inline bool isServablePackageName(const std::string& fileName) {
    std::string lower = fileName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto endsWith = [&](const char* suffix) {
        const std::string s(suffix);
        return lower.size() >= s.size() && lower.compare(lower.size() - s.size(), s.size(), s) == 0;
    };
    return endsWith(".pkg") || endsWith(".pup");
}

}  // namespace gth::application
