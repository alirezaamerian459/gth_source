#include <gtest/gtest.h>

#include <map>

#include "gth/application/auth/AdminAuthService.h"

using namespace gth::core;
using namespace gth::application;

namespace {

constexpr char kUsername[] = "admin";
constexpr char kChosenPassword[] = "first-use-passphrase";

class FakeCredentialStore : public ICredentialStore {
public:
    std::map<std::string, Credential> saved;
    bool failWrites = false;

    void save(const std::string& target, const std::string& username, const std::string& secret) override {
        if (failWrites) return;
        saved[target] = {username, secret};
    }
    std::optional<Credential> read(const std::string& target) override {
        auto it = saved.find(target);
        if (it == saved.end()) return std::nullopt;
        return it->second;
    }
    void remove(const std::string& target) override { saved.erase(target); }
};

struct AdminAuthServiceFixture {
    FakeCredentialStore credentialStore;
    AdminAuthService service{credentialStore};
};

}  // namespace

TEST(AdminAuthServiceTest, FirstSignInStoresTheCredentialsChosenByTheUser) {
    AdminAuthServiceFixture fixture;

    EXPECT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));
    EXPECT_TRUE(fixture.service.isSignedIn());
    EXPECT_EQ(fixture.service.currentUsername(), kUsername);
    const auto saved = fixture.credentialStore.read("admin_login");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->username, kUsername);
    EXPECT_EQ(saved->secret, kChosenPassword);
}

TEST(AdminAuthServiceTest, EmptyFirstSignInDoesNotCreateCredentials) {
    AdminAuthServiceFixture fixture;

    EXPECT_FALSE(fixture.service.signIn("", ""));
    EXPECT_FALSE(fixture.service.isSignedIn());
    EXPECT_FALSE(fixture.credentialStore.read("admin_login").has_value());
}

TEST(AdminAuthServiceTest, FirstSignInFailsWhenCredentialCannotBeStored) {
    AdminAuthServiceFixture fixture;
    fixture.credentialStore.failWrites = true;

    EXPECT_FALSE(fixture.service.signIn(kUsername, kChosenPassword));
    EXPECT_FALSE(fixture.service.isSignedIn());
}

TEST(AdminAuthServiceTest, WrongCredentialsFailAndDoNotSignIn) {
    AdminAuthServiceFixture fixture;
    fixture.credentialStore.save("admin_login", kUsername, kChosenPassword);

    EXPECT_FALSE(fixture.service.signIn(kUsername, "wrong"));
    EXPECT_FALSE(fixture.service.isSignedIn());
    EXPECT_FALSE(fixture.service.signIn("someoneelse", kChosenPassword));
    EXPECT_FALSE(fixture.service.isSignedIn());
}

TEST(AdminAuthServiceTest, SignOutResetsState) {
    AdminAuthServiceFixture fixture;

    ASSERT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.isSignedIn());
    EXPECT_EQ(fixture.service.currentUsername(), "");
}

TEST(AdminAuthServiceTest, ChangeCredentialsFailsWhenNotSignedIn) {
    AdminAuthServiceFixture fixture;

    EXPECT_FALSE(fixture.service.changeCredentials(kChosenPassword, std::nullopt, std::string("newpass")));
}

TEST(AdminAuthServiceTest, ChangeCredentialsFailsWithWrongCurrentPassword) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));

    EXPECT_FALSE(fixture.service.changeCredentials("wrongpassword", std::nullopt, std::string("newpass")));
    fixture.service.signOut();
    EXPECT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));
}

TEST(AdminAuthServiceTest, ChangePasswordOnlyLeavesUsernameUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));

    EXPECT_TRUE(fixture.service.changeCredentials(kChosenPassword, std::nullopt, std::string("newpass")));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.signIn(kUsername, kChosenPassword));
    EXPECT_TRUE(fixture.service.signIn(kUsername, "newpass"));
}

TEST(AdminAuthServiceTest, ChangeUsernameOnlyLeavesPasswordUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));

    EXPECT_TRUE(fixture.service.changeCredentials(kChosenPassword, std::string("newadmin"), std::nullopt));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.signIn(kUsername, kChosenPassword));
    EXPECT_TRUE(fixture.service.signIn("newadmin", kChosenPassword));
}

TEST(AdminAuthServiceTest, EmptyStringNewFieldsAlsoLeaveThatFieldUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn(kUsername, kChosenPassword));

    EXPECT_TRUE(fixture.service.changeCredentials(kChosenPassword, std::string(""), std::string("newpass")));
    fixture.service.signOut();

    EXPECT_TRUE(fixture.service.signIn(kUsername, "newpass"));
}
