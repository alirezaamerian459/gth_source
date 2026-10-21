#pragma once

#include <cctype>
#include <string>

namespace gth::application {

// The last path component of a file path or URL, without any query string.
inline std::string leafOfPath(const std::string& pathOrUrl) {
    std::string text = pathOrUrl.substr(0, pathOrUrl.find('?'));
    const auto slash = text.find_last_of("\\/");
    return slash == std::string::npos ? text : text.substr(slash + 1);
}

// "..._sc.pkg": the metadata piece Sony's PS5 packages end with.
inline bool isMetadataPieceFileName(const std::string& fileName) {
    static constexpr char kSuffix[] = "_sc.pkg";
    constexpr size_t kLength = sizeof(kSuffix) - 1;
    const std::string leaf = leafOfPath(fileName);
    if (leaf.size() <= kLength) return false;
    for (size_t i = 0; i < kLength; ++i) {
        if (std::tolower(static_cast<unsigned char>(leaf[leaf.size() - kLength + i])) != kSuffix[i]) return false;
    }
    return true;
}

// "..._12.pkg" -> 12; -1 when the name has no part number (the metadata piece
// has none, and neither does a package that is one file).
inline int partIndexOfFileName(const std::string& fileName) {
    const std::string leaf = leafOfPath(fileName);
    const auto dot = leaf.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return -1;
    size_t digitsStart = dot;
    while (digitsStart > 0 && std::isdigit(static_cast<unsigned char>(leaf[digitsStart - 1]))) --digitsStart;
    if (digitsStart == dot || digitsStart == 0 || leaf[digitsStart - 1] != '_') return -1;
    try {
        return std::stoi(leaf.substr(digitsStart, dot - digitsStart));
    } catch (const std::exception&) {
        return -1;
    }
}

// A name a package piece can safely be saved under: letters, digits and
// "._-" only, ending in .pkg. Anything else in a downloaded manifest (a path
// separator, "..", a drive letter) is refused rather than trusted.
inline bool isSafePieceFileName(const std::string& leaf) {
    if (leaf.size() < 5 || leaf.size() > 200) return false;
    if (leaf.compare(leaf.size() - 4, 4, ".pkg") != 0) return false;
    if (leaf.find("..") != std::string::npos) return false;
    for (char c : leaf) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

}  // namespace gth::application
