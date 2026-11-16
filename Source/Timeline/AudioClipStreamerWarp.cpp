// AudioClipStreamerWarp.cpp -- playback of a clip that follows the song's tempo
// (docs/timeline/clips.md#loops-follow-the-tempo): each slot's ClipWarp reads that slot's ring through the same window
// checks readFrames uses, so a warped read is silence on every miss exactly like a plain one.
#include "Timeline/AudioClipStreamer.h"
#include "Timeline/AudioClipWarp.h"
#include <algorithm>
#include <cmath>

namespace synth {

// The ring as a ClipWarp sees it. Frames past the end of the file count as served (silence is their content), so
// the last moments of a clip can still seek and play out.
struct AudioClipStreamer::RingWarpSource final : ClipWarpSource {
    RingWarpSource(const AudioClipStreamer& o, const StreamHandle& h)
        : owner(o)
        , handle(h) {}

    int read(std::int64_t frame, float* left, float* right, int numFrames) const noexcept override {
        const auto copy = owner.copyFromWindow(handle, frame, left, right, numFrames);
        const std::int64_t fileEnd = handle.playbackLengthFrames.load(std::memory_order_acquire);
        if (fileEnd <= 0 || !handle.ready.load(std::memory_order_acquire))
            return copy.copied;
        const std::int64_t end = frame + numFrames;
        if (end <= fileEnd)
            return copy.copied;
        // Served up to the file's end and silence after it: all of it is known.
        const bool servedToEnd =
            frame >= fileEnd || (copy.copied > 0 && copy.offset == 0 && frame + copy.copied >= fileEnd);
        return servedToEnd ? numFrames : copy.copied;
    }

    const AudioClipStreamer& owner;
    const StreamHandle& handle;
};

// The slot's warp is found by a 32-entry pointer scan, like acquire(); a slot with none yet plays silence.
int AudioClipStreamer::readWarpedFrames(const StreamHandle* handle, double sourcePosition, double ratio, float* left,
                                        float* right, int numFrames) const noexcept {
    if (numFrames <= 0)
        return 0;
    if (handle == nullptr || left == nullptr || right == nullptr) {
        zeroFrames(left, right, numFrames);
        return 0;
    }
    handle->wantedFrame.store((std::int64_t)std::floor(std::max(0.0, sourcePosition)), std::memory_order_release);

    ClipWarp* warp = nullptr;
    for (const auto& stream : streams_)
        if (&stream.handle == handle) {
            warp = stream.warpPtr.load(std::memory_order_acquire);
            break;
        }
    if (warp == nullptr) {
        zeroFrames(left, right, numFrames);
        return 0;
    }
    const RingWarpSource source(*this, *handle);
    return warp->render(source, handle->clipId.load(std::memory_order_acquire), sourcePosition, ratio, left, right,
                        numFrames);
}

// Prefetch thread. Made on the slot's first warped clip and kept, like the ring; prepared again when the engine rate
// changed. The rate is the engine's, or the file's while the engine's is unknown (playback frames are then file
// frames).
void AudioClipStreamer::prepareWarp(ClipStream& stream) {
    if (!stream.warpWanted || stream.reader == nullptr)
        return;
    const double engineRate = engineSampleRate_.load(std::memory_order_acquire);
    const double rate = engineRate > 0.0 ? engineRate : stream.reader->sampleRate;
    if (!(rate > 0.0))
        return;
    if (stream.warp == nullptr) {
        stream.warp = std::make_unique<ClipWarp>();
        stream.warp->prepare(rate);
        stream.warpPtr.store(stream.warp.get(), std::memory_order_release);
        return;
    }
    stream.warp->prepare(rate);
}

// What an offline render must have in the ring before a block of a warped clip: the block's input at the tempo ratio
// plus what a seek takes (the stretch's latencies, about 0.07 s plus 0.1 s per unit of ratio at the default preset).
// Capped well inside the prefetch reach, so priming can always finish.
std::int64_t AudioClipStreamer::warpedFramesAhead(double sourceBpm, double bpm, double sampleRate,
                                                  int framesAhead) noexcept {
    if (!(sourceBpm > 0.0) || !(bpm > 0.0) || !(sampleRate > 0.0))
        return framesAhead;
    const double ratio = std::clamp(bpm / sourceBpm, 1.0 / ClipWarp::kMaxRatio, ClipWarp::kMaxRatio);
    const double frames = (double)framesAhead * std::max(1.0, ratio) + (0.07 + 0.1 * ratio) * sampleRate;
    return std::min((std::int64_t)std::ceil(frames), (std::int64_t)kPrefetchAheadFrames / 2);
}

} // namespace synth
