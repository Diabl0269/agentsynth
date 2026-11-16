// AudioClipWarp.cpp -- the live time-stretch behind a clip that follows the song's tempo
// (docs/timeline/clips.md#loops-follow-the-tempo). Signalsmith Stretch, fed from the slot's ring. The input is kept a
// fixed lead ahead of the output so that what plays is always the source position the beat maps to: after a seek the
// stretch has consumed `inputLatency + ratio * outputLatency` frames from the seek point, and every chunk feeds input
// up to the same lead past its last output.
#include "Timeline/AudioClipWarp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace synth {

namespace {
// A fixed seed: a bounce of the same song renders the same audio.
constexpr long kStretchSeed = 0x5eed;
} // namespace

struct ClipWarp::Stretch {
    signalsmith::stretch::SignalsmithStretch<float> s{kStretchSeed};
};

ClipWarp::ClipWarp() = default;
ClipWarp::~ClipWarp() = default;

// The quality preset with its work spread over calls (splitComputation), so one block never carries a whole FFT
// frame's cost. The scratch holds the widest seek or chunk at kMaxRatio.
void ClipWarp::prepare(double sampleRate) {
    if (!(sampleRate > 0.0) || preparedRate_.load(std::memory_order_acquire) == sampleRate)
        return;
    const juce::SpinLock::ScopedLockType lock(lock_);
    if (stretch_ == nullptr)
        stretch_ = std::make_unique<Stretch>();
    auto& s = stretch_->s;
    s.presetDefault(2, (float)sampleRate, true);
    const double widestLead = (double)s.inputLatency() + kMaxRatio * (double)s.outputLatency();
    const auto scratch = (std::size_t)std::ceil(widestLead + kMaxRatio * (double)kChunkFrames) + 16;
    inLeft_.assign(scratch, 0.0f);
    inRight_.assign(scratch, 0.0f);
    running_ = false;
    preparedRate_.store(sampleRate, std::memory_order_release);
}

int ClipWarp::seekFrames(double ratio) const noexcept {
    if (stretch_ == nullptr)
        return 0;
    const double r = juce::jlimit(1.0 / kMaxRatio, kMaxRatio, ratio);
    return (int)std::ceil((double)stretch_->s.inputLatency() + r * (double)stretch_->s.outputLatency());
}

// The stretch starts again at `position`: it takes the seek's input in one go and pre-renders its own latency, so
// the next output sample is the one at `position`. Refused (silence, and a new try next block) while the ring cannot
// serve that input yet, e.g. just after a locate.
bool ClipWarp::seekTo(const ClipWarpSource& source, double position, double ratio) noexcept {
    const int need = std::min(seekFrames(ratio), (int)inLeft_.size());
    const auto start = (std::int64_t)std::llround(position);
    if (need <= 0 || source.read(start, inLeft_.data(), inRight_.data(), need) < need)
        return false;
    const float* inputs[2] = {inLeft_.data(), inRight_.data()};
    stretch_->s.outputSeek(inputs, need);
    nextInputFrame_ = start + need;
    expectedPosition_ = position;
    running_ = true;
    return true;
}

// A block that starts within kContinueToleranceFrames of where the last one ended continues it (taking the new
// position, so rounding never builds up); anything else (a locate, a loop wrap, the clip's first block, another clip
// in this slot) seeks.
int ClipWarp::render(const ClipWarpSource& source, std::int64_t clipId, double position, double ratio, float* left,
                     float* right, int numFrames) noexcept {
    if (numFrames <= 0 || left == nullptr || right == nullptr)
        return 0;
    const auto silence = [&] {
        std::memset(left, 0, sizeof(float) * (std::size_t)numFrames);
        std::memset(right, 0, sizeof(float) * (std::size_t)numFrames);
        return 0;
    };
    const juce::SpinLock::ScopedTryLockType tryLock(lock_);
    if (!tryLock.isLocked() || stretch_ == nullptr || preparedRate_.load(std::memory_order_acquire) <= 0.0)
        return silence();

    const double r = juce::jlimit(1.0 / kMaxRatio, kMaxRatio, ratio);
    const bool continues =
        running_ && clipId_ == clipId && std::abs(position - expectedPosition_) <= kContinueToleranceFrames;
    if (continues) {
        expectedPosition_ = position;
    } else {
        running_ = false;
        clipId_ = clipId;
        if (!seekTo(source, position, r))
            return silence();
    }

    auto& s = stretch_->s;
    const double lead = (double)s.inputLatency() + r * (double)s.outputLatency();
    for (int done = 0; done < numFrames; done += kChunkFrames) {
        const int chunk = std::min(kChunkFrames, numFrames - done);
        const double endPosition = expectedPosition_ + (double)chunk * r;
        const auto target = (std::int64_t)std::llround(endPosition + lead);
        const int feed = (int)std::clamp<std::int64_t>(target - nextInputFrame_, 0, (std::int64_t)inLeft_.size());
        if (feed > 0)
            source.read(nextInputFrame_, inLeft_.data(), inRight_.data(), feed);
        const float* inputs[2] = {inLeft_.data(), inRight_.data()};
        float* outputs[2] = {left + done, right + done};
        s.process(inputs, feed, outputs, chunk);
        nextInputFrame_ += feed;
        expectedPosition_ = endPosition;
    }
    return numFrames;
}

} // namespace synth
