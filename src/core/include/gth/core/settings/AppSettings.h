#pragma once

#include <set>
#include <string>

namespace gth::core {

enum class ThemeMode { Light, Dark };

// Whole-app user preferences - one row, unlike every other core entity here.
// hiddenTabs holds the exact sidebar label (UTF-8) of every tab the user has
// chosen to hide; Dashboard and Settings are never eligible so they never
// appear in this set regardless of what's persisted.
struct AppSettings {
    ThemeMode theme = ThemeMode::Dark;
    std::set<std::string> hiddenTabs;
    std::string defaultLocalFolder;
    std::string logLevel = "Info";  // Error / Warning / Info / Debug
    // Caps how many "Add Console" HTTP MITM Proxy instances a user can
    // create at once (beyond the original section) - a household only has
    // so many consoles/so much bandwidth, so the admin sets a realistic
    // ceiling rather than leaving it at the technical maximum. Admin-only
    // to change (see SettingsView's "Console Limits" section).
    int maxConsoles = 4;
};

}  // namespace gth::core
