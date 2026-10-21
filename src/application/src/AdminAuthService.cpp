#include "gth/application/auth/AdminAuthService.h"

namespace gth::application {

namespace {
constexpr const char* kCredentialTarget = "admin_login";
constexpr const char* kDefaultUsername = "drgame";
constexpr const char* kDefaultPassword = "12345678";
}  // namespace

AdminAuthService::AdminAuthService(core::ICredentialStore& credentialStore) : credentialStore_(credentialStore) {}

core::Credential AdminAuthService::loadOrSeedCredential() {
    if (auto existing = credentialStore_.read(kCredentialTarget)) return *existing;

    credentialStore_.save(kCredentialTarget, kDefaultUsername, kDefaultPassword);
    return core::Credential{kDefaultUsername, kDefaultPassword};
}

bool AdminAuthService::signIn(const std::string& username, const std::string& password) {
    const auto credential = loadOrSeedCredential();
    if (username != credential.username || password != credential.secret) return false;

    signedIn_ = true;
    signedInUsername_ = credential.username;
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

    const auto existing = loadOrSeedCredential();
    if (currentPassword != existing.secret) return false;

    const std::string username = (newUsername && !newUsername->empty()) ? *newUsername : existing.username;
    const std::string password = (newPassword && !newPassword->empty()) ? *newPassword : existing.secret;

    credentialStore_.save(kCredentialTarget, username, password);
    signedInUsername_ = username;
    return true;
}

}  // namespace gth::application
