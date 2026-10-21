#pragma once

#include <cctype>
#include <string>

namespace gth::application {

// The title ID inside a Sony Content ID: "EP0006-PPSA02343_00-NUTSEU0000000000"
// -> "PPSA02343". Empty when the text doesn't have that shape.
inline std::string titleIdFromContentId(const std::string& contentId) {
    const auto dash = contentId.find('-');
    if (dash == std::string::npos) return {};
    const auto underscore = contentId.find('_', dash + 1);
    if (underscore == std::string::npos || underscore - (dash + 1) != 9) return {};

    std::string id = contentId.substr(dash + 1, 9);
    for (size_t i = 0; i < id.size(); ++i) {
        const auto c = static_cast<unsigned char>(id[i]);
        if (i < 4 ? !std::isalpha(c) : !std::isdigit(c)) return {};
        id[i] = static_cast<char>(std::toupper(c));
    }
    return id;
}

// A GameVersion's RelativePath is "<containing folder>/<Content ID>"; this is
// the title ID that path's last component carries.
inline std::string titleIdFromVersionPath(const std::string& versionRelativePath) {
    const auto slash = versionRelativePath.find_last_of("\\/");
    return titleIdFromContentId(slash == std::string::npos ? versionRelativePath
                                                            : versionRelativePath.substr(slash + 1));
}

}  // namespace gth::application
