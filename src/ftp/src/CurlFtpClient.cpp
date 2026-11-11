#include "gth/ftp/CurlFtpClient.h"

#include <cctype>
#include <cstdio>
#include <format>
#include <sstream>

#include <curl/curl.h>

namespace gth::ftp {

namespace {

struct CancelContext {
    std::atomic<bool>* cancelFlag;
    std::function<void(long long, long long)> onProgress;
};

int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    auto* ctx = reinterpret_cast<CancelContext*>(clientp);
    if (ctx->onProgress) {
        const long long total = ultotal > 0 ? static_cast<long long>(ultotal) : static_cast<long long>(dltotal);
        const long long now = ulnow > 0 ? static_cast<long long>(ulnow) : static_cast<long long>(dlnow);
        ctx->onProgress(now, total);
    }
    if (ctx->cancelFlag && ctx->cancelFlag->load()) return 1;  // non-zero aborts the transfer
    return 0;
}

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* file = reinterpret_cast<std::FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, file);
}

size_t readFromFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* file = reinterpret_cast<std::FILE*>(userdata);
    return std::fread(ptr, size, nmemb, file);
}

size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = reinterpret_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string encodeFtpPath(const std::string& path) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(path.size());
    for (const unsigned char c : path) {
        const bool unreserved = std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (unreserved) {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[c >> 4]);
            encoded.push_back(kHex[c & 0x0F]);
        }
    }
    return encoded;
}

// libcurl's FTP LIST response is server-formatted plain text (typically Unix
// `ls -l` style). Parsed leniently: standard listings use the fields through
// the time column as metadata and preserve every token after that as the
// filename, so virtual folders such as "JB Library" remain browsable. A
// last-token fallback keeps compatibility with unusual server formats.
std::vector<core::FtpDirectoryEntry> parseListing(const std::string& raw) {
    std::vector<core::FtpDirectoryEntry> entries;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) eol = raw.size();
        std::string line = raw.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        core::FtpDirectoryEntry entry;
        entry.isDirectory = !line.empty() && line[0] == 'd';

        std::istringstream fields(line);
        std::vector<std::string> tokens;
        for (std::string token; fields >> token;) tokens.push_back(std::move(token));

        if (tokens.size() >= 9 && (tokens[0].front() == 'd' || tokens[0].front() == '-')) {
            entry.name = tokens[8];
            for (size_t index = 9; index < tokens.size(); ++index) entry.name += " " + tokens[index];
        } else {
            const size_t nameStart = line.find_last_of(' ');
            entry.name = nameStart == std::string::npos ? line : line.substr(nameStart + 1);
        }
        if (entry.name == "." || entry.name == "..") continue;

        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace

CurlFtpClient::CurlFtpClient(bool useTls) : useTls_(useTls) {}

CurlFtpClient::~CurlFtpClient() { disconnect(); }

std::string CurlFtpClient::buildUrl(const std::string& path) const {
    // Plain FTP remains available only through the explicit Ftp protocol selection for legacy servers.
    std::string url = useTls_ ? "ftps://" : "ftp://";
    url += params_.host;
    url += std::format(":{}", params_.port);
    url += "/";
    std::string cleanPath = path;
    while (!cleanPath.empty() && cleanPath.front() == '/') cleanPath.erase(cleanPath.begin());
    url += encodeFtpPath(cleanPath);
    return url;
}

core::FtpOperationResult CurlFtpClient::connect(const core::FtpConnectParams& params) {
    params_ = params;

    CURL* curl = curl_easy_init();
    if (!curl) return {false, "Failed to initialize libcurl."};

    const std::string url = buildUrl(params_.remoteBasePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, params_.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, params_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    if (useTls_) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    }

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return {false, curl_easy_strerror(res)};

    connected_ = true;
    return {true, ""};
}

void CurlFtpClient::disconnect() { connected_ = false; }

std::vector<core::FtpDirectoryEntry> CurlFtpClient::listDirectory(const std::string& path) {
    if (!connected_) return {};

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string url = buildUrl(path);
    if (url.back() != '/') url += "/";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, params_.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, params_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (useTls_) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    }

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return parseListing(response);
}

core::FtpOperationResult CurlFtpClient::upload(const std::string& localPath, const std::string& remotePath,
                                                const std::function<void(long long, long long)>& onProgress,
                                                std::atomic<bool>* cancelFlag) {
    if (!connected_) return {false, "Not connected."};

    std::FILE* file = nullptr;
    if (fopen_s(&file, localPath.c_str(), "rb") != 0 || !file) return {false, "Could not open local file."};

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        return {false, "Failed to initialize libcurl."};
    }

    CancelContext ctx{cancelFlag, onProgress};
    const std::string url = buildUrl(remotePath);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, params_.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, params_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromFile);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (useTls_) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    }

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    std::fclose(file);

    if (res != CURLE_OK) return {false, curl_easy_strerror(res)};
    return {true, ""};
}

core::FtpOperationResult CurlFtpClient::download(const std::string& remotePath, const std::string& localPath,
                                                  const std::function<void(long long, long long)>& onProgress,
                                                  std::atomic<bool>* cancelFlag) {
    if (!connected_) return {false, "Not connected."};

    std::FILE* file = nullptr;
    if (fopen_s(&file, localPath.c_str(), "wb") != 0 || !file) return {false, "Could not open local file."};

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        return {false, "Failed to initialize libcurl."};
    }

    CancelContext ctx{cancelFlag, onProgress};
    const std::string url = buildUrl(remotePath);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, params_.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, params_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (useTls_) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
        curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    }

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    std::fclose(file);

    if (res != CURLE_OK) return {false, curl_easy_strerror(res)};
    return {true, ""};
}

}  // namespace gth::ftp
