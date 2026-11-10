#include "gth/application/indexing/SonyAutomation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gth::application {

namespace {

std::string trimmedLower(const std::string& text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    auto first = std::find_if_not(text.begin(), text.end(), isSpace);
    auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    std::string out = first < last ? std::string(first, last) : std::string();
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<bool> parseSwitch(const std::string& value) {
    const std::string v = trimmedLower(value);
    if (v == "on" || v == "true" || v == "yes" || v == "1") return true;
    if (v == "off" || v == "false" || v == "no" || v == "0") return false;
    return std::nullopt;
}

bool hasCurrentSettingsVersion(const std::string& text) {
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const auto equals = line.find('=');
        if (equals == std::string::npos || trimmedLower(line.substr(0, equals)) != "settingsversion") continue;
        try {
            return std::stoi(trimmedLower(line.substr(equals + 1))) >= 2;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

std::string formatSize(long long bytes) {
    std::array<char, 32> buffer;
    if (bytes >= 1024LL * 1024 * 1024) {
        std::snprintf(buffer.data(), buffer.size(), "%.2f GB", static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0);
    } else {
        std::snprintf(buffer.data(), buffer.size(), "%.1f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    }
    return buffer.data();
}

std::string formatDuration(std::chrono::seconds duration) {
    const long long total = duration.count();
    if (total >= 3600) return std::format("{}h {}m", total / 3600, (total % 3600) / 60);
    if (total >= 60) return std::format("{}m", total / 60);
    return std::format("{}s", total);
}

std::string formatWhen(std::time_t when) {
    std::tm local{};
    localtime_s(&local, &when);
    std::array<char, 32> buffer;
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M", &local);
    return buffer.data();
}

constexpr char kReportMarker[] = "=== ";
constexpr size_t kMaxLogBytes = 512 * 1024;
constexpr size_t kKeepLogBytes = 256 * 1024;

}  // namespace

SonyAutomationSettings ParseSonyAutomationSettings(const std::string& text) {
    SonyAutomationSettings settings;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const std::string trimmedLine = trimmedLower(line);
        if (trimmedLine.empty() || trimmedLine[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trimmedLower(line.substr(0, equals));
        const std::string value = line.substr(equals + 1);

        if (key == "checkforupdates") {
            if (const auto on = parseSwitch(value); on.has_value()) settings.checkForUpdates = *on;
        } else if (key == "addmissingmetadata") {
            if (const auto on = parseSwitch(value); on.has_value()) settings.addMissingMetadata = *on;
        } else if (key == "downloadupdates") {
            if (const auto on = parseSwitch(value); on.has_value()) settings.downloadUpdates = *on;
        } else if (key == "scanfornewgames") {
            if (const auto on = parseSwitch(value); on.has_value()) settings.scanForNewGames = *on;
        } else if (key == "intervalhours") {
            try {
                settings.intervalHours = std::clamp(std::stoi(trimmedLower(value)), 1, 168);
            } catch (const std::exception&) {
            }
        }
    }
    return settings;
}

std::string SerializeSonyAutomationSettings(const SonyAutomationSettings& settings) {
    const auto onOff = [](bool on) { return on ? "on" : "off"; };
    return std::format(
        "# Game Transfer Hub - Sony automation. Edit here or in the Updates tab.\n"
        "# checkForUpdates:    ask Sony whether each game is up to date\n"
        "# addMissingMetadata: download the small metadata piece PS5 downloads lack, so titles and versions can be read\n"
        "# downloadUpdates:    also download out-of-date packages automatically (many GB)\n"
        "# scanForNewGames:    before each run, look through the storages for games added since the last scan\n"
        "# intervalHours:      how often, while the app is open (1-168)\n"
        "settingsVersion=2\n"
        "checkForUpdates={}\n"
        "addMissingMetadata={}\n"
        "downloadUpdates={}\n"
        "scanForNewGames={}\n"
        "intervalHours={}\n",
        onOff(settings.checkForUpdates), onOff(settings.addMissingMetadata), onOff(settings.downloadUpdates),
        onOff(settings.scanForNewGames), std::clamp(settings.intervalHours, 1, 168));
}

SonyAutomationSettings SonyAutomationSettingsStore::load() const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return {};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto settings = ParseSonyAutomationSettings(text);
    // Older releases saved the default downloadUpdates=off alongside all the other settings.
    // The new default is automatic updating, so migrate old settings once; new files carry a
    // version marker and preserve an explicit opt-out on future launches.
    if (!hasCurrentSettingsVersion(text)) {
        settings.downloadUpdates = true;
        save(settings);  // best effort: write the new marker so an explicit opt-out can be saved afterward
    }
    return settings;
}

bool SonyAutomationSettingsStore::save(const SonyAutomationSettings& settings) const {
    std::error_code ec;
    if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    out << SerializeSonyAutomationSettings(settings);
    out.flush();
    return static_cast<bool>(out);
}

bool IsSonyAutomationDue(const SonyAutomationSettings& settings, std::chrono::steady_clock::time_point appStarted,
                         std::optional<std::chrono::steady_clock::time_point> lastRun,
                         std::chrono::steady_clock::time_point now, std::chrono::seconds startupDelay) {
    if (!settings.anythingEnabled()) return false;
    if (!lastRun.has_value()) return now - appStarted >= startupDelay;
    return now - *lastRun >= std::chrono::hours(std::clamp(settings.intervalHours, 1, 168));
}

std::string FormatSonyRunReport(const SonyRunReportInput& run, const std::map<std::string, std::string>& titlesByTitleId) {
    const auto nameOf = [&](const std::string& titleId) {
        const auto found = titlesByTitleId.find(titleId);
        return found != titlesByTitleId.end() && !found->second.empty() ? found->second : titleId;
    };

    std::ostringstream out;
    out << kReportMarker << formatWhen(run.when) << " - " << (run.automatic ? "automatic run" : "manual run") << " (took "
        << formatDuration(run.duration) << ") ===\n";

    if (run.scannedForNewGames) {
        out << "Looked through the storages for new games: " << run.newGamesFound << " new game(s) found.\n";
    }
    if (run.linksLearnedFromFiles > 0) {
        out << "Read " << run.linksLearnedFromFiles << " Sony link(s) out of metadata pieces already in the archive.\n";
    }
    if (run.syncRan && run.oneGame) {
        out << nameOf(run.single.titleId) << ": " << run.single.message;
        if (run.single.filesDownloaded > 0) out << " [" << run.single.filesDownloaded << " file(s), " << formatSize(run.single.bytesDownloaded) << "]";
        out << "\n";
    } else if (run.syncRan) {
        const auto& b = run.batch;
        out << "Sony sync: " << b.completed << " metadata added, " << b.updated << " updated, " << b.alreadyFine
            << " already current, " << b.needUpdate << " out of date but not downloaded, " << b.unavailable
            << " without a usable Sony link, " << b.failed << " failed. Downloaded " << formatSize(b.bytesDownloaded) << ".";
        if (b.recoveredOnRetry > 0) out << " " << b.recoveredOnRetry << " game(s) failed at first and worked on a retry.";
        if (b.cancelled) out << " (cancelled)";
        out << "\n";
        for (const auto& game : b.games) {
            if (game.skippedNeedsUpdate) {
                out << "  ~ " << nameOf(game.titleId) << ": out of date, not downloaded - " << game.message << "\n";
            } else if (game.success) {
                out << "  + " << nameOf(game.titleId) << ": " << game.message << "\n";
            } else {
                out << "  ! " << nameOf(game.titleId) << ": FAILED - " << game.message << "\n";
            }
        }
    }
    if (run.storagesRescanned > 0) out << "Rescanned " << run.storagesRescanned << " storage(s) so the library shows the changes.\n";
    if (run.checked) {
        const auto& c = run.check;
        out << "Sony check of " << c.entries.size() << " game(s): " << c.upToDate << " up to date, " << c.updatesAvailable
            << " with an update available, " << c.installedUnknown << " version unknown, " << c.noSonyData
            << " without a Sony link, " << c.failed << " failed.\n";
        if (c.failed > 0 && !c.firstFailure.empty()) out << "  first failure: " << c.firstFailure << "\n";
    }
    if (!run.syncRan && !run.checked) out << "Nothing was switched on to do.\n";
    return out.str();
}

void AppendSonyRunReport(const std::filesystem::path& file, const std::string& text) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);

    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out << text << "\n";
    }

    // Keep the log to a sensible size: when it grows past the limit, drop the oldest reports.
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size <= kMaxLogBytes) return;
    std::ifstream in(file, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const size_t from = all.find(kReportMarker, all.size() - kKeepLogBytes);
    if (from == std::string::npos) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << all.substr(from);
}

std::string LastSonyRunSummary(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t last = all.rfind(kReportMarker);
    if (last == std::string::npos) return {};

    // The header, the sync line and the check line - the details below them stay in the file.
    std::istringstream lines(all.substr(last));
    std::string summary, line;
    int taken = 0;
    while (std::getline(lines, line) && taken < 6) {
        if (line.empty()) break;
        if (line.rfind("  ", 0) == 0) continue;  // per-game detail lines
        if (!line.empty() && line.back() == '\r') line.pop_back();
        summary += (summary.empty() ? "" : "\r\n") + line;
        ++taken;
    }
    return summary;
}

}  // namespace gth::application
