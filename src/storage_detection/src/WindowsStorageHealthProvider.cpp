#include "gth/storage/WindowsStorageHealthProvider.h"

#include <windows.h>
#include <winioctl.h>
#include <wbemidl.h>
#include <ntddscsi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace gth::storage {

using core::PhysicalDiskHealth;
using core::StorageHealthInfo;

std::vector<int> diskNumbersFromVolumeExtents(const void* buffer, std::size_t bytes) {
    if (buffer == nullptr || bytes < offsetof(VOLUME_DISK_EXTENTS, Extents)) return {};

    const auto* extents = static_cast<const VOLUME_DISK_EXTENTS*>(buffer);
    const std::size_t count = extents->NumberOfDiskExtents;
    // Compared by division so a bogus huge count can't overflow the size arithmetic.
    if (count > (bytes - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT)) return {};

    std::set<int> numbers;
    for (std::size_t i = 0; i < count; ++i) numbers.insert(static_cast<int>(extents->Extents[i].DiskNumber));
    return std::vector<int>(numbers.begin(), numbers.end());
}

namespace {

// The physical disks a drive-letter root path (e.g. "D:\\") occupies, or empty for anything that isn't a
// local volume (network shares, subst'd paths, etc.) - the IOCTLs simply fail for those, which is exactly
// the "can't determine, don't guess" case this provider is built around.
std::vector<int> resolveVolumeDiskNumbers(const std::string& rootPath) {
    if (rootPath.size() < 2 || rootPath[1] != ':') return {};

    const std::wstring volumePath = L"\\\\.\\" + std::wstring(1, static_cast<wchar_t>(rootPath[0])) + L":";
    HANDLE handle = ::CreateFileW(volumePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    std::vector<int> numbers;

    // Every extent of the volume: one for a plain volume, one per disk for a spanned/striped one. The
    // structure is variable-length, so when the buffer is too small Windows fills in the extent count
    // and reports ERROR_MORE_DATA - grow to fit and ask again.
    std::vector<std::byte> buffer(sizeof(VOLUME_DISK_EXTENTS) + 7 * sizeof(DISK_EXTENT));
    for (int attempt = 0; attempt < 3 && numbers.empty(); ++attempt) {
        DWORD returned = 0;
        if (::DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
            numbers = diskNumbersFromVolumeExtents(buffer.data(), returned);
            break;
        }
        if (::GetLastError() != ERROR_MORE_DATA) break;
        const DWORD count = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data())->NumberOfDiskExtents;
        buffer.resize(offsetof(VOLUME_DISK_EXTENTS, Extents) + static_cast<std::size_t>(count) * sizeof(DISK_EXTENT));
    }

    // Older/odd volumes that don't answer the extents query still name their one disk.
    if (numbers.empty()) {
        STORAGE_DEVICE_NUMBER deviceNumber{};
        DWORD returned = 0;
        if (::DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &deviceNumber, sizeof(deviceNumber),
                               &returned, nullptr)) {
            numbers.push_back(static_cast<int>(deviceNumber.DeviceNumber));
        }
    }
    ::CloseHandle(handle);
    return numbers;
}

#pragma pack(push, 1)
struct AtaPassThroughBuffer {
    ATA_PASS_THROUGH_EX header;
    std::array<UCHAR, 512> data{};
};
#pragma pack(pop)
static_assert(offsetof(AtaPassThroughBuffer, data) == sizeof(ATA_PASS_THROUGH_EX));
static_assert(sizeof(AtaPassThroughBuffer) == sizeof(ATA_PASS_THROUGH_EX) + 512);

// One ATA SMART sub-command that returns a 512-byte sector: READ DATA (feature 0xD0) or READ THRESHOLDS (0xD1).
// Both only read; nothing is written to the disk and no self-test is started. Command 0xB0 is SMART; the
// 0x4F / 0xC2 signature in the LBA mid/high registers is what ATA requires of every SMART sub-command.
bool ataSmartReadSector(HANDLE handle, UCHAR feature, std::array<unsigned char, 512>& sector) {
    AtaPassThroughBuffer buffer{};
    buffer.header.Length = sizeof(ATA_PASS_THROUGH_EX);
    buffer.header.AtaFlags = ATA_FLAGS_DRDY_REQUIRED | ATA_FLAGS_DATA_IN;
    buffer.header.DataTransferLength = sizeof(buffer.data);
    buffer.header.TimeOutValue = 3;
    buffer.header.DataBufferOffset = offsetof(AtaPassThroughBuffer, data);
    buffer.header.CurrentTaskFile[0] = feature;
    buffer.header.CurrentTaskFile[1] = 0x01;
    buffer.header.CurrentTaskFile[2] = 0x00;
    buffer.header.CurrentTaskFile[3] = 0x4F;
    buffer.header.CurrentTaskFile[4] = 0xC2;
    buffer.header.CurrentTaskFile[5] = 0xA0;
    buffer.header.CurrentTaskFile[6] = 0xB0;

    DWORD bytesReturned = 0;
    if (!::DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH, &buffer, sizeof(buffer), &buffer, sizeof(buffer),
                            &bytesReturned, nullptr)) {
        return false;
    }
    std::memcpy(sector.data(), buffer.data.data(), sector.size());
    return true;
}

// The NVMe SMART / Health Information log page (log id 2) through the standard storage-protocol query -
// how Windows itself reads it. NVMe disks have no ATA SMART table.
std::optional<NvmeHealth> readNvmeHealth(int driveNumber) {
    const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", driveNumber);
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        handle = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

    constexpr std::size_t kLogBytes = 512;
    std::vector<unsigned char> buffer(offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters) +
                                      sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + kLogBytes);
    auto* query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buffer.data());
    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;
    auto* protocol = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(query->AdditionalParameters);
    protocol->ProtocolType = ProtocolTypeNvme;
    protocol->DataType = NVMeDataTypeLogPage;
    protocol->ProtocolDataRequestValue = 2;  // NVME_LOG_PAGE_HEALTH_INFO
    protocol->ProtocolDataRequestSubValue = 0;
    protocol->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    protocol->ProtocolDataLength = static_cast<DWORD>(kLogBytes);

    DWORD returned = 0;
    const bool ok = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, buffer.data(), static_cast<DWORD>(buffer.size()),
                                       buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr) != 0;
    ::CloseHandle(handle);
    if (!ok || returned < sizeof(STORAGE_PROTOCOL_DATA_DESCRIPTOR)) return std::nullopt;

    const auto* descriptor = reinterpret_cast<const STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buffer.data());
    const auto& data = descriptor->ProtocolSpecificData;
    if (data.ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) || data.ProtocolDataLength < kLogBytes) return std::nullopt;

    // The log sits after the descriptor, at an offset counted from the start of the protocol-specific block.
    const std::size_t logStart = offsetof(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData) + data.ProtocolDataOffset;
    if (logStart + kLogBytes > returned) return std::nullopt;
    return parseNvmeHealthLog(buffer.data() + logStart, kLogBytes);
}

// The drive's own overall SMART verdict ("is failure predicted?"), available even for disks the Storage
// WMI provider doesn't describe (dynamic disks, some USB bridges). Needs no elevation. Same coarse
// translation as the WMI health enum below: a real yes/no signal, not an invented precision.
std::optional<int> readHealthViaPredictFailure(int driveNumber) {
    const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", driveNumber);
    HANDLE handle = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

    STORAGE_PREDICT_FAILURE prediction{};
    DWORD returned = 0;
    const bool ok = ::DeviceIoControl(handle, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &prediction, sizeof(prediction),
                                       &returned, nullptr) != 0;
    ::CloseHandle(handle);
    if (!ok) return std::nullopt;
    return prediction.PredictFailure ? 10 : 100;
}

// "Vendor Product" from the storage device descriptor - readable without elevation.
std::string readModelViaIoctl(int driveNumber) {
    const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", driveNumber);
    HANDLE handle = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    alignas(8) std::array<unsigned char, 1024> buffer{};
    DWORD returned = 0;
    const bool ok = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                                       static_cast<DWORD>(buffer.size()),
                                       &returned, nullptr) != 0;
    ::CloseHandle(handle);
    if (!ok || returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) return {};

    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
    const auto text = [&](DWORD offset) -> std::string {
        if (offset == 0 || offset >= returned) return {};
        std::string value(reinterpret_cast<const char*>(buffer.data()) + offset);
        while (!value.empty() && value.back() == ' ') value.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        return value;
    };
    std::string model = text(descriptor->VendorIdOffset);
    const std::string product = text(descriptor->ProductIdOffset);
    if (!model.empty() && !product.empty()) model += ' ';
    return model + product;
}

// Minimal BSTR RAII - avoids pulling in comdef.h/_bstr_t just for this.
class AutoBstr {
public:
    explicit AutoBstr(const std::wstring& text) : value_(::SysAllocString(text.c_str())) {}
    ~AutoBstr() { ::SysFreeString(value_); }
    AutoBstr(const AutoBstr&) = delete;
    AutoBstr& operator=(const AutoBstr&) = delete;
    operator BSTR() const { return value_; }

private:
    BSTR value_;
};

// Scoped, best-effort COM init: WMI works the same whether this thread
// already had COM initialized (e.g. by another view's Shell dialog) or
// not - only CoUninitialize when this call is the one that initialized it.
class ScopedCom {
public:
    ScopedCom() {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns_ = (hr == S_OK || hr == S_FALSE);
        usable_ = owns_ || hr == RPC_E_CHANGED_MODE;
    }
    ~ScopedCom() {
        if (owns_) ::CoUninitialize();
    }
    bool usable() const { return usable_; }

private:
    bool owns_ = false;
    bool usable_ = false;
};

// Wraps one MSFT_PhysicalDisk/MSFT_StorageReliabilityCounter WMI session.
// Every interface pointer is released in the destructor regardless of
// which step failed, so callers can just early-return on any error.
class WmiStorageSession {
public:
    bool connect() {
        if (FAILED(::CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                       reinterpret_cast<void**>(&locator_)))) {
            return false;
        }

        AutoBstr ns(L"ROOT\\Microsoft\\Windows\\Storage");
        if (FAILED(locator_->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_))) {
            return false;
        }

        ::CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }

    // Runs a WQL/associator query and returns every result object (capped, as
    // no real machine has more disks than that). Caller owns and must Release()
    // each returned pointer.
    std::vector<IWbemClassObject*> queryAll(const std::wstring& query) {
        std::vector<IWbemClassObject*> results;
        AutoBstr language(L"WQL");
        AutoBstr text(query);

        IEnumWbemClassObject* enumerator = nullptr;
        const HRESULT hr = services_->ExecQuery(language, text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                                  nullptr, &enumerator);
        if (FAILED(hr) || enumerator == nullptr) return results;

        while (results.size() < 64) {
            IWbemClassObject* result = nullptr;
            ULONG returned = 0;
            if (FAILED(enumerator->Next(5000, 1, &result, &returned)) || returned != 1) break;
            results.push_back(result);
        }
        enumerator->Release();
        return results;
    }

    // First result object, or nullptr. Caller owns and must Release() it.
    IWbemClassObject* queryFirst(const std::wstring& query) {
        auto all = queryAll(query);
        if (all.empty()) return nullptr;
        for (std::size_t i = 1; i < all.size(); ++i) all[i]->Release();
        return all.front();
    }

    // MSFT_StorageReliabilityCounter isn't reachable through an ASSOCIATORS
    // OF query - it's a dynamically-computed embedded object returned by
    // calling MSFT_PhysicalDisk's own GetStorageReliabilityCounter() method
    // (the same call PowerShell's Get-StorageReliabilityCounter cmdlet
    // makes). objectPath is the __PATH of the specific instance to invoke
    // it on. Caller owns and must Release() the returned pointer.
    IWbemClassObject* getEmbeddedMethodResult(BSTR objectPath, const wchar_t* methodName,
                                               const wchar_t* outParamName) {
        AutoBstr method(methodName);
        IWbemClassObject* outParams = nullptr;
        const HRESULT hr =
            services_->ExecMethod(objectPath, method, 0, nullptr, nullptr, &outParams, nullptr);
        if (FAILED(hr) || outParams == nullptr) return nullptr;

        VARIANT embedded;
        ::VariantInit(&embedded);
        IWbemClassObject* result = nullptr;
        if (SUCCEEDED(outParams->Get(outParamName, 0, &embedded, nullptr, nullptr)) && embedded.vt == VT_UNKNOWN &&
            embedded.punkVal != nullptr) {
            embedded.punkVal->QueryInterface(IID_IWbemClassObject, reinterpret_cast<void**>(&result));
        }
        ::VariantClear(&embedded);
        outParams->Release();
        return result;
    }

    ~WmiStorageSession() {
        if (services_ != nullptr) services_->Release();
        if (locator_ != nullptr) locator_->Release();
    }

private:
    IWbemLocator* locator_ = nullptr;
    IWbemServices* services_ = nullptr;
};

std::optional<int> readIntProperty(IWbemClassObject* obj, const wchar_t* name) {
    VARIANT value;
    ::VariantInit(&value);
    std::optional<int> result;
    if (SUCCEEDED(obj->Get(name, 0, &value, nullptr, nullptr)) && value.vt != VT_NULL && value.vt != VT_EMPTY) {
        result = static_cast<int>(value.lVal);
    }
    ::VariantClear(&value);
    return result;
}

std::optional<std::wstring> readStringProperty(IWbemClassObject* obj, const wchar_t* name) {
    VARIANT value;
    ::VariantInit(&value);
    std::optional<std::wstring> result;
    if (SUCCEEDED(obj->Get(name, 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR && value.bstrVal != nullptr) {
        result = std::wstring(value.bstrVal);
    }
    ::VariantClear(&value);
    return result;
}

// WMI hands 64-bit integers (like MSFT_PhysicalDisk.Size) over as strings.
std::optional<unsigned long long> readUInt64Property(IWbemClassObject* obj, const wchar_t* name) {
    VARIANT value;
    ::VariantInit(&value);
    std::optional<unsigned long long> result;
    if (SUCCEEDED(obj->Get(name, 0, &value, nullptr, nullptr))) {
        if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
            result = ::wcstoull(value.bstrVal, nullptr, 10);
        } else if (value.vt == VT_I8) {
            result = static_cast<unsigned long long>(value.llVal);
        } else if (value.vt == VT_I4 || value.vt == VT_UI4) {
            result = static_cast<unsigned long long>(static_cast<unsigned int>(value.lVal));
        }
    }
    ::VariantClear(&value);
    return result;
}

// MSFT_PhysicalDisk.HealthStatus is an enum (0 Healthy / 1 Warning /
// 2 Unhealthy / 5 Unknown), not a percentage - Windows doesn't expose a
// finer-grained number through this API. Mapping it to a coarse percent
// keeps the Storage page's health column simple, while staying honest:
// this is a translation of a real signal, not an invented precision.
std::optional<int> healthPercentFromStatus(int healthStatus) {
    switch (healthStatus) {
        case 0: return 100;             // Healthy
        case 1: return 50;              // Warning
        case 2: return 10;              // Unhealthy
        default: return std::nullopt;   // Unknown
    }
}

// If disk `number` is really a Storage Spaces virtual disk, the numbers of the pool's physical disks that
// hold its data (which is where the temperature and health actually live); empty when it is an ordinary disk.
std::vector<int> storageSpacesMemberDisks(WmiStorageSession& session, int number) {
    std::vector<int> members;

    IWbemClassObject* disk = session.queryFirst(std::format(L"SELECT * FROM MSFT_Disk WHERE Number = {}", number));
    if (disk == nullptr) return members;
    const auto diskPath = readStringProperty(disk, L"__RELPATH");
    disk->Release();
    if (!diskPath.has_value() || diskPath->empty()) return members;

    for (IWbemClassObject* virtualDisk : session.queryAll(L"ASSOCIATORS OF {" + *diskPath +
                                                           L"} WHERE AssocClass = MSFT_VirtualDiskToDisk")) {
        const auto virtualPath = readStringProperty(virtualDisk, L"__RELPATH");
        virtualDisk->Release();
        if (!virtualPath.has_value()) continue;

        const auto physicalDisks =
            session.queryAll(L"ASSOCIATORS OF {" + *virtualPath + L"} WHERE AssocClass = MSFT_VirtualDiskToPhysicalDisk");
        for (IWbemClassObject* physical : physicalDisks) {
            const auto id = readStringProperty(physical, L"DeviceId");
            physical->Release();
            if (!id.has_value()) continue;

            wchar_t* end = nullptr;
            const long value = ::wcstol(id->c_str(), &end, 10);
            if (end == id->c_str() || value < 0) continue;
            members.push_back(static_cast<int>(value));
        }
    }
    return members;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), len, nullptr, nullptr);
    return utf8;
}

// Read Windows' optional WMI properties for one disk; the SMART fallbacks stay in queryDiskBlocking.
void queryWindowsPhysicalDiskInfo(int driveNumber, PhysicalDiskHealth& info, std::optional<bool>& solidState) {
    ScopedCom com;
    if (!com.usable()) return;

    WmiStorageSession session;
    if (!session.connect()) return;

    const std::wstring idText = std::to_wstring(driveNumber);
    IWbemClassObject* disk =
        session.queryFirst(L"SELECT * FROM MSFT_PhysicalDisk WHERE DeviceId = '" + idText + L"'");
    if (disk == nullptr) return;

    if (const auto status = readIntProperty(disk, L"HealthStatus"); status.has_value()) {
        info.healthPercent = healthPercentFromStatus(*status);
    }
    if (const auto mediaType = readIntProperty(disk, L"MediaType"); mediaType.has_value()) {
        if (*mediaType == 4) solidState = true;
        if (*mediaType == 3) solidState = false;
    }
    if (const auto name = readStringProperty(disk, L"FriendlyName"); name.has_value()) info.model = wideToUtf8(*name);
    if (const auto size = readUInt64Property(disk, L"Size"); size.has_value()) info.sizeBytes = *size;

    if (const auto path = readStringProperty(disk, L"__PATH"); path.has_value()) {
        AutoBstr objectPath(*path);
        IWbemClassObject* counters =
            session.getEmbeddedMethodResult(objectPath, L"GetStorageReliabilityCounter", L"StorageReliabilityCounter");
        if (counters != nullptr) {
            info.temperatureCelsius = readIntProperty(counters, L"Temperature");
            counters->Release();
        }
    }
    disk->Release();
}

// Everything known about ONE physical disk. Runs on its own thread (see queryDisksBlocking) so several
// disks are asked at once.
PhysicalDiskHealth queryDiskBlocking(int driveNumber) {
    PhysicalDiskHealth info;
    info.diskNumber = driveNumber;
    std::optional<bool> solidState;  // from Windows' MediaType: 4 = SSD, 3 = HDD

    // WMI first (works unelevated on most drives); the fallbacks below run
    // independently of whether COM/WMI succeeded, since they don't use either.
    queryWindowsPhysicalDiskInfo(driveNumber, info, solidState);

    // The disk's own SMART data: a real health estimate and, when Windows gave none, the temperature. Windows'
    // three-level verdict still counts - if it says Warning or Unhealthy, that is never scored away.
    std::optional<HealthAssessment> smart;
    if (const auto attributes = readAtaSmartAttributes(driveNumber); attributes.has_value()) {
        smart = assessAtaHealth(*attributes, solidState);
        if (!info.temperatureCelsius.has_value()) info.temperatureCelsius = ataTemperatureCelsius(*attributes);
    } else if (const auto nvme = readNvmeHealth(driveNumber); nvme.has_value()) {
        smart = assessNvmeHealth(*nvme);
        if (!info.temperatureCelsius.has_value()) info.temperatureCelsius = nvme->temperatureCelsius;
    }
    if (smart.has_value()) {
        const std::optional<int> windowsHealth = info.healthPercent;
        info.healthPercent = (windowsHealth.has_value() && *windowsHealth < smart->percent) ? *windowsHealth : smart->percent;
        info.healthDetail = smart->detail;
        if (windowsHealth.has_value() && *windowsHealth < smart->percent) {
            info.healthDetail = *windowsHealth <= 10 ? "Windows reports this disk as Unhealthy" : "Windows reports a warning for this disk";
        }
    }
    if (!info.healthPercent.has_value()) info.healthPercent = readHealthViaPredictFailure(driveNumber);
    if (info.model.empty()) info.model = readModelViaIoctl(driveNumber);
    return info;
}

void expandStorageSpacesPhysicalDiskNumbers(const std::vector<int>& diskNumbers, std::set<int>& toQuery) {
    ScopedCom com;
    if (!com.usable()) return;

    WmiStorageSession session;
    if (!session.connect()) return;

    for (const int number : diskNumbers) {
        const auto members = storageSpacesMemberDisks(session, number);
        if (members.empty()) continue;
        toQuery.erase(number);
        toQuery.insert(members.begin(), members.end());
    }
}

// All the disks behind a set of disk numbers, each queried separately (in parallel).
std::vector<PhysicalDiskHealth> queryDisksBlocking(const std::vector<int>& diskNumbers) {
    std::set<int> toQuery(diskNumbers.begin(), diskNumbers.end());

    // A Storage Spaces virtual disk stands for the pool's real disks - swap it for them.
    expandStorageSpacesPhysicalDiskNumbers(diskNumbers, toQuery);

    std::vector<std::future<PhysicalDiskHealth>> pending;
    std::vector<std::jthread> workers;
    pending.reserve(toQuery.size());
    workers.reserve(toQuery.size());
    for (const int number : toQuery) {
        std::packaged_task<PhysicalDiskHealth()> task([number] { return queryDiskBlocking(number); });
        pending.push_back(task.get_future());
        workers.emplace_back([task = std::move(task)]() mutable { task(); });
    }
    workers.clear();  // jthread joins each disk query before its result is read

    std::vector<PhysicalDiskHealth> disks;
    disks.reserve(pending.size());
    for (auto& one : pending) disks.push_back(one.get());
    return disks;
}

// The WMI session (COM activation, a network-transport-capable ExecQuery/
// ExecMethod round trip) and the ATA passthrough fallback can each stall for
// seconds on a drive whose controller/driver doesn't respond cleanly -
// neither Win32 nor WMI offers a way to bound them directly. Run the whole
// lookup on a throwaway thread and give up waiting after a short deadline,
// same reasoning and pattern as LocalStorageDetector's per-drive probe: a
// stuck drive then reports "unknown health" for this scan instead of
// freezing it. The thread is detached, not joined, on timeout. The disks are
// asked in parallel, so several disks take about as long as one.
constexpr auto kHealthQueryTimeout = std::chrono::milliseconds(5000);

}  // namespace

std::optional<std::vector<SmartAttribute>> readAtaSmartAttributes(int diskNumber) {
    const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", diskNumber);
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

    std::array<unsigned char, 512> data{};
    if (!ataSmartReadSector(handle, 0xD0, data)) {
        ::CloseHandle(handle);
        return std::nullopt;
    }
    auto attributes = parseAtaSmartAttributes(data.data(), data.size());

    std::array<unsigned char, 512> thresholds{};
    if (ataSmartReadSector(handle, 0xD1, thresholds)) {
        applyAtaSmartThresholds(attributes, thresholds.data(), thresholds.size());
    }
    ::CloseHandle(handle);

    if (attributes.empty()) return std::nullopt;
    return attributes;
}

std::optional<std::vector<PhysicalDiskHealth>> WindowsStorageHealthProvider::queryDisks(const std::string& rootPath) {
    const auto diskNumbers = resolveVolumeDiskNumbers(rootPath);
    if (diskNumbers.empty()) return std::vector<PhysicalDiskHealth>{};

    auto task = std::make_shared<std::packaged_task<std::vector<PhysicalDiskHealth>()>>(
        [diskNumbers] { return queryDisksBlocking(diskNumbers); });

    std::future<std::vector<PhysicalDiskHealth>> future = task->get_future();
    std::jthread([task] { (*task)(); }).detach();

    if (future.wait_for(kHealthQueryTimeout) != std::future_status::ready) {
        return std::vector<PhysicalDiskHealth>{};  // unknown health/temperature rather than a hang
    }
    return future.get();
}

StorageHealthInfo WindowsStorageHealthProvider::queryHealth(const std::string& rootPath) {
    const auto disks = queryDisks(rootPath);
    return disks ? core::summarizeDisks(*disks) : StorageHealthInfo{};
}

}  // namespace gth::storage
