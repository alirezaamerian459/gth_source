#include "PatchAutoClickLogic.h"

#include <cwctype>
#include <string_view>

#include "gth/core/index/VersionComparer.h"

namespace gth::captcha {

bool ParsePatchHeading(const std::wstring& name, std::wstring& outVersion) {
    constexpr std::wstring_view kPrefix = L"Patch";
    if (name.compare(0, kPrefix.size(), kPrefix) != 0) return false;

    size_t i = kPrefix.size();
    if (i >= name.size() || !iswspace(name[i])) return false;  // needs at least one separating space
    while (i < name.size() && iswspace(name[i])) ++i;
    if (i >= name.size()) return false;

    const size_t versionStart = i;
    bool lastWasDigit = false;
    for (; i < name.size(); ++i) {
        const wchar_t c = name[i];
        if (iswdigit(c)) {
            lastWasDigit = true;
        } else if (c == L'.') {
            if (!lastWasDigit) return false;  // no leading dot, no "..", matching \d+(\.\d+)*
            lastWasDigit = false;
        } else {
            return false;  // anything else (including trailing text/whitespace) fails the exact match
        }
    }
    if (!lastWasDigit) return false;  // must end on a digit, not a trailing dot

    outVersion = name.substr(versionStart);
    return true;
}

std::optional<size_t> IndexOfNewestPatch(const std::vector<std::wstring>& elementNames) {
    std::optional<size_t> bestIndex;
    std::string bestVersion;
    for (size_t i = 0; i < elementNames.size(); ++i) {
        std::wstring version;
        if (!ParsePatchHeading(elementNames[i], version)) continue;

        // ASCII digits/dots only - guaranteed by ParsePatchHeading above - so this narrowing is safe.
        std::string versionUtf8;
        versionUtf8.reserve(version.size());
        for (const wchar_t c : version) versionUtf8.push_back(static_cast<char>(c));
        if (!bestIndex || gth::core::compareVersions(versionUtf8, bestVersion) > 0) {
            bestIndex = i;
            bestVersion = versionUtf8;
        }
    }
    return bestIndex;
}

std::wstring TitleIdFromWindowTitle(const std::wstring& windowTitle) {
    const size_t colon = windowTitle.find(L':');
    if (colon == std::wstring::npos) return {};

    size_t start = 0;
    while (start < colon && iswspace(windowTitle[start])) ++start;
    size_t end = colon;
    while (end > start && iswspace(windowTitle[end - 1])) --end;
    return windowTitle.substr(start, end - start);
}

}  // namespace gth::captcha
