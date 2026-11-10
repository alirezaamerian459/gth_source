#pragma once

#include <filesystem>
#include <system_error>

#include "gth/core/Guid.h"

namespace gth::test {

// Keep unit-test scratch data in one unpredictable child of the current
// user's OS temp directory. Tests never write directly into the shared root.
class TestTempRoot {
public:
    TestTempRoot()
        : path_(std::filesystem::temp_directory_path() / ("gth_" + core::Guid::newGuid())) {  // NOSONAR: per-user Windows Temp plus atomic GUID directory; see class comment.
        std::filesystem::create_directories(path_);
    }

    ~TestTempRoot() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

inline const std::filesystem::path& testTempRoot() {
    static const TestTempRoot root;
    return root.path();
}

}  // namespace gth::test
