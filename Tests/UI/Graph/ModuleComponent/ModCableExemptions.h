// ModCableExemptions.h
// The controls that deliberately take no modulation cable, each with the reason it takes none. Shared by the guards
// that walk the cards' controls (ModuleComponentModDotEveryControlTests, ModuleComponentMidiLearnCableTests): a control
// is either a cable target or listed here with its reason.
#pragma once

#include "Modules/DrumKit/DrumKitModule.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace modcable_test {

struct Exemption {
    std::string type, paramId, reason;
    bool onCardAsSlider = true; // false: a stepper or picker, which the slider walk does not reach
};

inline const std::vector<Exemption>& exemptions() {
    static const std::vector<Exemption> all = [] {
        std::vector<Exemption> out;
        const auto add = [&out](const std::string& type, const std::vector<std::string>& ids, const std::string& reason,
                                bool slider = true) {
            for (const auto& id : ids)
                out.push_back({type, id, reason, slider});
        };
        add("Oscillator", {"phase", "randomPhase"},
            "read once when a note starts, so a cable would have nothing to modulate");
        add("Wavetable", {"unison"},
            "a whole number of voices, not a plain amount: a cable dropped on it is refused with a status message");
        for (const auto* sequencer : {"Sequencer", "Poly Sequencer"})
            add(sequencer, {"bpm", "transpose", "gate", "velocity", "chance"},
                "read per step by the sequencer's own clock, not continuously");
        add("MIDI Keyboard", {"octave"}, "a whole octave number that shifts the keys, not a plain amount", false);
        for (const auto* sequencer : {"Sequencer", "Poly Sequencer"})
            add(sequencer, {"steps"}, "a whole step count that changes the pattern's length, not a plain amount",
                false);
        add("Macros", {"macroCount"}, "how many macro knobs the card shows, not a sound setting");
        for (int i = 1; i <= 16; ++i)
            add("Macros", {"macro" + std::to_string(i)}, "a macro knob is itself a modulation source");
        add("Channel Strip",
            {"gain", "pan", "send1Level", "send2Level", "send3Level", "send4Level", "send1Pan", "send2Pan", "send3Pan",
             "send4Pan"},
            "the strip's channel layout is frozen, with its send legs on fixed channels");
        add("Master", {"gain"}, "the strip's channel layout is frozen, with its send legs on fixed channels");
        return out;
    }();
    return all;
}

/** The reason `paramId` on a `type` card takes no cable, or null when it is not exempt. A Drum Kit pad knob is exempt
    for now: its cables come from the pad slots. */
inline const std::string* exemptionReason(const juce::String& type, const juce::String& paramId) {
    for (const auto& e : exemptions())
        if (type == juce::String(e.type) && paramId == juce::String(e.paramId))
            return &e.reason;
    static const std::string padReason = "pad knob cables come from pad slots";
    if (type == "Drum Kit" && DrumKitModule::isPadParamId(paramId))
        return &padReason;
    return nullptr;
}

} // namespace modcable_test
