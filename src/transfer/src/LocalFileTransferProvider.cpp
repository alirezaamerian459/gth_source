#include "gth/transfer/LocalFileTransferProvider.h"

#include <windows.h>

#include <array>
#include <format>
#include <filesystem>

namespace gth::transfer {

namespace fs = std::filesystem;

namespace {

struct CallbackContext {
    const std::function<void(long long, long long)>* onProgress;
    std::atomic<bool>* cancelFlag;
};

DWORD CALLBACK CopyProgressCallback(LARGE_INTEGER totalFileSize, LARGE_INTEGER totalBytesTransferred, LARGE_INTEGER,
                                     LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID data) {
    auto* ctx = reinterpret_cast<CallbackContext*>(data);
    if (ctx->onProgress && *ctx->onProgress) {
        (*ctx->onProgress)(totalBytesTransferred.QuadPart, totalFileSize.QuadPart);
    }
    if (ctx->cancelFlag && ctx->cancelFlag->load(std::memory_order_relaxed)) {
        return PROGRESS_CANCEL;
    }
    return PROGRESS_CONTINUE;
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string wideToUtf8(const wchar_t* wide) {
    if (!wide || !wide[0]) return "";
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

}  // namespace

std::string LocalFileTransferProvider::name() const { return "Local"; }

core::TransferOutcome LocalFileTransferProvider::copyFile(const std::string& sourcePath,
                                                            const std::string& destinationPath,
                                                            const std::function<void(long long, long long)>& onProgress,
                                                            std::atomic<bool>* cancelFlag) {
    core::TransferOutcome outcome;

    std::error_code ec;
    const fs::path destDir = fs::path(destinationPath).parent_path();
    if (!destDir.empty()) fs::create_directories(destDir, ec);

    CallbackContext ctx{&onProgress, cancelFlag};

    const std::wstring src = utf8ToWide(sourcePath);
    const std::wstring dst = utf8ToWide(destinationPath);

    BOOL cancelPlaceholder = FALSE;
    const BOOL result = ::CopyFileExW(src.c_str(), dst.c_str(), &CopyProgressCallback, &ctx, &cancelPlaceholder, 0);

    if (result) {
        outcome.success = true;
        return outcome;
    }

    const DWORD error = ::GetLastError();
    if (error == ERROR_REQUEST_ABORTED) {
        outcome.errorMessage = "Cancelled";
        return outcome;
    }

    std::array<wchar_t, 512> buf{};
    ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, buf.data(),
                     static_cast<DWORD>(buf.size() - 1), nullptr);
    const std::string message = wideToUtf8(buf.data());
    outcome.errorMessage = message.empty() ? std::format("CopyFileEx failed with error {}", error) : message;
    return outcome;
}

}  // namespace gth::transfer
