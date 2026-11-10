#include "gth/storage/LocalStorageDetector.h"

#include <windows.h>

#include <chrono>
#include <array>
#include <future>
#include <memory>
#include <thread>

namespace gth::storage {

using core::DetectedStorageInfo;
using core::StorageType;

namespace {

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                      nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr,
                           nullptr);
    return result;
}

// The drive Windows itself is installed on (almost always C:\, but not
// guaranteed) - scanning it for games is never what the user wants: it's
// millions of OS/Program Files entries, none of them a game, and the
// Platform/Title/Region/Version folder heuristic below has no way to tell
// "C:\Program Files\Google\Chrome\Application" isn't a game variant. Queried
// each call rather than cached once, since it's one cheap API call and the
// result should never realistically change within a process lifetime anyway.
std::wstring systemDriveRoot() {
    std::array<wchar_t, MAX_PATH> windowsDir{};
    const UINT windowsDirLength = ::GetWindowsDirectoryW(windowsDir.data(), static_cast<UINT>(windowsDir.size()));
    if (windowsDirLength < 2 || windowsDirLength >= windowsDir.size() || windowsDir[1] != L':') {
        return L"";
    }
    return std::wstring(1, windowsDir[0]) + L":\\";
}

// GetDiskFreeSpaceExW/GetVolumeInformationW give Windows no way to pass a
// timeout, and both can block for many seconds on removable media that's
// asleep/absent or a network share that's gone unreachable - one such drive
// would otherwise stall detect() (and, since detect() runs synchronously
// during a scan, the whole app) indefinitely. Run the pair on a throwaway
// thread and give up waiting after a short deadline instead: the drive is
// then reported unavailable for this scan rather than freezing everything.
// The thread is detached rather than joined on timeout - it finishes
// whenever the OS call eventually returns (or never, for a truly wedged
// driver), but by then nothing is waiting on it.
constexpr auto kDriveProbeTimeout = std::chrono::milliseconds(1500);

struct DriveProbeResult {
    bool spaceOk = false;
    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    bool volumeOk = false;
    std::wstring volumeName;
    std::wstring fileSystemName;
};

DriveProbeResult probeDriveWithTimeout(const std::wstring& rootPath) {
    auto task = std::make_shared<std::packaged_task<DriveProbeResult()>>([rootPath] {
        DriveProbeResult r;
        ULARGE_INTEGER totalFreeBytes{};
        r.spaceOk =
            ::GetDiskFreeSpaceExW(rootPath.c_str(), &r.freeBytesAvailable, &r.totalBytes, &totalFreeBytes) != 0;

        std::array<wchar_t, MAX_PATH + 1> volumeName{};
        std::array<wchar_t, MAX_PATH + 1> fileSystemName{};
        r.volumeOk = ::GetVolumeInformationW(rootPath.c_str(), volumeName.data(), MAX_PATH, nullptr, nullptr, nullptr,
                                              fileSystemName.data(), MAX_PATH) != 0;
        r.volumeName = volumeName.data();
        r.fileSystemName = fileSystemName.data();
        return r;
    });

    std::future<DriveProbeResult> future = task->get_future();
    std::jthread([task] { (*task)(); }).detach();

    if (future.wait_for(kDriveProbeTimeout) != std::future_status::ready) {
        return DriveProbeResult{};  // spaceOk=false, volumeOk=false -> reported unavailable
    }
    return future.get();
}

}  // namespace

std::vector<DetectedStorageInfo> LocalStorageDetector::detect() {
    std::vector<DetectedStorageInfo> results;

    const std::wstring systemRoot = systemDriveRoot();
    const DWORD driveMask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(driveMask & (1u << i))) continue;

        const wchar_t letter = static_cast<wchar_t>(L'A' + i);
        const std::wstring rootPath = std::wstring(1, letter) + L":\\";
        if (!systemRoot.empty() && _wcsicmp(rootPath.c_str(), systemRoot.c_str()) == 0) continue;

        StorageType type;
        switch (::GetDriveTypeW(rootPath.c_str())) {
            case DRIVE_FIXED:
                type = StorageType::LocalHdd;
                break;
            case DRIVE_REMOVABLE:
                type = StorageType::Usb;
                break;
            case DRIVE_REMOTE:
                type = StorageType::NetworkShare;
                break;
            default:
                // CD-ROM, RAM disk, "no root directory", or unrecognized -
                // not a storage location this detector manages.
                continue;
        }

        DetectedStorageInfo info;
        info.type = type;
        info.rootPath = wideToUtf8(rootPath);

        const DriveProbeResult probe = probeDriveWithTimeout(rootPath);

        info.isAvailable = probe.spaceOk;
        if (probe.spaceOk) {
            info.totalCapacityBytes = static_cast<long long>(probe.totalBytes.QuadPart);
            info.freeSpaceBytes = static_cast<long long>(probe.freeBytesAvailable.QuadPart);
        }
        if (probe.volumeOk && !probe.fileSystemName.empty()) {
            info.fileSystem = wideToUtf8(probe.fileSystemName);
        }

        const std::string letterUtf8 = wideToUtf8(std::wstring(1, letter) + L":");
        info.name = (probe.volumeOk && !probe.volumeName.empty()) ? (wideToUtf8(probe.volumeName) + " (" + letterUtf8 + ")")
                                                                    : letterUtf8;

        results.push_back(std::move(info));
    }

    return results;
}

}  // namespace gth::storage
