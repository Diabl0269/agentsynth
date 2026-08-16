#pragma once

#include "TimelineSnapshot.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace synth {

/**
 * @brief The engine-owned DISK-STREAMING service behind "Track Audio" clip playback.
 *
 * One shared prefetch thread keeps a per-clip ring filled ahead of the playhead; the audio thread
 * only ever copies out of those rings. Nothing here ever holds a whole file: a ten-minute take
 * costs ONE ring — `kRingFrames` stereo frames, 1 MiB — not 100 MB. That is the opposite of
 * `SamplerModule`'s deliberate whole-file-in-RAM model (right for a one-shot a voice retriggers
 * from arbitrary offsets, wrong for a timeline that may hold hours of takes).
 *
 * -- The three threads --
 *   AUDIO THREAD    `acquire()` + `readFrames()`, and nothing else. Both are lock-free, allocation-
 *                   free and file-I/O-free; the ring is the ONLY memory they touch, and every miss
 *                   (no stream, not filled yet, seeked away) is answered with SILENCE, never a
 *                   block and never stale bytes. `readFrames` also stores the frame it wanted into
 *                   the stream's `wantedFrame`, the prefetch thread's only steering signal — there
 *                   is no "seek" call, the reader simply notices where playback went.
 *   PREFETCH THREAD one `juce::TimeSliceThread` client for the WHOLE service (not one per clip). It
 *                   owns every `juce::AudioFormatReader` exclusively — opening, reading and closing
 *                   all happen here — tops each ring up towards `kPrefetchAheadFrames` ahead of
 *                   `wantedFrame`, and repositions a reader when playback jumped somewhere the ring
 *                   cannot serve.
 *   MESSAGE THREAD  `setAssetRoots()` and `syncToSnapshot()`. Neither touches a reader, a ring or a
 *                   window atomic: `syncToSnapshot` writes an ASSIGNMENT TABLE (which clip belongs
 *                   in which pool slot, and the already-resolved `juce::File` for it) under a plain
 *                   lock, and the prefetch thread picks the change up on its next slice — which is
 *                   what makes "never delete a reader the prefetch thread may be using" true by
 *                   construction, since the message thread never owns one.
 *
 * -- The ring: a sliding window, not a FIFO --
 * A clip's ring is `kRingFrames` INTERLEAVED stereo frames, and the slot for source frame `f` is
 * fixed at `f & (kRingFrames - 1)`. Two atomics describe what is currently readable:
 *     ringStartSourceFrame   first source frame the ring can serve
 *     ringEndSourceFrame     one past the last source frame the ring can serve
 * A FIFO is deliberately NOT used here: a FIFO's read cursor is owned by the consumer, so
 * repositioning after a locate would need BOTH threads to mutate the same cursor. With a fixed slot
 * mapping the prefetch thread is the sole writer of both window atomics and the audio thread
 * mutates nothing at all, so a seek is two ordinary release stores.
 *
 * **Fill ordering (the load-bearing part).** To append frames `[end, end + k)` the prefetch thread:
 *   1. computes `newStart = max(start, end + k - kRingFrames)` — the oldest frame that will still
 *      be intact once those `k` slots are overwritten;
 *   2. `ringStartSourceFrame.store(newStart, release)` — **the window shrinks BEFORE a single byte
 *      is overwritten**, so a frame the audio thread may still read is never silently clobbered;
 *   3. writes the slots;
 *   4. `ringEndSourceFrame.store(end + k, release)` — **frames are marked valid LAST**, so a reader
 *      that has seen the new end has, by release/acquire, also seen the samples.
 * A REPOSITION (seek) is the same shape with the window collapsed first: `ringEnd` is stored at the
 * new position (which empties the window), then `ringStart`, and only then does filling begin.
 * Invalidate, reposition, fill — in that order, always.
 *
 * The prefetch thread never fills past `wantedFrame + kPrefetchAheadFrames` (3/4 of the ring), so
 * step 1 can never move `newStart` past `wanted - kRingFrames/4`: the quarter-ring of history
 * behind the playhead is structurally safe. `readFrames` still re-reads both window atomics AFTER
 * copying and zeroes its output if the window moved under it, so "silence, never garbage" holds
 * even against timing.
 *
 * -- Sample rate: the v1 honest simplification --
 * Source frames are FILE frames, and this build does **not** resample. A file whose header rate
 * differs from the engine's is played at the ENGINE's rate, i.e. transposed. Record-taps are
 * written at the session rate so they are always correct; an imported file may not be.
 * `getStreamFileSampleRate()` exposes what a stream actually opened, for a UI warning later.
 * TODO(resample): a proper SRC belongs in the prefetch thread's fill step.
 *
 * -- Pool cap policy --
 * `kMaxStreams` clips can stream at once. Beyond that, `syncToSnapshot` keeps clips that ALREADY
 * have a stream and fills whatever slots are left in DOCUMENT ORDER (track order, then clip start
 * order); the remainder get no stream and render silence. Not playhead-aware — that needs a
 * position the message thread does not have at publish time. Silent, not logged: this runs on
 * every doc edit, and a log here would be the per-edit spam CLAUDE.md forbids.
 *
 * -- Determinism for tests --
 * `setPrefetchPausedForTest(true)` stops the service from ever starting its thread, and
 * `pumpForTest()` then runs slices synchronously on the calling thread until every stream is fully
 * caught up. Tests therefore have NO sleeps and no waits: publish, pump, render, assert.
 */
class AudioClipStreamer {
public:
    /** Clips that can stream at once. 32 stereo rings is 32 MiB *if every slot is in use* — and
     *  rings are allocated lazily, per slot, on first use, so a session with three audio clips
     *  costs three of them and an engine with no timeline costs none. */
    static constexpr int kMaxStreams = 32;

    /** Frames of ring per stream. 1 << 17 = 131072 frames is ~2.7 s at 48 kHz and 1 MiB of stereo
     *  float storage — the same sizing (and the same reasoning) as `RecordTapModule`'s capture ring:
     *  the background thread may be descheduled for seconds without a dropout. A POWER OF TWO is
     *  required, not just convenient: the slot mapping is `frame & (kRingFrames - 1)`. */
    static constexpr int kRingFrames = 1 << 17;
    static_assert((kRingFrames & (kRingFrames - 1)) == 0, "kRingFrames must be a power of two");

    /** How far ahead of `wantedFrame` the prefetch thread tops a ring up: 3/4 of the ring. The
     *  remaining quarter is history behind the playhead that the fill step can never clobber — see
     *  the class comment's fill-ordering argument. */
    static constexpr int kPrefetchAheadFrames = (kRingFrames / 4) * 3;

    /** Frames moved per stream per slice. Bounds one slice's work and the fill scratch. */
    static constexpr int kFillChunkFrames = 16384;

    /** Interleaved channels per ring frame. Fixed: "Track Audio" is a stereo node. */
    static constexpr int kNumChannels = 2;

    /** The "this slot is free" clip id. Doc ids start at 1, so 0 is never a real clip. */
    static constexpr std::int64_t kNoClip = 0;

    /** The reserved asset-ref prefix given to takes recorded before a project has ever been saved.
     *  See `synth::ProjectBundle`'s asset policy — such a ref resolves against app data rather
     *  than a bundle root, and is the one case `bundleRoot` does not cover. */
    static constexpr const char* kRecordingsRefPrefix = "Recordings/";

    /**
     * @brief One pool slot, as the audio thread sees it. Opaque by intent: the only legal thing to
     *        do with a handle is pass it back to `readFrames`.
     */
    struct StreamHandle {
        // The clip this slot serves, or kNoClip. Written ONLY by the prefetch thread, and it is the
        // slot's publication token: a release store here publishes the ring pointer, the open
        // reader and the collapsed window that were set up before it.
        std::atomic<std::int64_t> clipId{kNoClip};

        // The ring, allocated once (lazily, by the prefetch thread) and never freed or moved. Null
        // until this slot has been used at least once.
        std::atomic<float*> ringData{nullptr};

        // The readable window, in SOURCE FRAMES. Written only by the prefetch thread. See the class
        // comment for the ordering these two are stored in.
        std::atomic<std::int64_t> ringStartSourceFrame{0};
        std::atomic<std::int64_t> ringEndSourceFrame{0};

        // The frame the audio thread asked for most recently — the prefetch thread's steering
        // signal. `mutable` because the audio thread writes it through a `const StreamHandle*`:
        // conceptually it is telling the service where playback is, not mutating the stream.
        mutable std::atomic<std::int64_t> wantedFrame{0};

        // True once a reader is open for `clipId`. False for an assigned-but-unopenable slot, which
        // then renders silence exactly like an unassigned one.
        std::atomic<bool> ready{false};

        // The opened file's header sample rate (0 until open). Diagnostics only — see the class
        // comment's sample-rate note; nothing in the playback path reads it.
        std::atomic<double> fileSampleRate{0.0};
    };

    AudioClipStreamer();
    ~AudioClipStreamer();

    // ---- Message thread ----

    /** Where asset refs resolve. Either file may be invalid, which simply makes the refs that would
     *  have resolved against it unresolvable (and therefore silent — a separate "missing media"
     *  placeholder owns the UI for that). `bundleRoot` is the `.agsproj` directory; `recordingsRoot`
     *  is the `<app data>/<settings folder>/Recordings` directory unsaved-project takes are written
     *  into. Safe to call at any time; it affects the NEXT `syncToSnapshot`. */
    void setAssetRoots(const juce::File& bundleRoot, const juce::File& recordingsRoot);

    juce::File getBundleRoot() const;
    juce::File getRecordingsRoot() const;

    /** Opens / keeps / closes streams so that every audio clip in `snapshot` that resolves to a
     *  real file has one, up to `kMaxStreams` (see the class comment's pool-cap policy). Called by
     *  `AudioEngine::publishTimeline` with the SAME snapshot object it publishes, so the assignment
     *  can never describe a clip set the audio thread is not also about to see.
     *
     *  Nothing here opens a file: it resolves refs (which needs the roots, which live on this
     *  thread) and hands the resulting `juce::File`s to the prefetch thread. */
    void syncToSnapshot(const TimelineSnapshot& snapshot);

    /** Frees every reader and stops the prefetch thread. Called from `AudioEngine::shutdown()`,
     *  with the same precondition `TimelineSnapshotExchange::reclaimAllUnsafe()` has: nothing may
     *  be rendering. Idempotent. */
    void releaseAll();

    // ---- Prepare-path, NOT the audio thread and NOT the prefetch thread ----

    /** Called from `AudioEngine::handleStreamFormatChange` (`audioDeviceAboutToStart` /
     *  `prepareForHost`) whenever the engine's sample rate or block size changes. Every ring's
     *  content was filled to serve the OLD sample-rate-to-source-frame mapping
     *  (`TimelineAudioSourceModule::renderClip` recomputes `sourceFrame` from the engine's CURRENT
     *  sample rate every block, so a rate change is a discontinuity in that mapping, not a gentle
     *  drift). A miss self-heals already (see the class comment), but a miss is not guaranteed: the
     *  new mapping could coincidentally land inside the OLD window's still-valid range, which would
     *  read back real PCM samples at the WRONG position — wrong content, not silence. This call
     *  forces every read to be a miss (silence) until the prefetch thread has collapsed and refilled
     *  each ring at the new mapping, and wakes the prefetch thread immediately (rather than waiting
     *  for its next scheduled nap) so that refill starts as soon as possible.
     *
     *  Thread-safe to call from any non-audio, non-prefetch thread: it only sets an atomic flag the
     *  prefetch thread consumes on its own next slice (never touches ringData/windowStart/windowEnd
     *  directly, which are prefetch-thread-private) and the audio thread gates `readFrames` on
     *  directly (so the silence guarantee holds even before the prefetch thread has caught up). Safe
     *  (and a no-op the next slice) to call with no streams open. */
    void invalidateAllStreams();

    // ---- Audio thread (lock-free, allocation-free, never blocks) ----

    /** The slot currently serving `clipId`, or null. 32 acquire-loads; no allocation, no locks. */
    const StreamHandle* acquire(std::int64_t clipId) const noexcept;

    /** Fills `left`/`right` with `numFrames` frames starting at `sourceFrame`, taking whatever the
     *  ring can serve and ZEROING everything else (before the window, past it, or not filled yet).
     *  Also publishes `sourceFrame` as the stream's `wantedFrame`, which is how the prefetch thread
     *  learns that playback moved. Returns how many frames actually came from the ring — 0 means
     *  the caller was handed silence, which is a legitimate steady state right after a locate.
     *
     *  Never blocks, never allocates, never touches a file. A null handle zero-fills. */
    int readFrames(const StreamHandle* handle, std::int64_t sourceFrame, float* left, float* right,
                   int numFrames) const noexcept;

    // ---- Introspection / tests ----

    /** Pool slots currently assigned to a clip (whether or not their reader opened). */
    int getActiveStreamCount() const noexcept;

    /** True when `clipId` has a slot AND its reader is open. */
    bool isClipReady(std::int64_t clipId) const noexcept;

    /** The header sample rate of the file `clipId` streams from, or 0. See the class comment's
     *  sample-rate note — this is what a "this file is 44.1 kHz in a 48 kHz session" warning would
     *  read; nothing in the playback path consults it. */
    double getStreamFileSampleRate(std::int64_t clipId) const noexcept;

    /** Frames of audio this service holds resident for `clipId`: `kRingFrames` once its ring has
     *  been allocated, 0 before that — and NEVER the file's length, however long the take is. The
     *  accessor `AudioClipPlaybackTest.RamStaysBounded` asserts against. */
    int getResidentFramesForClip(std::int64_t clipId) const noexcept;

    /** Bytes of ring storage allocated across the whole pool. Grows only when a slot is used for
     *  the first time, and never shrinks. */
    std::size_t getTotalResidentBytes() const noexcept;

    /** Resolves a bundle-relative asset ref exactly as `syncToSnapshot` does — an invalid
     *  `juce::File` when the ref is empty, malformed, escaping, outside its root, or missing.
     *  Exposed so the escaping-ref refusal can be pinned directly. */
    juce::File resolveAssetRef(const juce::String& ref) const;

    /** TEST HOOK. When paused, the service never starts its prefetch thread (and stops it if it was
     *  running), so `pumpForTest()` is the only thing that ever fills a ring. Must be called before
     *  the first `syncToSnapshot` for a fully deterministic test. */
    void setPrefetchPausedForTest(bool paused);

    /** TEST HOOK. Runs prefetch slices synchronously on the CALLING thread until nothing is left to
     *  do (bounded), and returns the number of slices it ran. Deterministic by construction: with
     *  the thread paused there is no other filler, so "pump then render" always sees a full ring.
     *  Calling it unpaused is legal but pointless — it races the real thread. */
    int pumpForTest();

private:
    // One pool slot's full state: the audio-visible handle plus the prefetch thread's private
    // bookkeeping. Nothing outside the prefetch thread may touch anything below `handle`.
    struct ClipStream {
        StreamHandle handle;

        // ---- Prefetch thread only ----
        std::unique_ptr<juce::AudioFormatReader> reader;
        std::unique_ptr<float[]> ringStorage; // owns what handle.ringData points at
        std::int64_t assignedClipId = kNoClip;
        juce::File assignedFile;
        std::int64_t fileLengthFrames = 0;
        // Mirrors of the window atomics, so the fill path reasons about plain integers and the
        // atomics are written exactly once per transition.
        std::int64_t windowStart = 0;
        std::int64_t windowEnd = 0;
    };

    // What the message thread asks for, per slot. Guarded by assignmentLock_; copied wholesale by
    // the prefetch thread at the top of a slice, so the lock is never held across any I/O.
    //
    // `sourceStartSeconds` is a SEED for wantedFrame, not part of the slot's identity: a clip whose
    // trim moves does not need its reader reopened (the steering signal already handles that), so
    // it deliberately does not take part in the "has this slot changed?" comparison. Seeding matters
    // for the first block only — without it, a clip trimmed an hour into a take would fill from
    // frame 0 and be silent until the first prefetch slice after playback reached it.
    struct Assignment {
        std::int64_t clipId = kNoClip;
        juce::File file;
        double sourceStartSeconds = 0.0;
    };

    struct PrefetchClient : public juce::TimeSliceClient {
        explicit PrefetchClient(AudioClipStreamer& o)
            : owner(o) {}
        int useTimeSlice() override;
        AudioClipStreamer& owner;
    };

    // PREFETCH THREAD (or the test's thread, via pumpForTest). One pass over the pool: apply any
    // pending assignment change, then reposition/top up every open stream. Returns true if it did
    // anything, which is what makes the "keep going / nap" decision.
    bool runOneSlice();

    void applyPendingAssignments();
    void retireStream(ClipStream& stream);
    void openStream(ClipStream& stream, const Assignment& assignment);
    bool serviceStream(ClipStream& stream);

    // Collapses the window at `frame` (invalidate, then reposition — end first, then start).
    void collapseWindow(ClipStream& stream, std::int64_t frame);

    void startPrefetchThreadIfNeeded();

    int indexOfClip(std::int64_t clipId) const noexcept;

    std::array<ClipStream, kMaxStreams> streams_;

    juce::CriticalSection assignmentLock_;
    std::array<Assignment, kMaxStreams> assignments_;
    std::atomic<bool> assignmentsDirty_{false};

    // Message thread only.
    juce::File bundleRoot_;
    juce::File recordingsRoot_;
    juce::AudioFormatManager formatManager_;

    std::atomic<bool> prefetchPaused_{false};

    // Set by invalidateAllStreams() (release), any non-audio/non-prefetch thread. Cleared by
    // the prefetch thread (runOneSlice()) only after it has collapsed every open stream's window —
    // see invalidateAllStreams()'s comment. Also gates readFrames() directly so the silence
    // guarantee holds even in the gap before the prefetch thread has run.
    std::atomic<bool> forceInvalidate_{false};

    // De-interleaving scratch for the fill, sized once in the constructor. Prefetch thread only.
    juce::AudioBuffer<float> fillScratch_;

    juce::TimeSliceThread prefetchThread_{"Audio Clip Prefetch"};
    PrefetchClient prefetchClient_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioClipStreamer)
};

} // namespace synth
