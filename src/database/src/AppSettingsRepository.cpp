#include "gth/database/AppSettingsRepository.h"

#include <sstream>

namespace gth::database {

using core::AppSettings;
using core::ThemeMode;

namespace {

// hiddenTabs is a handful of short ASCII labels ("Games", "Ftp", ...) -
// pipe-joining into one TEXT column avoids standing up a second table for
// what is, in practice, a small fixed checklist.
std::string joinHiddenTabs(const std::set<std::string>& tabs) {
    std::ostringstream out;
    bool first = true;
    for (const auto& tab : tabs) {
        if (!first) out << '|';
        out << tab;
        first = false;
    }
    return out.str();
}

std::set<std::string> splitHiddenTabs(const std::string& joined) {
    std::set<std::string> tabs;
    std::string current;
    for (char c : joined) {
        if (c == '|') {
            if (!current.empty()) tabs.insert(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) tabs.insert(current);
    return tabs;
}

}  // namespace

AppSettingsRepository::AppSettingsRepository(Database& db) : db_(db) {}

AppSettings AppSettingsRepository::load() {
    AppSettings settings;

    // SteamGridDbApiKey (migration 16) is no longer read - the web fallback it configured was
    // replaced by a keyless lookup (see PosterCache/PsPricesClient) - but the column itself
    // stays in the schema; a shipped migration is never removed or renumbered.
    auto stmt = db_.prepare(
        "SELECT Theme, HiddenTabs, DefaultLocalFolder, LogLevel, MaxConsoles FROM AppSettings WHERE AppSettingsId = 1;");
    if (!stmt.step()) return settings;

    settings.theme = stmt.columnText(0) == "Dark" ? ThemeMode::Dark : ThemeMode::Light;
    settings.hiddenTabs = splitHiddenTabs(stmt.columnText(1));
    settings.defaultLocalFolder = stmt.columnText(2);
    const auto logLevel = stmt.columnTextOrNull(3);
    if (logLevel.has_value()) settings.logLevel = *logLevel;
    const auto maxConsoles = stmt.columnInt64OrNull(4);
    if (maxConsoles.has_value()) settings.maxConsoles = static_cast<int>(*maxConsoles);

    return settings;
}

void AppSettingsRepository::save(const AppSettings& settings) {
    auto stmt = db_.prepare(
        "INSERT INTO AppSettings (AppSettingsId, Theme, HiddenTabs, DefaultLocalFolder, LogLevel, MaxConsoles) "
        "VALUES (1, ?, ?, ?, ?, ?) "
        "ON CONFLICT(AppSettingsId) DO UPDATE SET Theme = excluded.Theme, HiddenTabs = excluded.HiddenTabs, "
        "DefaultLocalFolder = excluded.DefaultLocalFolder, LogLevel = excluded.LogLevel, "
        "MaxConsoles = excluded.MaxConsoles;");
    stmt.bind(1, settings.theme == ThemeMode::Dark ? std::string("Dark") : std::string("Light"));
    stmt.bind(2, joinHiddenTabs(settings.hiddenTabs));
    stmt.bind(3, settings.defaultLocalFolder);
    stmt.bind(4, settings.logLevel);
    stmt.bind(5, static_cast<long long>(settings.maxConsoles));
    stmt.step();
}

}  // namespace gth::database
