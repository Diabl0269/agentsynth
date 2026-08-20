#include "AudioClipStreamer.h"

#include "TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace synth {

namespace {

// Bounds pumpForTest()'s loop. One slice moves at most kFillChunkFrames per stream, so a fully
// caught-up pool needs ~(kPrefetchAheadFrames / kFillChunkFrames) slices; this is orders of
// magnitude above that and exists only so a bug cannot hang a test forever.
constexpr int kMaxPumpSlices = 100000;

} // namespace

//==============================================================================
AudioClipStreamer::AudioClipStreamer() {
    formatManager_.registerBasicFormats();
    // Sized once, here: the fill path must not allocate, and every stream shares this scratch
    // because only one stream is ever being filled at a time (the prefetch thread is single).
    fillScratch_.setSize(kNumChannels, kFillChunkFrames);
}

AudioClipStreamer::~AudioClipStreamer() { releaseAll(); }

//==============================================================================
// Message thread
//==============================================================================

void AudioClipStreamer::setAssetRoots(const juce::File& bundleRoot, const juce::File& recordingsRoot) {
    bundleRoot_ = bundleRoot;
    recordingsRoot_ = recordingsRoot;
}

juce::File AudioClipStreamer::getBundleRoot() const { return bundleRoot_; }

juce::File AudioClipStreamer::getRecordingsRoot() const { return recordingsRoot_; }

juce::File AudioClipStreamer::resolveAssetRef(const juce::String& ref) const {
    if (ref.isEmpty())
        return {};

    // The SAME predicate the document itself gates on (TimelineDoc::setClipAsset and fromVar both
    // use it): relative only, no `..` segment, no leading separator, no drive letter. Re-checked
    // here rather than trusted because this is the layer that actually opens a file — a ref can
    // reach a snapshot from a hand-edited bundle, and "the doc validated it" is not a property this
    // class should have to assume.
    if (!TimelineDoc::isValidAssetRef(ref))
        return {};

    juce::File root;
    juce::File resolved;

    if (ref.startsWith(kRecordingsRefPrefix)) {
        // The unsaved-project case: the ref INCLUDES the "Recordings/" segment and is resolved
        // against the directory that CONTAINS the Recordings folder — i.e. exactly the path
        // MainComponent::chooseTakeFiles built it from (`<app data>/<settings folder>` +
        // "Recordings/take-n.wav"). Containment is then checked against the Recordings folder
        // itself, which is tighter than the directory the ref was resolved against.
        if (recordingsRoot_ == juce::File())
            return {};
        root = recordingsRoot_;
        resolved = recordingsRoot_.getParentDirectory().getChildFile(ref);
    } else {
        if (bundleRoot_ == juce::File())
            return {};
        root = bundleRoot_;
        resolved = bundleRoot_.getChildFile(ref);
    }

    // Belt and braces over isValidAssetRef: whatever getChildFile made of the ref must still sit
    // UNDER its root. A ref that escapes is refused outright — this service never opens a file
    // outside the roots it was given, which is the whole point of storing refs relative in the
    // first place (see synth::ProjectBundle's asset policy).
    if (!resolved.isAChildOf(root))
        return {};

    if (!resolved.existsAsFile())
        return {};

    return resolved;
}

void AudioClipStreamer::syncToSnapshot(const TimelineSnapshot& snapshot) {
    // 1. What the document wants streamed, in DOCUMENT ORDER (track order, then clip start order —
    //    the snapshot's own run order). Refs that are empty, malformed, escaping or missing are
    //    skipped entirely: they render silence anyway, and skipping them here keeps them from
    //    burning a pool slot a playable clip could have used.
    std::vector<Assignment> desired;
    desired.reserve((std::size_t)kMaxStreams);

    for (const auto& track : snapshot.tracks) {
        if (track.kind != static_cast<int>(TrackKind::Audio))
            continue;

        const int first = track.firstAudioClip;
        const int last = track.firstAudioClip + track.numAudioClips;
        for (int i = first; i < last && i < (int)snapshot.audioClips.size(); ++i) {
            const auto& clip = snapshot.audioClips[(std::size_t)i];
            if (clip.clipId == kNoClip || clip.assetRef[0] == '\0')
                continue;

            const auto file = resolveAssetRef(juce::String::fromUTF8(clip.assetRef));
            if (file == juce::File())
                continue;

            desired.push_back({clip.clipId, file, clip.sourceStartSeconds, clip.startBeat, clip.lengthBeats});
        }
    }

    const juce::ScopedLock lock(assignmentLock_);

    std::array<Assignment, kMaxStreams> next{};
    std::vector<bool> placed(desired.size(), false);

    // 2. KEEP: a slot whose clip is still wanted, with the same file, stays exactly where it is —
    //    its reader stays open and its ring stays filled, so editing one clip never re-seeks the
    //    other thirty-one.
    for (int slot = 0; slot < kMaxStreams; ++slot) {
        const auto& current = assignments_[(std::size_t)slot];
        if (current.clipId == kNoClip)
            continue;

        for (std::size_t d = 0; d < desired.size(); ++d) {
            if (placed[d] || desired[d].clipId != current.clipId)
                continue;
            if (desired[d].file == current.file) {
                next[(std::size_t)slot] = desired[d]; // keeps the slot; refreshes the trim seed
                placed[d] = true;
            }
            break; // clip ids are unique doc-wide: there is no second entry to look at
        }
    }

    // 3. FILL: whatever is left goes into free slots in document order, until the pool runs out.
    //    Clips past the cap get no stream and are silent — see the class comment's policy note.
    for (std::size_t d = 0; d < desired.size(); ++d) {
        if (placed[d])
            continue;
        for (int slot = 0; slot < kMaxStreams; ++slot) {
            if (next[(std::size_t)slot].clipId != kNoClip)
                continue;
            next[(std::size_t)slot] = desired[d];
            placed[d] = true;
            break;
        }
    }

    assignments_ = next;
    assignmentsDirty_.store(true, std::memory_order_release);

    startPrefetchThreadIfNeeded();
}

void AudioClipStreamer::releaseAll() {
    // Stop the prefetch thread FIRST. It is the only owner of every reader, so once it is gone this
    // thread may free them — which is why there is no retire list here: no reader is ever visible
    // to two threads at once.
    prefetchThread_.removeTimeSliceClient(&prefetchClient_);
    prefetchThread_.stopThread(4000);

    {
        const juce::ScopedLock lock(assignmentLock_);
        for (auto& assignment : assignments_)
            assignment = {};
        assignmentsDirty_.store(false, std::memory_order_release);
    }

    for (auto& stream : streams_)
        retireStream(stream);
}

void AudioClipStreamer::invalidateAllStreams() {
    // Release-paired with runOneSlice()'s acquire-check below and readFrames()'s acquire-load: by
    // the time either sees this true, this store has already happened.
    forceInvalidate_.store(true, std::memory_order_release);

    // Wake the prefetch thread immediately rather than let it discover the flag on its next
    // scheduled nap (up to 5 ms — see PrefetchClient::useTimeSlice). A no-op if it isn't running
    // (paused for a test, or nothing has ever synced a snapshot) — pumpForTest()/the next real slice
    // picks the flag up regardless.
    if (!prefetchPaused_.load(std::memory_order_relaxed))
        prefetchThread_.moveToFrontOfQueue(&prefetchClient_);
}

void AudioClipStreamer::startPrefetchThreadIfNeeded() {
    if (prefetchPaused_.load(std::memory_order_relaxed))
        return;

    if (!prefetchThread_.isThreadRunning())
        prefetchThread_.startThread();
    prefetchThread_.addTimeSliceClient(&prefetchClient_); // addIfNotAlreadyThere — idempotent
}

void AudioClipStreamer::setPrefetchPausedForTest(bool paused) {
    prefetchPaused_.store(paused, std::memory_order_relaxed);
    if (paused) {
        prefetchThread_.removeTimeSliceClient(&prefetchClient_);
        prefetchThread_.stopThread(4000);
    } else {
        startPrefetchThreadIfNeeded();
    }
}

int AudioClipStreamer::pumpForTest() {
    int slices = 0;
    while (slices < kMaxPumpSlices && runOneSlice())
        ++slices;
    return slices;
}

//==============================================================================
// Offline render (message thread, blocking)
//==============================================================================

AudioClipStreamer::PrimeTargets AudioClipStreamer::primeTargetsForBeat(double beat, double bpm,
                                                                       double sampleRate) const {
    PrimeTargets targets{};

    const juce::ScopedLock lock(assignmentLock_);
    for (int slot = 0; slot < kMaxStreams; ++slot) {
        const auto& assignment = assignments_[(std::size_t)slot];
        if (assignment.clipId == kNoClip)
            continue;

        // Clamped into the clip's own span: a clip that has not started yet is primed at its trim
        // point (which is where playback WILL enter it), not at a frame before the file begins.
        const double clipEnd = assignment.startBeat + juce::jmax(0.0, assignment.lengthBeats);
        const double clipBeat = juce::jlimit(assignment.startBeat, juce::jmax(assignment.startBeat, clipEnd), beat);

        targets[(std::size_t)slot] = {
            assignment.clipId,
            sourceFrameForClipBeat(assignment.startBeat, assignment.sourceStartSeconds, clipBeat, bpm, sampleRate)};
    }

    return targets;
}

void AudioClipStreamer::publishWantedFrames(const PrimeTargets& targets) {
    for (int slot = 0; slot < kMaxStreams; ++slot) {
        const auto& target = targets[(std::size_t)slot];
        auto& handle = streams_[(std::size_t)slot].handle;
        if (target.clipId != kNoClip && handle.clipId.load(std::memory_order_acquire) == target.clipId)
            handle.wantedFrame.store(target.frame, std::memory_order_release);
    }
}

bool AudioClipStreamer::isPrimed(const PrimeTargets& targets, int framesAhead) const {
    // Nothing is readable at all while a format change is in flight — readFrames() forces silence
    // until the prefetch thread has collapsed every window. See invalidateAllStreams().
    if (forceInvalidate_.load(std::memory_order_acquire))
        return false;

    for (int slot = 0; slot < kMaxStreams; ++slot) {
        const auto& target = targets[(std::size_t)slot];
        if (target.clipId == kNoClip)
            continue;

        const auto& handle = streams_[(std::size_t)slot].handle;
        if (handle.clipId.load(std::memory_order_acquire) != target.clipId)
            return false; // the prefetch thread has not applied this assignment yet

        if (!handle.ready.load(std::memory_order_acquire))
            continue; // assigned but unplayable: silence by design, never something to wait for

        const std::int64_t fileLength = handle.fileLengthFrames.load(std::memory_order_acquire);
        if (fileLength <= 0 || target.frame >= fileLength)
            continue; // past the end of the take — silence is the correct content

        const std::int64_t need = std::min(target.frame + (std::int64_t)framesAhead, fileLength);
        if (handle.ringStartSourceFrame.load(std::memory_order_acquire) > target.frame)
            return false;
        if (handle.ringEndSourceFrame.load(std::memory_order_acquire) < need)
            return false;
    }

    return true;
}

bool AudioClipStreamer::waitUntilPrimed(double beat, double bpm, double sampleRate, int framesAhead, int timeoutMs) {
    if (!(sampleRate > 0.0))
        return true;

    const int ahead = juce::jmax(0, framesAhead);
    const auto targets = primeTargetsForBeat(beat, bpm, sampleRate);

    // Steer BEFORE checking: the prefetch thread cannot reposition a ring it has not been told
    // about, and the very first block of an offline render is exactly the case where it has not.
    publishWantedFrames(targets);

    if (prefetchPaused_.load(std::memory_order_relaxed)) {
        // Paused means this thread is the only filler there is — the same synchronous pump tests
        // drive the service with, so an offline render never waits on a thread that cannot run.
        pumpForTest();
        return isPrimed(targets, ahead);
    }

    startPrefetchThreadIfNeeded();

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)juce::jmax(0, timeoutMs);
    for (;;) {
        if (isPrimed(targets, ahead))
            return true;

        prefetchThread_.moveToFrontOfQueue(&prefetchClient_);

        const auto now = juce::Time::getMillisecondCounter();
        if (now >= deadline)
            return isPrimed(targets, ahead);

        sliceCompleted_.wait((int)juce::jmin<juce::uint32>(5, deadline - now));
    }
}

//==============================================================================
// Audio thread
//==============================================================================

const AudioClipStreamer::StreamHandle* AudioClipStreamer::acquire(std::int64_t clipId) const noexcept {
    if (clipId == kNoClip)
        return nullptr;

    for (const auto& stream : streams_)
        if (stream.handle.clipId.load(std::memory_order_acquire) == clipId)
            return &stream.handle;

    return nullptr;
}

int AudioClipStreamer::readFrames(const StreamHandle* handle, std::int64_t sourceFrame, float* left, float* right,
                                  int numFrames) const noexcept {
    if (numFrames <= 0)
        return 0;

    const auto zeroAll = [&] {
        if (left != nullptr)
            std::memset(left, 0, sizeof(float) * (std::size_t)numFrames);
        if (right != nullptr)
            std::memset(right, 0, sizeof(float) * (std::size_t)numFrames);
    };

    if (handle == nullptr || left == nullptr || right == nullptr) {
        zeroAll();
        return 0;
    }

    // The steering signal, published unconditionally — even on a miss, because a miss is precisely
    // when the prefetch thread most needs to know where playback went.
    handle->wantedFrame.store(sourceFrame, std::memory_order_release);

    // A format change is in flight. Force silence rather than risk a coincidental hit on ring
    // content filled under the OLD sample-rate-to-source-frame mapping — see invalidateAllStreams().
    // This is checked BEFORE the window, so it holds even in the gap before the prefetch thread has
    // had a chance to collapse anything.
    if (forceInvalidate_.load(std::memory_order_acquire)) {
        zeroAll();
        return 0;
    }

    float* ring = handle->ringData.load(std::memory_order_acquire);
    if (ring == nullptr || !handle->ready.load(std::memory_order_acquire)) {
        zeroAll();
        return 0;
    }

    const std::int64_t end0 = handle->ringEndSourceFrame.load(std::memory_order_acquire);
    const std::int64_t start0 = handle->ringStartSourceFrame.load(std::memory_order_acquire);

    // What the ring can serve of [sourceFrame, sourceFrame + numFrames): clipped to the published
    // window AND to the last kRingFrames frames (a slot older than that has been reused).
    const std::int64_t lo = std::max(sourceFrame, std::max(start0, end0 - (std::int64_t)kRingFrames));
    const std::int64_t hi = std::min(sourceFrame + (std::int64_t)numFrames, end0);
    if (hi <= lo) {
        zeroAll();
        return 0;
    }

    const int destOffset = (int)(lo - sourceFrame);
    const int count = (int)(hi - lo);

    if (destOffset > 0) {
        std::memset(left, 0, sizeof(float) * (std::size_t)destOffset);
        std::memset(right, 0, sizeof(float) * (std::size_t)destOffset);
    }
    const int tail = numFrames - destOffset - count;
    if (tail > 0) {
        std::memset(left + destOffset + count, 0, sizeof(float) * (std::size_t)tail);
        std::memset(right + destOffset + count, 0, sizeof(float) * (std::size_t)tail);
    }

    for (int i = 0; i < count; ++i) {
        const std::size_t slot = (std::size_t)((lo + i) & (std::int64_t)(kRingFrames - 1)) * (std::size_t)kNumChannels;
        left[destOffset + i] = ring[slot];
        right[destOffset + i] = ring[slot + 1];
    }

    // Re-check the window AFTER copying. The prefetch thread cannot structurally clobber anything at
    // or after `wantedFrame` (it never fills past wanted + 3/4 ring), but a reposition can retire
    // the whole window mid-copy, and this is what turns "should not happen" into "cannot be heard":
    // if the window moved off what we just read, the block is SILENCE rather than a mix of two
    // positions.
    const std::int64_t start1 = handle->ringStartSourceFrame.load(std::memory_order_acquire);
    const std::int64_t end1 = handle->ringEndSourceFrame.load(std::memory_order_acquire);
    if (start1 > lo || end1 - (std::int64_t)kRingFrames > lo || end1 < hi) {
        zeroAll();
        return 0;
    }

    return count;
}

//==============================================================================
// Introspection
//==============================================================================

int AudioClipStreamer::indexOfClip(std::int64_t clipId) const noexcept {
    if (clipId == kNoClip)
        return -1;
    for (int i = 0; i < kMaxStreams; ++i)
        if (streams_[(std::size_t)i].handle.clipId.load(std::memory_order_acquire) == clipId)
            return i;
    return -1;
}

int AudioClipStreamer::getActiveStreamCount() const noexcept {
    int count = 0;
    for (const auto& stream : streams_)
        if (stream.handle.clipId.load(std::memory_order_acquire) != kNoClip)
            ++count;
    return count;
}

bool AudioClipStreamer::isClipReady(std::int64_t clipId) const noexcept {
    const int index = indexOfClip(clipId);
    return index >= 0 && streams_[(std::size_t)index].handle.ready.load(std::memory_order_acquire);
}

double AudioClipStreamer::getStreamFileSampleRate(std::int64_t clipId) const noexcept {
    const int index = indexOfClip(clipId);
    return index >= 0 ? streams_[(std::size_t)index].handle.fileSampleRate.load(std::memory_order_acquire) : 0.0;
}

int AudioClipStreamer::getResidentFramesForClip(std::int64_t clipId) const noexcept {
    const int index = indexOfClip(clipId);
    if (index < 0)
        return 0;
    return streams_[(std::size_t)index].handle.ringData.load(std::memory_order_acquire) != nullptr ? kRingFrames : 0;
}

std::size_t AudioClipStreamer::getTotalResidentBytes() const noexcept {
    std::size_t total = 0;
    for (const auto& stream : streams_)
        if (stream.handle.ringData.load(std::memory_order_acquire) != nullptr)
            total += (std::size_t)kRingFrames * (std::size_t)kNumChannels * sizeof(float);
    return total;
}

//==============================================================================
// Prefetch thread
//==============================================================================

int AudioClipStreamer::PrefetchClient::useTimeSlice() {
    // Straight back for more while there is filling to do; a 5 ms nap when every ring is topped up.
    // Both numbers are latency-of-prefetch, never latency-of-audio — the audio thread never waits.
    return owner.runOneSlice() ? 0 : 5;
}

bool AudioClipStreamer::runOneSlice() {
    applyPendingAssignments();

    // Collapse every open stream's window to wherever it was last asked to read (its
    // wantedFrame — the steering signal readFrames() publishes on every call, hit or miss), THEN
    // clear the flag. Ordering matters: the flag must not be cleared (and readFrames()'s silence
    // gate lifted) until every stream has actually been collapsed, or a stream that hasn't been
    // serviced yet this pass could still be read against its stale, pre-invalidate window.
    if (forceInvalidate_.load(std::memory_order_acquire)) {
        for (auto& stream : streams_)
            if (stream.reader != nullptr)
                collapseWindow(stream, stream.handle.wantedFrame.load(std::memory_order_acquire));
        forceInvalidate_.store(false, std::memory_order_release);
    }

    bool didWork = false;
    for (auto& stream : streams_)
        didWork = serviceStream(stream) || didWork;

    // Whatever this pass achieved is visible now: wake anyone blocked in waitUntilPrimed() rather
    // than make an offline render discover it on a polling interval.
    sliceCompleted_.signal();

    return didWork;
}

void AudioClipStreamer::applyPendingAssignments() {
    if (!assignmentsDirty_.exchange(false, std::memory_order_acq_rel))
        return;

    // Copied out under the lock, then acted on outside it: opening a file must never happen with a
    // lock the message thread also takes.
    std::array<Assignment, kMaxStreams> wanted{};
    {
        const juce::ScopedLock lock(assignmentLock_);
        wanted = assignments_;
    }

    for (int slot = 0; slot < kMaxStreams; ++slot) {
        auto& stream = streams_[(std::size_t)slot];
        const auto& want = wanted[(std::size_t)slot];

        if (want.clipId == stream.assignedClipId && want.file == stream.assignedFile)
            continue;

        retireStream(stream);
        if (want.clipId != kNoClip)
            openStream(stream, want);
    }
}

void AudioClipStreamer::retireStream(ClipStream& stream) {
    // Un-publish FIRST: the audio thread finds a slot by its clip id, so clearing that is what makes
    // the slot invisible before anything it points at is torn down.
    stream.handle.clipId.store(kNoClip, std::memory_order_release);
    stream.handle.ready.store(false, std::memory_order_release);
    stream.handle.fileSampleRate.store(0.0, std::memory_order_release);
    stream.handle.fileLengthFrames.store(0, std::memory_order_release);
    collapseWindow(stream, 0);

    // The reader is prefetch-thread-owned, so destroying it here needs no hand-off. (releaseAll()
    // is the one message-thread caller, and it stops the prefetch thread before calling this.)
    stream.reader.reset();
    stream.assignedClipId = kNoClip;
    stream.assignedFile = juce::File();
    stream.fileLengthFrames = 0;
    // ringStorage_ is deliberately NOT freed: a slot keeps its ring for the process' lifetime, so
    // handle.ringData is stable the moment it is first published and the audio thread never sees it
    // change under a live clip id.
}

void AudioClipStreamer::openStream(ClipStream& stream, const Assignment& assignment) {
    const std::int64_t clipId = assignment.clipId;
    stream.assignedClipId = clipId;
    stream.assignedFile = assignment.file;

    stream.reader.reset(formatManager_.createReaderFor(assignment.file));

    if (stream.reader == nullptr || stream.reader->numChannels == 0 || stream.reader->lengthInSamples <= 0) {
        // Assigned but unplayable (deleted between resolve and open, unsupported format, empty).
        // The slot still carries the clip id so introspection can tell "no stream" from "a stream
        // that cannot play", and `ready` stays false, so the audio thread gets silence either way.
        stream.reader.reset();
        stream.fileLengthFrames = 0;
        stream.handle.clipId.store(clipId, std::memory_order_release);
        return;
    }

    stream.fileLengthFrames = stream.reader->lengthInSamples;
    stream.handle.fileLengthFrames.store(stream.fileLengthFrames, std::memory_order_release);

    // Allocate this slot's ring on FIRST use only, and never free or move it afterwards — that is
    // what makes the pointer safe to publish once and read forever. An engine whose session has no
    // audio clips therefore costs no ring memory at all.
    if (stream.ringStorage == nullptr) {
        stream.ringStorage = std::make_unique<float[]>((std::size_t)kRingFrames * (std::size_t)kNumChannels);
        std::memset(stream.ringStorage.get(), 0, sizeof(float) * (std::size_t)kRingFrames * (std::size_t)kNumChannels);
        stream.handle.ringData.store(stream.ringStorage.get(), std::memory_order_release);
    }

    // Start empty at the clip's own trim point, so the very first fill covers what playback will
    // actually ask for rather than the head of the file (see Assignment::sourceStartSeconds).
    const double rate = stream.reader->sampleRate > 0.0 ? stream.reader->sampleRate : 0.0;
    const std::int64_t seed = (std::int64_t)std::llround(
        juce::jlimit(0.0, (double)stream.fileLengthFrames, assignment.sourceStartSeconds * rate));
    collapseWindow(stream, seed);
    stream.handle.wantedFrame.store(seed, std::memory_order_release);
    stream.handle.fileSampleRate.store(stream.reader->sampleRate, std::memory_order_release);
    stream.handle.ready.store(true, std::memory_order_release);

    // Published LAST: this release store is what makes everything above visible to the audio
    // thread's acquire-load in acquire().
    stream.handle.clipId.store(clipId, std::memory_order_release);
}

void AudioClipStreamer::collapseWindow(ClipStream& stream, std::int64_t frame) {
    // END first: readability is gated on the end, so storing it collapses the window to empty
    // before the start moves — the audio thread can never observe a start past a stale end and
    // conclude that old bytes are valid.
    stream.windowEnd = frame;
    stream.handle.ringEndSourceFrame.store(frame, std::memory_order_release);
    stream.windowStart = frame;
    stream.handle.ringStartSourceFrame.store(frame, std::memory_order_release);
}

bool AudioClipStreamer::serviceStream(ClipStream& stream) {
    if (stream.reader == nullptr)
        return false;

    std::int64_t wanted = stream.handle.wantedFrame.load(std::memory_order_acquire);
    if (wanted < 0)
        wanted = 0;

    // A gap or a seek: playback is somewhere this ring cannot serve, so throw the window away and
    // start again from there. `wanted > windowEnd` is a forward jump past what we have; `wanted <
    // windowStart` is a locate backwards (or a loop wrap), which the ring cannot rewind into.
    if (wanted < stream.windowStart || wanted > stream.windowEnd)
        collapseWindow(stream, wanted);

    if (stream.windowEnd >= stream.fileLengthFrames)
        return false; // the whole rest of this file is silence; nothing left to fetch

    const std::int64_t target = wanted + (std::int64_t)kPrefetchAheadFrames;
    if (stream.windowEnd >= target)
        return false; // already a comfortable distance ahead of the playhead

    const std::int64_t roomToTarget = target - stream.windowEnd;
    const std::int64_t roomInFile = stream.fileLengthFrames - stream.windowEnd;
    const int frames = (int)std::min<std::int64_t>(std::min(roomToTarget, roomInFile), kFillChunkFrames);
    if (frames <= 0)
        return false;

    float* ring = stream.handle.ringData.load(std::memory_order_relaxed);
    if (ring == nullptr)
        return false;

    // STEP 1 — shrink the window BEFORE overwriting a single slot, so a frame the audio thread may
    // still be reading is never clobbered while it is still advertised as valid. Because the fill
    // never goes past wanted + 3/4 ring, newStart can never reach `wanted`: the quarter-ring of
    // history behind the playhead is structurally safe (see the header's fill-ordering argument).
    const std::int64_t newStart =
        std::max(stream.windowStart, stream.windowEnd + (std::int64_t)frames - (std::int64_t)kRingFrames);
    if (newStart != stream.windowStart) {
        stream.windowStart = newStart;
        stream.handle.ringStartSourceFrame.store(newStart, std::memory_order_release);
    }

    // STEP 2 — read into the de-interleaving scratch. Both channel flags true means: a MONO file is
    // duplicated into both channels, a file with more than two channels contributes its first two,
    // and an integer format is converted to float — all of it inside juce::AudioFormatReader.
    stream.reader->read(&fillScratch_, 0, frames, stream.windowEnd, true, true);

    const float* sourceLeft = fillScratch_.getReadPointer(0);
    const float* sourceRight = fillScratch_.getReadPointer(1);
    for (int i = 0; i < frames; ++i) {
        const std::size_t slot =
            (std::size_t)((stream.windowEnd + i) & (std::int64_t)(kRingFrames - 1)) * (std::size_t)kNumChannels;
        ring[slot] = sourceLeft[i];
        ring[slot + 1] = sourceRight[i];
    }

    // STEP 3 — mark the new frames valid LAST. Release-paired with the audio thread's acquire load,
    // so seeing the new end guarantees seeing the samples behind it.
    stream.windowEnd += frames;
    stream.handle.ringEndSourceFrame.store(stream.windowEnd, std::memory_order_release);
    return true;
}

} // namespace synth
