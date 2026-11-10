#include "gth/application/auth/AdminAuthService.h"

namespace gth::application {

namespace {
constexpr const char* kCredentialTarget = "admin_login";
}  // namespace

AdminAuthService::AdminAuthService(core::ICredentialStore& credentialStore) : credentialStore_(credentialStore) {}

std::optional<core::Credential> AdminAuthService::loadCredential() { return credentialStore_.read(kCredentialTarget); }

bool AdminAuthService::signIn(const std::string& username, const std::string& password) {
    auto credential = loadCredential();
    if (credential.has_value()) {
        if (username != credential->username || password != credential->secret) return false;
    } else {
        if (username.empty() || password.empty()) return false;
        credentialStore_.save(kCredentialTarget, username, password);
        credential = loadCredential();
        if (!credential.has_value() || credential->username != username || credential->secret != password) return false;
    }

    signedIn_ = true;
    signedInUsername_ = credential->username;
    return true;
}

void AdminAuthService::signOut() {
    signedIn_ = false;
    signedInUsername_.clear();
}

bool AdminAuthService::isSignedIn() const { return signedIn_; }

std::string AdminAuthService::currentUsername() const { return signedInUsername_; }

bool AdminAuthService::changeCredentials(const std::string& currentPassword,
                                          const std::optional<std::string>& newUsername,
                                          const std::optional<std::string>& newPassword) {
    if (!signedIn_) return false;

    const auto existing = loadCredential();
    if (!existing || currentPassword != existing->secret) return false;

    const std::string username = (newUsername && !newUsername->empty()) ? *newUsername : existing->username;
    const std::string password = (newPassword && !newPassword->empty()) ? *newPassword : existing->secret;

    credentialStore_.save(kCredentialTarget, username, password);
    signedInUsername_ = username;
    return true;
}

}  // namespace gth::application
