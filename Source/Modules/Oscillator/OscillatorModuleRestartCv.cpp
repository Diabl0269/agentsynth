// OscillatorModuleRestartCv.cpp -- the Phase and Rand knobs' cables. Both set where a note's waves start, so each is
// read on the sample the note starts. Their channels sit beyond Fixed rate and are cleared with the other outputs
// once the block is under way, so the block copies them first. The class and channel map: OscillatorModule.h.

#include "Modules/OscillatorModule.h"

namespace {
// Places in OscillatorModule::kModTargets. Looked up by place, not by id: this runs on the audio thread, where building
// a String allocates.
constexpr int kPhaseEntry = 0;
constexpr int kRandEntry = 1;
} // namespace

void OscillatorModule::cacheRestartCV(const juce::AudioBuffer<float>& buffer, int numSamples) {
    const int length = std::min(numSamples, (int)phaseCvCache_.size());
    const auto copy = [&](int channel, std::array<float, 4096>& cache) {
        if (channel >= buffer.getNumChannels() || !channelHasSignal(buffer, channel, numSamples))
            return false;
        std::copy_n(buffer.getReadPointer(channel), length, cache.begin());
        return true;
    };
    phaseCvActive_ = copy(mods_.firstChannel() + kPhaseEntry, phaseCvCache_);
    randCvActive_ = copy(mods_.firstChannel() + kRandEntry, randCvCache_);
}

float OscillatorModule::startPhaseDegreesAt(int sample) const noexcept {
    const float cv =
        phaseCvActive_ ? phaseCvCache_[(size_t)juce::jlimit(0, (int)phaseCvCache_.size() - 1, sample)] : 0.0f;
    return modulateNormalised(*phaseParam, phaseParam->get(), cv);
}

float OscillatorModule::randomPhasePercentAt(int sample) const noexcept {
    const float cv = randCvActive_ ? randCvCache_[(size_t)juce::jlimit(0, (int)randCvCache_.size() - 1, sample)] : 0.0f;
    return modulateNormalised(*randomPhaseParam, randomPhaseParam->get(), cv);
}
