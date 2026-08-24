// Tests for the flattened audio-thread TimelineSnapshot and the single-atomic-pointer
// exchange that publishes it with epoch-based reclamation.
//
// Two things are being pinned here. First the flatten policy (absolute beats, notes clipped to
// their clip's window, one merged sorted run per track) — every downstream consumer reads the
// snapshot assuming those, so they are contract, not implementation. Second the reclamation
// protocol: a snapshot may only be freed once the audio epoch has advanced twice past the epoch
// observed when it was retired. StressPublishAcquire is the tripwire for that — on its own it can
// only prove "nothing crashed on this machine today", but under the ASAN CI job (label `run-asan`)
// it becomes a hard use-after-free detector, because a premature free plus a concurrent read is
// exactly what ASAN reports.
//
// Headless/deterministic house rules apply: no audio device (every engine here is
// HostMode::Hosted), no network. The one thread this file starts is time-bounded and joined.

#include "../Source/AudioEngine.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Timeline/TimelineSnapshotExchange.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <iterator>
#include <random>
#include <thread>

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::MidiNote;
using synth::TimelineDoc;
using synth::TimelineSnapshot;
using synth::TimelineSnapshotExchange;
using synth::TrackId;
using synth::TrackKind;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 44100.0;

MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

AutomationLane::RangeSnapshot makeRange(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

int liveSnapshots() { return TimelineSnapshot::liveInstanceCount().load(std::memory_order_relaxed); }

void processHostBlocks(AudioEngine& engine, int numBlocks) {
    for (int i = 0; i < numBlocks; ++i) {
        juce::AudioBuffer<float> buffer(2, kBlockSize);
        buffer.clear();
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
    }
}

} // namespace

// ============================================================================
// Flatten policy
// ============================================================================

TEST(TimelineSnapshotTest, FlattenBasics) {
    TimelineDoc doc;

    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
    const auto bass = doc.addTrack(TrackKind::Midi, "Bass");
    doc.setTrackBinding(lead, "uuid-track-in-1");
    doc.setTrackMuted(bass, true);
    doc.setTrackArmed(lead, true);

    // Clips created out of order, and clipMid deliberately OVERLAPS clipEarly: a concatenation of
    // per-clip runs would come out unsorted, so this is what proves the build really merges.
    const auto clipLate = doc.addClip(lead, 8.0, 4.0, "Late");
    const auto clipEarly = doc.addClip(lead, 0.0, 4.0, "Early");
    const auto clipMid = doc.addClip(lead, 2.0, 4.0, "Mid");

    doc.addNote(clipEarly, makeNote(2.0, 67));
    doc.addNote(clipEarly, makeNote(0.0, 60, 0.5, 90, 2));
    doc.addNote(clipEarly, makeNote(3.5, 64, 2.0)); // overhangs the clip end -> truncated to 4.0
    doc.addNote(clipEarly, makeNote(4.0, 70));      // starts AT the clip end -> dropped
    doc.addNote(clipEarly, makeNote(9.0, 71));      // starts past the clip end -> dropped
    doc.addNote(clipMid, makeNote(0.0, 62));        // absolute 2.0 -> interleaves with clipEarly
    doc.addNote(clipLate, makeNote(1.25, 72, 2.0, 1, 16));

    doc.addClip(bass, 4.0, 8.0, "Bassline");

    const auto cutoff = doc.addLane(lead, "uuid-filter", "cutoff", makeRange(20.0f, 20000.0f, 1000.0f));
    doc.addBreakpoint(cutoff, 4.0, 8000.0);
    doc.addBreakpoint(cutoff, 0.0, 500.0, 0.25f, static_cast<int>(BreakpointCurve::Hold));
    doc.addBreakpoint(cutoff, 2.0, 1234.0);

    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->selfCheck());
    EXPECT_EQ(snapshot->revision, doc.getRevision());

    ASSERT_EQ(snapshot->tracks.size(), 2u);
    const auto& leadInfo = snapshot->tracks[0];
    const auto& bassInfo = snapshot->tracks[1];

    EXPECT_EQ(leadInfo.trackId, lead.value);
    EXPECT_EQ(leadInfo.kind, static_cast<int>(TrackKind::Midi));
    EXPECT_FALSE(leadInfo.muted);
    EXPECT_TRUE(leadInfo.armed);
    EXPECT_STREQ(leadInfo.bindingUuid, "uuid-track-in-1");
    EXPECT_EQ(leadInfo.bindingUuid[std::strlen("uuid-track-in-1")], '\0');

    EXPECT_EQ(bassInfo.trackId, bass.value);
    EXPECT_TRUE(bassInfo.muted);
    EXPECT_STREQ(bassInfo.bindingUuid, "") << "an unbound track must flatten to an empty NUL-terminated string";

    // One merged, absolute-beat, sorted run for the whole track — three clips, two dropped notes,
    // one truncated note.
    struct Expected {
        double startBeat;
        double endBeat;
        int pitch;
    };
    const Expected expected[] = {
        {0.0, 0.5, 60},   // clipEarly, unchanged
        {2.0, 3.0, 67},   // clipEarly
        {2.0, 3.0, 62},   // clipMid — same beat, stable merge keeps the earlier clip first
        {3.5, 4.0, 64},   // clipEarly, truncated at the clip end (would have run to 5.5)
        {9.25, 11.25, 72} // clipLate
    };
    constexpr int kExpectedNotes = static_cast<int>(std::size(expected));

    EXPECT_EQ(leadInfo.firstNote, 0);
    ASSERT_EQ(leadInfo.numNotes, kExpectedNotes)
        << "a note starting at or after its clip's end must be dropped, not clamped";
    for (int i = 0; i < kExpectedNotes; ++i) {
        const auto& note = snapshot->notes[static_cast<std::size_t>(leadInfo.firstNote + i)];
        EXPECT_DOUBLE_EQ(note.startBeat, expected[i].startBeat) << "note " << i;
        EXPECT_DOUBLE_EQ(note.endBeat, expected[i].endBeat) << "note " << i;
        EXPECT_EQ(note.pitch, expected[i].pitch) << "note " << i;
    }
    EXPECT_EQ(snapshot->notes[0].velocity, 90);
    EXPECT_EQ(snapshot->notes[0].channel, 2);
    EXPECT_EQ(snapshot->notes[4].channel, 16);

    EXPECT_EQ(bassInfo.firstNote, kExpectedNotes) << "the next track's run starts where this one ends";
    EXPECT_EQ(bassInfo.numNotes, 0) << "a clip with no notes contributes nothing";

    // Lanes and their range snapshot.
    ASSERT_EQ(snapshot->lanes.size(), 1u);
    EXPECT_EQ(leadInfo.firstLane, 0);
    EXPECT_EQ(leadInfo.numLanes, 1);
    EXPECT_EQ(bassInfo.firstLane, 1);
    EXPECT_EQ(bassInfo.numLanes, 0);

    const auto& lane = snapshot->lanes[0];
    EXPECT_EQ(lane.laneId, cutoff.value);
    EXPECT_STREQ(lane.nodeUuid, "uuid-filter");
    EXPECT_STREQ(lane.paramId, "cutoff");
    EXPECT_FLOAT_EQ(lane.minValue, 20.0f);
    EXPECT_FLOAT_EQ(lane.maxValue, 20000.0f);
    EXPECT_FLOAT_EQ(lane.defaultValue, 1000.0f);

    ASSERT_EQ(lane.numPoints, 3);
    EXPECT_EQ(lane.firstPoint, 0);
    EXPECT_DOUBLE_EQ(snapshot->points[0].beat, 0.0);
    EXPECT_DOUBLE_EQ(snapshot->points[0].value, 500.0);
    EXPECT_FLOAT_EQ(snapshot->points[0].tension, 0.25f);
    EXPECT_EQ(snapshot->points[0].curve, static_cast<int>(BreakpointCurve::Hold));
    EXPECT_DOUBLE_EQ(snapshot->points[1].beat, 2.0);
    EXPECT_DOUBLE_EQ(snapshot->points[2].beat, 4.0);
    EXPECT_DOUBLE_EQ(snapshot->points[2].value, 8000.0);
}

TEST(TimelineSnapshotTest, OverlongStringsAreTruncatedAndTerminated) {
    // uuids are 36 chars and paramIds are short, so this never happens in practice — but the audio
    // thread strcmps these buffers, so an unterminated copy would read off the end of the struct.
    const juce::String longUuid = juce::String::repeatedString("u", 200);
    const juce::String longParam = juce::String::repeatedString("p", 200);

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    doc.setTrackBinding(track, longUuid);
    doc.addLane(track, longUuid, longParam, makeRange(0.0f, 1.0f, 0.0f));

    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_EQ(snapshot->tracks.size(), 1u);
    ASSERT_EQ(snapshot->lanes.size(), 1u);

    constexpr std::size_t kUsable = TimelineSnapshot::kMaxStringBytes - 1;
    EXPECT_EQ(std::strlen(snapshot->tracks[0].bindingUuid), kUsable);
    EXPECT_EQ(std::strlen(snapshot->lanes[0].nodeUuid), kUsable);
    EXPECT_EQ(std::strlen(snapshot->lanes[0].paramId), kUsable);
    EXPECT_EQ(snapshot->tracks[0].bindingUuid[kUsable], '\0');
    EXPECT_TRUE(snapshot->selfCheck());
}

TEST(TimelineSnapshotTest, OverlongAssetRefIsDroppedNotTruncated) {
    // A truncated assetRef would point the streamer at a different, wrong bundle-relative path
    // rather than merely failing to match — so an assetRef too long to fit is dropped entirely
    // (the clip becomes inert, same contract as an empty assetRef) rather than silently cut down
    // to kMaxAssetRefBytes - 1 bytes.
    const juce::String longAssetRef =
        "Audio/" + juce::String::repeatedString("a", (size_t)TimelineSnapshot::kMaxAssetRefBytes) + ".wav";
    ASSERT_GE(longAssetRef.getNumBytesAsUTF8(), (size_t)TimelineSnapshot::kMaxAssetRefBytes);

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Audio, "A");
    const auto clip = doc.addClip(track, 0.0, 4.0, "clip");
    ASSERT_TRUE(doc.setClipAsset(clip, longAssetRef, 0.0));

    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_EQ(snapshot->audioClips.size(), 1u);
    EXPECT_STREQ(snapshot->audioClips[0].assetRef, "") << "over-long assetRef must be dropped, not truncated";
    EXPECT_TRUE(snapshot->selfCheck());
}

// ============================================================================
// Flatten policy: mute
// ============================================================================
// Mute is resolved HERE, once, rather than by each consumer. These two tests are the contract
// that lets TimelineMidiSource and the AudioClipStreamer's assignment table stay unaware that
// mute exists: muted content is simply absent from the snapshot they read.

TEST(TimelineSnapshotTest, MutedClipsAndNotesAreExcludedFromTheFlatten) {
    TimelineDoc doc;
    const auto lead = doc.addTrack(TrackKind::Midi, "Lead");

    const auto audible = doc.addClip(lead, 0.0, 4.0, "Audible");
    doc.addNote(audible, makeNote(0.0, 60));
    const auto silentNote = doc.addNote(audible, makeNote(1.0, 62));
    doc.addNote(audible, makeNote(2.0, 64));

    const auto silentClip = doc.addClip(lead, 8.0, 4.0, "Silent");
    doc.addNote(silentClip, makeNote(0.0, 70));
    doc.addNote(silentClip, makeNote(1.0, 71));

    ASSERT_TRUE(doc.setClipMuted(silentClip, true));
    ASSERT_TRUE(doc.setNoteMuted(silentNote, true));

    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->selfCheck());
    ASSERT_EQ(snapshot->tracks.size(), 1u);

    // Two notes survive: the muted clip contributes nothing at all, and the muted note is gone
    // from the run rather than emitted at velocity 0 or with a zero length.
    ASSERT_EQ(snapshot->tracks[0].numNotes, 2)
        << "a muted clip must contribute no notes, and a muted note must be skipped";
    EXPECT_EQ(snapshot->notes[0].pitch, 60);
    EXPECT_EQ(snapshot->notes[1].pitch, 64);
    for (const auto& note : snapshot->notes)
        EXPECT_LT(note.startBeat, 8.0) << "no event may come from the muted clip";

    // Un-muting restores exactly what was hidden — mute never destroyed anything.
    ASSERT_TRUE(doc.setClipMuted(silentClip, false));
    ASSERT_TRUE(doc.setNoteMuted(silentNote, false));
    const auto restored = TimelineSnapshot::buildFrom(doc);
    EXPECT_TRUE(restored->selfCheck());
    ASSERT_EQ(restored->tracks[0].numNotes, 5);
    EXPECT_EQ(restored->notes[1].pitch, 62) << "the previously muted note is back in sorted position";
    EXPECT_DOUBLE_EQ(restored->notes[3].startBeat, 8.0);
}

TEST(TimelineSnapshotTest, MutedAudioClipGetsNoAudioClipInfo) {
    // No AudioClipInfo means the streamer never even assigns the clip a stream, so a muted take
    // costs no ring and touches no file — the exclusion is cheaper than a gain of zero would be.
    TimelineDoc doc;
    const auto takes = doc.addTrack(TrackKind::Audio, "Takes");

    const auto audible = doc.addClip(takes, 0.0, 4.0, "Keeper");
    ASSERT_TRUE(doc.setClipAsset(audible, "Audio/keeper.wav", 0.0));
    const auto silent = doc.addClip(takes, 8.0, 4.0, "Reject");
    ASSERT_TRUE(doc.setClipAsset(silent, "Audio/reject.wav", 0.0));
    ASSERT_TRUE(doc.setClipMuted(silent, true));

    const auto snapshot = TimelineSnapshot::buildFrom(doc);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->selfCheck());
    ASSERT_EQ(snapshot->audioClips.size(), 1u) << "a muted audio clip must not reach the snapshot at all";
    EXPECT_EQ(snapshot->tracks[0].numAudioClips, 1);
    EXPECT_EQ(snapshot->audioClips[0].clipId, audible.value);
    EXPECT_STREQ(snapshot->audioClips[0].assetRef, "Audio/keeper.wav");

    ASSERT_TRUE(doc.setClipMuted(silent, false));
    const auto restored = TimelineSnapshot::buildFrom(doc);
    EXPECT_TRUE(restored->selfCheck());
    ASSERT_EQ(restored->audioClips.size(), 2u);
    EXPECT_EQ(restored->tracks[0].numAudioClips, 2);
    EXPECT_STREQ(restored->audioClips[1].assetRef, "Audio/reject.wav");
}

// ============================================================================
// Exchange: publication
// ============================================================================

TEST(TimelineSnapshotTest, EmptyDocAndNeverNull) {
    TimelineSnapshotExchange exchange;

    const auto& beforeAnyPublish = exchange.beginAudioBlock();
    EXPECT_EQ(&beforeAnyPublish, &TimelineSnapshotExchange::emptySnapshot())
        << "an exchange with nothing published must hand out the static empty snapshot, never null";
    EXPECT_TRUE(beforeAnyPublish.tracks.empty());
    EXPECT_EQ(beforeAnyPublish.revision, 0);
    EXPECT_TRUE(beforeAnyPublish.selfCheck());

    TimelineDoc emptyDoc;
    auto snapshot = TimelineSnapshot::buildFrom(emptyDoc);
    const auto* raw = snapshot.get();
    exchange.publish(std::move(snapshot));

    const auto& afterPublish = exchange.beginAudioBlock();
    EXPECT_EQ(&afterPublish, raw);
    EXPECT_TRUE(afterPublish.tracks.empty());
    EXPECT_TRUE(afterPublish.selfCheck());
}

TEST(TimelineSnapshotTest, PublishSwap) {
    TimelineSnapshotExchange exchange;

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "A");
    auto first = TimelineSnapshot::buildFrom(doc);
    const auto* firstRaw = first.get();
    const auto firstRevision = first->revision;
    exchange.publish(std::move(first));

    EXPECT_EQ(&exchange.beginAudioBlock(), firstRaw);

    doc.addTrack(TrackKind::Midi, "B");
    auto second = TimelineSnapshot::buildFrom(doc);
    const auto* secondRaw = second.get();
    exchange.publish(std::move(second));

    const auto& current = exchange.beginAudioBlock();
    EXPECT_EQ(&current, secondRaw) << "the next block must see the newly published snapshot";
    EXPECT_EQ(current.tracks.size(), 2u);
    EXPECT_GT(current.revision, firstRevision);
}

// ============================================================================
// Exchange: epoch reclamation
// ============================================================================

TEST(TimelineSnapshotTest, ReclamationWaitsForTwoEpochs) {
    TimelineSnapshotExchange exchange;
    const int baseline = liveSnapshots(); // after construction: the static empty snapshot exists

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "A");

    exchange.publish(TimelineSnapshot::buildFrom(doc));
    ASSERT_EQ(liveSnapshots(), baseline + 1);

    exchange.beginAudioBlock(); // epoch 1
    ASSERT_EQ(exchange.getAudioEpoch(), 1);

    exchange.publish(TimelineSnapshot::buildFrom(doc)); // retires A, tagged at epoch 1
    EXPECT_EQ(exchange.retiredCount(), 1);
    EXPECT_EQ(liveSnapshots(), baseline + 2)
        << "the displaced snapshot must NOT be freed while the epoch is still where it was retired "
           "— a callback that loaded it may not have finished its block";

    exchange.beginAudioBlock(); // epoch 2
    exchange.beginAudioBlock(); // epoch 3
    ASSERT_EQ(exchange.getAudioEpoch(), 3);

    exchange.reap();
    EXPECT_EQ(exchange.retiredCount(), 0);
    EXPECT_EQ(liveSnapshots(), baseline + 1) << "two epochs past the retire observation, the retiree must be freed";
}

TEST(TimelineSnapshotTest, RetireeSurvivesWhileAudioIsIdle) {
    // The idle-audio caveat: with the device stopped the epoch never advances, so nothing is
    // reclaimable however many times publish() or reap() runs.
    TimelineSnapshotExchange exchange;
    const int baseline = liveSnapshots();

    TimelineDoc doc;
    exchange.publish(TimelineSnapshot::buildFrom(doc));
    exchange.publish(TimelineSnapshot::buildFrom(doc));

    EXPECT_EQ(exchange.retiredCount(), 1);
    for (int i = 0; i < 5; ++i)
        exchange.reap();
    EXPECT_EQ(exchange.retiredCount(), 1) << "no epoch advance means no reclamation, ever";
    EXPECT_EQ(liveSnapshots(), baseline + 2);
}

TEST(TimelineSnapshotTest, ReclaimAllUnsafeFreesCurrentAndRetirees) {
    const int baseline = [] {
        TimelineSnapshotExchange warmUp; // makes sure the static empty snapshot is already counted
        return liveSnapshots();
    }();

    TimelineDoc doc;
    doc.addTrack(TrackKind::Midi, "A");

    {
        TimelineSnapshotExchange exchange;
        exchange.publish(TimelineSnapshot::buildFrom(doc));
        exchange.beginAudioBlock();
        exchange.publish(TimelineSnapshot::buildFrom(doc));
        ASSERT_EQ(exchange.retiredCount(), 1);
        ASSERT_EQ(liveSnapshots(), baseline + 2);

        exchange.reclaimAllUnsafe();
        EXPECT_EQ(exchange.retiredCount(), 0);
        EXPECT_EQ(liveSnapshots(), baseline) << "reclaimAllUnsafe must free the current snapshot too";

        EXPECT_EQ(&exchange.beginAudioBlock(), &TimelineSnapshotExchange::emptySnapshot())
            << "after reclaiming everything the exchange falls back to the empty snapshot";

        // And it stays usable afterwards, so shutdown ordering mistakes don't corrupt anything.
        exchange.publish(TimelineSnapshot::buildFrom(doc));
        ASSERT_EQ(liveSnapshots(), baseline + 1);
    }

    EXPECT_EQ(liveSnapshots(), baseline) << "the destructor must reclaim everything still held";
}

// ============================================================================
// Stress: the use-after-free tripwire
// ============================================================================

TEST(TimelineSnapshotTest, StressPublishAcquire) {
    // Time-bounded so the suite stays fast. On its own this proves little; run under the ASAN CI
    // job (PR label `run-asan`) it is a hard detector — freeing a snapshot one epoch too early
    // while the reader thread is walking it is exactly the use-after-free ASAN reports.
    constexpr auto kDuration = std::chrono::milliseconds(1200);

    TimelineSnapshotExchange exchange;
    const int baseline = liveSnapshots();

    std::atomic<bool> stop{false};
    std::atomic<int> inconsistencies{0};
    std::atomic<std::int64_t> blocksRead{0};
    std::atomic<double> sink{0.0};

    std::thread audioThread([&] {
        std::mt19937 rng{20250815u};
        double localSink = 0.0;
        std::int64_t blocks = 0;

        while (!stop.load(std::memory_order_relaxed)) {
            const auto& snapshot = exchange.beginAudioBlock();
            ++blocks;

            if (!snapshot.selfCheck())
                inconsistencies.fetch_add(1, std::memory_order_relaxed);

            if (!snapshot.tracks.empty()) {
                const auto& track = snapshot.tracks[rng() % snapshot.tracks.size()];

                // Actually touch every byte the track points at — a stale snapshot is only caught
                // if the reader reads it.
                for (int i = track.firstNote; i < track.firstNote + track.numNotes; ++i) {
                    const auto& note = snapshot.notes[static_cast<std::size_t>(i)];
                    if (!(note.endBeat > note.startBeat) || note.pitch < 0 || note.pitch > 127)
                        inconsistencies.fetch_add(1, std::memory_order_relaxed);
                    localSink += note.startBeat + note.endBeat + note.pitch;
                }

                for (int l = track.firstLane; l < track.firstLane + track.numLanes; ++l) {
                    const auto& lane = snapshot.lanes[static_cast<std::size_t>(l)];
                    if (std::strlen(lane.nodeUuid) >= static_cast<std::size_t>(TimelineSnapshot::kMaxStringBytes))
                        inconsistencies.fetch_add(1, std::memory_order_relaxed);
                    for (int p = lane.firstPoint; p < lane.firstPoint + lane.numPoints; ++p)
                        localSink += snapshot.points[static_cast<std::size_t>(p)].value;
                }

                localSink += static_cast<double>(std::strlen(track.bindingUuid));
            }
        }

        sink.store(localSink, std::memory_order_relaxed);
        blocksRead.store(blocks, std::memory_order_relaxed);
    });

    TimelineDoc doc;
    TrackId lead{};
    int publishes = 0;
    const auto deadline = std::chrono::steady_clock::now() + kDuration;

    for (int cycle = 0; std::chrono::steady_clock::now() < deadline; ++cycle) {
        // Varying sizes, including empty: phase 0 publishes an empty doc, phase 1 rebuilds the
        // tracks, the rest grow the doc a clip at a time.
        const int phase = cycle % 40;
        if (phase == 0) {
            doc.clear();
        } else if (phase == 1) {
            lead = doc.addTrack(TrackKind::Midi, "Lead");
            doc.setTrackBinding(lead, "uuid-track-in-stress");
            doc.addTrack(TrackKind::Midi, "Bass");
            const auto lane = doc.addLane(lead, "uuid-filter", "cutoff", makeRange(0.0f, 1.0f, 0.5f));
            for (int i = 0; i < 8; ++i)
                doc.addBreakpoint(lane, i * 0.5, i * 0.1);
        } else {
            const auto clip = doc.addClip(lead, phase * 2.0, 4.0, "C");
            for (int i = 0; i < 6; ++i)
                doc.addNote(clip, makeNote(i * 0.75, 48 + i, 1.5));
        }

        exchange.publish(TimelineSnapshot::buildFrom(doc));
        exchange.reap();
        ++publishes;
    }

    stop.store(true, std::memory_order_relaxed);
    audioThread.join();

    EXPECT_EQ(inconsistencies.load(), 0) << "the audio thread must never observe a torn or freed snapshot";
    EXPECT_GT(publishes, 100) << "sanity: the publisher must have actually churned";
    EXPECT_GT(blocksRead.load(), 0) << "sanity: the reader thread must have actually run";
    EXPECT_TRUE(std::isfinite(sink.load())); // keeps the reads above from being optimised away

    exchange.reclaimAllUnsafe();
    EXPECT_EQ(liveSnapshots(), baseline) << "every published snapshot must eventually be freed";
}

// ============================================================================
// Engine integration — the epoch ticks once per rendered block
// ============================================================================

TEST(TimelineSnapshotTest, EngineEpochAdvancesPerBlock) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);

    auto& exchange = engine.getTimelineSnapshots();
    const int baseline = liveSnapshots();

    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "A");
    doc.addNote(clip, makeNote(0.0, 60));

    exchange.publish(TimelineSnapshot::buildFrom(doc));
    ASSERT_EQ(liveSnapshots(), baseline + 1);

    processHostBlocks(engine, 3);
    EXPECT_EQ(exchange.getAudioEpoch(), 3) << "renderNextBlock must open exactly one timeline block per callback";

    doc.addNote(clip, makeNote(1.0, 64));
    exchange.publish(TimelineSnapshot::buildFrom(doc)); // retires the first snapshot, tagged at epoch 3
    ASSERT_EQ(exchange.retiredCount(), 1);
    ASSERT_EQ(liveSnapshots(), baseline + 2);

    processHostBlocks(engine, 2);
    EXPECT_EQ(exchange.getAudioEpoch(), 5);

    exchange.reap();
    EXPECT_EQ(exchange.retiredCount(), 0);
    EXPECT_EQ(liveSnapshots(), baseline + 1)
        << "the retiree being freed is what proves rendered blocks advanced the reclamation epoch";

    engine.releaseFromHost();
    engine.shutdown();
    EXPECT_EQ(liveSnapshots(), baseline) << "shutdown must reclaim the published snapshot";
}
