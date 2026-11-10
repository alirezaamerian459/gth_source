#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <array>

#include "gth/credentials/WindowsCredentialStore.h"

using namespace gth::credentials;
using namespace gth::core;

namespace {
bool supportsLocalMachineGenericCredentials() {
    std::array<DWORD, CRED_TYPE_MAXIMUM> maxPersistence{};
    if (!::CredGetSessionTypes(static_cast<DWORD>(maxPersistence.size()), maxPersistence.data())) {
        return ::GetLastError() != ERROR_NO_SUCH_LOGON_SESSION;
    }
    return maxPersistence[CRED_TYPE_GENERIC] >= CRED_PERSIST_LOCAL_MACHINE;
}

// Real Windows Credential Manager round-trip. Uses a test-only target and
// always cleans it up, even on assertion failure.
struct CredentialGuard {
    WindowsCredentialStore store;
    std::string target = "unit-test-target";
    ~CredentialGuard() { store.remove(target); }
};
}  // namespace

TEST(WindowsCredentialStoreTest, SaveAndReadRoundTrips) {
    if (!supportsLocalMachineGenericCredentials()) {
        GTEST_SKIP() << "This Windows logon session cannot store machine-persistent generic credentials";
    }

    CredentialGuard guard;
    guard.store.save(guard.target, "someuser", "s3cr3t!");

    auto credential = guard.store.read(guard.target);
    ASSERT_TRUE(credential.has_value());
    EXPECT_EQ(credential->username, "someuser");
    EXPECT_EQ(credential->secret, "s3cr3t!");
}

TEST(WindowsCredentialStoreTest, ReadReturnsNulloptForMissingTarget) {
    WindowsCredentialStore store;
    EXPECT_FALSE(store.read("definitely-does-not-exist-target").has_value());
}

TEST(WindowsCredentialStoreTest, RemoveDeletesEntry) {
    if (!supportsLocalMachineGenericCredentials()) {
        GTEST_SKIP() << "This Windows logon session cannot store machine-persistent generic credentials";
    }

    WindowsCredentialStore store;
    const std::string target = "unit-test-remove-target";
    store.save(target, "user", "pass");
    ASSERT_TRUE(store.read(target).has_value());

    store.remove(target);
    EXPECT_FALSE(store.read(target).has_value());
}

TEST(WindowsCredentialStoreTest, SaveOverwritesExistingEntry) {
    if (!supportsLocalMachineGenericCredentials()) {
        GTEST_SKIP() << "This Windows logon session cannot store machine-persistent generic credentials";
    }

    CredentialGuard guard;
    guard.store.save(guard.target, "first", "firstpass");
    guard.store.save(guard.target, "second", "secondpass");

    auto credential = guard.store.read(guard.target);
    ASSERT_TRUE(credential.has_value());
    EXPECT_EQ(credential->username, "second");
    EXPECT_EQ(credential->secret, "secondpass");
}
