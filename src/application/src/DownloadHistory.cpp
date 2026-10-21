#include "gth/application/indexing/DownloadHistory.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gth::application {

namespace {

constexpr size_t kMaxHistoryBytes = 512 * 1024;
constexpr size_t kKeepHistoryBytes = 256 * 1024;

std::string sanitizeField(std::string text) {
    std::replace(text.begin(), text.end(), '\t', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}

const char* kindToString(SyncKind kind) {
    switch (kind) {
        case SyncKind::NothingToDo: return "NothingToDo";
        case SyncKind::CompleteMetadata: return "Metadata";
        case SyncKind::Update: return "Update";
        case SyncKind::Unavailable: return "Unavailable";
    }
    return "Unavailable";
}

std::optional<SyncKind> stringToKind(const std::string& text) {
    if (text == "NothingToDo") return SyncKind::NothingToDo;
    if (text == "Metadata") return SyncKind::CompleteMetadata;
    if (text == "Update") return SyncKind::Update;
    if (text == "Unavailable") return SyncKind::Unavailable;
    return std::nullopt;
}

const char* outcomeToString(DownloadHistoryOutcome outcome) {
    switch (outcome) {
        case DownloadHistoryOutcome::Success: return "Success";
        case DownloadHistoryOutcome::Failed: return "Failed";
        case DownloadHistoryOutcome::Cancelled: return "Cancelled";
        case DownloadHistoryOutcome::SkippedNeedsUpdate: return "SkippedNeedsUpdate";
    }
    return "Failed";
}

std::optional<DownloadHistoryOutcome> stringToOutcome(const std::string& text) {
    if (text == "Success") return DownloadHistoryOutcome::Success;
    if (text == "Failed") return DownloadHistoryOutcome::Failed;
    if (text == "Cancelled") return DownloadHistoryOutcome::Cancelled;
    if (text == "SkippedNeedsUpdate") return DownloadHistoryOutcome::SkippedNeedsUpdate;
    return std::nullopt;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

std::optional<long long> parseLongLong(const std::string& text) {
    try {
        size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size()) return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

std::string FormatDownloadHistoryLine(const DownloadHistoryEntry& entry) {
    std::ostringstream out;
    out << static_cast<long long>(entry.when) << '\t' << sanitizeField(entry.titleId) << '\t' << sanitizeField(entry.title)
        << '\t' << kindToString(entry.kind) << '\t' << outcomeToString(entry.outcome) << '\t' << entry.bytesDownloaded << '\t'
        << entry.durationSeconds << '\t' << sanitizeField(entry.message);
    return out.str();
}

std::optional<DownloadHistoryEntry> ParseDownloadHistoryLine(const std::string& line) {
    if (line.empty()) return std::nullopt;
    const auto fields = splitTabs(line);
    if (fields.size() < 8) return std::nullopt;

    const auto when = parseLongLong(fields[0]);
    const auto kind = stringToKind(fields[3]);
    const auto outcome = stringToOutcome(fields[4]);
    const auto bytes = parseLongLong(fields[5]);
    const auto duration = parseLongLong(fields[6]);
    if (!when || !kind || !outcome || !bytes || !duration) return std::nullopt;

    DownloadHistoryEntry entry;
    entry.when = static_cast<std::time_t>(*when);
    entry.titleId = fields[1];
    entry.title = fields[2];
    entry.kind = *kind;
    entry.outcome = *outcome;
    entry.bytesDownloaded = *bytes;
    entry.durationSeconds = *duration;
    entry.message = fields[7];
    return entry;
}

void AppendDownloadHistory(const std::filesystem::path& file, const DownloadHistoryEntry& entry) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);

    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out << FormatDownloadHistoryLine(entry) << "\n";
    }

    // Keep the file to a sensible size: once it grows past the limit, drop the oldest lines.
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size <= kMaxHistoryBytes) return;
    std::ifstream in(file, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    size_t from = all.size() - kKeepHistoryBytes;
    const size_t nextLine = all.find('\n', from);
    if (nextLine != std::string::npos) from = nextLine + 1;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << all.substr(from);
}

std::vector<DownloadHistoryEntry> ReadDownloadHistory(const std::filesystem::path& file, size_t limit) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::vector<DownloadHistoryEntry> entries;
    std::istringstream lines(all);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (auto entry = ParseDownloadHistoryLine(line)) entries.push_back(std::move(*entry));
    }

    std::reverse(entries.begin(), entries.end());  // newest first
    if (limit > 0 && entries.size() > limit) entries.resize(limit);
    return entries;
}

}  // namespace gth::application
