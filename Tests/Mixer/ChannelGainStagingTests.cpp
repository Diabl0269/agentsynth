// Gain staging of the channels an AI build adds (Source/Mixer/ChannelGainStaging): where the pass measures and which
// faders it moves. The rendered and app-path tests are in ChannelFlow/ChannelFlowTimelineOpsHostTests.cpp.
#include "Mixer/ChannelGainStaging/ChannelGainStaging.h"
#include "Modules/ChannelStripModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace gs = synth::gainstaging;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

synth::MidiNote noteAt(double start, double length, int velocity = 100) {
    synth::MidiNote note;
    note.startBeat = start;
    note.lengthBeats = length;
    note.velocity = velocity;
    return note;
}

void addClip(synth::TimelineDoc& doc, synth::TrackId track, double start, double length,
             const std::vector<synth::MidiNote>& notes) {
    const auto clip = doc.addClip(track, start, length, "c");
    for (const auto& note : notes)
        doc.addNote(clip, note);
}

} // namespace

TEST(ChannelGainStagingPlan, EachTrackIsMeasuredWhereItPlaysMostAndTheRenderStartsEarlyEnoughForHeldNotes) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    const auto drums = doc.addTrack(synth::TrackKind::Midi, "Drums");
    addClip(doc, pad, 0.0, 64.0, {noteAt(0.0, 2.0, 60), noteAt(20.0, 8.0, 120), noteAt(20.0, 8.0, 120)});
    addClip(doc, drums, 0.0, 64.0, {noteAt(40.0, 0.25), noteAt(41.0, 0.25), noteAt(42.0, 0.25), noteAt(43.0, 0.25)});

    gs::WindowOptions options;
    options.windowsPerTrack = 1;
    options.maxTotalBeats = 16.0; // shorter than the song, so only the busiest stretches are rendered
    const auto windows = gs::planWindows(doc, options);
    ASSERT_EQ(windows.size(), 2u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 20.0);
    EXPECT_DOUBLE_EQ(windows[0].endBeat, 24.0);
    EXPECT_DOUBLE_EQ(windows[1].startBeat, 40.0);
    EXPECT_DOUBLE_EQ(windows[1].endBeat, 44.0);
}

TEST(ChannelGainStagingPlan, ANoteStartedBeforeTheStretchIsRenderedFromItsStart) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    addClip(doc, pad, 0.0, 64.0, {noteAt(10.0, 12.0)});
    gs::WindowOptions options;
    options.windowsPerTrack = 1;
    options.maxTotalBeats = 8.0;
    const auto windows = gs::planWindows(doc, options);
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 10.0) << "the held note is heard from its onset";
}

TEST(ChannelGainStagingPlan, AnArrangementThatFitsTheCapIsRenderedWholeFromBeatZeroToTheLastNoteEnd) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    const auto bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
    addClip(doc, pad, 8.0, 64.0, {noteAt(0.0, 2.0)});
    addClip(doc, bass, 0.0, 200.0, {noteAt(100.0, 4.0)});
    const auto windows = gs::planWindows(doc);
    ASSERT_EQ(windows.size(), 1u);
    EXPECT_DOUBLE_EQ(windows[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(windows[0].endBeat, 104.0);
}

TEST(ChannelGainStagingPlan, AnArrangementOverTheCapFallsBackToTheBusiestStretches) {
    synth::TimelineDoc doc;
    const auto pad = doc.addTrack(synth::TrackKind::Midi, "Pad");
    addClip(doc, pad, 0.0, 800.0, {noteAt(0.0, 1.0), noteAt(100.0, 1.0, 120), noteAt(700.0, 1.0, 90)});
    const auto windows = gs::planWindows(doc);
    double total = 0.0;
    for (const auto& window : windows)
        total += window.endBeat - window.startBeat;
    EXPECT_LT(total, 100.0) << "stretches, not the 701-beat song";
    ASSERT_GE(windows.size(), 3u) << "the default keeps up to four stretches per track";
}

TEST(ChannelGainStagingPlan, NoNotesMeansNothingToRenderAndAMutedTrackIsSkipped) {
    synth::TimelineDoc doc;
    EXPECT_TRUE(gs::planWindows(doc).empty());
    const auto bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
    addClip(doc, bass, 0.0, 8.0, {noteAt(0.0, 1.0)});
    doc.setTrackMuted(bass, true);
    EXPECT_TRUE(gs::planWindows(doc).empty());
}

TEST(ChannelGainStagingPlan, TheRenderIsCappedAndTheBusiestStretchOfEveryTrackIsKept) {
    synth::TimelineDoc doc;
    gs::WindowOptions options;
    options.windowsPerTrack = 2;
    options.maxTotalBeats = 96.0;
    for (int t = 0; t < 12; ++t) {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(t));
        addClip(doc, track, 0.0, 400.0, {noteAt(30.0 * t, 1.0, 120), noteAt(30.0 * t + 200.0, 1.0, 90)});
    }
    const auto windows = gs::planWindows(doc, options);
    double total = 0.0;
    for (const auto& window : windows)
        total += window.endBeat - window.startBeat;
    EXPECT_LE(total, 96.0);
    for (int t = 0; t < 12; ++t) {
        bool covered = false;
        for (const auto& window : windows)
            covered = covered || (window.startBeat <= 30.0 * t && window.endBeat >= 30.0 * t + 1.0);
        EXPECT_TRUE(covered) << "track " << t << " keeps its busiest stretch";
    }
}

TEST(ChannelGainStagingTrims, OnlyAChannelOverTheTargetMovesAndOnlyDownByTheExcess) {
    const NodeID hot{1}, quiet{2}, silent{3}, other{4};
    const std::vector<gs::ChannelPeak> peaks{{hot, 5.4}, {quiet, -11.0}, {silent, -kInf}, {other, 2.0}};
    const auto trims = gs::planTrims(peaks, {hot, quiet, silent}, [](NodeID) { return 0.0; });
    ASSERT_EQ(trims.size(), 1u) << "quiet and silent stay; `other` is not a new channel";
    EXPECT_EQ(trims[0].strip, hot);
    EXPECT_NEAR(trims[0].toDb, -8.9, 0.11) << "5.4 down to the target less the margin";
    EXPECT_LE(5.4 + trims[0].toDb, gs::kTargetPeakDb - gs::kTrimMarginDb) << "rounded down, never short of it";
}

TEST(ChannelGainStagingTrims, TheFaderIsReadFromWhereItIsAndStopsAtItsFloor) {
    const NodeID a{1}, b{2};
    const auto trims = gs::planTrims({{a, 0.0}, {b, 60.0}}, {a, b}, [&](NodeID id) { return id == a ? -2.0 : 0.0; });
    ASSERT_EQ(trims.size(), 2u);
    EXPECT_NEAR(trims[0].toDb, -5.5, 0.11);
    EXPECT_DOUBLE_EQ(trims[1].toDb, ChannelStripModule::kMinGainDb);
}
