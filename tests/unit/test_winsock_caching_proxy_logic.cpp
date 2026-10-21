#include <gtest/gtest.h>

#include "gth/acceleratorserver/WinsockCachingProxyServerInternal.h"

using namespace gth::acceleratorserver::internal;

TEST(WinsockCachingProxyLogicTest, ParsesARequestLine) {
    ParsedRequest req;
    ASSERT_TRUE(parseRequestLine("GET http://example.com/foo HTTP/1.1", req));
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.target, "http://example.com/foo");
    EXPECT_EQ(req.version, "HTTP/1.1");
}

TEST(WinsockCachingProxyLogicTest, RejectsAMalformedRequestLine) {
    ParsedRequest req;
    EXPECT_FALSE(parseRequestLine("", req));
}

TEST(WinsockCachingProxyLogicTest, ParsesHeaderLinesCaseInsensitively) {
    std::vector<HeaderLine> headers;
    parseHeaderLines("GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: TestConsole/1.0\r\n\r\n", headers);

    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(findHeader(headers, "host").value_or(""), "example.com");
    EXPECT_EQ(findHeader(headers, "user-agent").value_or(""), "TestConsole/1.0");
    EXPECT_FALSE(findHeader(headers, "content-length").has_value());
}

TEST(WinsockCachingProxyLogicTest, ResolvesAnAbsoluteFormTarget) {
    ParsedRequest req;
    req.target = "http://cdn.example.com:8080/path/to/file.pkg";

    TargetUrl out;
    ASSERT_TRUE(resolveTargetUrl(req, out));
    EXPECT_EQ(out.host, "cdn.example.com");
    EXPECT_EQ(out.port, 8080);
    EXPECT_EQ(out.pathAndQuery, "/path/to/file.pkg");
}

TEST(WinsockCachingProxyLogicTest, ResolvesAnAbsoluteFormTargetWithNoExplicitPort) {
    ParsedRequest req;
    req.target = "http://cdn.example.com/manifest.json";

    TargetUrl out;
    ASSERT_TRUE(resolveTargetUrl(req, out));
    EXPECT_EQ(out.host, "cdn.example.com");
    EXPECT_EQ(out.port, 80);
    EXPECT_EQ(out.pathAndQuery, "/manifest.json");
}

// A transparent proxy config (the shape this proxy actually receives from a console pointed at it) sends
// origin-form requests - just a path, with the real host only in the Host header.
TEST(WinsockCachingProxyLogicTest, FallsBackToTheHostHeaderForAnOriginFormTarget) {
    ParsedRequest req;
    req.target = "/path/to/file.pkg";
    req.headers = {{"Host", "cdn.example.com:9000"}};

    TargetUrl out;
    ASSERT_TRUE(resolveTargetUrl(req, out));
    EXPECT_EQ(out.host, "cdn.example.com");
    EXPECT_EQ(out.port, 9000);
    EXPECT_EQ(out.pathAndQuery, "/path/to/file.pkg");
}

TEST(WinsockCachingProxyLogicTest, FailsToResolveAnOriginFormTargetWithNoHostHeader) {
    ParsedRequest req;
    req.target = "/path/to/file.pkg";

    TargetUrl out;
    EXPECT_FALSE(resolveTargetUrl(req, out));
}

TEST(WinsockCachingProxyLogicTest, BuildsAnAbsoluteUrlOmittingTheDefaultPort) {
    TargetUrl t;
    t.host = "cdn.example.com";
    t.port = 80;
    t.pathAndQuery = "/manifest.json";
    EXPECT_EQ(absoluteUrl(t), "http://cdn.example.com/manifest.json");
}

TEST(WinsockCachingProxyLogicTest, BuildsAnAbsoluteUrlIncludingANonDefaultPort) {
    TargetUrl t;
    t.host = "cdn.example.com";
    t.port = 8080;
    t.pathAndQuery = "/manifest.json";
    EXPECT_EQ(absoluteUrl(t), "http://cdn.example.com:8080/manifest.json");
}

TEST(WinsockCachingProxyLogicTest, RecognizesHopByHopHeaders) {
    EXPECT_TRUE(isHopByHop("connection"));
    EXPECT_TRUE(isHopByHop("proxy-connection"));
    EXPECT_TRUE(isHopByHop("proxy-authorization"));
    EXPECT_TRUE(isHopByHop("keep-alive"));
    EXPECT_FALSE(isHopByHop("host"));
    EXPECT_FALSE(isHopByHop("content-length"));
}
