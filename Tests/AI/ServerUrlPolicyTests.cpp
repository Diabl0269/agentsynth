// ServerUrlPolicyTests.cpp -- bug kind credential-sent-to-unverified-host: the account token and the device id go only
// to the official server, a custom server address is https (http only for loopback), and a sign-in page is opened only
// on our own hosts.

#include "../UI/SourceScan.h"
#include "AI/ServerUrlPolicy.h"
#include "Branding.h"

#include <gtest/gtest.h>
#include <regex>

using namespace synth::serverurl;

TEST(ServerUrlPolicy, AcceptableServerUrlTable) {
    struct Row {
        const char* url;
        bool acceptable;
    };
    const Row rows[] = {
        {"https://my.server.example", true},
        {"https://my.server.example:8443/base", true},
        {"HTTPS://My.Server.Example", true},
        {"http://localhost:1234", true},
        {"http://127.0.0.1:11434", true},
        {"http://[::1]:8080", true},
        {"http://localhost.evil.example", false},
        {"http://my.server.example", false},
        {"http://192.168.1.20:8787", false},
        {"ftp://my.server.example", false},
        {"my.server.example", false},
        {"https://", false},
        {"https://user@evil.example", false},
        {"", false},
    };
    for (const auto& row : rows)
        EXPECT_EQ(isAcceptableServerUrl(row.url), row.acceptable) << row.url;
}

TEST(ServerUrlPolicy, OnlyTheApiHostIsOfficial) {
    EXPECT_TRUE(isOfficialServer(synth::branding::kApiBaseUrl));
    EXPECT_TRUE(isOfficialServer(juce::String(synth::branding::kApiBaseUrl) + "/"));
    EXPECT_FALSE(isOfficialServer("https://synth-api-6eft3t2kxq-uc.a.run.app.evil.example"));
    EXPECT_FALSE(isOfficialServer("https://evil.example"));
    EXPECT_FALSE(isOfficialServer("http://localhost:8787"));
    EXPECT_FALSE(isOfficialServer("http://synth-api-6eft3t2kxq-uc.a.run.app"));
    EXPECT_FALSE(isOfficialServer({}));
}

TEST(ServerUrlPolicy, LoopbackHosts) {
    EXPECT_TRUE(isLoopbackHost("localhost"));
    EXPECT_TRUE(isLoopbackHost("127.0.0.1"));
    EXPECT_TRUE(isLoopbackHost("::1"));
    EXPECT_FALSE(isLoopbackHost("127.0.0.2.evil.example"));
    EXPECT_FALSE(isLoopbackHost("example.com"));
}

TEST(ServerUrlPolicy, TrustedVerificationUri) {
    EXPECT_TRUE(isTrustedVerificationUri(juce::String(synth::branding::kApiBaseUrl) + "/device?code=ABCD"));
    EXPECT_TRUE(isTrustedVerificationUri("https://agentsynth.app/device?user_code=ABCD"));
    EXPECT_TRUE(isTrustedVerificationUri("https://www.agentsynth.app/device"));
    EXPECT_FALSE(isTrustedVerificationUri("http://agentsynth.app/device"));
    EXPECT_FALSE(isTrustedVerificationUri("https://agentsynth.app.evil.example/device"));
    EXPECT_FALSE(isTrustedVerificationUri("https://evilagentsynth.app/device"));
    EXPECT_FALSE(isTrustedVerificationUri("https://evil.example/agentsynth.app"));
    EXPECT_FALSE(isTrustedVerificationUri("javascript:alert(1)"));
    EXPECT_FALSE(isTrustedVerificationUri({}));
}

// Guard (allow-list of three files): every place that attaches an Authorization header names itself here.
// RemoteProvider talks to a user-chosen address and so must gate on isOfficialServer; AuthClient is only ever built
// with the official base URL, which the second half of this test checks for every construction.
TEST(CredentialSentToUnverifiedHostGuard, EveryAuthorizationHeaderSiteGatesOnTheOfficialServer) {
    static const std::regex attach(R"re("Authorization"|"Bearer )re");
    static const std::regex authClientBuild(R"re(AuthClient\s*\(([^;]*))re");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (std::regex_search(text, attach)) {
            if (file == "Source/AI/RemoteProvider.cpp" || file == "Source/AI/RemoteProviderAuth.cpp") {
                EXPECT_NE(text.find("isOfficialServer"), std::string::npos)
                    << file << " attaches the token without checking isOfficialServer";
            } else {
                EXPECT_EQ(file, "Source/AI/AuthClient.cpp")
                    << file << " attaches an Authorization header: only the official server may get the account "
                    << "token, so gate on synth::serverurl::isOfficialServer and add the file to this allow-list";
            }
        }
        if (file == "Source/AI/AuthClient.cpp" || file == "Source/AI/AuthClient.h" ||
            file == "Source/AI/AccountService.cpp" || file == "Source/AI/AccountService.h")
            continue;
        for (std::sregex_iterator it(text.begin(), text.end(), authClientBuild), end; it != end; ++it) {
            const std::string args = (*it)[1].str().substr(0, 80);
            if (args.find("branding::") == std::string::npos)
                ADD_FAILURE() << file << " builds an AuthClient from something other than synth::branding: " << args;
        }
    }
}
