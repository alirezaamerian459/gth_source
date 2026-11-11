#include "gth/storage/LocalStorageDetector.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <chrono>
#include <array>
#include <cwctype>
#include <filesystem>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

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

std::wstring lowerCopy(std::wstring value) {
    for (wchar_t& c : value) c = std::towlower(c);
    return value;
}

struct DesktopShareShortcut {
    std::wstring name;
    std::wstring target;
};

// Windows does not enumerate an un-mapped UNC shortcut through GetLogicalDrives.
// Resolve Desktop .lnk files that point at reachable UNC directories so a share
// such as "Public (NAS...)" participates in the same storage scan as a mapped
// drive. Only directory targets are accepted; ordinary file/application links
// are never treated as storage.
std::vector<DesktopShareShortcut> desktopShareShortcuts() {
    std::vector<DesktopShareShortcut> result;

    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool ownsCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return result;
    struct ComGuard {
        bool owns = false;
        ~ComGuard() {
            if (owns) ::CoUninitialize();
        }
    } comGuard{ownsCom};

    PWSTR desktopPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktopPath)) || !desktopPath) {
        return result;
    }
    const std::filesystem::path desktop(desktopPath);
    ::CoTaskMemFree(desktopPath);

    std::error_code ec;
    std::set<std::wstring> seenTargets;
    for (std::filesystem::directory_iterator it(desktop, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec || lowerCopy(it->path().extension().wstring()) != L".lnk") continue;

        IShellLinkW* shellLink = nullptr;
        if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                       reinterpret_cast<void**>(&shellLink))) || shellLink == nullptr) {
            continue;
        }

        IPersistFile* persistFile = nullptr;
        const bool loaded = SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile,
                                                                  reinterpret_cast<void**>(&persistFile))) &&
                            persistFile != nullptr && SUCCEEDED(persistFile->Load(it->path().c_str(), STGM_READ));
        if (!loaded) {
            if (persistFile) persistFile->Release();
            shellLink->Release();
            continue;
        }

        std::array<wchar_t, MAX_PATH> targetBuffer{};
        WIN32_FIND_DATAW findData{};
        const HRESULT pathResult = shellLink->GetPath(targetBuffer.data(), static_cast<int>(targetBuffer.size()),
                                                       &findData, SLGP_RAWPATH);
        persistFile->Release();
        shellLink->Release();
        if (FAILED(pathResult)) continue;

        const std::wstring target = targetBuffer.data();
        if (target.rfind(L"\\\\", 0) != 0) continue;
        const DWORD attributes = ::GetFileAttributesW(target.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

        const std::wstring key = lowerCopy(target);
        if (!seenTargets.insert(key).second) continue;
        result.push_back({it->path().stem().wstring(), target});
    }
    return result;
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

    for (const auto& shortcut : desktopShareShortcuts()) {
        DetectedStorageInfo info;
        info.type = StorageType::NetworkShare;
        info.rootPath = wideToUtf8(shortcut.target);

        const DriveProbeResult probe = probeDriveWithTimeout(shortcut.target);
        info.isAvailable = probe.spaceOk;
        if (probe.spaceOk) {
            info.totalCapacityBytes = static_cast<long long>(probe.totalBytes.QuadPart);
            info.freeSpaceBytes = static_cast<long long>(probe.freeBytesAvailable.QuadPart);
        }
        if (probe.volumeOk && !probe.fileSystemName.empty()) info.fileSystem = wideToUtf8(probe.fileSystemName);
        info.name = wideToUtf8(shortcut.name) + " (" + info.rootPath + ")";
        results.push_back(std::move(info));
    }

    return results;
}

}  // namespace gth::storage
