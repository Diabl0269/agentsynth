// SoundPackIndexTests.cpp -- the hosted pack list: parsing, validation and the size wording, plus the SHA-256 check.
#include "SoundPackTestKit.h"
#include "SoundPacks/SoundPackChecksum.h"
#include "SoundPacks/SoundPackIndex.h"
#include <gtest/gtest.h>

using namespace synth::soundpacks;

namespace {

juce::String packJson(const juce::String& id, const juce::String& url = "https://example.test/a.zip",
                      const juce::String& sha = juce::String::repeatedString("ab", 32)) {
    return R"({"id":")" + id + R"(","name":"A pack","version":"1.2.0","url":")" + url + R"(","sha256":")" + sha +
           R"(","downloadBytes":100,"installedBytes":200,"licence":"CC0","licenceUrl":"https://example.test/l",
        "credit":"Credit line","instruments":[{"id":"x","name":"X","category":"Keys"}]})";
}

juce::String indexOf(const juce::StringArray& packs) {
    return R"({"format":1,"packs":[)" + packs.joinIntoString(",") + "]}";
}

} // namespace

TEST(SoundPackIndexTest, ParsesAWellFormedList) {
    const auto parsed = SoundPackIndex::parse(indexOf({packJson("core")}));
    ASSERT_TRUE(parsed.ok) << parsed.error;
    ASSERT_EQ(parsed.index.packs.size(), 1u);
    const auto& pack = parsed.index.packs[0];
    EXPECT_EQ(pack.id, "core");
    EXPECT_EQ(pack.name, "A pack");
    EXPECT_EQ(pack.version, "1.2.0");
    EXPECT_EQ(pack.downloadBytes, 100);
    EXPECT_EQ(pack.installedBytes, 200);
    EXPECT_EQ(pack.licenceUrl, "https://example.test/l");
    EXPECT_EQ(pack.credit, "Credit line");
    ASSERT_EQ(pack.instruments.size(), 1u);
    EXPECT_EQ(pack.instruments[0].name, "X");
    EXPECT_EQ(parsed.index.skippedPacks, 0);
    EXPECT_NE(parsed.index.find("core"), nullptr);
    EXPECT_EQ(parsed.index.find("other"), nullptr);
}

TEST(SoundPackIndexTest, AnUnknownFormatOrUnreadableJsonFailsInPlainWords) {
    const auto newer = SoundPackIndex::parse(R"({"format":2,"packs":[]})");
    EXPECT_FALSE(newer.ok);
    EXPECT_TRUE(newer.error.contains("different version")) << newer.error;

    for (const char* bad : {"not json", "[]", R"({"packs":[]})", R"({"format":1})", ""}) {
        const auto parsed = SoundPackIndex::parse(bad);
        EXPECT_FALSE(parsed.ok) << bad;
        EXPECT_TRUE(parsed.error.isNotEmpty()) << bad;
    }
}

TEST(SoundPackIndexTest, PathEscapingAndOtherBadIdsAreSkippedNotInstalled) {
    juce::StringArray packs;
    for (const char* id : {"../evil", "a/b", "a\\b", "..", "Upper", "", "has space", "x.y"})
        packs.add(packJson(id));
    packs.add(packJson("fine-1_x"));
    const auto parsed = SoundPackIndex::parse(indexOf(packs));
    ASSERT_TRUE(parsed.ok);
    ASSERT_EQ(parsed.index.packs.size(), 1u);
    EXPECT_EQ(parsed.index.packs[0].id, "fine-1_x");
    EXPECT_EQ(parsed.index.skippedPacks, 8);
}

TEST(SoundPackIndexTest, BadUrlsChecksumsAndDuplicatesAreSkipped) {
    const auto parsed = SoundPackIndex::parse(
        indexOf({packJson("http-only", "http://example.test/a.zip"), packJson("file-url", "file:///etc/passwd"),
                 packJson("short-sha", "https://example.test/a.zip", "abcd"),
                 packJson("upper-sha", "https://example.test/a.zip", juce::String::repeatedString("AB", 32)),
                 packJson("ok"), packJson("ok")}));
    ASSERT_TRUE(parsed.ok);
    // An upper-case checksum is normalised, so it is accepted; the plain-http, file and short ones are not.
    ASSERT_EQ(parsed.index.packs.size(), 2u);
    EXPECT_EQ(parsed.index.packs[0].id, "upper-sha");
    EXPECT_EQ(parsed.index.packs[1].id, "ok");
    EXPECT_EQ(parsed.index.skippedPacks, 4);
}

TEST(SoundPackIndexTest, ALicenceLinkThatIsNotHttpsIsDropped) {
    auto json = packJson("core");
    json = json.replace("https://example.test/l", "javascript:alert(1)");
    const auto parsed = SoundPackIndex::parse(indexOf({json}));
    ASSERT_TRUE(parsed.ok);
    EXPECT_TRUE(parsed.index.packs[0].licenceUrl.isEmpty());
}

TEST(SoundPackIndexTest, SizesReadInDecimalMegabytesAndGigabytes) {
    EXPECT_EQ(formatPackSize(32548439), "33 MB");
    EXPECT_EQ(formatPackSize(1), "1 MB");
    EXPECT_EQ(formatPackSize(650 * 1000 * 1000), "650 MB");
    EXPECT_EQ(formatPackSize(1100LL * 1000 * 1000), "1.1 GB");
}

TEST(SoundPackChecksumTest, MatchesTheFipsTestVectors) {
    EXPECT_EQ(sha256Hex("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const juce::String two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(sha256Hex(two.toRawUTF8(), two.getNumBytesAsUTF8()),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const juce::String million = juce::String::repeatedString("a", 1000000);
    EXPECT_EQ(sha256Hex(million.toRawUTF8(), million.getNumBytesAsUTF8()),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(SoundPackChecksumTest, AFileHashesLikeItsBytesAndACancelGivesNothing) {
    sound_pack_test::TempDir dir;
    const auto file = dir.path().getChildFile("f.bin");
    file.replaceWithText("abc");
    const std::atomic<bool> never{false};
    EXPECT_EQ(sha256Hex(file, never), sha256Hex("abc", 3));
    const std::atomic<bool> stop{true};
    EXPECT_TRUE(sha256Hex(file, stop).isEmpty());
    EXPECT_TRUE(sha256Hex(dir.path().getChildFile("missing"), never).isEmpty());
}
