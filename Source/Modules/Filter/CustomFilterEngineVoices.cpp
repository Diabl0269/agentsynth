// CustomFilterEngineVoices.cpp -- the Custom filter's per-note path: the per-note settings handoff, one
// cursor and one kernel per poly voice, and the per-block design budget that keeps eight moving voices
// affordable on the audio thread.
#include "CustomFilterEngine.h"
#include <cmath>

namespace synth {

namespace {
constexpr int kSamplesPerDesign = 128;
constexpr float kUrgentPriority = 1000.0f; // a note that just started
constexpr float kRedrawPriority = 10.0f;   // new shapes, Shift, Depth, Cutoff or Resonance moved
} // namespace

/** Mode and smoothness are single values, so they cross as relaxed atomics. The key map is a fixed 128-entry table
    and crosses the way the LFO's custom table does: the message thread writes the pending copy under keysLock_ and
    raises keysPending_; the audio thread only try-locks (readPerNoteSettings), so it never waits, and a missed
    block picks the map up on the next one. */
void CustomFilterEngine::setPerNote(const CustomFilterPerNote& perNote) {
    perNote_ = perNote;
    perNote_.smoothness = std::isfinite(perNote_.smoothness) ? juce::jlimit(0.0f, 1.0f, perNote_.smoothness)
                                                             : CustomFilterPerNote::kDefaultSmoothness;
    perNoteMode_.store((int)perNote_.mode, std::memory_order_relaxed);
    perNoteSmoothness_.store(perNote_.smoothness, std::memory_order_relaxed);
    {
        const juce::SpinLock::ScopedLockType lock(keysLock_);
        pendingKeys_ = perNote_.keys;
        keysPending_ = true;
    }
    shapesGeneration_.fetch_add(1, std::memory_order_relaxed);
}

CustomFilterVoiceCursors::Settings CustomFilterEngine::readPerNoteSettings() noexcept {
    {
        const juce::SpinLock::ScopedTryLockType lock(keysLock_);
        if (lock.isLocked() && keysPending_) {
            audioKeys_ = pendingKeys_;
            keysPending_ = false;
        }
    }
    CustomFilterVoiceCursors::Settings s;
    s.mode = static_cast<CustomFilterPerNote::Mode>(
        juce::jlimit(0, CustomFilterPerNote::kNumModes - 1, perNoteMode_.load(std::memory_order_relaxed)));
    s.smoothness = perNoteSmoothness_.load(std::memory_order_relaxed);
    s.keys = audioKeys_.data();
    return s;
}

/** Roughly one kernel design per 128 samples, never fewer than two per block nor more than one per voice. A
    design takes about 65 us of audio-thread time (Release, Apple Silicon; DISABLED_CpuCost), so at 64-sample
    blocks two designs are about a tenth of the block and at 512 samples four are about 2%. A chord of more notes
    than the budget settles over a few blocks, the notes that just started first. */
int CustomFilterEngine::voiceDesignBudget(int numSamples) noexcept {
    return juce::jlimit(2, kMaxVoices, numSamples / kSamplesPerDesign);
}

/** Voices with no note yet stay on the shared kernel (group 0), so turning a mode on changes nothing until notes
    arrive. A voice's first note moves both its legs onto its own group, which starts as a copy of the shared
    kernel, so that move is silent; its own design then crossfades in like any other kernel change. */
void CustomFilterEngine::engageVoices(int numVoices, int numChannels) noexcept {
    for (int v = 0; v < numVoices; ++v) {
        if (voiceOnOwnGroup_[(size_t)v] || !cursors_.isEngaged(v))
            continue;
        convolver_.copyLiveKernel(0, v + 1);
        for (int c = v; c < numChannels && c < kMaxChannels; c += kMaxVoices)
            convolver_.setChannelGroup(c, v + 1);
        voiceOnOwnGroup_[(size_t)v] = true;
        voiceDesigned_[(size_t)v] = designed_;
        voiceTableDirty_[(size_t)v] = tableChanged_ || convolver_.isCrossfading(0);
    }
}

bool CustomFilterEngine::voiceIsIdle(int v, int numChannels) const noexcept {
    for (int c = v; c < numChannels && c < kMaxChannels; c += kMaxVoices)
        if (!convolver_.isChannelIdle(c))
            return false;
    return true;
}

bool CustomFilterEngine::groupZeroIsHeard(int numChannels) const noexcept {
    for (int c = 0; c < numChannels && c < kMaxChannels; ++c)
        if (convolver_.getChannelGroup(c) == 0 && !convolver_.isChannelIdle(c))
            return true;
    return false;
}

/** Every voice back on the shared kernel; the next per-note block starts from "no note yet". */
void CustomFilterEngine::leavePerVoice() noexcept {
    for (int c = 0; c < kMaxChannels; ++c)
        convolver_.setChannelGroup(c, 0);
    voiceOnOwnGroup_.fill(false);
    cursors_.reset();
    perVoiceActive_ = false;
}

/** Cursors move every block for every voice (cheap); kernels follow within the budget. The shared kernel is
    redesigned only while a voice that has had no note still sounds through it (or before it first exists). */
void CustomFilterEngine::processPerVoice(float* const* channels, int numChannels, int numSamples,
                                         const Controls& controls, const VoiceInput* voices, int numVoices,
                                         const CustomFilterVoiceCursors::Settings& settings) noexcept {
    perVoiceActive_ = true;
    numVoices = juce::jmin(numVoices, kMaxVoices);
    const ShapeTable& table = *tables_.current();
    const CustomFilterVoiceCursors::ShapeMap map{table.count, table.x.data(), table.y.data(), table.orderByX.data()};
    cursors_.update(voices, numVoices, numSamples, settings, map, controls.morphX, controls.morphY);

    int budget = voiceDesignBudget(numSamples);
    if (needsDesign(controls) && !convolver_.isCrossfading(0) &&
        (!convolver_.hasKernel(0) || groupZeroIsHeard(numChannels))) {
        design(controls, numSamples);
        --budget;
    }
    engageVoices(numVoices, numChannels); // after the shared design, so a voice's copy is never empty
    designVoices(controls, numVoices, numChannels, numSamples, budget);
    convolver_.process(channels, numChannels, numSamples);
}

/** Picks at most `budget` voices to redesign this block: a note that just started first (so Smoothness 0 lands in
    the note's first block), then voices whose shapes, Shift, Depth, Cutoff or Resonance changed, then the ones whose
   cursor is furthest from the cursor their kernel was designed for. A voice that has been silent long enough to be idle
    waits, and a skipped voice keeps gliding: it only takes a coarser step when its turn comes. */
void CustomFilterEngine::designVoices(const Controls& controls, int numVoices, int numChannels, int numSamples,
                                      int budget) noexcept {
    std::array<float, kMaxVoices> priority{};
    priority.fill(-1.0f);
    for (int v = 0; v < numVoices; ++v) {
        if (!voiceOnOwnGroup_[(size_t)v] || convolver_.isCrossfading(v + 1))
            continue;
        const Controls& d = voiceDesigned_[(size_t)v];
        const float dist = std::hypot(cursors_.getX(v) - d.morphX, cursors_.getY(v) - d.morphY);
        const bool redraw = voiceTableDirty_[(size_t)v] || !convolver_.hasKernel(v + 1) || toneMoved(controls, d);
        if (!redraw && dist <= kMorphEpsilon)
            continue;
        // A pitch can step on a silent voice (nothing to hear yet), so only a sounding note jumps the queue, and a
        // voice sounding this block is never idle whatever its history says.
        const bool audible = cursors_.isAudible(v);
        const bool urgent = cursors_.noteStarted(v) && audible;
        if (!audible && voiceIsIdle(v, numChannels))
            continue;
        priority[(size_t)v] = (urgent ? kUrgentPriority : 0.0f) + (redraw ? kRedrawPriority : 0.0f) + dist;
    }
    for (; budget > 0; --budget) {
        const auto best = std::max_element(priority.begin(), priority.end());
        if (*best < 0.0f)
            return;
        const int v = (int)(best - priority.begin());
        *best = -1.0f;
        Controls c = controls;
        c.morphX = cursors_.getX(v);
        c.morphY = cursors_.getY(v);
        designKernel(c);
        convolver_.setKernel(v + 1, kernel_.data(), numSamples);
        voiceDesigned_[(size_t)v] = c;
        voiceTableDirty_[(size_t)v] = false;
        designCount_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace synth
