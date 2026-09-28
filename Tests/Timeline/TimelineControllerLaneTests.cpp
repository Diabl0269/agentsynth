// Clip CC lanes across the two gates downstream of the doc: validateTimeline (the untrusted door)
// and TimelineSnapshot::buildFrom (the audio thread's flattened view). See
// docs/timeline/piano-roll-lanes.md.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Timeline/TimelineValidator.h"
#include <functional>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using synth::BreakpointCurve;
using synth::ControllerPoint;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TimelineValidationError;
using synth::TrackKind;

namespace {

ControllerPoint cp(double beat, double value, int curve = 1) { return {beat, value, curve}; }

synth::MidiNote noteOn(double start, int channel) {
    synth::MidiNote note;
    note.startBeat = start;
    note.channel = channel;
    return note;
}

struct CcDoc {
    TimelineDoc doc;
    synth::TrackId track;
    synth::ClipId clip;
    CcDoc() {
        track = doc.addTrack(TrackKind::Midi, "Lead");
        clip = doc.addClip(track, 2.0, 4.0, "Verse");
        doc.addNote(clip, noteOn(0.0, 1));
        doc.setControllerLanePoints(clip, 1, {cp(0.0, 0.0), cp(2.0, 127.0)});
    }
};

// A fresh deep copy of the doc's var, one clip-level object to break.
juce::var freshVar(const TimelineDoc& doc) { return juce::JSON::parse(juce::JSON::toString(doc.toVar())); }
juce::DynamicObject& clipOf(juce::var& root) { return *root["tracks"][0]["clips"][0].getDynamicObject(); }
juce::DynamicObject& laneOf(juce::var& root) { return *clipOf(root).getProperty("controllers")[0].getDynamicObject(); }
juce::DynamicObject& pointOf(juce::var& root) { return *laneOf(root).getProperty("points")[0].getDynamicObject(); }

} // namespace

// ---- validateTimeline --------------------------------------------------------------------------

TEST(TimelineControllerLaneValidatorTest, AcceptsAWellFormedCcLane) {
    CcDoc f;
    juce::AudioProcessorGraph graph;
    const auto result = synth::validateTimeline(freshVar(f.doc), graph);
    EXPECT_TRUE(result.ok) << result.message;
}

TEST(TimelineControllerLaneValidatorTest, RejectsEachBadShapeWithTheRightCode) {
    struct Case {
        TimelineValidationError expected;
        const char* what;
        std::function<void(juce::var&)> breakIt;
    };
    const std::vector<Case> cases = {
        {TimelineValidationError::ControllerOutOfRange, "ccNumber 128",
         [](juce::var& r) { laneOf(r).setProperty("ccNumber", 128); }},
        {TimelineValidationError::ControllerOutOfRange, "ccNumber -1",
         [](juce::var& r) { laneOf(r).setProperty("ccNumber", -1); }},
        {TimelineValidationError::MalformedRoot, "ccNumber missing",
         [](juce::var& r) { laneOf(r).removeProperty("ccNumber"); }},
        {TimelineValidationError::MalformedRoot, "duplicate CC lane",
         [](juce::var& r) {
             auto lane = clipOf(r).getProperty("controllers")[0];
             clipOf(r).getProperty("controllers").getArray()->add(juce::JSON::parse(juce::JSON::toString(lane)));
         }},
        {TimelineValidationError::MalformedRoot, "unknown lane key",
         [](juce::var& r) { laneOf(r).setProperty("channel", 3); }},
        {TimelineValidationError::MalformedRoot, "unknown point key",
         [](juce::var& r) { pointOf(r).setProperty("tension", 0.5); }},
        {TimelineValidationError::ControllerOutOfRange, "value above 127",
         [](juce::var& r) { pointOf(r).setProperty("value", 128.0); }},
        {TimelineValidationError::ControllerOutOfRange, "value below 0",
         [](juce::var& r) { pointOf(r).setProperty("value", -0.5); }},
        {TimelineValidationError::MalformedRoot, "reserved Bezier curve",
         [](juce::var& r) { pointOf(r).setProperty("curve", 2); }},
        {TimelineValidationError::BeatOutOfBounds, "negative beat",
         [](juce::var& r) { pointOf(r).setProperty("beat", -1.0); }},
        {TimelineValidationError::BeatOutOfBounds, "beat past kMaxPpqUntrusted",
         [](juce::var& r) { pointOf(r).setProperty("beat", synth::kMaxPpqUntrusted * 2.0); }},
        {TimelineValidationError::MalformedRoot, "value is a string",
         [](juce::var& r) { pointOf(r).setProperty("value", "loud"); }},
        {TimelineValidationError::MalformedRoot, "controllers is not an array",
         [](juce::var& r) { clipOf(r).setProperty("controllers", 7); }},
        {TimelineValidationError::TooManyBreakpoints, "one point over the per-lane cap",
         [](juce::var& r) {
             auto* points = laneOf(r).getProperty("points").getArray();
             const auto first = points->getFirst();
             while (points->size() <= TimelineDoc::kMaxControllerPointsPerLane)
                 points->add(first);
         }},
    };

    juce::AudioProcessorGraph graph;
    for (const auto& c : cases) {
        CcDoc f;
        auto root = freshVar(f.doc);
        c.breakIt(root);
        const auto result = synth::validateTimeline(root, graph);
        EXPECT_FALSE(result.ok) << c.what;
        EXPECT_EQ(result.error, c.expected) << c.what << ": " << result.message;
    }
}

TEST(TimelineControllerLaneValidatorTest, ErrorNameIsStable) {
    EXPECT_EQ(synth::timelineValidationErrorName(TimelineValidationError::ControllerOutOfRange),
              juce::String("ControllerOutOfRange"));
}

// ---- TimelineSnapshot ----------------------------------------------------------------------------

TEST(TimelineControllerLaneSnapshotTest, FlattensToAbsoluteBeatsWithTheClipWindow) {
    CcDoc f;
    f.doc.addNote(f.clip, noteOn(1.0, 3)); // channels 1 and 3
    const auto snapshot = TimelineSnapshot::buildFrom(f.doc);
    ASSERT_TRUE(snapshot->selfCheck());
    ASSERT_EQ(snapshot->tracks.size(), 1u);
    const auto& track = snapshot->tracks[0];
    ASSERT_EQ(track.numControllers, 1);
    const auto& lane = snapshot->controllers[(size_t)track.firstController];
    EXPECT_EQ(lane.ccNumber, 1);
    EXPECT_EQ(lane.channelMask, (1u << 0) | (1u << 2));
    EXPECT_DOUBLE_EQ(lane.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(lane.endBeat, 6.0);
    ASSERT_EQ(lane.numPoints, 2);
    EXPECT_DOUBLE_EQ(snapshot->points[(size_t)lane.firstPoint].beat, 2.0) << "clip-relative 0 -> absolute 2";
    EXPECT_DOUBLE_EQ(snapshot->points[(size_t)lane.firstPoint + 1].beat, 4.0);
}

TEST(TimelineControllerLaneSnapshotTest, MutedClipEmptyLaneAndAudioTrackContributeNothing) {
    CcDoc f;
    f.doc.addControllerLane(f.clip, 11); // no points: nothing to send
    auto snapshot = TimelineSnapshot::buildFrom(f.doc);
    EXPECT_EQ(snapshot->tracks[0].numControllers, 1);

    f.doc.setClipMuted(f.clip, true);
    snapshot = TimelineSnapshot::buildFrom(f.doc);
    EXPECT_EQ(snapshot->tracks[0].numControllers, 0) << "a muted clip contributes no CC";
    EXPECT_TRUE(snapshot->controllers.empty());

    TimelineDoc audioDoc;
    const auto audioTrack = audioDoc.addTrack(TrackKind::Audio, "Vox");
    const auto clip = audioDoc.addClip(audioTrack, 0.0, 4.0, "take");
    audioDoc.setControllerLanePoints(clip, 1, {cp(0.0, 64.0)});
    snapshot = TimelineSnapshot::buildFrom(audioDoc);
    EXPECT_TRUE(snapshot->controllers.empty()) << "an audio track's CC lanes are inert";
}

TEST(TimelineControllerLaneSnapshotTest, ClipWithoutNotesPlaysOnChannelOne) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto clip = doc.addClip(track, 0.0, 4.0, "c");
    doc.setControllerLanePoints(clip, 7, {cp(0.0, 100.0, 0)});
    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_EQ(snapshot->controllers.size(), 1u);
    EXPECT_EQ(snapshot->controllers[0].channelMask, 1u);
    EXPECT_EQ(snapshot->points[(size_t)snapshot->controllers[0].firstPoint].curve,
              static_cast<int>(BreakpointCurve::Hold));
}
