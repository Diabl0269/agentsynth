// ThemeMeterZoneTests.cpp -- FRO146: the meter's mid/high/clip colour-zone tokens
// (meterMid/meterHigh/meterClip, docs/layout/theming.md's token table) -- loader plumbing only
// (optional-with-default parsing + JSON round-trip + built-in distinctness). Boundary/zone-
// selection behaviour itself lives in MeterColourStopsTests.cpp. Split out of ThemeTests.cpp
// (same "one topic, split by concern" convention as the note/track-button colour tokens there)
// to keep that file under the repo's 1,000-line cap. All headless.
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"
#include "UI/Theme/ThemeLoader.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

TEST(ThemeLoaderTest, MeterZoneTokensAbsentFallBackToObsidianDefaults) {
    // A pre-FRO146 user theme has none of the new keys; it must still load, and every new token
    // must come back as the Obsidian default (Colors()' default-constructed value).
    const juce::String legacy = R"({
        "name": "Legacy",
        "colors": {
            "bg0": "#FF000000", "surface": "#FF111111", "accent": "#FF00D1FF",
            "textPrimary": "#FFFFFFFF", "audioWire": "#FFEEEEEE", "modWire": "#FF00D1FF"
        }
    })";

    const auto parsed = synth::theme::ThemeLoader::parseTheme(juce::JSON::parse(legacy), "legacy");
    ASSERT_TRUE(parsed.has_value()) << synth::theme::ThemeLoader::getLastError();

    const synth::theme::Colors defaults;
    EXPECT_EQ(parsed->colors.meterMid, defaults.meterMid);
    EXPECT_EQ(parsed->colors.meterHigh, defaults.meterHigh);
    EXPECT_EQ(parsed->colors.meterClip, defaults.meterClip);
}

TEST(ThemeLoaderTest, MalformedMeterZoneTokenRejectsWholeTheme) {
    const juce::String bad = R"({
        "name": "BadMeterMid",
        "colors": {
            "bg0": "#FF000000", "surface": "#FF111111", "accent": "#FF00D1FF",
            "textPrimary": "#FFFFFFFF", "audioWire": "#FFEEEEEE", "modWire": "#FF00D1FF",
            "meterMid": "not-a-colour"
        }
    })";
    EXPECT_FALSE(synth::theme::ThemeLoader::parseTheme(juce::JSON::parse(bad)).has_value())
        << "Expected nullopt for malformed meterMid value";
}

TEST(ThemeBuiltInsTest, AllFourBuiltInsPopulateMeterZoneTokensDistinctly) {
    auto themes = synth::theme::builtInThemes();
    ASSERT_EQ(themes.size(), 4u);
    for (const auto& t : themes) {
        EXPECT_NE(t.colors.meterFill, t.colors.meterMid) << t.name;
        EXPECT_NE(t.colors.meterMid, t.colors.meterHigh) << t.name;
        EXPECT_NE(t.colors.meterHigh, t.colors.meterClip) << t.name;
    }
}
