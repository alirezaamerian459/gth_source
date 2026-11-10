#pragma once

#include <optional>
#include <string>

namespace gth::application {

// Gates the admin-only sidebar tabs and FTP-tab sections behind a
// username+password sign-in. Sign-in state is deliberately session-only
// (never persisted) - every fresh launch starts locked, matching the
// regular-user default. The credential itself (not the unlock state)
// persists via whatever ICredentialStore this is backed by.
class IAdminAuthService {
public:
    virtual ~IAdminAuthService() = default;

    // If no credential has been saved yet, the first non-empty pair is stored
    // in the OS credential store and signs the user in.
    virtual bool signIn(const std::string& username, const std::string& password) = 0;
    virtual void signOut() = 0;
    virtual bool isSignedIn() const = 0;
    virtual std::string currentUsername() const = 0;

    // currentPassword must match the stored secret. std::nullopt (or an
    // empty string) for newUsername/newPassword leaves that field
    // unchanged.
    virtual bool changeCredentials(const std::string& currentPassword,
                                    const std::optional<std::string>& newUsername,
                                    const std::optional<std::string>& newPassword) = 0;
};

}  // namespace gth::application
