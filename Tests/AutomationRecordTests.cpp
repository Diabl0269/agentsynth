// Automation record modes, gesture capture, the programmatic-write guard and RDP thinning.
//
// Most of this file needs no engine and no graph: a bare synth::TransportService is ticked exactly
// the way AudioEngine::renderNextBlock ticks it, and a standalone FilterModule supplies a real
// juce::RangedAudioParameter to gesture against — the same headless house style MidiRecorderTests
// uses for its sibling capture path. A gesture is simulated with the exact trio a JUCE slider
// attachment produces: beginChangeGesture / setValueNotifyingHost / endChangeGesture.
//
// The last few tests DO need a hosted AudioEngine, because they are about the applier and the
// recorder seeing each other correctly across the audio/message thread boundary.
//
// Timing arithmetic: 48 kHz, 120 BPM => 24000 samples per beat, so tick(24000) advances exactly one
// beat. TransportService publishes the position at the START of a block, so the ppq a gesture sees
// is the position of the tick that most recently ran.

#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Timeline/AutomationApplier.h"
#include "../Source/Timeline/AutomationRecorder.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineSnapshot.h"
#include "../Source/Transport/TransportService.h"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#endif

using synth::AutomationRecorder;
using synth::LaneRecordMode;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kSamplesPerBeat = 24000; // 120 BPM at 48 kHz — TransportService's default BPM

constexpr const char* kNodeUuid = "b0440000-0000-0000-0000-000000000001";

constexpr float kCutoffMin = 20.0f;
constexpr float kCutoffMax = 20000.0f;
constexpr float kCutoffDefault = 440.0f;

// 0.2% of the cutoff lane's 19980 Hz span. Anything a test wants THINNED must deviate by less than
// this from the chord it sits on; anything it wants KEPT must deviate by more.
constexpr double kEpsilon = (kCutoffMax - kCutoffMin) * AutomationRecorder::kThinningEpsilonFraction;

int mode(LaneRecordMode m) { return static_cast<int>(m); }

synth::AutomationLane::RangeSnapshot cutoffRange() {
    synth::AutomationLane::RangeSnapshot snapshot;
    snapshot.minValue = kCutoffMin;
    snapshot.maxValue = kCutoffMax;
    snapshot.defaultValue = kCutoffDefault;
    return snapshot;
}

// Bare transport + doc + undo + one real module parameter + the recorder under test.
//
// Declaration ORDER is load-bearing (members die in reverse): `doc` outlives `undo`, whose undo
// stack holds a reference to it, and `filter` outlives `recorder`, whose destructor calls
// removeListener on the filter's parameter.
struct Harness {
    synth::TransportService transport;
    TimelineDoc doc;
    AppUndoManager undo;
    FilterModule filter;
    AutomationRecorder recorder;

    synth::TrackId trackId;
    synth::LaneId laneId;

    Harness() {
        transport.prepare(kSampleRate, kBlock);
        recorder.attachTo(doc, undo, transport);
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        laneId = doc.addLane(trackId, kNodeUuid, "cutoff", cutoffRange());
        recorder.bindLane(laneId, cutoff(), {});
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(&filter, "cutoff"); }

    // Advances exactly `beats` and leaves the PUBLISHED position there. Two ticks, not one:
    // TransportService publishes the position at the START of a block and only then advances, so a
    // single tick would leave getPositionSnapshot() reporting the beat we just left. The zero-length
    // second tick republishes without moving — which is also what the next real callback would do.
    void advanceBeat(double beats = 1.0) {
        transport.tick(static_cast<int>(beats * kSamplesPerBeat));
        transport.tick(0);
    }

    // play() + one tick, so the published snapshot actually reads "playing" before anything asks.
    void startPlaying() {
        ASSERT_TRUE(transport.play());
        transport.tick(0); // drains the command and republishes without moving
        recorder.update();
    }

    void stopPlaying() {
        ASSERT_TRUE(transport.stop());
        transport.tick(0);
        recorder.update();
    }

    // The trio a JUCE slider attachment produces for a drag of a single value.
    void setCutoffHz(double hz) { cutoff()->setValueNotifyingHost(cutoff()->convertTo0to1(static_cast<float>(hz))); }

    double cutoffHz() { return static_cast<double>(cutoff()->convertFrom0to1(cutoff()->getValue())); }

    const synth::AutomationLane* lane() const { return doc.getLane(laneId); }

    void setMode(LaneRecordMode m) { ASSERT_TRUE(doc.setLaneRecordMode(laneId, mode(m))); }
};

std::vector<AutomationRecorder::CapturedPoint> makePoints(std::initializer_list<std::pair<double, double>> pairs) {
    std::vector<AutomationRecorder::CapturedPoint> points;
    for (const auto& [beat, value] : pairs)
        points.push_back({beat, value});
    return points;
}

} // namespace

// ============================================================================
// 1. Touch: a gesture is captured and committed as exactly one undo step
// ============================================================================

TEST(AutomationRecordTest, TouchGestureCapturesAndCommitsOneUndoStep) {
    Harness h;
    h.setMode(LaneRecordMode::Touch);

    // Prior content: one point inside the span the gesture will cover, one well outside it.
    ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, 2.0, 500.0));
    ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, 10.0, 600.0));

    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();

    const double values[] = {1000.0, 2000.0, 8000.0, 3000.0, 5000.0};

    h.cutoff()->beginChangeGesture();
    for (int i = 0; i < 5; ++i) {
        h.setCutoffHz(values[i]);
        if (i < 4)
            h.advanceBeat();
    }
    h.cutoff()->endChangeGesture();

    const auto* lane = h.lane();
    ASSERT_NE(lane, nullptr);

    // Five deliberately non-collinear values one beat apart: none of them is within kEpsilon of the
    // chord through its neighbours, so thinning keeps all five. The anchor the commit lays down at
    // the span start is replaced by the point captured at that same beat.
    ASSERT_EQ(lane->points.size(), 6u) << "five captured beats plus the untouched point at beat 10";
    for (int i = 0; i < 5; ++i) {
        EXPECT_NEAR(lane->points[static_cast<size_t>(i)].beat, static_cast<double>(i), 1e-9);
        EXPECT_NEAR(lane->points[static_cast<size_t>(i)].value, values[i], 1.0);
    }
    EXPECT_NEAR(lane->points[5].beat, 10.0, 1e-9) << "a point outside the span must survive untouched";
    EXPECT_NEAR(lane->points[5].value, 600.0, 1e-6);

    // Exactly one undo step, and undoing it restores the lane byte for byte.
    ASSERT_TRUE(h.undo.canUndo());
    ASSERT_TRUE(h.undo.undo());
    EXPECT_FALSE(h.undo.canUndo()) << "a gesture is ONE undo step, not one per captured value";

    const auto* restored = h.lane();
    ASSERT_NE(restored, nullptr);
    ASSERT_EQ(restored->points.size(), 2u);
    EXPECT_NEAR(restored->points[0].beat, 2.0, 1e-9);
    EXPECT_NEAR(restored->points[0].value, 500.0, 1e-6);
    EXPECT_NEAR(restored->points[1].beat, 10.0, 1e-9);
    EXPECT_NEAR(restored->points[1].value, 600.0, 1e-6);
}

// ============================================================================
// 2. The programmatic-write guard: no gesture, no capture
// ============================================================================

TEST(AutomationRecordTest, ProgrammaticWritesWithoutGestureAreIgnored) {
    Harness h;
    h.setMode(LaneRecordMode::Touch);
    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();

    // Exactly what a preset load / AI patch apply / undo restore does: setValueNotifyingHost with no
    // surrounding gesture. Armed, rolling, on a Touch lane — and still nothing may be captured.
    for (int i = 0; i < 8; ++i) {
        h.setCutoffHz(1000.0 + 500.0 * i);
        h.advanceBeat(0.25);
    }

    h.recorder.update();
    h.stopPlaying();

    ASSERT_NE(h.lane(), nullptr);
    EXPECT_TRUE(h.lane()->points.empty()) << "a value change with no gesture is not a performance";
    EXPECT_FALSE(h.undo.canUndo());
}

// ============================================================================
// 3. ScopedProgrammaticApply suppresses even a fully gestured write
// ============================================================================

TEST(AutomationRecordTest, ScopedProgrammaticApplySuppressesEvenGesturedWrites) {
    Harness h;
    h.setMode(LaneRecordMode::Touch);
    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();

    {
        AutomationRecorder::ScopedProgrammaticApply guard(h.recorder);
        EXPECT_TRUE(h.recorder.isSuspended());

        h.cutoff()->beginChangeGesture();
        h.setCutoffHz(3000.0);
        h.advanceBeat();
        h.setCutoffHz(9000.0);
        h.cutoff()->endChangeGesture();
    }

    EXPECT_FALSE(h.recorder.isSuspended());
    ASSERT_NE(h.lane(), nullptr);
    EXPECT_TRUE(h.lane()->points.empty()) << "a suspended recorder must ignore gestured writes too";
    EXPECT_FALSE(h.undo.canUndo());

    // And the suspension is not a latch: an identical gesture afterwards records normally.
    h.cutoff()->beginChangeGesture();
    h.setCutoffHz(3000.0);
    h.advanceBeat();
    h.setCutoffHz(9000.0);
    h.cutoff()->endChangeGesture();

    EXPECT_FALSE(h.lane()->points.empty());
    EXPECT_TRUE(h.undo.canUndo());
}

// ============================================================================
// 4. RDP thinning
// ============================================================================

TEST(AutomationRecordTest, RDPThinningReducesStraightLines) {
    std::vector<AutomationRecorder::CapturedPoint> line;
    for (int i = 0; i < 100; ++i)
        line.push_back({static_cast<double>(i), 100.0 + 10.0 * i});

    const auto thinnedLine = AutomationRecorder::thinPoints(line, kEpsilon);
    EXPECT_GE(thinnedLine.size(), 2u);
    EXPECT_LE(thinnedLine.size(), 3u) << "a straight ramp is two points, however densely it was sampled";
    EXPECT_NEAR(thinnedLine.front().value, 100.0, 1e-9);
    EXPECT_NEAR(thinnedLine.back().value, 100.0 + 10.0 * 99, 1e-9);
}

TEST(AutomationRecordTest, RDPThinningKeepsCornersExactly) {
    // A triangle: up to a peak at index 50, back down. Its two endpoints and its corner must all
    // survive, with their captured values untouched — a thinner that averaged or moved a kept point
    // would round the corner off.
    std::vector<AutomationRecorder::CapturedPoint> triangle;
    for (int i = 0; i <= 50; ++i)
        triangle.push_back({static_cast<double>(i), 1000.0 + 100.0 * i});
    for (int i = 51; i <= 100; ++i)
        triangle.push_back({static_cast<double>(i), 6000.0 - 100.0 * (i - 50)});

    const auto thinned = AutomationRecorder::thinPoints(triangle, kEpsilon);
    ASSERT_EQ(thinned.size(), 3u);
    EXPECT_NEAR(thinned[0].beat, 0.0, 1e-9);
    EXPECT_NEAR(thinned[0].value, 1000.0, 1e-9);
    EXPECT_NEAR(thinned[1].beat, 50.0, 1e-9);
    EXPECT_NEAR(thinned[1].value, 6000.0, 1e-9) << "the corner keeps its EXACT captured value";
    EXPECT_NEAR(thinned[2].beat, 100.0, 1e-9);
    EXPECT_NEAR(thinned[2].value, 1000.0, 1e-9);
}

TEST(AutomationRecordTest, RDPThinningPassesShortRunsThrough) {
    const auto two = makePoints({{0.0, 1.0}, {1.0, 2.0}});
    EXPECT_EQ(AutomationRecorder::thinPoints(two, kEpsilon).size(), 2u);
    EXPECT_TRUE(AutomationRecorder::thinPoints({}, kEpsilon).empty());
}

// ============================================================================
// 5. Write overwrites its span and auto-drops to Touch on stop
// ============================================================================

TEST(AutomationRecordTest, WriteOverwritesSpanAndAutoDropsToTouch) {
    Harness h;

    // Existing automation across the whole region the take will cover, plus one point past it.
    for (int i = 0; i <= 3; ++i)
        ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, static_cast<double>(i), 700.0 + 10.0 * i));
    ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, 12.0, 999.0));

    h.setMode(LaneRecordMode::Write);
    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying(); // opens the Write span at beat 0, with no gesture anywhere

    const double values[] = {2000.0, 12000.0, 4000.0};
    for (int i = 0; i < 3; ++i) {
        h.setCutoffHz(values[i]); // NO gesture: Write captures value changes on its own
        h.advanceBeat();
    }

    h.stopPlaying();

    const auto* lane = h.lane();
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->recordMode, mode(LaneRecordMode::Touch)) << "Write auto-drops to Touch on stop";

    // Span [0, 3]: everything that used to live there is gone, replaced by the take. The point at
    // beat 12 is outside and survives.
    ASSERT_EQ(lane->points.size(), 5u);
    EXPECT_NEAR(lane->points[0].value, values[0], 1.0); // the entry anchor, replaced by the beat-0 value
    EXPECT_NEAR(lane->points[1].beat, 1.0, 1e-9);
    EXPECT_NEAR(lane->points[1].value, values[1], 1.0);
    EXPECT_NEAR(lane->points[2].beat, 2.0, 1e-9);
    EXPECT_NEAR(lane->points[2].value, values[2], 1.0);
    EXPECT_NEAR(lane->points[3].beat, 3.0, 1e-9) << "Write closes its span with a terminal anchor";
    EXPECT_NEAR(lane->points[3].value, values[2], 1.0);
    EXPECT_NEAR(lane->points[4].beat, 12.0, 1e-9);
    EXPECT_NEAR(lane->points[4].value, 999.0, 1e-6);

    // One undo step for the DATA. Undoing restores the old points but leaves the mode at Touch —
    // the auto-drop is arming, not an edit, and is deliberately outside the undo step.
    ASSERT_TRUE(h.undo.canUndo());
    ASSERT_TRUE(h.undo.undo());
    EXPECT_FALSE(h.undo.canUndo());

    const auto* restored = h.lane();
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->points.size(), 5u);
    EXPECT_NEAR(restored->points[0].value, 700.0, 1e-6);
    EXPECT_EQ(restored->recordMode, mode(LaneRecordMode::Touch)) << "undo must not silently re-arm the lane";
}

// ============================================================================
// 6. Latch keeps its span open after the gesture ends
// ============================================================================

TEST(AutomationRecordTest, LatchKeepsCapturingAfterGestureEnds) {
    Harness h;
    h.setMode(LaneRecordMode::Latch);
    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();

    h.cutoff()->beginChangeGesture();
    h.setCutoffHz(1000.0);
    h.advanceBeat();
    h.setCutoffHz(5000.0);
    h.cutoff()->endChangeGesture(); // Touch would commit here; Latch keeps the span open

    ASSERT_NE(h.lane(), nullptr);
    EXPECT_TRUE(h.lane()->points.empty()) << "Latch commits on stop, not on gesture end";

    h.advanceBeat();
    h.setCutoffHz(12000.0); // no gesture, but the latched span is still open
    h.advanceBeat();

    h.stopPlaying();

    const auto* lane = h.lane();
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 4u);
    EXPECT_NEAR(lane->points[0].beat, 0.0, 1e-9);
    EXPECT_NEAR(lane->points[0].value, 1000.0, 1.0);
    EXPECT_NEAR(lane->points[1].beat, 1.0, 1e-9);
    EXPECT_NEAR(lane->points[1].value, 5000.0, 1.0);
    EXPECT_NEAR(lane->points[2].beat, 2.0, 1e-9);
    EXPECT_NEAR(lane->points[2].value, 12000.0, 1.0) << "captured after the gesture ended";
    EXPECT_NEAR(lane->points[3].beat, 3.0, 1e-9);
    EXPECT_NEAR(lane->points[3].value, 12000.0, 1.0) << "terminal anchor at the stop position";
    EXPECT_TRUE(h.undo.canUndo());
}

// ============================================================================
// 7. Empty spans: Touch is a no-op, Write writes flat anchors
// ============================================================================

TEST(AutomationRecordTest, EmptyTouchGestureIsNoOp) {
    Harness h;
    h.setMode(LaneRecordMode::Touch);
    ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, 0.5, 1234.0));

    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();

    h.cutoff()->beginChangeGesture(); // grabbed and released without moving
    h.advanceBeat();
    h.cutoff()->endChangeGesture();

    ASSERT_NE(h.lane(), nullptr);
    ASSERT_EQ(h.lane()->points.size(), 1u) << "an empty Touch gesture must not erase what it spanned";
    EXPECT_NEAR(h.lane()->points[0].value, 1234.0, 1e-6);
    EXPECT_FALSE(h.undo.canUndo());
}

TEST(AutomationRecordTest, EmptyWriteSpanWritesFlatAnchors) {
    Harness h;
    ASSERT_TRUE(h.doc.addBreakpoint(h.laneId, 1.0, 1234.0));
    h.setMode(LaneRecordMode::Write);

    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();
    h.advanceBeat(3.0); // rolls through the span without anything being touched
    h.stopPlaying();

    const auto* lane = h.lane();
    ASSERT_NE(lane, nullptr);
    // Write means "overwrite from play to stop", so even a take where nothing moved flattens the
    // span to the value the parameter held when it started.
    ASSERT_EQ(lane->points.size(), 2u);
    EXPECT_NEAR(lane->points[0].beat, 0.0, 1e-9);
    EXPECT_NEAR(lane->points[0].value, static_cast<double>(kCutoffDefault), 1.0);
    EXPECT_NEAR(lane->points[1].beat, 3.0, 1e-9);
    EXPECT_NEAR(lane->points[1].value, static_cast<double>(kCutoffDefault), 1.0);
    EXPECT_TRUE(h.undo.canUndo());
}

// ============================================================================
// 8. Ring overflow drops events, raises the flag, and still commits
// ============================================================================

TEST(AutomationRecordTest, OverflowSetsFlagAndSurvives) {
    Harness h;
    h.setMode(LaneRecordMode::Touch);
    h.recorder.setGlobalRecordEnable(true);
    h.startPlaying();
    ASSERT_FALSE(h.recorder.hadOverrun());

    // Well past kRingCapacity, with no update() in between to drain it: the excess is dropped rather
    // than allocated for, exactly like MidiRecorder's take.
    h.cutoff()->beginChangeGesture();
    for (int i = 0; i < AutomationRecorder::kRingCapacity + 4000; ++i) {
        h.setCutoffHz(1000.0 + static_cast<double>(i % 900) * 10.0);
        if (i % 100 == 0)
            h.advanceBeat(0.1);
    }
    h.cutoff()->endChangeGesture();

    EXPECT_TRUE(h.recorder.hadOverrun());
    ASSERT_NE(h.lane(), nullptr);
    EXPECT_FALSE(h.lane()->points.empty()) << "an overrun truncates the take, it does not cancel it";
    EXPECT_TRUE(h.undo.canUndo());
}

// ============================================================================
// 9. Off / Read / Write-while-disarmed, straight at the applier
// ============================================================================

namespace {

// Drives AutomationApplier against a hand-built binding table — no engine, no graph. The applier
// only ever reads `snapshot`, `laneIndex` and `param`, so this is the whole of what it needs.
struct ApplierProbe {
    TimelineDoc doc;
    FilterModule filter;
    std::unique_ptr<synth::TimelineSnapshot> snapshot;
    synth::AutomationApplier applier;
    synth::LaneId laneId;

    ApplierProbe() {
        const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        laneId = doc.addLane(trackId, kNodeUuid, "cutoff", cutoffRange());
        doc.addBreakpoint(laneId, 0.0, 8000.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold));
    }

    juce::RangedAudioParameter* cutoff() { return findParameterByID(&filter, "cutoff"); }
    double cutoffHz() { return static_cast<double>(cutoff()->convertFrom0to1(cutoff()->getValue())); }

    // Rebuilds the snapshot from the CURRENT doc and pushes one playing block at beat 1.
    void applyOneBlock(const synth::AutomationRecordState* recordState) {
        snapshot = synth::TimelineSnapshot::buildFrom(doc);

        synth::AutomationBindingTable table;
        table.snapshot = snapshot.get();
        synth::AutomationBindingTable::Binding binding;
        binding.laneIndex = 0;
        binding.param = cutoff();
        table.bindings.push_back(std::move(binding));

        synth::BlockTimeInfo info;
        info.playing = true;
        info.startPpq = 1.0;
        info.numSamples = kBlock;
        info.sampleRate = kSampleRate;

        applier.applyBlock(table, info, recordState);
    }
};

} // namespace

TEST(AutomationApplierTest, OffLaneIsCompletelyInert) {
    ApplierProbe probe;
    ASSERT_TRUE(probe.doc.setLaneRecordMode(probe.laneId, mode(LaneRecordMode::Off)));

    probe.applyOneBlock(nullptr);
    EXPECT_NEAR(probe.cutoffHz(), static_cast<double>(kCutoffDefault), 0.01)
        << "an Off lane never writes, recorder installed or not";

    // Flip to Read and the very same lane drives the parameter.
    ASSERT_TRUE(probe.doc.setLaneRecordMode(probe.laneId, mode(LaneRecordMode::Read)));
    probe.applyOneBlock(nullptr);
    EXPECT_NEAR(probe.cutoffHz(), 8000.0, 1.0);
}

TEST(AutomationApplierTest, WriteWithGlobalRecordOffActsLikeRead) {
    ApplierProbe probe;
    ASSERT_TRUE(probe.doc.setLaneRecordMode(probe.laneId, mode(LaneRecordMode::Write)));

    synth::AutomationRecordState recordState;
    recordState.globalRecordEnable.store(false, std::memory_order_relaxed);

    probe.applyOneBlock(&recordState);
    EXPECT_NEAR(probe.cutoffHz(), 8000.0, 1.0) << "armed-but-not-recording still has to play back";

    // Arm, and the same lane goes silent: the take owns the parameter now.
    probe.cutoff()->setValue(probe.cutoff()->convertTo0to1(1234.0f));
    recordState.globalRecordEnable.store(true, std::memory_order_relaxed);
    probe.applyOneBlock(&recordState);
    EXPECT_NEAR(probe.cutoffHz(), 1234.0, 1.0) << "a recording Write lane must not fight the hand writing it";
}

TEST(AutomationApplierTest, ClaimedTouchLaneYieldsToTheHand) {
    ApplierProbe probe;
    ASSERT_TRUE(probe.doc.setLaneRecordMode(probe.laneId, mode(LaneRecordMode::Touch)));

    synth::AutomationRecordState recordState;
    recordState.globalRecordEnable.store(true, std::memory_order_relaxed);

    // Unclaimed: a Touch lane plays back exactly like a Read lane.
    probe.applyOneBlock(&recordState);
    EXPECT_NEAR(probe.cutoffHz(), 8000.0, 1.0);

    // Claimed: the applier steps aside and the user's value stands.
    probe.cutoff()->setValue(probe.cutoff()->convertTo0to1(1234.0f));
    recordState.claims.params[0].store(probe.cutoff(), std::memory_order_relaxed);
    EXPECT_TRUE(recordState.claims.isClaimed(probe.cutoff()));
    probe.applyOneBlock(&recordState);
    EXPECT_NEAR(probe.cutoffHz(), 1234.0, 1.0);

    // Released: playback resumes on the very next block, with no reset in between.
    recordState.claims.params[0].store(nullptr, std::memory_order_relaxed);
    probe.applyOneBlock(&recordState);
    EXPECT_NEAR(probe.cutoffHz(), 8000.0, 1.0);
}

// ============================================================================
// 10. recordMode is document format
// ============================================================================

TEST(AutomationRecordTest, RecordModeRoundTripsAndDefaultsToRead) {
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto laneId = doc.addLane(trackId, kNodeUuid, "cutoff", cutoffRange());
    ASSERT_NE(doc.getLane(laneId), nullptr);
    EXPECT_EQ(doc.getLane(laneId)->recordMode, mode(LaneRecordMode::Read)) << "a fresh lane is playback-only";

    EXPECT_FALSE(doc.setLaneRecordMode(laneId, -1));
    EXPECT_FALSE(doc.setLaneRecordMode(laneId, 5));
    EXPECT_EQ(doc.getLane(laneId)->recordMode, mode(LaneRecordMode::Read));

    ASSERT_TRUE(doc.setLaneRecordMode(laneId, mode(LaneRecordMode::Latch)));
    const auto revisionAfterSet = doc.getRevision();
    EXPECT_TRUE(doc.setLaneRecordMode(laneId, mode(LaneRecordMode::Latch)));
    EXPECT_EQ(doc.getRevision(), revisionAfterSet) << "setting the mode it already has is a no-op";

    const juce::var state = doc.toVar();
    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(state));
    ASSERT_NE(reloaded.getLane(laneId), nullptr);
    EXPECT_EQ(reloaded.getLane(laneId)->recordMode, mode(LaneRecordMode::Latch));

    // An older file with no recordMode at all must load as Read.
    auto* root = state.getDynamicObject();
    ASSERT_NE(root, nullptr);
    auto* track = root->getProperty("tracks")[0].getDynamicObject();
    ASSERT_NE(track, nullptr);
    auto* laneVar = track->getProperty("lanes")[0].getDynamicObject();
    ASSERT_NE(laneVar, nullptr);
    laneVar->removeProperty("recordMode");

    TimelineDoc legacy;
    ASSERT_TRUE(legacy.fromVar(state));
    ASSERT_NE(legacy.getLane(laneId), nullptr);
    EXPECT_EQ(legacy.getLane(laneId)->recordMode, mode(LaneRecordMode::Read));

    // And an out-of-range one is malformed, not something to clamp.
    laneVar->setProperty("recordMode", 9);
    TimelineDoc rejected;
    EXPECT_FALSE(rejected.fromVar(state));
}

TEST(AutomationRecordTest, SnapshotCarriesTheRecordMode) {
    TimelineDoc doc;
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto laneId = doc.addLane(trackId, kNodeUuid, "cutoff", cutoffRange());
    ASSERT_TRUE(doc.setLaneRecordMode(laneId, mode(LaneRecordMode::Write)));

    const auto snapshot = synth::TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(snapshot->selfCheck());
    ASSERT_EQ(snapshot->lanes.size(), 1u);
    EXPECT_EQ(snapshot->lanes[0].recordMode, mode(LaneRecordMode::Write));
}

#if SYNTH_ENABLE_TIMELINE

// ============================================================================
// 11. Engine level: claims really stop the applier, and the applier is inaudible
//     to the recorder
// ============================================================================

namespace {

constexpr const char* kEngineFilterUuid = "b0440000-0000-0000-0000-000000000002";

juce::String buildPatchJson() {
    return juce::String(R"({
        "nodes": [
            {"id": 1, "type": "Filter",       "uuid": ")") +
           kEngineFilterUuid + R"("},
            {"id": 2, "type": "Audio Output", "uuid": "b0440000-0000-0000-0000-000000000003"}
        ],
        "connections": [
            {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0}
        ]
    })";
}

// A hosted engine with one Filter, a timeline lane on its cutoff, and a recorder wired into both.
struct EngineFixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    AppUndoManager undo;
    AutomationRecorder recorder;
    synth::TrackId trackId;
    synth::LaneId laneId;

    bool build(LaneRecordMode laneMode) {
        engine.initialise();

        const juce::var patch = juce::JSON::parse(buildPatchJson());
        if (!patch.isObject())
            return false;
        if (!synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true,
                                                    /*trusted=*/true))
            return false;

        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlock, 2);

        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        if (!trackId.isValid())
            return false;
        laneId = doc.addLane(trackId, kEngineFilterUuid, "cutoff", cutoffRange());
        if (!laneId.isValid())
            return false;
        doc.addBreakpoint(laneId, 0.0, 8000.0, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold));
        doc.setLaneRecordMode(laneId, mode(laneMode));

        recorder.attachTo(doc, undo, engine.getTransport());
        recorder.bindLane(laneId, cutoff(), nodeFor(kEngineFilterUuid));
        engine.setAutomationRecorder(&recorder);
        engine.publishTimeline(doc);
        return cutoff() != nullptr;
    }

    juce::AudioProcessorGraph::Node::Ptr nodeFor(const char* uuid) {
        for (auto* node : engine.getGraph().getNodes())
            if (node != nullptr && node->properties["uuid"].toString() == juce::String(uuid))
                return node;
        return {};
    }

    juce::RangedAudioParameter* cutoff() {
        auto node = nodeFor(kEngineFilterUuid);
        return node != nullptr ? findParameterByID(node->getProcessor(), "cutoff") : nullptr;
    }

    double cutoffHz() { return static_cast<double>(cutoff()->convertFrom0to1(cutoff()->getValue())); }
    void setCutoffHz(double hz) { cutoff()->setValueNotifyingHost(cutoff()->convertTo0to1(static_cast<float>(hz))); }

    ~EngineFixture() {
        // Unhook before anything is torn down: the engine must not read a dying recorder, and the
        // recorder must not outlive the graph nodes whose parameters it listens to.
        engine.setAutomationRecorder(nullptr);
        recorder.detach();
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

TEST(AutomationRecordTest, ApplierRespectsClaims) {
    EngineFixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Touch));

    f.recorder.setGlobalRecordEnable(true);
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);
    f.recorder.update();

    // Unclaimed Touch behaves like Read.
    EXPECT_NEAR(f.cutoffHz(), 8000.0, 1.0);

    // A hand goes on the knob: the applier must stop writing for as long as the gesture lasts.
    f.cutoff()->beginChangeGesture();
    f.setCutoffHz(1234.0);
    EXPECT_TRUE(f.recorder.getAudioState().claims.isClaimed(f.cutoff()));

    f.driver->renderBlocks(32);
    EXPECT_NEAR(f.cutoffHz(), 1234.0, 1.0) << "automation must not pull the knob out of the user's hand";

    // Let go and playback takes back over on the next block.
    f.cutoff()->endChangeGesture();
    EXPECT_FALSE(f.recorder.getAudioState().claims.isClaimed(f.cutoff()));
    f.driver->renderBlocks(4);
    EXPECT_NEAR(f.cutoffHz(), 8000.0, 1.0);
}

TEST(AutomationRecordTest, RecorderNeverHearsTheApplier) {
    EngineFixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Touch));

    // Armed Touch lane, transport rolling, and NOBODY touching anything: the applier writes the
    // parameter on every single block, through setValue. If those writes notified listeners the
    // recorder would capture its own playback and every pass would re-record the lane.
    f.recorder.setGlobalRecordEnable(true);
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(200);
    f.recorder.update();

    ASSERT_TRUE(f.driver->getTransport().stop());
    f.driver->renderBlocks(2);
    f.recorder.update();

    const auto* lane = f.doc.getLane(f.laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_EQ(lane->points.size(), 1u) << "the lane must still be exactly what it was authored as";
    EXPECT_NEAR(lane->points[0].value, 8000.0, 1e-6);
    EXPECT_FALSE(f.undo.canUndo()) << "playback must never create an undo step";
    EXPECT_FALSE(f.recorder.hadOverrun());
}

#endif // SYNTH_ENABLE_TIMELINE
