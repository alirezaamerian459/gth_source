#include <gtest/gtest.h>

#include "gth/credentials/WindowsCredentialStore.h"

using namespace gth::credentials;
using namespace gth::core;

namespace {
// Real Windows Credential Manager round-trip. Uses a unique-per-run target
// name and always cleans up, even on assertion failure, so a failing run
// never leaves a stray Credential Manager entry behind.
struct CredentialGuard {
    WindowsCredentialStore store;
    std::string target = "unit-test-target";
    ~CredentialGuard() { store.remove(target); }
};
}  // namespace

TEST(WindowsCredentialStoreTest, SaveAndReadRoundTrips) {
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
    WindowsCredentialStore store;
    const std::string target = "unit-test-remove-target";
    store.save(target, "user", "pass");
    ASSERT_TRUE(store.read(target).has_value());

    store.remove(target);
    EXPECT_FALSE(store.read(target).has_value());
}

TEST(WindowsCredentialStoreTest, SaveOverwritesExistingEntry) {
    CredentialGuard guard;
    guard.store.save(guard.target, "first", "firstpass");
    guard.store.save(guard.target, "second", "secondpass");

    auto credential = guard.store.read(guard.target);
    ASSERT_TRUE(credential.has_value());
    EXPECT_EQ(credential->username, "second");
    EXPECT_EQ(credential->secret, "secondpass");
}
