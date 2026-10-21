#include "gth/infrastructure/LocalGameBackupSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include "gth/core/enums/GameVariantStatus.h"

namespace gth::infrastructure {

namespace fs = std::filesystem;

namespace {

fs::path fromUtf8(const std::string& text) {
    std::u8string value;
    value.reserve(text.size());
    for (const unsigned char byte : text) value.push_back(static_cast<char8_t>(byte));
    return fs::path(value);
}

std::string toUtf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool hasReparsePoint(const fs::path& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

struct SourceFile {
    fs::path path;
    fs::path relative;
    uintmax_t size = 0;
    fs::file_time_type modified{};
};

bool insideRoot(const fs::path& root, const fs::path& path) {
    const fs::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& part : relative) {
        if (part == L"..") return false;
    }
    return true;
}

bool copyFile(const fs::path& source, const fs::path& target, long long& done, long long total,
              const application::BackupCallbacks& callbacks, std::string& error) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        error = "Couldn't open a local game file: " + toUtf8(source.filename());
        return false;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Couldn't create a staged game file: " + toUtf8(target.filename());
        return false;
    }

    std::vector<char> buffer(1 << 20);
    while (in) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) return false;
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = in.gcount();
        if (read > 0) {
            out.write(buffer.data(), read);
            if (!out) {
                error = "Couldn't stage a local game file (disk full?).";
                return false;
            }
            done += read;
            if (callbacks.onProgress) callbacks.onProgress("Copying local game files", done, total);
        }
    }
    if (!in.eof()) {
        error = "A local game file couldn't be read completely.";
        return false;
    }
    out.close();
    if (!out) {
        error = "Couldn't finish staging a local game file.";
        return false;
    }
    return true;
}

}  // namespace

bool LocalGameBackupSource::downloadGame(const application::BackupGame& game, const fs::path& destination,
                                         const application::BackupCallbacks& callbacks, std::string& error) {
    const auto variant = variants_.getById(game.id);
    if (!variant || variant->status != core::GameVariantStatus::Active) {
        error = "This game is no longer available in the GTH library.";
        return false;
    }
    const auto storage = storages_.getById(variant->storageId);
    if (!storage || !storage->isAvailable) {
        error = "The storage containing this game is currently unavailable.";
        return false;
    }

    const fs::path relative = fromUtf8(variant->relativePath);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        error = "The GTH library contains an invalid game path.";
        return false;
    }
    for (const auto& part : relative) {
        if (part == L"." || part == L"..") {
            error = "The GTH library contains an unsafe game path.";
            return false;
        }
    }

    std::error_code ec;
    const fs::path root = fs::canonical(fromUtf8(storage->rootPath), ec);
    if (ec || !fs::is_directory(root, ec) || ec) {
        error = "The storage root can't be reached.";
        return false;
    }
    const fs::path candidate = root / relative;
    const fs::path gameRoot = fs::canonical(candidate, ec);
    if (ec || !insideRoot(root, gameRoot) || !fs::is_directory(gameRoot, ec) || ec) {
        error = "The game's GTH folder can't be reached or is outside its registered storage.";
        return false;
    }

    // Refuse junctions/symlinks in the path leading to the game folder so a
    // registered path cannot redirect staging outside the selected storage.
    fs::path component = root;
    if (hasReparsePoint(component)) {
        error = "The registered storage root is a Windows reparse point; backup stopped for safety.";
        return false;
    }
    for (const auto& part : relative) {
        component /= part;
        if (hasReparsePoint(component)) {
            error = "The GTH game path contains a Windows reparse point; backup stopped for safety.";
            return false;
        }
    }

    std::vector<SourceFile> files;
    long long total = 0;
    fs::recursive_directory_iterator it(gameRoot, fs::directory_options::none, ec), end;
    for (; it != end; it.increment(ec)) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) {
            error = "Backup cancelled.";
            return false;
        }
        if (ec) {
            error = "Couldn't enumerate the local game folder: " + ec.message();
            return false;
        }
        const auto status = it->symlink_status(ec);
        if (ec) {
            error = "Couldn't inspect a local game path: " + ec.message();
            return false;
        }
        if (fs::is_symlink(status) || hasReparsePoint(it->path())) {
            error = "The local game folder contains a symbolic link or junction; backup stopped to avoid reading outside it.";
            return false;
        }
        if (!fs::is_regular_file(status)) continue;
        const auto size = it->file_size(ec);
        if (ec || size > static_cast<uintmax_t>(std::numeric_limits<long long>::max()) ||
            total > std::numeric_limits<long long>::max() - static_cast<long long>(size)) {
            error = "A local game file has an invalid or overflowing size.";
            return false;
        }
        const fs::path rel = it->path().lexically_relative(gameRoot);
        if (!insideRoot(gameRoot, it->path())) {
            error = "A local game file resolved outside its GTH folder.";
            return false;
        }
        const auto modified = it->last_write_time(ec);
        if (ec) {
            error = "Couldn't read a local game's file timestamp.";
            return false;
        }
        total += static_cast<long long>(size);
        files.push_back({it->path(), rel, size, modified});
    }
    if (ec) {
        error = "Couldn't finish enumerating the local game folder: " + ec.message();
        return false;
    }
    if (files.empty()) {
        error = "The selected game folder contains no files.";
        return false;
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.relative < b.relative; });

    long long done = 0;
    for (const auto& file : files) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) {
            error = "Backup cancelled.";
            return false;
        }
        if (hasReparsePoint(file.path)) {
            error = "A local game file changed into a symbolic link or junction while backup was running.";
            return false;
        }
        const fs::path target = destination / file.relative;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "Couldn't create the local backup staging folder: " + ec.message();
            return false;
        }
        if (!copyFile(file.path, target, done, total, callbacks, error)) {
            if (error.empty()) error = "Backup cancelled.";
            return false;
        }
        const auto currentSize = fs::file_size(file.path, ec);
        const auto currentModified = fs::last_write_time(file.path, ec);
        if (ec || currentSize != file.size || currentModified != file.modified) {
            error = "A local game file changed while it was being staged; backup stopped.";
            return false;
        }
    }
    return true;
}

}  // namespace gth::infrastructure
