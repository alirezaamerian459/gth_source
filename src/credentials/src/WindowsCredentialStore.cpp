#include "gth/credentials/WindowsCredentialStore.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <cwchar>

#pragma comment(lib, "advapi32.lib")

namespace gth::credentials {

namespace {
std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

std::string toUtf8(const wchar_t* wide, size_t count) {
    if (count == 0 || !wide) return {};
    const int len =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(count), utf8.data(), len, nullptr, nullptr);
    return utf8;
}
}  // namespace

std::wstring WindowsCredentialStore::qualifiedTargetName(const std::string& target) {
    return toWide("GTH_" + target);
}

void WindowsCredentialStore::save(const std::string& target, const std::string& username,
                                   const std::string& secret) {
    const std::wstring targetName = qualifiedTargetName(target);
    const std::wstring userNameW = toWide(username);

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(userNameW.c_str());

    ::CredWriteW(&cred, 0);
}

std::optional<core::Credential> WindowsCredentialStore::read(const std::string& target) {
    const std::wstring targetName = qualifiedTargetName(target);

    PCREDENTIALW cred = nullptr;
    if (!::CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &cred) || cred == nullptr) return std::nullopt;

    const size_t usernameLength = cred->UserName ? ::wcsnlen_s(cred->UserName, CRED_MAX_USERNAME_LENGTH + 1) : 0;
    if (usernameLength > CRED_MAX_USERNAME_LENGTH) {
        ::CredFree(cred);
        return std::nullopt;
    }

    core::Credential result;
    result.username = toUtf8(cred->UserName, usernameLength);
    // CredentialBlob is the raw byte string we wrote in save() (UTF-8 text),
    // not wide text - copy it back as plain bytes.
    result.secret.assign(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);

    ::CredFree(cred);
    return result;
}

void WindowsCredentialStore::remove(const std::string& target) {
    const std::wstring targetName = qualifiedTargetName(target);
    ::CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0);
}

}  // namespace gth::credentials
