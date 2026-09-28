// TimelineDoc controller-lane tests: batched velocity edits, per-clip CC lanes (mutators, structural
// edits, serialisation). See docs/timeline/piano-roll-lanes.md.

#include "TimelineDocTestHelpers.h"
#include <functional>

using synth::ClipControllerLane;
using synth::ControllerPoint;

namespace {

ControllerPoint cp(double beat, double value, BreakpointCurve curve = BreakpointCurve::Linear) {
    return {beat, value, static_cast<int>(curve)};
}

struct ClipFixture {
    TimelineDoc doc;
    TrackId track;
    ClipId clip;
    ClipFixture() {
        track = doc.addTrack(TrackKind::Midi, "T");
        clip = doc.addClip(track, 4.0, 8.0, "c");
    }
};

} // namespace

TEST(TimelineDocControllersTest, SetNoteVelocitiesIsOneMutation) {
    ClipFixture f;
    const auto a = f.doc.addNote(f.clip, makeNote(0.0, 60));
    const auto b = f.doc.addNote(f.clip, makeNote(1.0, 62));
    const auto c = f.doc.addNote(f.clip, makeNote(2.0, 64));
    CountingListener listener;
    f.doc.addListener(&listener);
    const auto revision = f.doc.getRevision();

    EXPECT_TRUE(f.doc.setNoteVelocities({{a, 10}, {b, 20}, {c, 30}}));
    EXPECT_EQ(listener.calls, 1);
    EXPECT_EQ(f.doc.getRevision(), revision + 1);
    EXPECT_EQ(f.doc.getNote(a)->velocity, 10);
    EXPECT_EQ(f.doc.getNote(b)->velocity, 20);
    EXPECT_EQ(f.doc.getNote(c)->velocity, 30);

    // Same values again: a no-op. A repeated id resolves last-one-wins before the no-op check.
    EXPECT_TRUE(f.doc.setNoteVelocities({{a, 10}, {b, 99}, {b, 20}}));
    EXPECT_EQ(listener.calls, 1);
    f.doc.removeListener(&listener);
}

TEST(TimelineDocControllersTest, SetNoteVelocitiesRejectsWholeBatch) {
    ClipFixture f;
    const auto a = f.doc.addNote(f.clip, makeNote(0.0, 60));
    const auto revision = f.doc.getRevision();
    EXPECT_FALSE(f.doc.setNoteVelocities({{a, 50}, {a, 0}}));
    EXPECT_FALSE(f.doc.setNoteVelocities({{a, 50}, {a, 128}}));
    EXPECT_FALSE(f.doc.setNoteVelocities({{a, 50}, {NoteId{999}, 60}}));
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_EQ(f.doc.getNote(a)->velocity, 100);
}

TEST(TimelineDocControllersTest, AddAndRemoveControllerLane) {
    ClipFixture f;
    EXPECT_FALSE(f.doc.addControllerLane(f.clip, -1));
    EXPECT_FALSE(f.doc.addControllerLane(f.clip, 128));
    EXPECT_FALSE(f.doc.addControllerLane(ClipId{999}, 1));

    EXPECT_TRUE(f.doc.addControllerLane(f.clip, 64));
    EXPECT_TRUE(f.doc.addControllerLane(f.clip, 1));
    const auto revision = f.doc.getRevision();
    EXPECT_TRUE(f.doc.addControllerLane(f.clip, 1)); // already there: no-op
    EXPECT_EQ(f.doc.getRevision(), revision);

    const auto& lanes = f.doc.getClip(f.clip)->controllers;
    ASSERT_EQ(lanes.size(), 2u);
    EXPECT_EQ(lanes[0].ccNumber, 1) << "lanes stay sorted by CC number";
    EXPECT_EQ(lanes[1].ccNumber, 64);

    EXPECT_TRUE(f.doc.removeControllerLane(f.clip, 1));
    EXPECT_FALSE(f.doc.removeControllerLane(f.clip, 1));
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 1), nullptr);
    EXPECT_NE(f.doc.getControllerLane(f.clip, 64), nullptr);
}

TEST(TimelineDocControllersTest, SetPointsCreatesLaneSortsDedupesAndClamps) {
    ClipFixture f;
    CountingListener listener;
    f.doc.addListener(&listener);

    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 11, {cp(2.0, 200.0), cp(0.0, -5.0), cp(2.0, 90.0)}));
    EXPECT_EQ(listener.calls, 1) << "creating the lane and writing its points is ONE mutation";
    const auto* lane = f.doc.getControllerLane(f.clip, 11);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 2u);
    EXPECT_DOUBLE_EQ(lane->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(lane->points[0].value, 0.0) << "clamped into 0..127";
    EXPECT_DOUBLE_EQ(lane->points[1].beat, 2.0);
    EXPECT_DOUBLE_EQ(lane->points[1].value, 90.0) << "same beat: the last one wins";

    // Identical list: no-op.
    EXPECT_TRUE(f.doc.setControllerLanePoints(f.clip, 11, {cp(0.0, 0.0), cp(2.0, 90.0)}));
    EXPECT_EQ(listener.calls, 1);
    // Empty list on a missing lane: no-op, no lane created.
    EXPECT_TRUE(f.doc.setControllerLanePoints(f.clip, 2, {}));
    EXPECT_EQ(f.doc.getControllerLane(f.clip, 2), nullptr);
    // Empty list on an existing lane clears it but keeps the lane.
    EXPECT_TRUE(f.doc.setControllerLanePoints(f.clip, 11, {}));
    ASSERT_NE(f.doc.getControllerLane(f.clip, 11), nullptr);
    EXPECT_TRUE(f.doc.getControllerLane(f.clip, 11)->points.empty());
    f.doc.removeListener(&listener);
}

TEST(TimelineDocControllersTest, SetPointsRejectsBadInputWithoutMutating) {
    ClipFixture f;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto revision = f.doc.getRevision();
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 128, {cp(0.0, 1.0)}));
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 1, {cp(-1.0, 1.0)}));
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 1, {cp(nan, 1.0)}));
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 1, {cp(0.0, nan)}));
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 1, {cp(0.0, 1.0, BreakpointCurve::Bezier)}))
        << "the reserved Bezier curve has no CC meaning";
    std::vector<ControllerPoint> tooMany((size_t)TimelineDoc::kMaxControllerPointsPerLane + 1, cp(0.0, 1.0));
    EXPECT_FALSE(f.doc.setControllerLanePoints(f.clip, 1, tooMany));
    EXPECT_EQ(f.doc.getRevision(), revision);
    EXPECT_TRUE(f.doc.getClip(f.clip)->controllers.empty());
}

TEST(TimelineDocControllersTest, SplitKeepsBothHalvesPlayingTheSameCurve) {
    ClipFixture f; // clip at beat 4, 8 beats long
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {cp(0.0, 0.0), cp(4.0, 100.0), cp(6.0, 20.0)}));
    const auto [left, right] = f.doc.splitClip(f.clip, 2.0);
    ASSERT_TRUE(right.isValid());

    const auto* l = f.doc.getControllerLane(left, 1);
    const auto* r = f.doc.getControllerLane(right, 1);
    ASSERT_NE(l, nullptr);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(l->points.size(), 2u);
    EXPECT_DOUBLE_EQ(l->points[1].beat, 2.0);
    EXPECT_DOUBLE_EQ(l->points[1].value, 50.0) << "boundary point carries the mid-ramp value";
    ASSERT_EQ(r->points.size(), 3u);
    EXPECT_DOUBLE_EQ(r->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(r->points[0].value, 50.0);
    EXPECT_DOUBLE_EQ(r->points[1].beat, 2.0) << "re-based onto the right clip's start";
    EXPECT_DOUBLE_EQ(r->points[2].beat, 4.0);
}

TEST(TimelineDocControllersTest, JoinMergesAndDuplicateCopiesLanes) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 0.0, 4.0, "a");
    const auto b = doc.addClip(track, 4.0, 4.0, "b");
    ASSERT_TRUE(doc.setControllerLanePoints(a, 1, {cp(0.0, 10.0)}));
    ASSERT_TRUE(doc.setControllerLanePoints(b, 1, {cp(1.0, 20.0)}));
    ASSERT_TRUE(doc.setControllerLanePoints(b, 64, {cp(0.0, 127.0, BreakpointCurve::Hold)}));

    const auto dup = doc.duplicateClip(b);
    ASSERT_TRUE(dup.isValid());
    ASSERT_NE(doc.getControllerLane(dup, 64), nullptr);
    EXPECT_EQ(doc.getControllerLane(dup, 64)->points[0].curve, static_cast<int>(BreakpointCurve::Hold));

    ASSERT_TRUE(doc.joinClips(a, b));
    const auto* mod = doc.getControllerLane(a, 1);
    ASSERT_NE(mod, nullptr);
    ASSERT_EQ(mod->points.size(), 2u);
    EXPECT_DOUBLE_EQ(mod->points[1].beat, 5.0) << "b's point re-based by b.start - a.start";
    ASSERT_NE(doc.getControllerLane(a, 64), nullptr) << "a CC only b had becomes a lane of the joined clip";
    EXPECT_DOUBLE_EQ(doc.getControllerLane(a, 64)->points[0].beat, 4.0);
}

TEST(TimelineDocControllersTest, ControllersRoundTripThroughToVarFromVar) {
    ClipFixture f;
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {cp(0.0, 0.0), cp(3.5, 127.0)}));
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 64, {cp(1.0, 127.0, BreakpointCurve::Hold)}));
    ASSERT_TRUE(f.doc.addControllerLane(f.clip, 11)); // empty lane survives too

    TimelineDoc copy;
    ASSERT_TRUE(copy.fromVar(f.doc.toVar()));
    EXPECT_EQ(dump(copy), dump(f.doc));
    const auto& lanes = copy.getClip(f.clip)->controllers;
    ASSERT_EQ(lanes.size(), 3u);
    EXPECT_EQ(lanes[2].ccNumber, 64);
    EXPECT_EQ(lanes[2].points[0].curve, static_cast<int>(BreakpointCurve::Hold));
}

TEST(TimelineDocControllersTest, AbsentControllersLoadEmptyAndBadOnesAreRefused) {
    ClipFixture f;
    ASSERT_TRUE(f.doc.setControllerLanePoints(f.clip, 1, {cp(0.0, 64.0)}));

    auto withClip = [&](const std::function<void(juce::DynamicObject&)>& edit) {
        auto state = juce::JSON::parse(juce::JSON::toString(f.doc.toVar()));
        auto* clipObj = state["tracks"][0]["clips"][0].getDynamicObject();
        edit(*clipObj);
        TimelineDoc loaded;
        return loaded.fromVar(state) ? loaded.getClip(f.clip)->controllers.size() : (size_t)999;
    };

    EXPECT_EQ(withClip([](juce::DynamicObject& c) { c.removeProperty("controllers"); }), 0u)
        << "a file written before CC lanes existed loads with none";
    EXPECT_EQ(withClip([](juce::DynamicObject&) {}), 1u);
    EXPECT_EQ(withClip([](juce::DynamicObject& c) { c.setProperty("controllers", "nope"); }), 999u);
    EXPECT_EQ(withClip([](juce::DynamicObject& c) {
                  c.getProperty("controllers")[0].getDynamicObject()->setProperty("ccNumber", 200);
              }),
              999u);
    EXPECT_EQ(withClip([](juce::DynamicObject& c) {
                  auto lane = c.getProperty("controllers")[0];
                  c.getProperty("controllers").getArray()->add(lane); // duplicate CC 1
              }),
              999u);
    EXPECT_EQ(withClip([](juce::DynamicObject& c) {
                  c.getProperty("controllers")[0]["points"][0].getDynamicObject()->setProperty("curve", 2);
              }),
              999u);
    EXPECT_EQ(withClip([](juce::DynamicObject& c) {
                  c.getProperty("controllers")[0]["points"][0].getDynamicObject()->setProperty("beat", -2.0);
              }),
              999u);
}

TEST(TimelineDocControllersTest, SplittingAFullLaneKeepsBothHalvesWithinTheCapAndLoadable) {
    // Review regression: the boundary point used to push a full lane's half to cap + 1, and the
    // saved project then failed to load.
    for (const bool allLeft : {true, false}) {
        TimelineDoc doc;
        const auto track = doc.addTrack(TrackKind::Midi, "T");
        const auto clip = doc.addClip(track, 0.0, 64.0, "c");
        std::vector<ControllerPoint> full;
        const double base = allLeft ? 0.0 : 33.0;
        for (int i = 0; i < TimelineDoc::kMaxControllerPointsPerLane; ++i)
            full.push_back(cp(base + i * (30.0 / TimelineDoc::kMaxControllerPointsPerLane), (double)(i % 128)));
        ASSERT_TRUE(doc.setControllerLanePoints(clip, 1, full));
        const auto [left, right] = doc.splitClip(clip, 32.0);
        ASSERT_TRUE(right.isValid());
        EXPECT_LE((int)doc.getControllerLane(left, 1)->points.size(), TimelineDoc::kMaxControllerPointsPerLane);
        EXPECT_LE((int)doc.getControllerLane(right, 1)->points.size(), TimelineDoc::kMaxControllerPointsPerLane);
        TimelineDoc reloaded;
        EXPECT_TRUE(reloaded.fromVar(doc.toVar())) << (allLeft ? "all points left of the cut" : "all right");
    }
}
