#include "gth/core/index/VersionComparer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <vector>

namespace gth::core {

namespace {

std::vector<std::string> splitOnDots(const std::string& version) {
    std::vector<std::string> parts;
    std::stringstream ss(version);
    std::string part;
    while (std::getline(ss, part, '.')) parts.push_back(part);
    return parts;
}

bool isAllDigits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

int compareVersions(const std::string& a, const std::string& b) {
    if (a == b) return 0;

    const auto partsA = splitOnDots(a);
    const auto partsB = splitOnDots(b);
    const size_t count = std::max(partsA.size(), partsB.size());

    for (size_t i = 0; i < count; ++i) {
        const std::string partA = i < partsA.size() ? partsA[i] : "0";
        const std::string partB = i < partsB.size() ? partsB[i] : "0";

        if (isAllDigits(partA) && isAllDigits(partB)) {
            const long long numA = std::stoll(partA);
            const long long numB = std::stoll(partB);
            if (numA != numB) return numA < numB ? -1 : 1;
        } else if (partA != partB) {
            return partA < partB ? -1 : 1;
        }
    }

    return 0;
}

}  // namespace gth::core
