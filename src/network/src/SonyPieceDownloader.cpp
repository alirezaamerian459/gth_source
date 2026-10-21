#include "gth/network/SonyPieceDownloader.h"

#include <chrono>
#include <fstream>
#include <thread>

#include "SonyHttp.h"

namespace gth::network {

namespace {

using namespace sonyhttp;
namespace fs = std::filesystem;

constexpr DWORD kConnectTimeoutMs = 15000;
constexpr DWORD kSendTimeoutMs = 30000;
constexpr DWORD kReceiveTimeoutMs = 60000;
constexpr int kMaxAttempts = 4;
constexpr DWORD kChunkBytes = 1 << 20;

enum class Attempt { Done, Cancelled, Retry, Fatal };

struct AttemptOutcome {
    Attempt result = Attempt::Fatal;
    std::string error;
};

AttemptOutcome fatal(std::string why) { return {Attempt::Fatal, std::move(why)}; }
AttemptOutcome retry(std::string why) { return {Attempt::Retry, std::move(why)}; }

// One pass at fetching the rest of the file. Appends to `destination`.
AttemptOutcome fetchRemainder(const ParsedUrl& url, const fs::path& destination, long long expectedSize,
                              const std::function<bool(long long)>& onProgress) {
    std::error_code ec;
    long long have = fs::exists(destination, ec) ? static_cast<long long>(fs::file_size(destination, ec)) : 0;
    if (have > expectedSize) {  // more than the whole file: it can't be a partial of this one
        fs::remove(destination, ec);
        have = 0;
    }
    if (have == expectedSize) return {Attempt::Done, {}};

    WinHttpHandle session(WinHttpOpen(L"GameTransferHub/5.9.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return retry(describeWinHttpError(GetLastError()));
    WinHttpSetTimeouts(session, kConnectTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs);

    WinHttpHandle connection(WinHttpConnect(session, url.host.c_str(), url.port, 0));
    if (!connection) return retry(describeWinHttpError(GetLastError()));

    WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0));
    if (!request) return retry(describeWinHttpError(GetLastError()));

    if (url.https) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;  // Sony's private root; the chain is pinned below
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }

    std::wstring headers;
    if (have > 0) headers = L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return retry(describeWinHttpError(GetLastError()));
    }
    if (url.https) {
        std::string why;
        if (!serverChainIsSonys(request, why)) return fatal("refused the connection: " + why);
    }
    if (!WinHttpReceiveResponse(request, nullptr)) return retry(describeWinHttpError(GetLastError()));

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    bool append = have > 0;
    if (status == 200) {
        append = false;  // the server ignored the Range header (or there was none): the whole file follows
        have = 0;
    } else if (status != 206) {
        if (status >= 500 || status == 429) return retry("Sony's server answered HTTP " + std::to_string(status));
        return fatal("Sony's server answered HTTP " + std::to_string(status));
    }

    std::ofstream out(destination, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) return fatal("couldn't write to " + destination.string());

    std::string buffer(kChunkBytes, '\0');
    long long written = have;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), kChunkBytes, &read)) {
            out.flush();
            return retry(describeWinHttpError(GetLastError()));
        }
        if (read == 0) break;
        out.write(buffer.data(), read);
        if (!out) return fatal("couldn't write to " + destination.string() + " (disk full?)");
        written += read;
        if (onProgress && !onProgress(written)) {
            out.flush();
            return {Attempt::Cancelled, {}};
        }
    }
    out.flush();
    if (written != expectedSize) return retry("the download ended at " + std::to_string(written) + " of " + std::to_string(expectedSize) + " bytes");
    return {Attempt::Done, {}};
}

}  // namespace

bool IsSonyDownloadUrl(const std::string& url) {
    const auto parsed = crackUrl(url);
    return parsed && isSonyHost(parsed->host);
}

core::PieceDownloadResult SonyPieceDownloader::download(const std::string& url, const fs::path& destination,
                                                        long long expectedSizeBytes,
                                                        const std::function<bool(long long)>& onProgress) {
    core::PieceDownloadResult result;
    const auto parsed = crackUrl(url);
    if (!parsed || !isSonyHost(parsed->host)) {
        result.error = "refusing to download from an address that isn't on Sony's servers: " + url;
        return result;
    }
    if (expectedSizeBytes <= 0) {
        result.error = "the size of the file isn't known";
        return result;
    }

    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);

    std::string lastError;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) {
            // 2s, 4s, 8s - in short steps, so a Cancel doesn't wait out the pause.
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1 << attempt);
            while (std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                const long long onDisk = fs::exists(destination, ec) ? static_cast<long long>(fs::file_size(destination, ec)) : 0;
                if (onProgress && !onProgress(onDisk)) {
                    result.cancelled = true;
                    return result;
                }
            }
        }

        const AttemptOutcome outcome = fetchRemainder(*parsed, destination, expectedSizeBytes, onProgress);
        switch (outcome.result) {
            case Attempt::Done: result.success = true; return result;
            case Attempt::Cancelled: result.cancelled = true; return result;
            case Attempt::Fatal: result.error = outcome.error; return result;
            case Attempt::Retry: lastError = outcome.error; break;  // resumes from what's on disk
        }
    }
    result.error = "gave up after " + std::to_string(kMaxAttempts) + " attempts: " + lastError;
    return result;
}

}  // namespace gth::network
