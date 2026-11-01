// A lane's modulator rows each have their own height (AutomationLane::modulatorHeightScales, by modulatorOrder key, and
// modulatorHeightScale for a row with no entry): the setters, the save and load (absent fields keep older files byte
// for byte, and a file written while the rows scaled with their lane loads looking the same), and the carries.

#include "TimelineDocTestHelpers.h"

namespace {
struct RowHeightFixture : TimelineDocTest {
    TrackId track;
    LaneId a, b;

    void SetUp() override {
        TimelineDocTest::SetUp();
        track = doc.addTrack(TrackKind::Midi, "T");
        a = doc.addLane(track, "u-a", "a", makeRange(0.0f, 1.0f, 0.0f));
        b = doc.addLane(track, "u-b", "b", makeRange(0.0f, 1.0f, 0.0f));
    }

    juce::DynamicObject* savedLane(juce::var& saved, int index) {
        return saved.getProperty("tracks", {})[0].getProperty("lanes", {})[index].getDynamicObject();
    }
};
} // namespace

TEST_F(RowHeightFixture, ARowWithNoEntryUsesTheLanesDefaultAndOneRowChangesAlone) {
    const auto& lane = *doc.getLane(a);
    EXPECT_EQ(lane.modulatorRowScale("atten-1"), 1.0);
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "atten-1", 2.0));
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-1"), 2.0);
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-2"), 1.0) << "the other rows keep their size";
    EXPECT_EQ(doc.getLane(a)->heightScale, 1.0) << "the lane's own row keeps its size";
}

TEST_F(RowHeightFixture, TheSetterClampsRefusesBadInputAndIsOneMutationOnlyOnAChange) {
    const auto revision = doc.getRevision();
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "k", 99.0));
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("k"), synth::AutomationLane::kMaxHeightScale);
    EXPECT_EQ(doc.getRevision(), revision + 1);
    EXPECT_TRUE(doc.setModulatorHeightScale(a, "k", 50.0)) << "already at the clamp: true, nothing written";
    EXPECT_EQ(doc.getRevision(), revision + 1);
    EXPECT_FALSE(doc.setModulatorHeightScale(a, "", 2.0));
    EXPECT_FALSE(doc.setModulatorHeightScale(a, "k", std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(doc.setModulatorHeightScale(LaneId{999}, "k", 2.0));
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "k", 0.01));
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("k"), synth::AutomationLane::kMinHeightScale);
}

TEST_F(RowHeightFixture, AtTheDefaultsNothingNewIsWritten) {
    auto plain = doc.toVar();
    auto* lane = savedLane(plain, 0);
    ASSERT_NE(lane, nullptr);
    EXPECT_FALSE(lane->hasProperty("modulatorHeightScale"));
    EXPECT_FALSE(lane->hasProperty("modulatorHeightScales"));

    ASSERT_TRUE(doc.setLaneHeightScale(a, 2.0));
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "atten-1", 1.5));
    auto saved = doc.toVar();
    EXPECT_TRUE(savedLane(saved, 0)->hasProperty("modulatorHeightScale"))
        << "the rows' default differs from the lane's own row, so it is written";
    EXPECT_FALSE(savedLane(saved, 1)->hasProperty("modulatorHeightScales")) << "per lane";
}

TEST_F(RowHeightFixture, EveryRowsHeightSurvivesASaveAndALoad) {
    ASSERT_TRUE(doc.setLaneHeightScale(a, 2.0));
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "atten-1", 1.5));
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "lfo-1/0//2", 0.75));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(doc.toVar()));
    const auto& lane = *loaded.getLane(a);
    EXPECT_EQ(lane.heightScale, 2.0);
    EXPECT_EQ(lane.modulatorRowScale("atten-1"), 1.5);
    EXPECT_EQ(lane.modulatorRowScale("lfo-1/0//2"), 0.75);
    EXPECT_EQ(lane.modulatorRowScale("atten-other"), 1.0) << "a row with no entry keeps the default, not the lane's";
}

TEST_F(RowHeightFixture, AFileFromBeforeRowHeightsLoadsItsModulatorRowsAtTheLanesOldScale) {
    ASSERT_TRUE(doc.setLaneHeightScale(a, 2.0));
    auto saved = doc.toVar();
    auto* lane = savedLane(saved, 0);
    lane->removeProperty("modulatorHeightScale"); // what a file written before per-row heights holds
    ASSERT_FALSE(lane->hasProperty("modulatorHeightScales"));
    TimelineDoc loaded;
    ASSERT_TRUE(loaded.fromVar(saved));
    EXPECT_EQ(loaded.getLane(a)->heightScale, 2.0);
    EXPECT_EQ(loaded.getLane(a)->modulatorRowScale("atten-1"), 2.0) << "the rows scaled with their lane then";
    EXPECT_EQ(loaded.getLane(b)->modulatorRowScale("atten-1"), 1.0);

    TimelineDoc again; // and it saves and reloads the same
    ASSERT_TRUE(again.fromVar(loaded.toVar()));
    EXPECT_EQ(again.getLane(a)->modulatorRowScale("atten-1"), 2.0);
}

TEST_F(RowHeightFixture, AMalformedRowHeightIsRejectedOnLoad) {
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "k", 2.0));
    for (const auto& bad : {juce::var("not an array"), juce::var(juce::Array<juce::var>{juce::var(3)})}) {
        auto saved = doc.toVar();
        savedLane(saved, 0)->setProperty("modulatorHeightScales", bad);
        TimelineDoc loaded;
        EXPECT_FALSE(loaded.fromVar(saved));
    }
}

TEST_F(RowHeightFixture, RekeyingMovesAHeightToTheNewRoutingAndADuplicateCopiesThem) {
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "atten-1", 2.0));
    ASSERT_TRUE(doc.setModulatorHeightScale(a, "atten-2", 0.5));
    ASSERT_TRUE(doc.rekeyModulatorHeights(a, {{"atten-1", "atten-9"}}));
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-9"), 2.0);
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-1"), 1.0) << "the old key is gone";
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-2"), 0.5) << "keys not carried stay";
    EXPECT_FALSE(doc.rekeyModulatorHeights(a, {{"nothing", "else"}}));

    const auto copy = doc.duplicateLane(a, "u-d", "d", makeRange(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(doc.getLane(copy)->modulatorHeightScales, doc.getLane(a)->modulatorHeightScales);
    ASSERT_TRUE(doc.retargetLane(a, "u-e", "e", makeRange(0.0f, 2.0f, 0.0f)));
    EXPECT_EQ(doc.getLane(a)->modulatorRowScale("atten-9"), 2.0) << "a retarget keeps them";
}
