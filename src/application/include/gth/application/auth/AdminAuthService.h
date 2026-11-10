#pragma once

#include "gth/application/auth/IAdminAuthService.h"
#include "gth/core/credentials/ICredentialStore.h"

namespace gth::application {

class AdminAuthService : public IAdminAuthService {
public:
    explicit AdminAuthService(core::ICredentialStore& credentialStore);

    bool signIn(const std::string& username, const std::string& password) override;
    void signOut() override;
    bool isSignedIn() const override;
    std::string currentUsername() const override;
    bool changeCredentials(const std::string& currentPassword, const std::optional<std::string>& newUsername,
                            const std::optional<std::string>& newPassword) override;

private:
    // A first sign-in saves the values supplied by the user. No default
    // password is embedded in the application.
    std::optional<core::Credential> loadCredential();

    core::ICredentialStore& credentialStore_;
    bool signedIn_ = false;
    std::string signedInUsername_;
};

}  // namespace gth::application
