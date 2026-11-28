// Concern: rendering one pass of a TrackRenderPlan: the per-job steps, their input mixes and the graph's IO nodes.
#include "AudioEngine/TrackRenderPlan.h"
#include "AudioEngine/RealtimeScope.h"
#include "AudioEngine/TrackRenderPlanSteps.h"
#include <utility>

namespace synth {

TrackRenderPlan::TrackRenderPlan() = default;
TrackRenderPlan::~TrackRenderPlan() = default;

bool TrackRenderPlan::fits(const juce::AudioBuffer<float>& io) const noexcept {
    return usable() && io.getNumSamples() > 0 && io.getNumSamples() <= maxBlockSize_ &&
           io.getNumChannels() <= maxIoChannels_;
}

// The graph's own output buffer is cleared at max(1, io channels), exactly as JUCE's render sequence sizes it, so an
// Audio Output node sums into the same channel count either way.
void TrackRenderPlan::beginPass(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi,
                                juce::AudioPlayHead* playHead) noexcept {
    numSamples_ = io.getNumSamples();
    audioIn_ = &io;
    midiIn_ = &midi;
    playHead_ = playHead;
    outChannels_ = std::max(1, io.getNumChannels());
    audioOutClear_ = true;
    for (int ch = 0; ch < outChannels_; ++ch)
        juce::FloatVectorOperations::clear(audioOut_.getWritePointer(ch), numSamples_);
    midiOut_.clear();
}

void TrackRenderPlan::endPass(juce::AudioBuffer<float>& io, juce::MidiBuffer& midi) noexcept {
    for (int ch = 0; ch < io.getNumChannels(); ++ch)
        juce::FloatVectorOperations::copy(io.getWritePointer(ch), audioOut_.getReadPointer(ch), numSamples_);
    midi.clear();
    midi.addEvents(midiOut_, 0, numSamples_, 0);
    audioIn_ = nullptr;
    midiIn_ = nullptr;
}

int TrackRenderPlan::numJobs() const noexcept { return (int)jobs_.size(); }
int TrackRenderPlan::numWaits(int job) const noexcept { return jobs_[(size_t)job]->waits; }
std::span<const int> TrackRenderPlan::successors(int job) const noexcept { return jobs_[(size_t)job]->successors; }

void TrackRenderPlan::runJob(int job) noexcept {
    auto& j = *jobs_[(size_t)job];
    for (auto* step : j.steps)
        renderStep(*step, j);
}

// One node, as JUCE's NodeOp/ProcessOp renders it: its inputs mixed into its channels (the first source copied, the
// rest added in the order the graph's builder adds them, each delayed first when its latency is short of the
// node's), output-only channels cleared, then processBlock (or processBlockBypassed) in place under the node's
// callback lock. The four IO node kinds read or write the pass's own input and output instead.
void TrackRenderPlan::renderStep(Step& step, Job& job) noexcept {
    const int n = numSamples_;
    for (const auto& mix : step.mixes) {
        float* dst = step.channels[(size_t)mix.channel];
        if (mix.first.source != nullptr)
            juce::FloatVectorOperations::copy(dst, mix.first.source, n);
        else
            juce::FloatVectorOperations::clear(dst, n);
        if (mix.first.delay >= 0)
            delays_[(size_t)mix.first.delay]->process(dst, n);
        for (const auto& feed : mix.rest) {
            const float* src = feed.source;
            if (feed.delay >= 0) {
                juce::FloatVectorOperations::copy(job.delayScratch, src, n);
                delays_[(size_t)feed.delay]->process(job.delayScratch, n);
                src = job.delayScratch;
            }
            juce::FloatVectorOperations::add(dst, src, n);
        }
    }
    for (const int ch : step.clears)
        juce::FloatVectorOperations::clear(step.channels[(size_t)ch], n);

    step.midi.clear();
    if (step.midiFirst != nullptr)
        step.midi.addEvents(*step.midiFirst, 0, -1, 0);
    for (const auto* src : step.midiRest)
        step.midi.addEvents(*src, 0, n, 0);

    auto& processor = *step.processor;
    processor.setPlayHead(playHead_);
    auto& buffer = step.view;
    buffer.setDataToReferTo(step.channels.data(), step.numAudioChannels, n);
    if (processor.isSuspended()) {
        buffer.clear();
        return;
    }
    const bool bypass = step.node->isBypassed() && processor.getBypassParameter() == nullptr;
    if (bypass && step.io != StepIo::None)
        return;
    switch (step.io) {
    case StepIo::None: {
        // JUCE's own per-node lock and the module's render, same pause/re-arm split as the single-thread graph call.
        const RealtimePause graphIsJuces;
        const juce::ScopedLock lock(processor.getCallbackLock());
        if (bypass)
            processor.processBlockBypassed(buffer, step.midi);
        else
            processor.processBlock(buffer, step.midi);
        break;
    }
    case StepIo::AudioIn:
        for (int ch = std::min(audioIn_->getNumChannels(), buffer.getNumChannels()); --ch >= 0;)
            buffer.copyFrom(ch, 0, *audioIn_, ch, 0, n);
        break;
    case StepIo::AudioOut:
        // juce::AudioBuffer::addFrom into a cleared buffer copies instead of adding (it keeps a -0 a -0), and JUCE
        // walks the channels from the top down, so the first channel written copies and the rest add onto zeros.
        for (int ch = std::min(outChannels_, buffer.getNumChannels()); --ch >= 0;) {
            if (std::exchange(audioOutClear_, false))
                juce::FloatVectorOperations::copy(audioOut_.getWritePointer(ch), buffer.getReadPointer(ch), n);
            else
                juce::FloatVectorOperations::add(audioOut_.getWritePointer(ch), buffer.getReadPointer(ch), n);
        }
        break;
    case StepIo::MidiIn:
        step.midi.addEvents(*midiIn_, 0, n, 0);
        break;
    case StepIo::MidiOut:
        midiOut_.addEvents(step.midi, 0, n, 0);
        break;
    }
}

} // namespace synth
