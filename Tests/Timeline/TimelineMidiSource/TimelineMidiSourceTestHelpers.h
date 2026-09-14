#pragma once

// Shared fixture and helpers for the "Track In" module (TimelineMidiSourceModule) test suite
// (Tests/Timeline/TimelineMidiSource/TimelineMidiSource*Tests.cpp) — the graph-side end of a
// timeline MIDI track.
//
// The module is a PULL consumer: nothing schedules events into it, so every test drives it the
// same way the real audio callback does — tick the transport, park the block's snapshot on it
// (TransportService::setCurrentTimelineSnapshot), call processBlock, read the
// MIDI buffer. Most of the suite needs no engine and no graph at all, which is what keeps the
// timing assertions exact.
//
// Timing arithmetic used throughout: 48 kHz, 512-sample blocks, 120 BPM => 24000 samples per beat.
// Beat 1.0 is sample 24000, which is block 46 (46 * 512 = 23552) at offset 448.
//
// Headless/deterministic house rules apply: no audio device (the one engine used is
// HostMode::Hosted), no network, no sleeps.
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Transport/TransportService.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TrackKind;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kSamplesPerBeat = 24000; // 120 BPM at 48 kHz
constexpr const char* kMyUuid = "11111111-2222-3333-4444-555555555555";

// The block a given absolute beat lands in, and the offset within it.
inline int blockOfBeat(double beat) { return (int)((beat * kSamplesPerBeat) / kBlock); }
inline int offsetOfBeat(double beat) { return (int)((juce::int64)(beat * kSamplesPerBeat) % kBlock); }

struct Event {
    int sample = 0;
    bool isNoteOn = false;
    int pitch = 0;
    int velocity = 0;
    int channel = 0;
    // The module's contract is "note-ons and note-offs, nothing else" — in particular never a
    // blanket all-notes-off (CC 123), which would silence other sources sharing the MIDI cable.
    bool isNoteOff = false;
    bool isOther = false;
    juce::String description;
};

inline std::vector<Event> eventsOf(const juce::MidiBuffer& buffer) {
    std::vector<Event> events;
    for (const auto metadata : buffer) {
        const auto message = metadata.getMessage();
        Event e;
        e.sample = metadata.samplePosition;
        e.isNoteOn = message.isNoteOn();
        e.pitch = message.getNoteNumber();
        e.velocity = message.getVelocity();
        e.channel = message.getChannel();
        e.isNoteOff = message.isNoteOff();
        e.isOther = !e.isNoteOn && !e.isNoteOff;
        e.description = message.getDescription();
        events.push_back(e);
    }
    return events;
}

// Drives one module exactly as AudioEngine::renderNextBlock does, minus the graph.
struct Harness {
    synth::TransportService transport;
    TimelineMidiSourceModule module;
    juce::AudioBuffer<float> buffer{1, kBlock};
    juce::MidiBuffer midi;

    explicit Harness(const char* uuid = kMyUuid) {
        transport.prepare(kSampleRate, kBlock);
        module.setPlayHead(&transport);
        module.prepareToPlay(kSampleRate, kBlock);
        if (uuid != nullptr)
            module.setNodeUuid(uuid);
    }

    std::vector<Event> renderBlock(const TimelineSnapshot* snapshot, int numSamples = kBlock) {
        transport.tick(numSamples);
        transport.setCurrentTimelineSnapshot(snapshot);
        if (buffer.getNumSamples() != numSamples)
            buffer.setSize(1, numSamples, false, false, false);
        buffer.clear();
        midi.clear();
        module.processBlock(buffer, midi);
        return eventsOf(midi);
    }

    // The transport sample position this block started at — the block info survives processBlock,
    // so a test can turn a block-relative offset into an absolute (== loop-relative, when the loop
    // starts at 0) sample position.
    std::int64_t blockStartSample() const { return transport.getCurrentBlockInfo().blockStartSample; }
    int loopWrapSample() const { return transport.getCurrentBlockInfo().loopWrapSample; }

    // Renders `numBlocks` blocks, returning only those that produced events, tagged with the
    // 0-based block index — so a failure says WHICH block was wrong, not just that one was.
    std::vector<std::pair<int, std::vector<Event>>> renderBlocks(const TimelineSnapshot* snapshot, int numBlocks,
                                                                 int firstBlockIndex = 0) {
        std::vector<std::pair<int, std::vector<Event>>> out;
        for (int i = 0; i < numBlocks; ++i) {
            auto events = renderBlock(snapshot);
            if (!events.empty())
                out.emplace_back(firstBlockIndex + i, std::move(events));
        }
        return out;
    }
};

inline MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

// A one-track document bound to `uuid`, with one clip (16 beats by default) starting at 0.
struct Doc {
    TimelineDoc doc;
    synth::TrackId trackId;
    synth::ClipId clipId;

    explicit Doc(const char* uuid = kMyUuid, double clipLengthBeats = 16.0) {
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        doc.setTrackBinding(trackId, uuid);
        clipId = doc.addClip(trackId, 0.0, clipLengthBeats, "Clip");
    }

    std::unique_ptr<TimelineSnapshot> snapshot() const { return TimelineSnapshot::buildFrom(doc); }
};

// ---- helpers ----------------------------------------------------------------------------------

// One emitted event plus the block it came out of. A wrapping block splits into two beat ranges, so
// "which block, and where was that block on the transport" is what turns an offset into a musical
// position a test can assert on.
struct BlockEvent {
    int blockIndex = 0;
    std::int64_t blockStart = 0;
    int wrapSample = -1;
    Event event;

    // Absolute transport sample of the event. Only meaningful for events in the block's PRIMARY
    // range — past the wrap the block's samples belong to the next loop pass.
    std::int64_t absSample() const { return blockStart + event.sample; }
};

inline std::vector<BlockEvent> renderCollect(Harness& h, const TimelineSnapshot* snapshot, int numBlocks,
                                             int numSamples = kBlock) {
    std::vector<BlockEvent> out;
    for (int i = 0; i < numBlocks; ++i) {
        const auto events = h.renderBlock(snapshot, numSamples);
        for (const auto& e : events)
            out.push_back({i, h.blockStartSample(), h.loopWrapSample(), e});
    }
    return out;
}

// A model of what the emitted stream says is sounding, and the three invariants that make the
// module's promise checkable from the outside: nothing but note-ons and note-offs is ever emitted,
// a note-on for a key already down means we never released it, and a note-off for a key that is up
// means we emitted an off we never owed.
struct HeldKeys {
    std::map<std::pair<int, int>, int> down; // (channel, pitch) -> 0 or 1
    int ons = 0;
    int offs = 0;
    juce::String firstFailure;

    void apply(const Event& e, const juce::String& where) {
        if (e.isOther) {
            fail(where + ": emitted a non-note message (" + e.description +
                 ") — this source sends per-note offs only, never CC 123");
            return;
        }

        const auto key = std::make_pair(e.channel, e.pitch);
        const juce::String what = where + ": ch" + juce::String(e.channel) + " pitch " + juce::String(e.pitch);
        if (e.isNoteOn) {
            ++ons;
            if (down[key] != 0)
                fail(what + " note-on while it is already sounding (no release in between)");
            down[key] = 1;
        } else {
            ++offs;
            if (down[key] != 1)
                fail(what + " note-off for a key that is not sounding");
            down[key] = 0;
        }
    }

    int numDown() const {
        int n = 0;
        for (const auto& entry : down)
            n += entry.second;
        return n;
    }

    void fail(const juce::String& message) {
        if (firstFailure.isEmpty())
            firstFailure = message;
    }
};
