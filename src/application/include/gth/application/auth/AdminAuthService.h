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
    // Reads the stored admin credential, seeding the default
    // (drgame/12345678) on first use if nothing's been saved yet.
    core::Credential loadOrSeedCredential();

    core::ICredentialStore& credentialStore_;
    bool signedIn_ = false;
    std::string signedInUsername_;
};

}  // namespace gth::application
