#pragma once

#include "gth/core/credentials/ICredentialStore.h"

namespace gth::credentials {

// Windows Credential Manager-backed ICredentialStore (wincred.h:
// CredWriteW/CredReadW/CredDeleteW), generic credential type. Every target
// name this store is asked to save under is prefixed with "GTH_" before
// hitting the OS API, so GTH's entries (FTP connection secrets, the admin
// login, or anything else that reuses this same store) are trivially
// distinguishable from other applications' in Credential Manager
// (rundll32.exe keymgr.dll,KRShowKeyMgr or `cmdkey /list`).
class WindowsCredentialStore : public core::ICredentialStore {
public:
    void save(const std::string& target, const std::string& username, const std::string& secret) override;
    std::optional<core::Credential> read(const std::string& target) override;
    void remove(const std::string& target) override;

private:
    static std::wstring qualifiedTargetName(const std::string& target);
};

}  // namespace gth::credentials
