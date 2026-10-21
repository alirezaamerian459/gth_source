#pragma once

#include <optional>
#include <string>
#include <vector>

// The pure (no COM/UIA/mouse) matching logic behind PatchAutoClicker, split out so it's
// unit-testable without a live browser: which accessible-element Name is a patch heading, which
// of several is the newest, and how to identify a tracker page/tab by its window title.
namespace gth::captcha {

// True (with outVersion set to the part after "Patch ") when `name` is exactly a patch heading -
// "Patch " followed only by digits and dots, e.g. "Patch 01.000.028". The shape the browser
// userscript this replaces matched with /^Patch\s+\d+(\.\d+)*$/ against prosperopatches.com's own
// patch tile headings.
bool ParsePatchHeading(const std::wstring& name, std::wstring& outVersion);

// Index of whichever element in `elementNames` is the highest-version patch heading, by
// gth::core::compareVersions - std::nullopt if none of them are patch headings at all. Elements
// that aren't patch headings (page chrome, "SC"/"CRC" button labels, ...) are simply ignored, not
// treated as an error.
std::optional<size_t> IndexOfNewestPatch(const std::vector<std::wstring>& elementNames);

// prosperopatches.com sets its page (and so the browser window) title to "<TITLEID>: <game name>"
// - the same shape SonyLinkCopyWatcher's tab-closing already matches Chrome tab names by. Returns
// the trimmed part before the first ':', or "" if the title has none (not a tracker page, or a
// browser window showing something else entirely).
std::wstring TitleIdFromWindowTitle(const std::wstring& windowTitle);

}  // namespace gth::captcha
