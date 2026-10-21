#include <gtest/gtest.h>

#include <filesystem>

#include "gth/network/WindowsFirewallService.h"

using gth::network::BuildFirewallRuleSpecs;
using gth::network::FirewallOptOutList;
using gth::network::IsBlockingRuleForApp;
using gth::network::SameFirewallPath;
using gth::network::WindowsFirewallService;

// Nothing here adds, changes, disables or removes a firewall rule - the tests
// only check the rule definitions the app *would* install, how it recognises
// a rule that blocks it, the opt-out memory, and a read-only status query.

TEST(WindowsFirewallServiceTest, InstallsAnInboundRuleLimitedToTheLocalSubnetAndAnOutboundRule) {
    const std::wstring exe = L"C:\\Games\\GameTransferHub\\GameTransferHub.exe";
    const auto specs = BuildFirewallRuleSpecs(exe);

    ASSERT_EQ(specs.size(), 2u);
    const auto& inbound = specs[0];
    const auto& outbound = specs[1];

    EXPECT_TRUE(inbound.inbound);
    EXPECT_EQ(inbound.protocol, 6) << "the FTP server, proxy and accelerator are all TCP";
    EXPECT_EQ(inbound.remoteAddresses, L"LocalSubnet")
        << "incoming connections must only be allowed from the local network, not the internet";

    EXPECT_FALSE(outbound.inbound);

    for (const auto& spec : specs) {
        EXPECT_EQ(spec.applicationPath, exe) << "rules are scoped to this program, not opened by port";
        EXPECT_FALSE(spec.name.empty());
        EXPECT_FALSE(spec.description.empty());
    }
    EXPECT_NE(inbound.name, outbound.name);
}

TEST(WindowsFirewallServiceTest, ComparesWindowsPathsWithoutRegardToCase) {
    EXPECT_TRUE(SameFirewallPath(L"C:\\Apps\\GameTransferHub.exe", L"c:\\apps\\gametransferhub.EXE"));
    EXPECT_FALSE(SameFirewallPath(L"C:\\Apps\\GameTransferHub.exe", L"C:\\Other\\GameTransferHub.exe"));
    EXPECT_FALSE(SameFirewallPath(L"C:\\Apps\\a.exe", L"C:\\Apps\\a.exe2"));
}

// Real rules found on the machine this was developed on: dismissing Windows'
// "allow this app?" prompt left "gametransferhub.exe" BLOCK rules for TCP and
// UDP on Public networks (stored with a lower-cased path), beside allow rules
// for Private. A block rule beats any allow rule, so these are what stopped
// other devices connecting until the whole firewall was switched off.
TEST(WindowsFirewallServiceTest, RecognisesTheBlockRuleWindowsLeavesBehindWhenItsPromptIsDismissed) {
    const std::wstring exe = L"C:\\Users\\DrGame\\Desktop\\GameTransferHub.exe";
    const std::wstring storedByWindows = L"c:\\users\\drgame\\desktop\\gametransferhub.exe";

    EXPECT_TRUE(IsBlockingRuleForApp(/*block=*/true, /*enabled=*/true, storedByWindows, exe));
}

TEST(WindowsFirewallServiceTest, DoesNotTreatAllowRulesDisabledRulesOrOtherProgramsAsBlockers) {
    const std::wstring exe = L"C:\\Users\\DrGame\\Desktop\\GameTransferHub.exe";

    EXPECT_FALSE(IsBlockingRuleForApp(false, true, exe, exe)) << "an allow rule isn't a blocker";
    EXPECT_FALSE(IsBlockingRuleForApp(true, false, exe, exe)) << "an already-disabled block rule doesn't block";
    EXPECT_FALSE(IsBlockingRuleForApp(true, true, L"C:\\Other\\GameTransferHub.exe", exe))
        << "another copy's rule is none of this copy's business";
    EXPECT_FALSE(IsBlockingRuleForApp(true, true, L"", exe)) << "a rule with no program isn't aimed at this exe";
}

// The user removing the rules on purpose must stick across restarts, per copy
// of the app (a re-extracted portable copy is a different program to Windows).
TEST(WindowsFirewallServiceTest, RemembersWhichCopiesOfTheAppHadTheirRulesRemovedOnPurpose) {
    const auto file = std::filesystem::temp_directory_path() / "gth_firewall_optout_test" / "off.txt";
    std::filesystem::remove_all(file.parent_path());
    const std::wstring copyA = L"C:\\Apps\\GTH 3.9\\GameTransferHub.exe";
    const std::wstring copyB = L"C:\\Apps\\GTH 4.0\\GameTransferHub.exe";

    FirewallOptOutList list(file);
    EXPECT_FALSE(list.contains(copyA));

    list.add(copyA);
    list.add(copyA);  // adding twice must not duplicate
    EXPECT_TRUE(list.contains(copyA));
    EXPECT_FALSE(list.contains(copyB));
    EXPECT_TRUE(FirewallOptOutList(file).contains(L"c:\\apps\\gth 3.9\\GAMETRANSFERHUB.EXE"))
        << "a fresh reader (a restart) sees it, whatever the path's case";

    list.add(copyB);
    list.remove(copyA);
    EXPECT_FALSE(list.contains(copyA)) << "explicitly allowing again clears the opt-out";
    EXPECT_TRUE(list.contains(copyB)) << "without disturbing other copies";

    std::filesystem::remove_all(file.parent_path());
}

TEST(WindowsFirewallServiceTest, ReadOnlyStatusQueryReportsThisTestProgramHasNoRules) {
    WindowsFirewallService service(std::filesystem::temp_directory_path() / "gth_firewall_status_test.txt");

    const auto status = service.status();

    if (!status.available) {
        EXPECT_FALSE(status.detail.empty()) << "an unavailable firewall must say why";
    } else {
        EXPECT_FALSE(status.appAllowed) << "this test exe was never given a rule";
        EXPECT_EQ(status.blockingRules, 0) << "nothing blocks the test exe by name";
    }
}
