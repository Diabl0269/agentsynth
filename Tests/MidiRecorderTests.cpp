// Tests for TL3-3: MidiRecorder — records external MIDI into a new timeline clip, as one undo step.
//
// Most of this file needs no engine and no graph: it drives a bare synth::TransportService exactly
// the way AudioEngine::renderNextBlock does (tick(), then captureBlock() against the published
// BlockTimeInfo), the same house style TimelineMidiSourceTests.cpp uses for the "Track In" module.
//
// Timing arithmetic used throughout: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples per beat
// (TransportService's default BPM).
//
// The last test (DoublePathRegression) is the one place a real, hosted AudioEngine is exercised —
// it proves the recorder captures the single collector-merged/host-delivered buffer and never the
// ExternalMidiModule push-path copies, which is the whole point of TL3-3's "single source" design.

#include "../Source/AppUndoManager.h"
#include "../Source/Timeline/MidiRecorder.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

#if SYNTH_ENABLE_TIMELINE
#include "../Source/AudioEngine.h"
#include "../Source/Modules/ExternalMidiModule.h"
#endif

using synth::MidiRecorder;
using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kSamplesPerBeat = 24000; // 120 BPM at 48 kHz — TransportService's default BPM

// Drives a bare transport + recorder exactly like AudioEngine::renderNextBlock does, minus the graph.
struct Harness {
    synth::TransportService transport;
    MidiRecorder recorder;

    Harness() { transport.prepare(kSampleRate, kBlock); }

    // Renders one block, feeding `midi` (may be empty) to the recorder against this block's info.
    void renderBlock(const juce::MidiBuffer& midi, int numSamples = kBlock) {
        const auto& info = transport.tick(numSamples);
        recorder.captureBlock(midi, info);
    }

    // Renders `numBlocks` empty blocks — silence, or just advancing the clock.
    void renderSilentBlocks(int numBlocks) {
        juce::MidiBuffer empty;
        for (int i = 0; i < numBlocks; ++i)
            renderBlock(empty);
    }
};

double beatOfSample(std::int64_t sample) { return (double)sample / (double)kSamplesPerBeat; }

} // namespace

// ============================================================================
// 1. CapturesTimestampedTake
// ============================================================================

TEST(MidiRecorderTest, CapturesTimestampedTake) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);

    // Note-on at block 10, offset 100; note-off at block 30, offset 200.
    for (int block = 0; block < 31; ++block) {
        juce::MidiBuffer midi;
        if (block == 10)
            midi.addEvent(juce::MidiMessage::noteOn(3, 64, (juce::uint8)111), 100);
        if (block == 30)
            midi.addEvent(juce::MidiMessage::noteOff(3, 64), 200);
        h.renderBlock(midi);
    }

    ASSERT_TRUE(h.recorder.stopAndCommit(doc, undo));
    EXPECT_FALSE(h.recorder.hadOverrun());

    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);
    const auto& clip = trackPtr->clips[0];
    ASSERT_EQ(clip.notes.size(), 1u);

    const double onBeat = beatOfSample(10 * kBlock + 100);
    const double offBeat = beatOfSample(30 * kBlock + 200);
    const double expectedClipStart = std::floor(onBeat);

    EXPECT_NEAR(clip.startBeat, expectedClipStart, 1e-6);
    EXPECT_NEAR(clip.notes[0].startBeat, onBeat - expectedClipStart, 1e-6);
    EXPECT_NEAR(clip.notes[0].lengthBeats, offBeat - onBeat, 1e-6);
    EXPECT_EQ(clip.notes[0].pitch, 64);
    EXPECT_EQ(clip.notes[0].velocity, 111);
    EXPECT_EQ(clip.notes[0].channel, 3);
}

// ============================================================================
// 2. OneUndoStep
// ============================================================================

TEST(MidiRecorderTest, OneUndoStep) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);

    for (int block = 0; block < 5; ++block) {
        juce::MidiBuffer midi;
        if (block == 1)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20);
        h.renderBlock(midi);
    }

    ASSERT_FALSE(undo.canUndo());
    ASSERT_TRUE(h.recorder.stopAndCommit(doc, undo));
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u);
    ASSERT_TRUE(undo.canUndo());

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(doc.getTrack(track)->clips.size(), 0u) << "one undo must remove the whole take";
    EXPECT_FALSE(undo.canUndo()) << "the commit must have been exactly one undo step";
    ASSERT_TRUE(undo.canRedo());

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(doc.getTrack(track)->clips.size(), 1u) << "redo must restore the clip";
}

// ============================================================================
// 3. DanglingNoteOnGetsStopLength
// ============================================================================

TEST(MidiRecorderTest, DanglingNoteOnGetsStopLength) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);

    for (int block = 0; block < 5; ++block) {
        juce::MidiBuffer midi;
        if (block == 2)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 50);
        h.renderBlock(midi); // no note-off, ever
    }

    ASSERT_TRUE(h.recorder.stopAndCommit(doc, undo));
    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);
    ASSERT_EQ(trackPtr->clips[0].notes.size(), 1u);
    EXPECT_NEAR(trackPtr->clips[0].notes[0].lengthBeats, MidiRecorder::kMinNoteLengthBeats, 1e-9)
        << "a note with no matching off must be closed at the stop position, floored at 1/32 beat";
}

// ============================================================================
// 4. NotPlayingCapturesNothing / NotRecordingCapturesNothing
// ============================================================================

TEST(MidiRecorderTest, NotPlayingCapturesNothing) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    h.recorder.startRecording(track, 0.0); // armed, but the transport is never told to play

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
    h.renderBlock(midi);

    EXPECT_FALSE(h.recorder.stopAndCommit(doc, undo));
    EXPECT_TRUE(doc.getTrack(track)->clips.empty());
}

TEST(MidiRecorderTest, NotRecordingCapturesNothing) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play()); // playing, but startRecording() was never called

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
    h.renderBlock(midi);

    EXPECT_FALSE(h.recorder.stopAndCommit(doc, undo));
    EXPECT_TRUE(doc.getTrack(track)->clips.empty());
}

// ============================================================================
// 5. WrapAwareBeatMath
// ============================================================================

// Loop [0, 2): beat 2.0 is sample 48000, which lands in block 93 (93 * 512 = 47616) at offset 384 —
// the same wrap point TimelineMidiSourceTests.cpp uses for its loop-wrap coverage.
TEST(MidiRecorderTest, WrapAwareBeatMath) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.setLoop(0.0, 2.0, true));
    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);

    h.renderSilentBlocks(93); // blocks 0..92: run up to the wrap boundary

    // Block 93 wraps at offset 384. Offset 400 is 16 samples PAST the wrap — the post-wrap range.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 400);
    const auto& info = h.transport.tick(kBlock);
    ASSERT_EQ(info.loopWrapSample, 384) << "test setup assumption: this block must actually wrap here";
    h.recorder.captureBlock(midi, info);

    ASSERT_TRUE(h.recorder.stopAndCommit(doc, undo));
    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);
    ASSERT_EQ(trackPtr->clips[0].notes.size(), 1u);

    // Expected: loopStartPpq (0.0) + (400 - 384) samples past the wrap, in beats.
    const double expectedBeat = (double)(400 - 384) / (double)kSamplesPerBeat;
    const double clipStart = trackPtr->clips[0].startBeat;
    EXPECT_NEAR(clipStart + trackPtr->clips[0].notes[0].startBeat, expectedBeat, 1e-6)
        << "an event past the wrap point must use the POST-wrap beat, not the primary range's";
}

// ============================================================================
// 6. OverrunSetsFlagAndDrops
// ============================================================================

TEST(MidiRecorderTest, OverrunSetsFlagAndDrops) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);

    // Well over the ring's 4096-slot capacity, in one single block.
    juce::MidiBuffer midi;
    for (int i = 0; i < 5000; ++i)
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), i % kBlock);

    EXPECT_NO_THROW(h.renderBlock(midi));
    EXPECT_TRUE(h.recorder.hadOverrun());

    // The captured prefix still commits — no crash, and something usable comes out the other end.
    EXPECT_TRUE(h.recorder.stopAndCommit(doc, undo));
    ASSERT_EQ(doc.getTrack(track)->clips.size(), 1u);
    EXPECT_GT(doc.getTrack(track)->clips[0].notes.size(), 0u);
}

// ============================================================================
// 7. EmptyTakeCommitsNothing
// ============================================================================

TEST(MidiRecorderTest, EmptyTakeCommitsNothing) {
    Harness h;
    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(h.transport.play());
    h.recorder.startRecording(track, 0.0);
    h.renderSilentBlocks(4); // nothing at all arrives

    EXPECT_FALSE(h.recorder.stopAndCommit(doc, undo));
    EXPECT_TRUE(doc.getTrack(track)->clips.empty());
    EXPECT_FALSE(undo.canUndo()) << "an empty take must not create an undo step";
}

// ============================================================================
// 8. DoublePathRegression: hosted AudioEngine, both delivery paths, one recorded note
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

// AudioEngine::handleIncomingMidiMessage forwards every message to TWO places: a direct
// pushMidiMessage() into any bound ExternalMidiModule, and the buffer that reaches the graph (the
// collector drain in standalone mode, or — as here — whatever the host hands processHostBlock
// directly). This test reproduces exactly that dual delivery and checks the recorder — wired via
// AudioEngine::setMidiCaptureSink, reading only renderNextBlock's own buffer — records the note
// EXACTLY ONCE, never doubled by the ExternalMidiModule path it must not read.
TEST(MidiRecorderTest, DoublePathRegression) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    auto* extMidiNode = engine.getGraph().addNode(std::make_unique<ExternalMidiModule>()).get();
    auto* extMidi = dynamic_cast<ExternalMidiModule*>(extMidiNode->getProcessor());
    ASSERT_NE(extMidi, nullptr);

    engine.prepareForHost(kSampleRate, kBlock, 0, 2);

    MidiRecorder recorder;
    engine.setMidiCaptureSink(&recorder);

    TimelineDoc doc;
    AppUndoManager undo;
    const auto track = doc.addTrack(TrackKind::Midi, "Track 1");

    ASSERT_TRUE(engine.getTransport().play());
    recorder.startRecording(track, 0.0);

    const auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
    const auto noteOff = juce::MidiMessage::noteOff(1, 60);

    // Block 0: note-on through BOTH paths — the ExternalMidi push (must never reach the recorder)
    // and the buffer handed to processHostBlock (the recorder's one true source, mirroring the
    // collector-drained buffer in standalone mode).
    {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(noteOn, 10);
        extMidi->pushMidiMessage(noteOn);
        engine.processHostBlock(buffer, midi);
    }

    for (int i = 0; i < 3; ++i) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
    }

    // Block 4: note-off, again through both paths.
    {
        juce::AudioBuffer<float> buffer(2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(noteOff, 20);
        extMidi->pushMidiMessage(noteOff);
        engine.processHostBlock(buffer, midi);
    }

    ASSERT_TRUE(recorder.stopAndCommit(doc, undo));

    const auto* trackPtr = doc.getTrack(track);
    ASSERT_NE(trackPtr, nullptr);
    ASSERT_EQ(trackPtr->clips.size(), 1u);
    ASSERT_EQ(trackPtr->clips[0].notes.size(), 1u)
        << "the note must appear exactly once — never doubled by the ExternalMidi push path";
    EXPECT_EQ(trackPtr->clips[0].notes[0].pitch, 60);

    engine.releaseFromHost();
    engine.shutdown();
}

#endif // SYNTH_ENABLE_TIMELINE
