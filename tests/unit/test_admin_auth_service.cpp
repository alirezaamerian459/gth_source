#include <gtest/gtest.h>

#include <map>

#include "gth/application/auth/AdminAuthService.h"

using namespace gth::core;
using namespace gth::application;

namespace {

class FakeCredentialStore : public ICredentialStore {
public:
    std::map<std::string, Credential> saved;

    void save(const std::string& target, const std::string& username, const std::string& secret) override {
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

TEST(AdminAuthServiceTest, DefaultCredentialSignsInWithNoPriorSave) {
    AdminAuthServiceFixture fixture;

    EXPECT_TRUE(fixture.service.signIn("drgame", "12345678"));
    EXPECT_TRUE(fixture.service.isSignedIn());
    EXPECT_EQ(fixture.service.currentUsername(), "drgame");
}

TEST(AdminAuthServiceTest, WrongCredentialsFailAndDoNotSignIn) {
    AdminAuthServiceFixture fixture;

    EXPECT_FALSE(fixture.service.signIn("drgame", "wrong"));
    EXPECT_FALSE(fixture.service.isSignedIn());
    EXPECT_FALSE(fixture.service.signIn("someoneelse", "12345678"));
    EXPECT_FALSE(fixture.service.isSignedIn());
}

TEST(AdminAuthServiceTest, SignOutResetsState) {
    AdminAuthServiceFixture fixture;

    ASSERT_TRUE(fixture.service.signIn("drgame", "12345678"));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.isSignedIn());
    EXPECT_EQ(fixture.service.currentUsername(), "");
}

TEST(AdminAuthServiceTest, ChangeCredentialsFailsWhenNotSignedIn) {
    AdminAuthServiceFixture fixture;

    EXPECT_FALSE(fixture.service.changeCredentials("12345678", std::nullopt, std::string("newpass")));
}

TEST(AdminAuthServiceTest, ChangeCredentialsFailsWithWrongCurrentPassword) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn("drgame", "12345678"));

    EXPECT_FALSE(fixture.service.changeCredentials("wrongpassword", std::nullopt, std::string("newpass")));
    // Old password still works - nothing was changed.
    fixture.service.signOut();
    EXPECT_TRUE(fixture.service.signIn("drgame", "12345678"));
}

TEST(AdminAuthServiceTest, ChangePasswordOnlyLeavesUsernameUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn("drgame", "12345678"));

    EXPECT_TRUE(fixture.service.changeCredentials("12345678", std::nullopt, std::string("newpass")));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.signIn("drgame", "12345678"));
    EXPECT_TRUE(fixture.service.signIn("drgame", "newpass"));
}

TEST(AdminAuthServiceTest, ChangeUsernameOnlyLeavesPasswordUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn("drgame", "12345678"));

    EXPECT_TRUE(fixture.service.changeCredentials("12345678", std::string("newadmin"), std::nullopt));
    fixture.service.signOut();

    EXPECT_FALSE(fixture.service.signIn("drgame", "12345678"));
    EXPECT_TRUE(fixture.service.signIn("newadmin", "12345678"));
}

TEST(AdminAuthServiceTest, EmptyStringNewFieldsAlsoLeaveThatFieldUnchanged) {
    AdminAuthServiceFixture fixture;
    ASSERT_TRUE(fixture.service.signIn("drgame", "12345678"));

    EXPECT_TRUE(fixture.service.changeCredentials("12345678", std::string(""), std::string("newpass")));
    fixture.service.signOut();

    EXPECT_TRUE(fixture.service.signIn("drgame", "newpass"));
}
