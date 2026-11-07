#include "gth/application/backup/BackupService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"

namespace gth::application {

namespace fs = std::filesystem;

namespace {

constexpr const char* kManifestMagic = "GTH_BACKUP_V1";
// Telegram's hosted Bot API can restore files only up to 20 MB; keeping the
// manifest at the same 16 MiB bound as data chunks leaves safe headroom.
constexpr uintmax_t kMaxManifestBytes = BackupService::kChunkBytes;

std::string pathToUtf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

fs::path pathFromUtf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return fs::path(encoded);
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::optional<std::string> hexDecode(const std::string& value) {
    if (value.size() % 2 != 0) return std::nullopt;
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result(value.size() / 2, '\0');
    for (size_t i = 0; i < result.size(); ++i) {
        const int high = nibble(value[i * 2]);
        const int low = nibble(value[i * 2 + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result[i] = static_cast<char>((high << 4) | low);
    }
    return result;
}

bool isDigest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool isValidUtf8(const std::string& value) {
    size_t i = 0;
    while (i < value.size()) {
        const auto first = static_cast<unsigned char>(value[i]);
        if (first <= 0x7f) {
            ++i;
            continue;
        }
        size_t count = 0;
        uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 2; codepoint = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { count = 3; codepoint = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 4; codepoint = first & 0x07; }
        else return false;
        if (i + count > value.size()) return false;
        for (size_t j = 1; j < count; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        i += count;
    }
    return true;
}

bool isSafeRelativePath(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return false;
    for (const auto& component : relative) {
        const auto name = pathToUtf8(component);
        if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
        std::string base = name.substr(0, name.find('.'));
        std::transform(base.begin(), base.end(), base.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const bool reserved = base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
            (base.size() == 4 && (base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0) &&
             base[3] >= '1' && base[3] <= '9');
        if (reserved) return false;
        for (const unsigned char c : name) {
            if (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
                return false;
        }
    }
    return true;
}

std::string safeLeaf(std::string value) {
    for (char& c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
            c == '?' || c == '*') c = '_';
    }
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    if (value.empty() || value == "." || value == "..") return "game";
    return value;
}

bool cancelled(const BackupCallbacks& callbacks) {
    return callbacks.shouldCancel && callbacks.shouldCancel();
}

void report(const BackupCallbacks& callbacks, const std::string& stage, long long done, long long total) {
    if (callbacks.onProgress) callbacks.onProgress(stage, done, total);
}

struct ChunkRecord {
    long long size = 0;
    std::string digest;
    std::string objectId;
};

struct FileRecord {
    std::string relativePath;
    long long size = 0;
    std::string digest;
    std::vector<ChunkRecord> chunks;
};

struct Manifest {
    std::string backupId;
    std::string title;
    std::string platform;
    std::string version;
    std::string createdAt;
    std::vector<FileRecord> files;
};

std::string serialize(const Manifest& manifest) {
    std::ostringstream out;
    out << kManifestMagic << '\n'
        << "G\t" << hexEncode(manifest.backupId) << '\t' << hexEncode(manifest.title) << '\t'
        << hexEncode(manifest.platform) << '\t' << hexEncode(manifest.version) << '\t'
        << hexEncode(manifest.createdAt) << '\n';
    for (const auto& file : manifest.files) {
        out << "F\t" << hexEncode(file.relativePath) << '\t' << file.size << '\t' << file.digest << '\t'
            << file.chunks.size() << '\n';
        for (const auto& chunk : file.chunks) {
            out << "C\t" << chunk.size << '\t' << chunk.digest << '\t' << hexEncode(chunk.objectId) << '\n';
        }
    }
    return out.str();
}

bool parseInteger(const std::string& text, long long& value) {
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        value = std::stoll(text, &consumed);
        return consumed == text.size() && value >= 0;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        const size_t end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

bool appendCatalog(const fs::path& path, const BackupManifestRef& entry, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Couldn't create the backup catalog directory: " + ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        error = "Couldn't open the local backup catalog for writing.";
        return false;
    }
    out << "B\t" << hexEncode(entry.objectId) << '\t' << hexEncode(entry.backupId) << '\t'
        << hexEncode(entry.title) << '\t' << hexEncode(entry.platform) << '\t' << hexEncode(entry.version) << '\t'
        << hexEncode(entry.createdAt) << '\t' << entry.totalBytes << '\n';
    out.flush();
    if (!out) {
        error = "Couldn't save the uploaded backup in the local catalog.";
        return false;
    }
    return true;
}

std::optional<Manifest> parseManifest(const std::string& text, std::string& error) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kManifestMagic) {
        error = "The backup manifest format is unsupported.";
        return std::nullopt;
    }
    if (!std::getline(in, line)) {
        error = "The backup manifest is incomplete.";
        return std::nullopt;
    }
    const auto game = splitTabs(line);
    if (game.size() != 6 || game[0] != "G") {
        error = "The backup manifest has invalid game metadata.";
        return std::nullopt;
    }
    Manifest manifest;
    auto backupId = hexDecode(game[1]);
    auto title = hexDecode(game[2]);
    auto platform = hexDecode(game[3]);
    auto version = hexDecode(game[4]);
    auto createdAt = hexDecode(game[5]);
    if (!backupId || backupId->empty() || !title || title->empty() || !platform || !version || !createdAt ||
        !isValidUtf8(*backupId) || !isValidUtf8(*title) || !isValidUtf8(*platform) || !isValidUtf8(*version) ||
        !isValidUtf8(*createdAt)) {
        error = "The backup manifest contains invalid text fields.";
        return std::nullopt;
    }
    manifest.backupId = std::move(*backupId);
    manifest.title = std::move(*title);
    manifest.platform = std::move(*platform);
    manifest.version = std::move(*version);
    manifest.createdAt = std::move(*createdAt);

    long long totalBytes = 0;
    std::set<std::string> seenPaths;
    while (std::getline(in, line)) {
        const auto fileFields = splitTabs(line);
        if (fileFields.size() != 5 || fileFields[0] != "F") {
            error = "The backup manifest has an invalid file entry.";
            return std::nullopt;
        }
        auto path = hexDecode(fileFields[1]);
        long long size = 0;
        long long chunkCount = 0;
        if (!path || path->empty() || !parseInteger(fileFields[2], size) || !isDigest(fileFields[3]) ||
            !parseInteger(fileFields[4], chunkCount) || chunkCount > 1000000 || (size > 0 && chunkCount == 0) ||
            (size == 0 && chunkCount != 0)) {
            error = "The backup manifest has invalid file metadata.";
            return std::nullopt;
        }
        if (!isValidUtf8(*path)) {
            error = "The backup manifest contains a file path that isn't valid UTF-8.";
            return std::nullopt;
        }
        fs::path relative;
        try {
            relative = pathFromUtf8(*path);
        } catch (...) {
            error = "The backup manifest contains a file path Windows cannot represent.";
            return std::nullopt;
        }
        if (!isSafeRelativePath(relative)) {
            error = "The backup manifest contains an unsafe file path.";
            return std::nullopt;
        }
        const auto normalizedPath = pathToUtf8(relative.lexically_normal());
        if (normalizedPath.empty() || normalizedPath == "." || !seenPaths.insert(lowercase(normalizedPath)).second) {
            error = "The backup manifest contains a duplicate or invalid file path.";
            return std::nullopt;
        }
        FileRecord file;
        file.relativePath = normalizedPath;
        file.size = size;
        file.digest = lowercase(fileFields[3]);
        long long chunksBytes = 0;
        for (long long index = 0; index < chunkCount; ++index) {
            if (!std::getline(in, line)) {
                error = "The backup manifest is missing one or more file chunks.";
                return std::nullopt;
            }
            const auto fields = splitTabs(line);
            long long chunkSize = 0;
            auto objectId = fields.size() == 4 ? hexDecode(fields[3]) : std::nullopt;
            if (fields.size() != 4 || fields[0] != "C" || !parseInteger(fields[1], chunkSize) || chunkSize <= 0 ||
                chunkSize > BackupService::kChunkBytes || !isDigest(fields[2]) || !objectId || objectId->empty() ||
                !isValidUtf8(*objectId) ||
                chunksBytes > std::numeric_limits<long long>::max() - chunkSize) {
                error = "The backup manifest has invalid chunk metadata.";
                return std::nullopt;
            }
            chunksBytes += chunkSize;
            file.chunks.push_back({chunkSize, lowercase(fields[2]), std::move(*objectId)});
        }
        if (chunksBytes != size || totalBytes > std::numeric_limits<long long>::max() - size) {
            error = "The backup manifest file size does not match its chunks.";
            return std::nullopt;
        }
        totalBytes += size;
        manifest.files.push_back(std::move(file));
    }
    if (manifest.files.empty()) {
        error = "The backup manifest contains no game files.";
        return std::nullopt;
    }
    return manifest;
}

bool copyRange(const fs::path& source, long long offset, long long count, const fs::path& destination,
               const BackupCallbacks& callbacks, std::string& error) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        error = "Couldn't read " + source.string();
        return false;
    }
    in.seekg(offset, std::ios::beg);
    if (!in) {
        error = "Couldn't seek in " + source.string();
        return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Couldn't create a temporary backup chunk.";
        return false;
    }
    std::vector<char> buffer(1 << 20);
    long long remaining = count;
    while (remaining > 0) {
        if (cancelled(callbacks)) return false;
        const auto requested = static_cast<std::streamsize>(std::min<long long>(remaining, buffer.size()));
        in.read(buffer.data(), requested);
        const auto read = in.gcount();
        if (read <= 0) {
            error = "A game file changed or ended while it was being backed up.";
            return false;
        }
        out.write(buffer.data(), read);
        if (!out) {
            error = "Couldn't write a temporary backup chunk (disk full?).";
            return false;
        }
        remaining -= read;
    }
    out.close();
    if (!out) {
        error = "Couldn't finish writing a temporary backup chunk.";
        return false;
    }
    return true;
}

}  // namespace

BackupService::BackupService(IBackupSource& source, IBackupTransport& transport, core::IFileHasher& hasher,
                             fs::path workingDirectory)
    : source_(source), transport_(transport), hasher_(hasher), workingDirectory_(std::move(workingDirectory)) {}

BackupResult BackupService::createBackup(const BackupGame& game, const BackupCallbacks& callbacks) {
    BackupResult result;
    if (game.id.empty() || game.title.empty() || game.platform.empty() || game.version.empty()) {
        result.error = "Select a game with a known platform and version before backing it up.";
        return result;
    }

    result.backupId = core::Guid::newGuid();
    const fs::path jobRoot = workingDirectory_ / result.backupId;
    const fs::path sourceRoot = jobRoot / "source";
    std::error_code ec;
    fs::create_directories(sourceRoot, ec);
    if (ec) {
        result.error = "Couldn't create backup staging directory: " + ec.message();
        return result;
    }
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{jobRoot};

    std::string adapterError;
    if (!source_.downloadGame(game, sourceRoot, callbacks, adapterError)) {
        result.cancelled = cancelled(callbacks);
        result.error = result.cancelled ? "Backup cancelled." :
            (adapterError.empty() ? "The game source failed to download the selected game." : adapterError);
        return result;
    }
    if (cancelled(callbacks)) {
        result.cancelled = true;
        result.error = "Backup cancelled.";
        return result;
    }

    std::vector<fs::path> files;
    fs::recursive_directory_iterator it(sourceRoot, fs::directory_options::none, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            result.error = "Couldn't enumerate the downloaded game: " + ec.message();
            return result;
        }
        const auto status = it->symlink_status(ec);
        if (ec) {
            result.error = "Couldn't inspect a downloaded game path: " + ec.message();
            return result;
        }
        if (fs::is_symlink(status)) {
            result.error = "The downloaded game contains a symbolic link; backup stopped to avoid reading outside its staging folder.";
            return result;
        }
        if (fs::is_regular_file(status)) files.push_back(it->path());
    }
    if (files.empty()) {
        result.error = "The source endpoint returned no regular game files.";
        return result;
    }
    std::sort(files.begin(), files.end());

    Manifest manifest;
    manifest.backupId = result.backupId;
    manifest.title = game.title;
    manifest.platform = game.platform;
    manifest.version = game.version;
    manifest.createdAt = core::Clock::utcNowIso8601();

    long long totalBytes = 0;
    for (const auto& path : files) {
        const auto size = fs::file_size(path, ec);
        if (ec || size > static_cast<uintmax_t>(std::numeric_limits<long long>::max()) ||
            totalBytes > std::numeric_limits<long long>::max() - static_cast<long long>(size)) {
            result.error = "A downloaded game file has an invalid or overflowing size.";
            return result;
        }
        totalBytes += static_cast<long long>(size);
    }
    long long transferred = 0;
    size_t fileIndex = 0;
    for (const auto& path : files) {
        if (cancelled(callbacks)) {
            result.cancelled = true;
            result.error = "Backup cancelled.";
            return result;
        }
        FileRecord file;
        const auto relative = path.lexically_relative(sourceRoot);
        if (relative.empty()) {
            result.error = "Couldn't resolve a relative path inside the downloaded game.";
            return result;
        }
        file.relativePath = pathToUtf8(relative);
        ec.clear();
        const auto fileSize = fs::file_size(path, ec);
        if (ec || fileSize > static_cast<uintmax_t>(std::numeric_limits<long long>::max())) {
            result.error = "Couldn't determine the size of " + file.relativePath + ".";
            return result;
        }
        file.size = static_cast<long long>(fileSize);
        auto fileDigest = hasher_.sha256(path);
        if (!fileDigest || !isDigest(*fileDigest)) {
            result.error = "Couldn't compute SHA-256 for " + file.relativePath;
            return result;
        }
        file.digest = lowercase(*fileDigest);
        long long offset = 0;
        int chunkIndex = 0;
        while (offset < file.size) {
            if (cancelled(callbacks)) {
                result.cancelled = true;
                result.error = "Backup cancelled.";
                return result;
            }
            const long long chunkSize = std::min(kChunkBytes, file.size - offset);
            const fs::path chunkPath = jobRoot / std::format("chunk-{}-{}.bin", fileIndex, chunkIndex);
            if (!copyRange(path, offset, chunkSize, chunkPath, callbacks, result.error)) {
                result.cancelled = cancelled(callbacks);
                if (result.cancelled) result.error = "Backup cancelled.";
                return result;
            }
            const auto copiedDigest = hasher_.sha256(chunkPath);
            if (!copiedDigest || !isDigest(*copiedDigest)) {
                result.error = std::format("Couldn't verify chunk {} of {}", chunkIndex + 1, file.relativePath);
                return result;
            }
            const std::string objectName = std::format("{}-f{}-c{}.bin", result.backupId, fileIndex, chunkIndex);
            auto objectId = transport_.uploadObject(objectName, chunkPath, chunkSize, callbacks, adapterError);
            if (!objectId || objectId->empty()) {
                result.cancelled = cancelled(callbacks);
                result.error = result.cancelled ? "Backup cancelled." :
                    (adapterError.empty() ? "The transport did not return an ID for a chunk upload." : adapterError);
                return result;
            }
            file.chunks.push_back({chunkSize, lowercase(*copiedDigest), std::move(*objectId)});
            std::error_code ignored;
            fs::remove(chunkPath, ignored);
            offset += chunkSize;
            transferred += chunkSize;
            result.bytesTransferred += chunkSize;
            report(callbacks, "Uploading " + file.relativePath, transferred, totalBytes);
            ++chunkIndex;
        }
        manifest.files.push_back(std::move(file));
        ++fileIndex;
    }

    const fs::path manifestPath = jobRoot / "manifest.gth";
    {
        std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
        const std::string text = serialize(manifest);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            result.error = "Couldn't write the backup manifest.";
            return result;
        }
    }
    const auto manifestSize = fs::file_size(manifestPath, ec);
    if (ec || manifestSize > static_cast<uintmax_t>(std::numeric_limits<long long>::max())) {
        result.error = "Couldn't determine the backup manifest size.";
        return result;
    }
    if (manifestSize > static_cast<uintmax_t>(kMaxManifestBytes)) {
        result.error = "The backup manifest is larger than Telegram's restore limit; no manifest was published.";
        return result;
    }
    auto manifestId = transport_.uploadObject(result.backupId + "-manifest.gth", manifestPath,
                                                static_cast<long long>(manifestSize), callbacks, adapterError);
    if (!manifestId || manifestId->empty()) {
        result.cancelled = cancelled(callbacks);
        result.error = result.cancelled ? "Backup cancelled." :
            (adapterError.empty() ? "The transport did not return an ID for the backup manifest." : adapterError);
        return result;
    }
    result.manifestObjectId = std::move(*manifestId);
    const BackupManifestRef catalogEntry{result.manifestObjectId, manifest.backupId, manifest.title,
                                         manifest.platform, manifest.version, manifest.createdAt, totalBytes};
    if (!appendCatalog(workingDirectory_ / "backup-catalog.tsv", catalogEntry, result.error)) return result;
    result.success = true;
    report(callbacks, "Backup complete", totalBytes, totalBytes);
    return result;
}

BackupBatchResult BackupService::createBackups(const std::vector<BackupGame>& games,
                                                const BackupCallbacks& callbacks) {
    BackupBatchResult batch;
    batch.games.reserve(games.size());
    for (const auto& game : games) {
        if (cancelled(callbacks)) {
            batch.cancelled = true;
            break;
        }
        BackupCallbacks perGame = callbacks;
        if (callbacks.onProgress) {
            const auto onProgress = callbacks.onProgress;
            const std::string title = game.title;
            perGame.onProgress = [onProgress, title](const std::string& stage, long long done, long long total) {
                onProgress(title + ": " + stage, done, total);
            };
        }
        auto result = createBackup(game, perGame);
        if (result.success) {
            ++batch.backedUp;
        } else {
            ++batch.failed;
            if (result.cancelled) batch.cancelled = true;
        }
        batch.games.push_back(std::move(result));
        if (batch.cancelled) break;
    }
    return batch;
}

std::vector<BackupManifestRef> BackupService::listBackups() const {
    std::vector<BackupManifestRef> entries;
    std::ifstream in(workingDirectory_ / "backup-catalog.tsv", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const auto fields = splitTabs(line);
        if (fields.size() != 8 || fields[0] != "B") continue;
        auto objectId = hexDecode(fields[1]);
        auto backupId = hexDecode(fields[2]);
        auto title = hexDecode(fields[3]);
        auto platform = hexDecode(fields[4]);
        auto version = hexDecode(fields[5]);
        auto createdAt = hexDecode(fields[6]);
        long long totalBytes = 0;
        if (!objectId || objectId->empty() || !backupId || backupId->empty() || !title || title->empty() ||
            !platform || !version || !createdAt || !parseInteger(fields[7], totalBytes)) continue;
        entries.push_back({std::move(*objectId), std::move(*backupId), std::move(*title), std::move(*platform),
                           std::move(*version), std::move(*createdAt), totalBytes});
    }
    std::reverse(entries.begin(), entries.end());
    return entries;
}

BackupResult BackupService::restoreBackup(const std::string& manifestObjectId, const fs::path& destinationRoot,
                                         const BackupCallbacks& callbacks) {
    BackupResult result;
    if (manifestObjectId.empty()) {
        result.error = "A manifest object ID is required to restore a backup.";
        return result;
    }
    const fs::path jobRoot = workingDirectory_ / ("restore-" + core::Guid::newGuid());
    std::error_code ec;
    fs::create_directories(jobRoot, ec);
    if (ec) {
        result.error = "Couldn't create restore staging directory: " + ec.message();
        return result;
    }
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{jobRoot};

    const fs::path manifestPath = jobRoot / "manifest.gth";
    std::string adapterError;
    if (!transport_.downloadObject(manifestObjectId, manifestPath, callbacks, adapterError)) {
        result.cancelled = cancelled(callbacks);
        result.error = result.cancelled ? "Restore cancelled." :
            (adapterError.empty() ? "Couldn't download the backup manifest from Telegram." : adapterError);
        return result;
    }
    const auto manifestSize = fs::file_size(manifestPath, ec);
    if (ec || manifestSize > kMaxManifestBytes) {
        result.error = "The backup manifest is missing or exceeds the allowed size.";
        return result;
    }
    std::ifstream manifestFile(manifestPath, std::ios::binary);
    std::string manifestText((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());
    if (!manifestFile.eof() && manifestFile.fail()) {
        result.error = "Couldn't read the downloaded backup manifest.";
        return result;
    }
    std::string parseError;
    auto manifest = parseManifest(manifestText, parseError);
    if (!manifest) {
        result.error = parseError;
        return result;
    }
    result.backupId = manifest->backupId;

    const std::string restoreFolder = safeLeaf(manifest->title) + "-" + safeLeaf(manifest->platform) + "-" +
        safeLeaf(manifest->version) + "-" + safeLeaf(manifest->backupId);
    const fs::path restoreRoot = destinationRoot / pathFromUtf8(restoreFolder);
    if (fs::exists(restoreRoot, ec)) {
        result.error = "The restore destination already exists: " + restoreRoot.string();
        return result;
    }
    ec.clear();
    const fs::path stagedTree = jobRoot / "restored";
    fs::create_directories(stagedTree, ec);
    if (ec) {
        result.error = "Couldn't create the restore staging tree: " + ec.message();
        return result;
    }
    long long totalBytes = 0;
    for (const auto& file : manifest->files) totalBytes += file.size;
    long long restoredBytes = 0;

    for (size_t fileIndex = 0; fileIndex < manifest->files.size(); ++fileIndex) {
        if (cancelled(callbacks)) {
            result.cancelled = true;
            result.error = "Restore cancelled.";
            return result;
        }
        const auto& file = manifest->files[fileIndex];
        const fs::path target = stagedTree / pathFromUtf8(file.relativePath);
        const fs::path normalized = target.lexically_normal();
        const fs::path normalizedRoot = stagedTree.lexically_normal();
        auto rootIt = normalizedRoot.begin();
        auto targetIt = normalized.begin();
        for (; rootIt != normalizedRoot.end() && targetIt != normalized.end(); ++rootIt, ++targetIt) {
            if (*rootIt != *targetIt) break;
        }
        if (rootIt != normalizedRoot.end()) {
            result.error = "The backup manifest contains a path outside its restore folder.";
            return result;
        }
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            result.error = "Couldn't create a restore folder: " + ec.message();
            return result;
        }
        const fs::path partial = target.string() + ".gth-partial";
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "Couldn't create restored file " + file.relativePath;
            return result;
        }
        long long fileBytes = 0;
        for (size_t chunkIndex = 0; chunkIndex < file.chunks.size(); ++chunkIndex) {
            if (cancelled(callbacks)) {
                result.cancelled = true;
                result.error = "Restore cancelled.";
                return result;
            }
            const auto& chunk = file.chunks[chunkIndex];
            const fs::path chunkPath = jobRoot / std::format("download-{}-{}.bin", fileIndex, chunkIndex);
            if (!transport_.downloadObject(chunk.objectId, chunkPath, callbacks, adapterError)) {
                result.cancelled = cancelled(callbacks);
                result.error = result.cancelled ? "Restore cancelled." :
                    (adapterError.empty() ? "Couldn't download a game chunk." : adapterError);
                return result;
            }
            const auto chunkSize = fs::file_size(chunkPath, ec);
            if (ec || chunkSize != static_cast<uintmax_t>(chunk.size)) {
                result.error = "A downloaded backup chunk has the wrong size.";
                return result;
            }
            const auto digest = hasher_.sha256(chunkPath);
            if (!digest || lowercase(*digest) != chunk.digest) {
                result.error = "A downloaded backup chunk failed SHA-256 verification.";
                return result;
            }
            std::ifstream chunkIn(chunkPath, std::ios::binary);
            std::vector<char> buffer(1 << 20);
            while (chunkIn) {
                if (cancelled(callbacks)) {
                    result.cancelled = true;
                    result.error = "Restore cancelled.";
                    return result;
                }
                chunkIn.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto read = chunkIn.gcount();
                if (read > 0) out.write(buffer.data(), read);
                if (!out) {
                    result.error = "Couldn't write a restored file (disk full?).";
                    return result;
                }
            }
            if (!chunkIn.eof()) {
                result.error = "Couldn't read a downloaded backup chunk.";
                return result;
            }
            fileBytes += chunk.size;
            restoredBytes += chunk.size;
            result.bytesTransferred += chunk.size;
            report(callbacks, "Restoring " + file.relativePath, restoredBytes, totalBytes);
        }
        out.close();
        if (!out || fileBytes != file.size) {
            result.error = "A restored file has the wrong size or could not be flushed.";
            return result;
        }
        const auto fileDigest = hasher_.sha256(partial);
        if (!fileDigest || lowercase(*fileDigest) != file.digest) {
            result.error = "A restored file failed its full-file SHA-256 check: " + file.relativePath;
            return result;
        }
        fs::rename(partial, target, ec);
        if (ec) {
            result.error = "Couldn't finalize restored file " + file.relativePath + ": " + ec.message();
            return result;
        }
    }

    fs::create_directories(destinationRoot, ec);
    if (ec) {
        result.error = "Couldn't create the requested restore destination: " + ec.message();
        return result;
    }
    fs::rename(stagedTree, restoreRoot, ec);
    if (ec) {
        result.error = "Couldn't atomically install the restored game: " + ec.message();
        return result;
    }
    result.success = true;
    report(callbacks, "Restore complete", totalBytes, totalBytes);
    return result;
}

}  // namespace gth::application
