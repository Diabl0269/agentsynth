// Tests for recording from one MIDI input: MidiRecorder's per-target filters (device lane and channel, the merged
// stream as the pass-through default, a missing device recording nothing), the per-device lanes the engine keeps in
// MidiInputSources, and the threads those lanes cross (written to be run under -fsanitize=thread too).
//
// Timing as in MidiRecorderTests.cpp: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples per beat.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/MidiInputSources.h"
#include "Timeline/MidiRecorder.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <thread>
#include <vector>

using synth::MidiInputSources;
using synth::MidiRecorder;
using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kSamplesPerBeat = 24000;

MidiInputSources::Event noteEvent(int source, int position, bool on, int channel, int pitch) {
    MidiInputSources::Event event;
    event.source = source;
    event.samplePosition = position;
    event.bytes[0] = static_cast<std::uint8_t>((on ? 0x90 : 0x80) | (channel - 1));
    event.bytes[1] = static_cast<std::uint8_t>(pitch);
    event.bytes[2] = on ? 100 : 0;
    return event;
}

// A bare transport + recorder driven like AudioEngine::renderPass: tick, then capture this block's merged MIDI and
// per-device notes.
struct Harness {
    synth::TransportService transport;
    MidiRecorder recorder;
    TimelineDoc doc;
    AppUndoManager undo;

    Harness() {
        transport.prepare(kSampleRate, kBlock);
        transport.play();
        transport.tick(kBlock); // drains play()
    }

    void render(const juce::MidiBuffer& merged, const std::vector<MidiInputSources::Event>& sourced = {}) {
        const auto& info = transport.tick(kBlock);
        recorder.captureBlock(merged, info, {sourced.data(), (int)sourced.size(), 0, kBlock});
    }

    // One note (on at sample 10, off at sample 20 of the next block) per device, and the merged copy of both.
    void playOneNoteEach(int sourceA, int pitchA, int channelA, int sourceB, int pitchB, int channelB) {
        juce::MidiBuffer mergedOn, mergedOff;
        mergedOn.addEvent(juce::MidiMessage::noteOn(channelA, pitchA, (juce::uint8)100), 10);
        mergedOn.addEvent(juce::MidiMessage::noteOn(channelB, pitchB, (juce::uint8)100), 10);
        mergedOff.addEvent(juce::MidiMessage::noteOff(channelA, pitchA), 20);
        mergedOff.addEvent(juce::MidiMessage::noteOff(channelB, pitchB), 20);
        render(mergedOn,
               {noteEvent(sourceA, 10, true, channelA, pitchA), noteEvent(sourceB, 10, true, channelB, pitchB)});
        render(mergedOff,
               {noteEvent(sourceA, 20, false, channelA, pitchA), noteEvent(sourceB, 20, false, channelB, pitchB)});
    }

    std::vector<int> pitchesOn(TrackId track) const {
        std::vector<int> pitches;
        if (const auto* t = doc.getTrack(track))
            for (const auto& clip : t->clips)
                for (const auto& note : clip.notes)
                    pitches.push_back(note.pitch);
        return pitches;
    }
};

} // namespace

TEST(MidiRecorderInputFilterTest, AllInputsAnyChannelRecordsTheMergedStreamAndIgnoresTheDeviceCopies) {
    Harness h;
    const auto track = h.doc.addTrack(TrackKind::Midi, "Keys");
    h.recorder.startRecording(track, 0.0); // the default target: every input, any channel
    h.playOneNoteEach(0, 60, 1, 1, 72, 9);
    ASSERT_TRUE(h.recorder.stopAndCommit(h.doc, h.undo));
    EXPECT_EQ(h.pitchesOn(track), (std::vector<int>{60, 72})) << "each note once, from the merged stream only";
}

TEST(MidiRecorderInputFilterTest, AChannelFilterKeepsOnlyThatChannelOfTheMergedStream) {
    Harness h;
    const auto track = h.doc.addTrack(TrackKind::Midi, "Drums");
    h.recorder.startRecording({{track, MidiRecorder::kAnySource, 10}}, 0.0);
    h.playOneNoteEach(0, 60, 1, 1, 36, 10);
    ASSERT_TRUE(h.recorder.stopAndCommit(h.doc, h.undo));
    EXPECT_EQ(h.pitchesOn(track), (std::vector<int>{36}));
    EXPECT_EQ(h.doc.getTrack(track)->clips.front().notes.front().channel, 10);
}

TEST(MidiRecorderInputFilterTest, TwoDeviceTargetsEachRecordOnlyTheirOwnDeviceInOneUndoStep) {
    Harness h;
    const auto keys = h.doc.addTrack(TrackKind::Midi, "Keys");
    const auto pads = h.doc.addTrack(TrackKind::Midi, "Pads");
    h.recorder.startRecording({{keys, 0, 0}, {pads, 1, 0}}, 0.0);
    h.playOneNoteEach(0, 60, 1, 1, 72, 1);
    ASSERT_TRUE(h.recorder.stopAndCommit(h.doc, h.undo));
    EXPECT_EQ(h.pitchesOn(keys), (std::vector<int>{60}));
    EXPECT_EQ(h.pitchesOn(pads), (std::vector<int>{72}));

    // Both takes are one Record press, so one undo removes both clips.
    ASSERT_TRUE(h.undo.undo());
    EXPECT_TRUE(h.doc.getTrack(keys)->clips.empty());
    EXPECT_TRUE(h.doc.getTrack(pads)->clips.empty());
}

TEST(MidiRecorderInputFilterTest, ADeviceAndChannelTargetNeedsBoth) {
    Harness h;
    const auto track = h.doc.addTrack(TrackKind::Midi, "Bass");
    h.recorder.startRecording({{track, 1, 2}}, 0.0);
    h.playOneNoteEach(1, 40, 1, 1, 43, 2); // both on device 1, channels 1 and 2
    ASSERT_TRUE(h.recorder.stopAndCommit(h.doc, h.undo));
    EXPECT_EQ(h.pitchesOn(track), (std::vector<int>{43}));
}

TEST(MidiRecorderInputFilterTest, AMissingDeviceRecordsNothingAndNeverFallsBackToEveryInput) {
    Harness h;
    const auto track = h.doc.addTrack(TrackKind::Midi, "Keys");
    h.recorder.startRecording({{track, MidiInputSources::kNoSource, 0}}, 0.0);
    h.playOneNoteEach(0, 60, 1, 1, 72, 1);
    EXPECT_FALSE(h.recorder.stopAndCommit(h.doc, h.undo));
    EXPECT_TRUE(h.doc.getTrack(track)->clips.empty());
    EXPECT_FALSE(h.undo.canUndo());
}

// A sliced callback hands the recorder the whole callback's device notes on every pass; each pass must take only the
// ones inside its own slice, at that slice's beat.
TEST(MidiRecorderInputFilterTest, ASlicedCallbackRecordsEachDeviceNoteOnceAtItsOwnBeat) {
    Harness h;
    const auto track = h.doc.addTrack(TrackKind::Midi, "Keys");
    h.recorder.startRecording({{track, 0, 0}}, 0.0);
    const std::vector<MidiInputSources::Event> callback{noteEvent(0, 300, true, 1, 60),
                                                        noteEvent(0, 400, false, 1, 60)};
    const juce::MidiBuffer none;
    std::int64_t firstSliceStart = 0;
    for (int offset = 0; offset < kBlock; offset += 256) {
        const auto& info = h.transport.tick(256);
        if (offset == 0)
            firstSliceStart = info.blockStartSample;
        h.recorder.captureBlock(none, info, {callback.data(), (int)callback.size(), offset, 256});
    }
    ASSERT_TRUE(h.recorder.stopAndCommit(h.doc, h.undo));
    const auto& clip = h.doc.getTrack(track)->clips.front();
    ASSERT_EQ(clip.notes.size(), 1u);
    const double expectedStart = (double)(firstSliceStart + 300) / kSamplesPerBeat;
    EXPECT_NEAR(clip.startBeat + clip.notes.front().startBeat, expectedStart, 1e-9);
    EXPECT_NEAR(clip.notes.front().lengthBeats, 100.0 / kSamplesPerBeat, 1e-9);
}

// ---- MidiInputSources ------------------------------------------------------------------------------------------

TEST(MidiInputSourcesTest, ALaneIsHandedOutOncePerIdentifierAndNeverRecycled) {
    MidiInputSources sources;
    EXPECT_EQ(sources.findSource("dev-a"), MidiInputSources::kNoSource);
    const int a = sources.sourceFor("dev-a");
    const int b = sources.sourceFor("dev-b");
    EXPECT_NE(a, MidiInputSources::kNoSource);
    EXPECT_NE(a, b);
    EXPECT_EQ(sources.sourceFor("dev-a"), a);
    EXPECT_EQ(sources.findSource("dev-b"), b);
    EXPECT_EQ(sources.sourceFor(""), MidiInputSources::kNoSource);
    for (int i = 0; i < MidiInputSources::kMaxSources; ++i)
        sources.sourceFor("more-" + juce::String(i));
    EXPECT_EQ(sources.sourceFor("one-too-many"), MidiInputSources::kNoSource);
    EXPECT_EQ(sources.findSource("dev-a"), a) << "a full table keeps every lane it handed out";
}

TEST(MidiInputSourcesTest, DrainPlacesNotesByTheirAgeAndKeepsOnlyNotesFromKnownDevices) {
    MidiInputSources sources;
    sources.setSampleRate(kSampleRate);
    const int a = sources.sourceFor("dev-a");
    const double nowMs = 100000.0;
    MidiInputSources::Block block;
    sources.drainBlock(block, nowMs - 1000.0 * kBlock / kSampleRate, kBlock); // the previous callback

    auto at = [](juce::MidiMessage message, double ms) {
        message.setTimeStamp(ms * 0.001);
        return message;
    };
    // 5 ms before the callback ends: 240 samples back from its end at 48 kHz.
    sources.push("dev-a", at(juce::MidiMessage::noteOn(1, 60, (juce::uint8)90), nowMs - 5.0));
    sources.push("dev-a", at(juce::MidiMessage::controllerEvent(1, 7, 100), nowMs - 4.0));        // not a note
    sources.push("dev-x", at(juce::MidiMessage::noteOn(1, 61, (juce::uint8)90), nowMs - 4.0));    // unknown device
    sources.push("dev-a", at(juce::MidiMessage::noteOn(1, 62, (juce::uint8)90), nowMs - 5000.0)); // stale backlog
    sources.drainBlock(block, nowMs, kBlock);

    ASSERT_EQ(block.count, 1);
    EXPECT_EQ(block.events[0].source, a);
    EXPECT_EQ(block.events[0].samplePosition, kBlock - 240);
    EXPECT_EQ(block.events[0].bytes[1], 60);

    sources.drainBlock(block, nowMs + 10.0, kBlock);
    EXPECT_EQ(block.count, 0) << "a drain empties the lanes";
}

// Through the engine's real entry point: a note reaches its device's lane after the same early-outs as the merged
// stream, so a bounce's suspended MIDI stays out of the lanes too.
TEST(MidiInputSourcesTest, TheEngineCopiesEachDevicesNotesIntoItsOwnLane) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    auto& sources = engine.getMidiInputSources();
    sources.setSampleRate(kSampleRate);
    const int a = sources.sourceFor("dev-a");
    const int b = sources.sourceFor("dev-b");
    auto now = [] {
        auto m = juce::MidiMessage::noteOn(1, 60, (juce::uint8)90);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        return m;
    };
    engine.handleIncomingMidiMessageFromSource("dev-a", now());
    engine.handleIncomingMidiMessageFromSource("dev-b", now());
    engine.handleIncomingMidiMessageFromSource("dev-b", now());
    engine.suspendExternalMidi();
    engine.handleIncomingMidiMessageFromSource("dev-a", now()); // dropped: a bounce is running
    engine.resumeExternalMidi();

    MidiInputSources::Block block;
    sources.drainBlock(block, juce::Time::getMillisecondCounterHiRes(), kBlock);
    int fromA = 0, fromB = 0;
    for (int i = 0; i < block.count; ++i) {
        fromA += block.events[(size_t)i].source == a ? 1 : 0;
        fromB += block.events[(size_t)i].source == b ? 1 : 0;
    }
    EXPECT_EQ(fromA, 1);
    EXPECT_EQ(fromB, 2);
    engine.shutdown();
}

// Two device threads push while an "audio" thread drains and records and the message thread hands out new lanes:
// the thread contract MidiInputSources and MidiRecorder rely on. Run under -fsanitize=thread to check it; without it
// this still proves every note lands on its own track exactly once.
TEST(MidiInputSourcesTest, DeviceThreadsAudioThreadAndMessageThreadRunTogether) {
    // Note-ons only (each is closed at the take's end, so none is zero-length however fast the threads run); 200
    // per lane fits even if the drain falls behind.
    constexpr int kNotesPerDevice = 200;
    MidiInputSources sources;
    sources.setSampleRate(kSampleRate);
    const int a = sources.sourceFor("dev-a");
    const int b = sources.sourceFor("dev-b");

    TimelineDoc doc;
    AppUndoManager undo;
    const auto keys = doc.addTrack(TrackKind::Midi, "Keys");
    const auto pads = doc.addTrack(TrackKind::Midi, "Pads");
    MidiRecorder recorder;
    recorder.startRecording({{keys, a, 0}, {pads, b, 0}}, 0.0);

    std::atomic<int> producersDone{0};
    auto produce = [&](const juce::String& id, int pitch) {
        for (int i = 0; i < kNotesPerDevice; ++i) {
            const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
            sources.push(id, juce::MidiMessage::noteOn(1, pitch, (juce::uint8)90).withTimeStamp(t));
        }
        producersDone.fetch_add(1);
    };

    synth::BlockTimeInfo info;
    info.playing = true;
    info.sampleRate = kSampleRate;
    info.numSamples = kBlock;
    MidiInputSources::Block block;
    auto renderOnce = [&] {
        sources.drainBlock(block, juce::Time::getMillisecondCounterHiRes(), kBlock);
        recorder.captureBlock(juce::MidiBuffer(), info, {block.events.data(), block.count, 0, kBlock});
        info.startPpq += kBlock * info.beatsPerSample();
    };
    std::thread audio([&] {
        while (producersDone.load() < 2)
            renderOnce();
        renderOnce(); // whatever the producers pushed after the last drain
    });
    std::thread deviceA(produce, juce::String("dev-a"), 60);
    std::thread deviceB(produce, juce::String("dev-b"), 72);
    for (int i = 0; i < 20; ++i) { // the message thread opening more devices meanwhile
        sources.sourceFor("late-" + juce::String(i));
        EXPECT_EQ(sources.findSource("dev-b"), b);
    }
    deviceA.join();
    deviceB.join();
    audio.join();

    ASSERT_TRUE(recorder.stopAndCommit(doc, undo));
    EXPECT_FALSE(recorder.hadOverrun());
    int keysNotes = 0, padsNotes = 0;
    for (const auto& clip : doc.getTrack(keys)->clips)
        for (const auto& note : clip.notes)
            keysNotes += note.pitch == 60 ? 1 : 1000;
    for (const auto& clip : doc.getTrack(pads)->clips)
        for (const auto& note : clip.notes)
            padsNotes += note.pitch == 72 ? 1 : 1000;
    EXPECT_EQ(keysNotes, kNotesPerDevice);
    EXPECT_EQ(padsNotes, kNotesPerDevice);
}
