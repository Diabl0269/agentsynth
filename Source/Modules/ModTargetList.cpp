// ModTargetList: a module's knob-only cables declared once, from which its channels, port labels and modulation
// targets follow. See docs/modules/modulation.md#one-list-per-module.
#include "Modules/ModTargetList.h"

namespace synth {

// Resolved by id, never by index: parameter order is not part of a module's contract. A spec naming no parameter is a
// programming error caught in debug; in release its channel simply reads as unmodulated.
void ModTargetList::bind(const juce::AudioProcessor& module) {
    for (auto& entry : entries_) {
        entry.param = nullptr;
        for (auto* p : module.getParameters())
            if (auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(p);
                ranged != nullptr && ranged->paramID == entry.spec.paramId) {
                entry.param = ranged;
                break;
            }
        jassert(entry.param != nullptr);
    }
}

int ModTargetList::channelOf(const juce::String& paramId) const {
    for (int k = 0; k < size(); ++k)
        if (paramId == entries_[(std::size_t)k].spec.paramId)
            return firstChannel_ + k;
    return -1;
}

const juce::RangedAudioParameter* ModTargetList::paramOn(int channel) const noexcept {
    return ownsChannel(channel) ? entries_[(std::size_t)(channel - firstChannel_)].param : nullptr;
}

ModRead ModTargetList::readOn(int channel) const noexcept {
    return ownsChannel(channel) ? entries_[(std::size_t)(channel - firstChannel_)].spec.read : ModRead::Live;
}

std::vector<ModulationTarget> ModTargetList::targets() const {
    std::vector<ModulationTarget> out;
    out.reserve(entries_.size());
    for (int k = 0; k < size(); ++k) {
        const auto& entry = entries_[(std::size_t)k];
        const auto name = entry.param != nullptr ? entry.param->getName(64) : juce::String(entry.spec.paramId);
        out.push_back({name, firstChannel_ + k, entry.spec.paramId, /*knobOnly*/ true});
    }
    return out;
}

juce::String ModTargetList::label(int channel) const {
    const auto* param = paramOn(channel);
    return param != nullptr ? param->getName(64) : juce::String();
}

// A linear scan: a list holds a few dozen entries at most, and this runs a handful of times per note.
int ModTargetList::indexOf(const juce::RangedAudioParameter& param) const noexcept {
    for (int k = 0; k < size(); ++k)
        if (entries_[(std::size_t)k].param == &param)
            return k;
    return -1;
}

float ModTargetList::cvAt(const juce::AudioBuffer<float>& buffer, const juce::RangedAudioParameter& param,
                          int sample) const noexcept {
    const int k = indexOf(param);
    const int channel = firstChannel_ + k;
    if (k < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return 0.0f;
    return buffer.getReadPointer(channel)[juce::jlimit(0, buffer.getNumSamples() - 1, sample)];
}

float ModTargetList::valueAt(const juce::AudioBuffer<float>& buffer, const juce::RangedAudioParameter& param,
                             int sample) const noexcept {
    const float base = param.convertFrom0to1(param.getValue());
    return ModuleBase::modulateNormalised(param, base, cvAt(buffer, param, sample));
}

} // namespace synth
