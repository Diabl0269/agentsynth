// MeterColourStopsTests.cpp -- FRO146: the meter's level-to-colour zone model
// (Source/UI/Mixer/MeterColourStops.h), tested at its exact band edges, its POSITIONAL band
// iteration (forEachBand -- Cubase/most DAWs' stacked-zone meter convention), and its
// sorted/deduped/never-empty stop-set maintenance (setStops/the vector constructor).
#include "UI/Mixer/MeterColourStops.h"
#include "UI/Theme/Theme.h"
#include <gtest/gtest.h>
#include <vector>

using namespace synth::ui;

namespace {
synth::theme::Colors makeDistinctColors() {
    synth::theme::Colors colors;
    colors.meterFill = juce::Colour(0xff111111);
    colors.meterMid = juce::Colour(0xff222222);
    colors.meterHigh = juce::Colour(0xff333333);
    colors.meterClip = juce::Colour(0xff444444);
    return colors;
}
} // namespace

TEST(MeterColourStopsTest, BuiltFromTheThemesFourTokensNeverACodeLiteral) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_EQ(stops.colourForDb(-30.0f), colors.meterFill);
    EXPECT_EQ(stops.colourForDb(-10.0f), colors.meterMid);
    EXPECT_EQ(stops.colourForDb(-3.0f), colors.meterHigh);
    EXPECT_EQ(stops.colourForDb(3.0f), colors.meterClip);
}

TEST(MeterColourStopsTest, ExactBoundariesBelongToTheHigherZone) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kMidFromDb), colors.meterMid) << "-18 dB itself is MID, not low";
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kHighFromDb), colors.meterHigh) << "-6 dB itself is HIGH, not mid";
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kClipFromDb), colors.meterClip) << "0 dB itself is CLIP, not high";
}

TEST(MeterColourStopsTest, JustBelowEachBoundaryStaysInTheLowerZone) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kMidFromDb - 0.01f), colors.meterFill);
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kHighFromDb - 0.01f), colors.meterMid);
    EXPECT_EQ(stops.colourForDb(MeterColourStops::kClipFromDb - 0.01f), colors.meterHigh);
}

TEST(MeterColourStopsTest, FarBelowEverythingIsStillTheLowZone) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_EQ(stops.colourForDb(-1000.0f), colors.meterFill);
}

// ============================================================================
// forEachBand -- positional bands (Cubase/most DAWs' meter convention): a bar's fill is stacked
// zone bands, not one whole-bar colour.
// ============================================================================

namespace {
struct Band {
    float fromDb;
    float toDb;
    juce::Colour colour;
};

std::vector<Band> collectBands(const MeterColourStops& stops, float fromDb, float toDb) {
    std::vector<Band> bands;
    stops.forEachBand(fromDb, toDb, [&](float bandFromDb, float bandToDb, juce::Colour colour) {
        bands.push_back({bandFromDb, bandToDb, colour});
    });
    return bands;
}
} // namespace

TEST(MeterColourStopsTest, ABarReachingIntoClipYieldsAllFourBandsStackedLowToHigh) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    // A bar displayed at +4 dB (above kClipFromDb): low/mid/high/clip, bottom to top.
    const auto bands = collectBands(stops, -60.0f, 4.0f);
    ASSERT_EQ(bands.size(), 4u);
    EXPECT_FLOAT_EQ(bands[0].fromDb, -60.0f);
    EXPECT_FLOAT_EQ(bands[0].toDb, MeterColourStops::kMidFromDb);
    EXPECT_EQ(bands[0].colour, colors.meterFill);
    EXPECT_FLOAT_EQ(bands[1].fromDb, MeterColourStops::kMidFromDb);
    EXPECT_FLOAT_EQ(bands[1].toDb, MeterColourStops::kHighFromDb);
    EXPECT_EQ(bands[1].colour, colors.meterMid);
    EXPECT_FLOAT_EQ(bands[2].fromDb, MeterColourStops::kHighFromDb);
    EXPECT_FLOAT_EQ(bands[2].toDb, MeterColourStops::kClipFromDb);
    EXPECT_EQ(bands[2].colour, colors.meterHigh);
    EXPECT_FLOAT_EQ(bands[3].fromDb, MeterColourStops::kClipFromDb);
    EXPECT_FLOAT_EQ(bands[3].toDb, 4.0f);
    EXPECT_EQ(bands[3].colour, colors.meterClip);
}

TEST(MeterColourStopsTest, ABarStoppingMidwayThroughMidYieldsLowPlusAPartialMidBandOnly) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    const auto bands = collectBands(stops, -60.0f, -10.0f); // stops inside the mid zone (-18..-6)
    ASSERT_EQ(bands.size(), 2u);
    EXPECT_FLOAT_EQ(bands[0].fromDb, -60.0f);
    EXPECT_FLOAT_EQ(bands[0].toDb, MeterColourStops::kMidFromDb);
    EXPECT_EQ(bands[0].colour, colors.meterFill);
    EXPECT_FLOAT_EQ(bands[1].fromDb, MeterColourStops::kMidFromDb);
    EXPECT_FLOAT_EQ(bands[1].toDb, -10.0f) << "the mid band is cut off exactly at the bar's own displayed level";
    EXPECT_EQ(bands[1].colour, colors.meterMid);
}

TEST(MeterColourStopsTest, ARangeStartingInsideAZoneClipsThatBandsStartToTheRequestedFrom) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    // fromDb (-10) lands inside the mid zone (-18..-6) -- the emitted band must start at -10, not
    // at mid's own -18 (nothing below -10 was asked for).
    const auto bands = collectBands(stops, -10.0f, 3.0f);
    ASSERT_EQ(bands.size(), 3u);
    EXPECT_FLOAT_EQ(bands[0].fromDb, -10.0f);
    EXPECT_FLOAT_EQ(bands[0].toDb, MeterColourStops::kHighFromDb);
    EXPECT_EQ(bands[0].colour, colors.meterMid);
}

TEST(MeterColourStopsTest, ForEachBandIsANoOpWhenFromIsNotBeforeTo) {
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_TRUE(collectBands(stops, 0.0f, 0.0f).empty());
    EXPECT_TRUE(collectBands(stops, 3.0f, -3.0f).empty());
}

TEST(MeterColourStopsTest, PeakHoldStaysASingleColourNotABand) {
    // The peak-hold line is a 1 px cap, not a filled span -- callers colour it with colourForDb()
    // directly (MixerMeter::paint), never forEachBand(). This just pins that colourForDb() alone
    // (not a band) is what a single dB value resolves to, unaffected by the banding work above.
    const auto colors = makeDistinctColors();
    const auto stops = MeterColourStops::fromTheme(colors);
    EXPECT_EQ(stops.colourForDb(-2.0f), colors.meterHigh);
}

// ============================================================================
// setStops / the vector constructor -- sorted, deduped, arbitrary count, never empty.
// ============================================================================

TEST(MeterColourStopsTest, UnsortedInputIsSortedAscendingByDbFrom) {
    const MeterColourStops stops({
        {0.0f, juce::Colour(0xffAAAAAA)},
        {-60.0f, juce::Colour(0xffBBBBBB)},
        {-18.0f, juce::Colour(0xffCCCCCC)},
    });
    ASSERT_EQ(stops.getStops().size(), 3u);
    EXPECT_FLOAT_EQ(stops.getStops()[0].dbFrom, -60.0f);
    EXPECT_FLOAT_EQ(stops.getStops()[1].dbFrom, -18.0f);
    EXPECT_FLOAT_EQ(stops.getStops()[2].dbFrom, 0.0f);
}

TEST(MeterColourStopsTest, DuplicateDbFromCollapsesToTheFirstListedOfTheTie) {
    const juce::Colour firstListed(0xff111111);
    const juce::Colour secondListed(0xff222222);
    const MeterColourStops stops({
        {-18.0f, firstListed},
        {-60.0f, juce::Colour(0xff333333)},
        {-18.0f, secondListed}, // same dbFrom as the first entry -- the first-listed one wins
    });
    ASSERT_EQ(stops.getStops().size(), 2u) << "the duplicate dbFrom collapses to one stop";
    EXPECT_EQ(stops.getStops()[1].dbFrom, -18.0f);
    EXPECT_EQ(stops.getStops()[1].colour, firstListed) << "stable sort keeps the ORIGINAL, pre-sort order for ties";
}

TEST(MeterColourStopsTest, EmptyInputFallsBackToOneStopRatherThanLeavingNothing) {
    // std::vector<MeterColourStop>{}, not a bare {} -- an empty braced-init-list is otherwise
    // ambiguous between this vector conversion and the implicit copy/move constructor.
    const MeterColourStops stops(std::vector<MeterColourStop>{});
    ASSERT_EQ(stops.getStops().size(), 1u);
    // Still safe to query -- no crash, a defined (if arbitrary) colour comes back.
    EXPECT_EQ(stops.colourForDb(-3.0f), stops.getStops().front().colour);
}

TEST(MeterColourStopsTest, DefaultConstructedIsAlreadySafeWithOneStop) {
    const MeterColourStops stops;
    EXPECT_EQ(stops.getStops().size(), 1u);
    EXPECT_NO_FATAL_FAILURE(stops.colourForDb(0.0f));
}

TEST(MeterColourStopsTest, ArbitraryStopCountsAllWork) {
    // 1 stop: a single flat colour everywhere.
    const juce::Colour flat(0xff123456);
    const MeterColourStops one({{-60.0f, flat}});
    ASSERT_EQ(one.getStops().size(), 1u);
    EXPECT_EQ(one.colourForDb(-60.0f), flat);
    EXPECT_EQ(one.colourForDb(3.0f), flat);
    const auto oneBands = collectBands(one, -60.0f, 3.0f);
    ASSERT_EQ(oneBands.size(), 1u);
    EXPECT_EQ(oneBands[0].colour, flat);

    // 2 stops.
    const juce::Colour lowC(0xff000001), highC(0xff000002);
    const MeterColourStops two({{-60.0f, lowC}, {-6.0f, highC}});
    ASSERT_EQ(two.getStops().size(), 2u);
    EXPECT_EQ(two.colourForDb(-30.0f), lowC);
    EXPECT_EQ(two.colourForDb(0.0f), highC);
    const auto twoBands = collectBands(two, -60.0f, 3.0f);
    ASSERT_EQ(twoBands.size(), 2u);

    // 6 stops -- an arbitrary, finer-grained model (the Settings > Appearance follow-up's whole
    // point: any count must work, not just the built-in four).
    std::vector<MeterColourStop> six;
    for (int i = 0; i < 6; ++i)
        six.push_back({-60.0f + (float)i * 10.0f, juce::Colour((juce::uint8)i, (juce::uint8)i, (juce::uint8)i)});
    const MeterColourStops sixStops(six);
    ASSERT_EQ(sixStops.getStops().size(), 6u);
    const auto sixBands = collectBands(sixStops, -60.0f, 3.0f);
    ASSERT_EQ(sixBands.size(), 6u);
    for (size_t i = 0; i < sixBands.size(); ++i)
        EXPECT_EQ(sixBands[i].colour, six[i].colour) << "band " << i;
}
