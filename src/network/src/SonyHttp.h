#pragma once

// Internal helpers shared by SonyUpdateService and SonyPieceDownloader: WinHTTP
// plumbing and the check that a Sony host's certificate chain is Sony's.
// Not part of the library's public interface.

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string>

#include "gth/network/SonyUpdateService.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace gth::network::sonyhttp {

// Thin RAII wrapper so early returns can't leak a WinHTTP handle.
class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    operator HINTERNET() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

inline std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

inline bool endsWith(const std::wstring& text, const std::wstring& suffix) {
    return text.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                                                       [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

inline std::string describeWinHttpError(DWORD code) {
    std::string meaning;
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: meaning = "couldn't resolve Sony's server - no internet or DNS blocked"; break;
        case ERROR_WINHTTP_CANNOT_CONNECT: meaning = "couldn't connect to Sony's server - firewall or no internet"; break;
        case ERROR_WINHTTP_TIMEOUT: meaning = "Sony's server didn't answer in time"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR: meaning = "the connection was dropped"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: meaning = "secure connection (TLS) failed"; break;
        default: meaning = "network error"; break;
    }
    return std::format("{} (error {})", meaning, code);
}

inline std::string sha256Hex(const BYTE* data, DWORD length) {
    std::array<BYTE, 32> hash;
    DWORD size = static_cast<DWORD>(hash.size());
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, data, length, hash.data(), &size)) return {};
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (DWORD i = 0; i < size; ++i) {
        hex += digits[hash[i] >> 4];
        hex += digits[hash[i] & 0xF];
    }
    return hex;
}

struct ParsedUrl {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;  // including any query
};

inline std::optional<ParsedUrl> crackUrl(const std::string& url) {
    const std::wstring wide = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) return std::nullopt;

    ParsedUrl parsed;
    parsed.https = parts.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    parsed.port = parts.nPort;
    if (parts.lpszUrlPath && parts.dwUrlPathLength) parsed.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) parsed.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (parsed.path.empty()) parsed.path = L"/";
    return parsed;
}

// Only Sony's own hosts ever get relaxed certificate handling.
inline bool isSonyHost(const std::wstring& host) { return endsWith(host, L".playstation.net"); }

// After the request went out, look at the chain the server actually
// presented and accept it only if it is Sony's (see IsPinnedSonyChain).
inline bool serverChainIsSonys(HINTERNET request, std::string& why) {
    PCCERT_CONTEXT leaf = nullptr;
    DWORD size = sizeof(leaf);
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &leaf, &size) || leaf == nullptr) {
        why = "the server sent no certificate";
        return false;
    }

    CERT_CHAIN_PARA para{};
    para.cbSize = sizeof(para);
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    const BOOL built = CertGetCertificateChain(nullptr, leaf, nullptr, leaf->hCertStore, &para, 0, nullptr, &chain);
    bool accepted = false;
    if (!built || chain == nullptr || chain->cChain == 0 || chain->rgpChain[0]->cElement == 0) {
        why = "couldn't build the certificate chain";
    } else {
        const CERT_SIMPLE_CHAIN* simple = chain->rgpChain[0];
        const CERT_CONTEXT* root = simple->rgpElement[simple->cElement - 1]->pCertContext;
        accepted = IsPinnedSonyChain(chain->TrustStatus.dwErrorStatus, sha256Hex(root->pbCertEncoded, root->cbCertEncoded));
        if (!accepted) {
            why = std::format("the server's certificate isn't Sony's (chain status {})",
                              chain->TrustStatus.dwErrorStatus);
        }
    }
    if (chain) CertFreeCertificateChain(chain);
    CertFreeCertificateContext(leaf);
    return accepted;
}

}  // namespace gth::network::sonyhttp
