#pragma once

#include <optional>
#include <string>

namespace gth::core {

// One username/secret pair as stored by an ICredentialStore. `target` is the
// opaque store-specific key it was saved under - never persisted anywhere
// else (see WindowsCredentialStore, and FtpConnection::credentialTarget).
struct Credential {
    std::string username;
    std::string secret;
};

// Abstraction over an OS-level secret store. FTP/SFTP passwords must never
// be written to the SQLite database (see FtpConnection) - only the target
// name returned implicitly by save() is persisted there, and the actual
// secret lives here instead.
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    virtual void save(const std::string& target, const std::string& username, const std::string& secret) = 0;
    virtual std::optional<Credential> read(const std::string& target) = 0;
    virtual void remove(const std::string& target) = 0;
};

}  // namespace gth::core
