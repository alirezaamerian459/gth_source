#include <gtest/gtest.h>

#include "gth/network/TcpReachabilityChecker.h"

using namespace gth::network;

// Real network reachability is machine/environment-dependent, so this only
// asserts the one thing that's reliably true anywhere: a port nothing is
// listening on is reported unreachable, and the call returns within its
// timeout rather than hanging.
TEST(TcpReachabilityCheckerTest, ReportsUnreachableForAClosedLocalPort) {
    TcpReachabilityChecker checker;
    // Port 1 is reserved/unassigned and essentially never has a listener.
    EXPECT_FALSE(checker.isReachable("127.0.0.1", 1, 500));
}

TEST(TcpReachabilityCheckerTest, ReportsUnreachableForAnInvalidAddress) {
    TcpReachabilityChecker checker;
    EXPECT_FALSE(checker.isReachable("not-an-ip-address", 80, 500));
}
