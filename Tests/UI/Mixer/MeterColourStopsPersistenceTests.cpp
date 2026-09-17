// MeterColourStopsPersistenceTests.cpp -- FRO147: the "meterColourStops" global override
// (Source/UI/Mixer/MeterColourStops.h's persistence section) and the AppLookAndFeel cache every
// meter painter reads (Source/UI/Theme/AppLookAndFeel/AppLookAndFeel.h's meter-colour-stops
// section). Modeled on NoteColourTests.cpp's persistence half: round-trip, absent-key and
// malformed-value coverage, plus (new here) the theme-switch interaction the cache itself owns.

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth::ui;

namespace {

juce::File tempSettingsFile(const juce::String& name) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
}

std::unique_ptr<juce::PropertiesFile> makeProps(const juce::String& name) {
    auto file = tempSettingsFile(name);
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

std::vector<MeterColourStop> fourDistinctStops() {
    return {
        {kMeterMinDb, juce::Colour(0xff112233)},
        {-18.0f, juce::Colour(0xff223344)},
        {-6.0f, juce::Colour(0xff334455)},
        {0.0f, juce::Colour(0xff445566)},
    };
}

} // namespace

//==============================================================================
// serializeMeterColourStops / parseMeterColourStops
//==============================================================================

TEST(MeterColourStopsPersistenceTest, RoundTripsAnArbitraryStopSet) {
    const MeterColourStops original(fourDistinctStops());
    const auto raw = serializeMeterColourStops(original);
    const auto parsed = parseMeterColourStops(raw);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->getStops().size(), original.getStops().size());
    for (size_t i = 0; i < original.getStops().size(); ++i) {
        EXPECT_FLOAT_EQ(parsed->getStops()[i].dbFrom, original.getStops()[i].dbFrom) << "stop " << i;
        EXPECT_EQ(parsed->getStops()[i].colour, original.getStops()[i].colour) << "stop " << i;
    }
}

TEST(MeterColourStopsPersistenceTest, RoundTripsASingleStop) {
    const MeterColourStops one({{kMeterMinDb, juce::Colour(0xffabcdef)}});
    const auto parsed = parseMeterColourStops(serializeMeterColourStops(one));
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->getStops().size(), 1u);
    EXPECT_EQ(parsed->getStops()[0].colour, juce::Colour(0xffabcdef));
}

TEST(MeterColourStopsPersistenceTest, RoundTripsTheMaximumEightStops) {
    std::vector<MeterColourStop> eight;
    for (int i = 0; i < MeterColourStops::kMaxStops; ++i)
        eight.push_back({kMeterMinDb + (float)i * 9.0f, juce::Colour((juce::uint8)i, (juce::uint8)i, (juce::uint8)i)});
    const MeterColourStops stops(eight);
    ASSERT_EQ(stops.getStops().size(), (size_t)MeterColourStops::kMaxStops);
    const auto parsed = parseMeterColourStops(serializeMeterColourStops(stops));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->getStops().size(), (size_t)MeterColourStops::kMaxStops);
}

TEST(MeterColourStopsPersistenceTest, EmptyStringIsMalformed) { EXPECT_FALSE(parseMeterColourStops("").has_value()); }

TEST(MeterColourStopsPersistenceTest, MoreThanEightStopsIsMalformed) {
    juce::StringArray tokens;
    for (int i = 0; i < MeterColourStops::kMaxStops + 1; ++i)
        tokens.add(juce::String(-60.0f + (float)i, 1) + ":FF112233");
    EXPECT_FALSE(parseMeterColourStops(tokens.joinIntoString(",")).has_value());
}

TEST(MeterColourStopsPersistenceTest, ATokenMissingTheColonIsMalformed) {
    EXPECT_FALSE(parseMeterColourStops("-60.0FF112233").has_value());
}

TEST(MeterColourStopsPersistenceTest, AGarbageDbFieldIsMalformedNotZero) {
    // getFloatValue() on garbage returns 0 -- this must be rejected outright, never silently
    // manufacture a stop at 0 dB.
    EXPECT_FALSE(parseMeterColourStops("-abc:FF112233").has_value());
    EXPECT_FALSE(parseMeterColourStops("1.2.3:FF112233").has_value());
    EXPECT_FALSE(parseMeterColourStops("--5:FF112233").has_value());
    EXPECT_FALSE(parseMeterColourStops("5-:FF112233").has_value());
}

TEST(MeterColourStopsPersistenceTest, ADbValueOutsideTheScaleIsMalformed) {
    EXPECT_FALSE(parseMeterColourStops("-61.0:FF112233").has_value());
    EXPECT_FALSE(parseMeterColourStops("4.0:FF112233").has_value());
}

TEST(MeterColourStopsPersistenceTest, AShortOrNonHexColourFieldIsMalformed) {
    EXPECT_FALSE(parseMeterColourStops("-60.0:FF1122").has_value());   // 6 chars, not 8
    EXPECT_FALSE(parseMeterColourStops("-60.0:GG112233").has_value()); // non-hex character
}

TEST(MeterColourStopsPersistenceTest, OneMalformedTokenFailsTheWholeKeyNotAPartialApply) {
    // Two good tokens, one bad -- the whole parse must fail rather than silently dropping the bad
    // one and keeping the other two.
    EXPECT_FALSE(parseMeterColourStops("-60.0:FF112233,-abc:FF223344,0.0:FF334455").has_value());
}

TEST(MeterColourStopsPersistenceTest, UnsortedOrDuplicateInputStillNormalisesRatherThanFailing) {
    // Not something serializeMeterColourStops() itself ever emits, but a hand-edited settings file
    // could -- MeterColourStops' own vector constructor is the safety net (sorts, dedups, never
    // empty), same contract as MeterColourStopsTests.cpp's own coverage of that constructor.
    const auto parsed = parseMeterColourStops("0.0:FFAAAAAA,-60.0:FFBBBBBB");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->getStops().size(), 2u);
    EXPECT_FLOAT_EQ(parsed->getStops()[0].dbFrom, -60.0f);
}

//==============================================================================
// load/write/save/clear against a real juce::PropertiesFile
//==============================================================================

TEST(MeterColourStopsPersistenceTest, AbsentKeyLoadsAsNullopt) {
    auto props = makeProps("MeterColourStopsAbsent");
    EXPECT_FALSE(loadMeterColourStopsOverride(*props).has_value());
}

TEST(MeterColourStopsPersistenceTest, MalformedStoredValueLoadsAsNulloptNotACrash) {
    auto props = makeProps("MeterColourStopsMalformed");
    props->setValue(meterColourStopsKey(), "garbage-not-a-stop-list");
    EXPECT_FALSE(loadMeterColourStopsOverride(*props).has_value());
}

TEST(MeterColourStopsPersistenceTest, WriteThenLoadRoundTrips) {
    auto props = makeProps("MeterColourStopsWriteLoad");
    const MeterColourStops stops(fourDistinctStops());
    writeMeterColourStopsOverride(*props, stops);
    const auto loaded = loadMeterColourStopsOverride(*props);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->getStops().size(), stops.getStops().size());
}

TEST(MeterColourStopsPersistenceTest, SaveThenLoadRoundTrips) {
    auto props = makeProps("MeterColourStopsSaveLoad");
    const MeterColourStops stops({{kMeterMinDb, juce::Colour(0xff998877)}});
    saveMeterColourStopsOverride(*props, stops);
    const auto loaded = loadMeterColourStopsOverride(*props);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->getStops()[0].colour, juce::Colour(0xff998877));
}

TEST(MeterColourStopsPersistenceTest, ClearRemovesTheKeyRatherThanWritingTheThemeIn) {
    auto props = makeProps("MeterColourStopsClear");
    saveMeterColourStopsOverride(*props, MeterColourStops(fourDistinctStops()));
    ASSERT_TRUE(props->containsKey(meterColourStopsKey()));
    clearMeterColourStopsOverride(*props);
    EXPECT_FALSE(props->containsKey(meterColourStopsKey()));
    EXPECT_FALSE(loadMeterColourStopsOverride(*props).has_value());
}

//==============================================================================
// AppLookAndFeel's cache -- the ONE thing every meter painter reads (MixerMeter.cpp,
// ChannelChipComponent.cpp). See MeterColourStopsLiveApplyTests.cpp for the painter-facing half
// (pixel sampling); this half is the cache's own bookkeeping.
//==============================================================================

TEST(MeterColourStopsAppLookAndFeelTest, NoOverrideFollowsTheActiveTheme) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());
    EXPECT_FALSE(laf.hasMeterColourStopsOverride());
    const auto expected = MeterColourStops::fromTheme(synth::theme::makeObsidian().colors);
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(-30.0f), expected.colourForDb(-30.0f));
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(2.0f), expected.colourForDb(2.0f));
}

TEST(MeterColourStopsAppLookAndFeelTest, SettingAnOverrideMakesItTheEffectiveStopsRegardlessOfTheme) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());

    const MeterColourStops custom({{kMeterMinDb, juce::Colour(0xff010101)}, {0.0f, juce::Colour(0xfffefefe)}});
    laf.setMeterColourStopsOverride(custom);
    EXPECT_TRUE(laf.hasMeterColourStopsOverride());
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(-30.0f), juce::Colour(0xff010101));
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(1.0f), juce::Colour(0xfffefefe));
}

TEST(MeterColourStopsAppLookAndFeelTest, AnOverrideSurvivesAThemeSwitch) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeObsidian());

    const MeterColourStops custom({{kMeterMinDb, juce::Colour(0xff010101)}});
    laf.setMeterColourStopsOverride(custom);
    laf.applyTheme(synth::theme::makeDaylight()); // theme switch must NOT clear the pinned override

    EXPECT_TRUE(laf.hasMeterColourStopsOverride());
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(-30.0f), juce::Colour(0xff010101));
}

TEST(MeterColourStopsAppLookAndFeelTest, ClearingTheOverrideFallsBackToTheCurrentTheme) {
    synth::theme::AppLookAndFeel laf;
    laf.applyTheme(synth::theme::makeDaylight());
    laf.setMeterColourStopsOverride(MeterColourStops({{kMeterMinDb, juce::Colour(0xff010101)}}));
    ASSERT_TRUE(laf.hasMeterColourStopsOverride());

    laf.setMeterColourStopsOverride(std::nullopt);
    EXPECT_FALSE(laf.hasMeterColourStopsOverride());
    const auto expected = MeterColourStops::fromTheme(synth::theme::makeDaylight().colors);
    EXPECT_EQ(laf.getMeterColourStops().colourForDb(-30.0f), expected.colourForDb(-30.0f));
}
