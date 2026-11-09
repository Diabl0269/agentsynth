#pragma once

#include "Modules/ModuleBase.h"
#include <array>
#include <cstddef>
#include <vector>

namespace synth {

/** When a module reads a knob's cable: every block (Live), or once, on the sample a note starts (NoteOn) or ends
    (NoteOff). */
enum class ModRead { Live, NoteOn, NoteOff };

/** One knob that takes a cable: the parameter's id and when the module reads its cable. */
struct ModTargetSpec {
    const char* paramId;
    ModRead read;
};

/** A module's knob-only modulation targets, declared once as a list. Entry k owns raw input channel
    `firstChannel + k`, so the module's input count, its port labels and getModulationTargets() all come from the
    list. Append new entries at the end: saved cables keep their channel. Bind after the parameters exist. */
class ModTargetList {
public:
    template <std::size_t N>
    explicit ModTargetList(const std::array<ModTargetSpec, N>& specs, int firstChannel = 0)
        : firstChannel_(firstChannel) {
        entries_.reserve(N);
        for (const auto& spec : specs)
            entries_.push_back({spec, nullptr});
    }

    /** Resolves every entry's parameter on `module`. Message thread, once, after the last addParameter. */
    void bind(const juce::AudioProcessor& module);

    int size() const noexcept { return (int)entries_.size(); }
    int firstChannel() const noexcept { return firstChannel_; }
    int endChannel() const noexcept { return firstChannel_ + size(); }
    bool ownsChannel(int channel) const noexcept { return channel >= firstChannel_ && channel < endChannel(); }

    /** The raw channel of `paramId`'s cable, or -1 when the list does not hold it. */
    int channelOf(const juce::String& paramId) const;
    /** The parameter on `channel`, or null when the list does not own that channel. */
    const juce::RangedAudioParameter* paramOn(int channel) const noexcept;
    /** When the module reads the cable on `channel` (Live for a channel the list does not own). */
    ModRead readOn(int channel) const noexcept;

    /** The port of an owned `channel`: a cable-only head of its own, indexed by the raw channel so a drop on the knob
        maps raw -> jack -> raw back to the same channel. */
    LogicalPort portOf(int channel) const;
    /** One knob-only target per entry, named with its parameter's display name. */
    std::vector<ModulationTarget> targets() const;
    /** The port label of `channel`: its parameter's display name, empty when the list does not own it. */
    juce::String label(int channel) const;

    /** Audio-safe. The cable of `param` at `sample` (0 when the list does not hold it or the buffer lacks the
        channel). Read a NoteOn / NoteOff cable on the note's sample, before the render writes that sample. */
    float cvAt(const juce::AudioBuffer<float>& buffer, const juce::RangedAudioParameter& param,
               int sample) const noexcept;
    /** Audio-safe. `param`'s current value moved by its cable at `sample`, in its normalised range. */
    float valueAt(const juce::AudioBuffer<float>& buffer, const juce::RangedAudioParameter& param,
                  int sample) const noexcept;

private:
    struct Entry {
        ModTargetSpec spec;
        const juce::RangedAudioParameter* param;
    };
    int indexOf(const juce::RangedAudioParameter& param) const noexcept;

    int firstChannel_ = 0;
    std::vector<Entry> entries_;
};

} // namespace synth
