#include "gth/network/TelegramBackupTransport.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "gth/application/backup/BackupService.h"

#pragma comment(lib, "winhttp.lib")

namespace gth::network {

namespace {

constexpr const wchar_t* kApiOrigin = L"https://api.telegram.org";
constexpr size_t kMaxApiResponseBytes = 4 * 1024 * 1024;
constexpr size_t kIoBufferBytes = 1 << 20;

class Handle {
public:
    Handle() = default;
    explicit Handle(HINTERNET value) : value_(value) {}
    ~Handle() { if (value_) WinHttpCloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
private:
    HINTERNET value_ = nullptr;
};

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size) <= 0)
        return {};
    return result;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::optional<uint32_t> readHex4(std::string_view text, size_t pos) {
    if (pos + 4 > text.size()) return std::nullopt;
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = text[pos + i];
        const int digit = c >= '0' && c <= '9' ? c - '0' :
                          c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                          c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return std::nullopt;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    return value;
}

std::optional<size_t> fieldValueStart(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += needle.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos++] != ':') return std::nullopt;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    return pos;
}

std::optional<std::string> stringField(std::string_view json, std::string_view key) {
    auto start = fieldValueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '"') return std::nullopt;
    std::string value;
    for (size_t pos = *start + 1; pos < json.size(); ++pos) {
        const unsigned char c = static_cast<unsigned char>(json[pos]);
        if (c == '"') return value;
        if (c != '\\') {
            value.push_back(static_cast<char>(c));
            continue;
        }
        if (++pos >= json.size()) return std::nullopt;
        switch (json[pos]) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                auto code = readHex4(json, pos + 1);
                if (!code) return std::nullopt;
                pos += 4;
                uint32_t cp = *code;
                if (cp >= 0xd800 && cp <= 0xdbff && pos + 6 < json.size() && json[pos + 1] == '\\' && json[pos + 2] == 'u') {
                    auto low = readHex4(json, pos + 3);
                    if (low && *low >= 0xdc00 && *low <= 0xdfff) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (*low - 0xdc00);
                        pos += 6;
                    }
                }
                appendUtf8(value, cp);
                break;
            }
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<unsigned long> unsignedField(std::string_view json, std::string_view key) {
    auto start = fieldValueStart(json, key);
    if (!start) return std::nullopt;
    size_t end = *start;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') ++end;
    if (end == *start) return std::nullopt;
    try { return std::stoul(std::string(json.substr(*start, end - *start))); }
    catch (...) { return std::nullopt; }
}

bool isOk(std::string_view json) {
    auto start = fieldValueStart(json, "ok");
    return start && json.substr(*start, 4) == "true";
}

std::string apiError(std::string_view json, const std::string& fallback) {
    const auto description = stringField(json, "description");
    return description && !description->empty() ? "Telegram: " + *description : fallback;
}

std::string formEncode(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            encoded.push_back('+');
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 15]);
        }
    }
    return encoded;
}

std::string pathEncode(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 15]);
        }
    }
    return encoded;
}

std::wstring urlPath(const std::string& path) { return wide(path); }

bool queryStatus(HINTERNET request, DWORD& status) {
    DWORD bytes = sizeof(status);
    return WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) != FALSE;
}

bool readResponse(HINTERNET request, std::string& body, std::string& error) {
    body.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = "Couldn't read Telegram's response.";
            return false;
        }
        if (available == 0) return true;
        if (body.size() + available > kMaxApiResponseBytes) {
            error = "Telegram returned an unexpectedly large response.";
            return false;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            error = "Couldn't read Telegram's response.";
            return false;
        }
        body.resize(offset + read);
        if (read == 0) return true;
    }
}

bool apiRequest(HINTERNET session, const std::wstring& path, const std::string& method,
                const std::string& body, const std::string& contentType, DWORD& httpStatus,
                std::string& response, std::string& error) {
    const std::wstring host = L"api.telegram.org";
    Handle connection(WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        error = "Couldn't connect to Telegram.";
        return false;
    }
    const std::wstring methodW = wide(method);
    const DWORD flags = WINHTTP_FLAG_SECURE;
    Handle request(WinHttpOpenRequest(connection, methodW.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        error = "Couldn't prepare a secure Telegram request.";
        return false;
    }
    const std::wstring headers = contentType.empty() ? L"" : wide("Content-Type: " + contentType + "\r\n");
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        error = "The request to Telegram failed before a response arrived.";
        return false;
    }
    if (!WinHttpReceiveResponse(request, nullptr) || !queryStatus(request, httpStatus)) {
        error = "Telegram didn't return a readable response.";
        return false;
    }
    return readResponse(request, response, error);
}

bool waitCancelable(std::chrono::steady_clock::duration duration, const application::BackupCallbacks& callbacks) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !(callbacks.shouldCancel && callbacks.shouldCancel());
}

std::string safeFilename(std::string name) {
    if (name.empty()) name = "backup.bin";
    for (char& c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 32 || c == '"' || c == '\\' || c == '/' || c == ';') c = '_';
    }
    if (name.size() > 100) name.resize(100);
    return name;
}

bool validBotToken(const std::string& token) {
    const auto colon = token.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == token.size()) return false;
    return std::all_of(token.begin(), token.begin() + static_cast<std::ptrdiff_t>(colon),
                       [](unsigned char c) { return std::isdigit(c) != 0; }) &&
           std::all_of(token.begin() + static_cast<std::ptrdiff_t>(colon + 1), token.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
           });
}

bool validChatId(const std::string& chatId) {
    if (chatId.size() >= 2 && chatId.front() == '@') {
        return chatId.size() <= 33 &&
            std::all_of(chatId.begin() + 1, chatId.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_';
            });
    }
    size_t start = !chatId.empty() && chatId.front() == '-' ? 1 : 0;
    return start < chatId.size() && std::all_of(chatId.begin() + static_cast<std::ptrdiff_t>(start), chatId.end(),
                                               [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

TelegramBackupTransport::TelegramBackupTransport() {
    session_ = WinHttpOpen(L"GameTransferHub/6.9.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 15000, 15000, 30000, 120000);
    }
}

TelegramBackupTransport::~TelegramBackupTransport() {
    if (session_) WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

void TelegramBackupTransport::configure(std::string botToken, std::string chatId) {
    botToken_ = std::move(botToken);
    chatId_ = std::move(chatId);
}

bool TelegramBackupTransport::ready(std::string& error) const {
    if (!session_) {
        error = "Windows couldn't start a secure network session.";
        return false;
    }
    if (!validBotToken(botToken_)) {
        error = "Enter a valid Telegram bot token.";
        return false;
    }
    if (!validChatId(chatId_)) {
        error = "Enter a Telegram @channelusername or numeric channel chat ID.";
        return false;
    }
    return true;
}

bool TelegramBackupTransport::testConnection(std::string& error) {
    if (!ready(error)) return false;
    DWORD status = 0;
    std::string body;
    if (!apiRequest(static_cast<HINTERNET>(session_), urlPath("/bot" + botToken_ + "/getMe"), "GET", {}, {}, status, body, error)) return false;
    if (status != 200 || !isOk(body)) {
        error = apiError(body, "Telegram rejected the bot token (HTTP " + std::to_string(status) + ").");
        return false;
    }
    body.clear();
    const std::string query = "chat_id=" + formEncode(chatId_);
    if (!apiRequest(static_cast<HINTERNET>(session_), urlPath("/bot" + botToken_ + "/getChat"), "POST", query,
                    "application/x-www-form-urlencoded; charset=utf-8", status, body, error)) return false;
    if (status != 200 || !isOk(body)) {
        error = apiError(body, "Telegram couldn't access that channel (HTTP " + std::to_string(status) + "). Add the bot as a channel administrator.");
        return false;
    }
    error.clear();
    return true;
}

std::optional<std::string> TelegramBackupTransport::uploadObject(const std::string& name, const std::filesystem::path& localPath,
                                                                  long long sizeBytes, const application::BackupCallbacks& callbacks,
                                                                  std::string& error) {
    if (!ready(error)) return std::nullopt;
    if (sizeBytes <= 0 || sizeBytes > application::BackupService::kChunkBytes) {
        error = "A Telegram backup object is outside the supported 16 MiB size limit.";
        return std::nullopt;
    }
    std::ifstream file(localPath, std::ios::binary);
    if (!file) {
        error = "Couldn't open a staged backup object.";
        return std::nullopt;
    }
    std::string bytes(static_cast<size_t>(sizeBytes), '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (file.gcount() != static_cast<std::streamsize>(bytes.size()) || file.peek() != std::char_traits<char>::eof()) {
        error = "A staged backup object changed size before upload.";
        return std::nullopt;
    }
    if (callbacks.shouldCancel && callbacks.shouldCancel()) {
        error = "Backup cancelled.";
        return std::nullopt;
    }

    const std::string boundary = "----GTHBackupBoundary7MA4YWxkTrZu0gW";
    const std::string filename = safeFilename(name);
    const std::string prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + chatId_ +
        "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" + filename +
        "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    const std::string suffix = "\r\n--" + boundary + "--\r\n";
    std::string body;
    body.reserve(prefix.size() + bytes.size() + suffix.size());
    body += prefix;
    body += bytes;
    body += suffix;
    const std::string contentType = "multipart/form-data; boundary=" + boundary;

    for (int attempt = 0; attempt < 6; ++attempt) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) {
            error = "Backup cancelled.";
            return std::nullopt;
        }
        if (lastMessageAt_.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::steady_clock::now() - lastMessageAt_;
            if (elapsed < std::chrono::seconds(1) && !waitCancelable(std::chrono::seconds(1) - elapsed, callbacks)) {
                error = "Backup cancelled.";
                return std::nullopt;
            }
        }
        lastMessageAt_ = std::chrono::steady_clock::now();
        DWORD status = 0;
        std::string response;
        if (!apiRequest(static_cast<HINTERNET>(session_), urlPath("/bot" + botToken_ + "/sendDocument"), "POST", body,
                        contentType, status, response, error)) return std::nullopt;
        if (status == 429) {
            const auto retryAfter = unsignedField(response, "retry_after").value_or(2UL);
            if (attempt == 5 || !waitCancelable(std::chrono::seconds(std::min<unsigned long>(retryAfter, 300)), callbacks)) {
                error = callbacks.shouldCancel && callbacks.shouldCancel() ? "Backup cancelled." : apiError(response, "Telegram rate limited this backup.");
                return std::nullopt;
            }
            continue;
        }
        if (status != 200 || !isOk(response)) {
            error = apiError(response, "Telegram rejected a backup object (HTTP " + std::to_string(status) + ").");
            return std::nullopt;
        }
        const auto fileId = stringField(response, "file_id");
        if (!fileId || fileId->empty()) {
            error = "Telegram accepted the upload but returned no reusable file ID.";
            return std::nullopt;
        }
        if (callbacks.onProgress) callbacks.onProgress("Uploaded " + filename, sizeBytes, sizeBytes);
        return fileId;
    }
    error = "Telegram rate limited this backup too many times.";
    return std::nullopt;
}

bool TelegramBackupTransport::downloadObject(const std::string& objectId, const std::filesystem::path& destination,
                                               const application::BackupCallbacks& callbacks, std::string& error) {
    if (!ready(error)) return false;
    if (objectId.empty()) {
        error = "The backup manifest contains an empty Telegram file ID.";
        return false;
    }
    const std::string form = "file_id=" + formEncode(objectId);
    DWORD status = 0;
    std::string response;
    if (!apiRequest(static_cast<HINTERNET>(session_), urlPath("/bot" + botToken_ + "/getFile"), "POST", form,
                    "application/x-www-form-urlencoded; charset=utf-8", status, response, error)) return false;
    if (status != 200 || !isOk(response)) {
        error = apiError(response, "Telegram couldn't locate a backup object (HTTP " + std::to_string(status) + ").");
        return false;
    }
    const auto remotePath = stringField(response, "file_path");
    if (!remotePath || remotePath->empty() || remotePath->find("..") != std::string::npos || remotePath->front() == '/') {
        error = "Telegram returned an invalid backup file path.";
        return false;
    }
    const std::wstring fullPath = urlPath("/file/bot" + botToken_ + "/" + pathEncode(*remotePath));
    Handle connection(WinHttpConnect(static_cast<HINTERNET>(session_), L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        error = "Couldn't connect to Telegram's file server.";
        return false;
    }
    Handle request(WinHttpOpenRequest(connection, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr) || !queryStatus(request, status)) {
        error = "Couldn't download the backup object from Telegram.";
        return false;
    }
    if (status != 200) {
        error = "Telegram couldn't download the backup object (HTTP " + std::to_string(status) + ").";
        return false;
    }
    DWORD expected = 0;
    DWORD expectedBytes = sizeof(expected);
    const bool hasExpected = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                                  WINHTTP_HEADER_NAME_BY_INDEX, &expected, &expectedBytes,
                                                  WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (hasExpected && expected > application::BackupService::kChunkBytes) {
        error = "Telegram returned an oversized backup object.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        error = "Couldn't create a temporary restore folder: " + ec.message();
        return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Couldn't create a temporary restore file.";
        return false;
    }
    std::vector<char> buffer(kIoBufferBytes);
    unsigned long long total = 0;
    for (;;) {
        if (callbacks.shouldCancel && callbacks.shouldCancel()) {
            out.close();
            std::filesystem::remove(destination, ec);
            error = "Restore cancelled.";
            return false;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            out.close();
            std::filesystem::remove(destination, ec);
            error = "Telegram's file download was interrupted.";
            return false;
        }
        if (read == 0) break;
        total += read;
        if (total > application::BackupService::kChunkBytes) {
            out.close();
            std::filesystem::remove(destination, ec);
            error = "Telegram returned an oversized backup object.";
            return false;
        }
        out.write(buffer.data(), read);
        if (!out) {
            out.close();
            std::filesystem::remove(destination, ec);
            error = "Couldn't write a downloaded backup object (disk full?).";
            return false;
        }
        if (callbacks.onProgress) callbacks.onProgress("Downloading from Telegram", static_cast<long long>(total),
                                                        hasExpected ? expected : 0);
    }
    out.close();
    if (!out || (hasExpected && total != expected)) {
        std::filesystem::remove(destination, ec);
        error = "Telegram's downloaded backup object was incomplete.";
        return false;
    }
    return true;
}

}  // namespace gth::network
