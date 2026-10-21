#include <gtest/gtest.h>

#include "gth/network/Win32NetworkInterfaceDetector.h"

using namespace gth::core;
using namespace gth::network;

// Real adapters vary by machine, so these are structural sanity checks
// (doesn't crash, fields make sense when present) rather than assertions
// about exact interfaces - unlike the SHA-256 tests, there's no fixed
// "known vector" for a network adapter list.

TEST(Win32NetworkInterfaceDetectorTest, DoesNotCrashAndReturnsAVector) {
    Win32NetworkInterfaceDetector detector;
    auto results = detector.detect();
    SUCCEED();  // reaching here without crashing/throwing is the actual assertion
    (void)results;
}

TEST(Win32NetworkInterfaceDetectorTest, EveryReturnedInterfaceHasANonEmptyName) {
    Win32NetworkInterfaceDetector detector;
    for (const auto& iface : detector.detect()) {
        EXPECT_FALSE(iface.name.empty());
    }
}

TEST(Win32NetworkInterfaceDetectorTest, ExcludesLoopback) {
    Win32NetworkInterfaceDetector detector;
    for (const auto& iface : detector.detect()) {
        EXPECT_NE(iface.ipv4, "127.0.0.1");
    }
}

TEST(Win32NetworkInterfaceDetectorTest, UpInterfacesTypicallyHaveAMacAddress) {
    Win32NetworkInterfaceDetector detector;
    for (const auto& iface : detector.detect()) {
        if (iface.status == NetworkInterfaceStatus::Up && iface.type != NetworkInterfaceType::VirtualAdapter) {
            EXPECT_FALSE(iface.macAddress.empty()) << "interface: " << iface.name;
        }
    }
}

TEST(NetworkInterfaceInfoTest, ToStringCoversEveryEnumValue) {
    EXPECT_EQ(toString(NetworkInterfaceType::Ethernet), "Ethernet");
    EXPECT_EQ(toString(NetworkInterfaceType::WiFi), "Wi-Fi");
    EXPECT_EQ(toString(NetworkInterfaceType::VirtualAdapter), "Virtual Adapter");
    EXPECT_EQ(toString(NetworkInterfaceType::Other), "Other");
    EXPECT_EQ(toString(NetworkInterfaceType::Unknown), "Unknown");

    EXPECT_EQ(toString(NetworkInterfaceStatus::Up), "Up");
    EXPECT_EQ(toString(NetworkInterfaceStatus::Down), "Down");
    EXPECT_EQ(toString(NetworkInterfaceStatus::Unknown), "Unknown");
}
