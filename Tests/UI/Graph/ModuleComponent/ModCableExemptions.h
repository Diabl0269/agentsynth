// ModCableExemptions.h
// The controls that deliberately take no modulation cable, each with the reason it takes none. Shared by the guards
// that walk the cards' controls (ModuleComponentModDotEveryControlTests, ModuleComponentMidiLearnCableTests): a control
// is either a cable target or listed here with its reason.
#pragma once

#include "Modules/ModSlotHost.h"
#include "Modules/ModuleBase.h"
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
        add("MIDI Keyboard", {"octave"}, "a whole octave number that shifts the keys, not a plain amount", false);
        for (const auto* sequencer : {"Sequencer", "Poly Sequencer"})
            add(sequencer, {"steps"}, "a whole step count that changes the pattern's length, not a plain amount",
                false);
        add("Macros", {"macroCount"}, "how many macro knobs the card shows, not a sound setting");
        return out;
    }();
    return all;
}

/** The reason `paramId` on a `type` card takes no cable, or null when it is not exempt. */
inline const std::string* exemptionReason(const juce::String& type, const juce::String& paramId) {
    for (const auto& e : exemptions())
        if (type == juce::String(e.type) && paramId == juce::String(e.paramId))
            return &e.reason;
    return nullptr;
}

/** True when a cable can reach `paramId` on `module` without a listed target: a module that binds modulation slots on
    demand (a hosted plugin, the Drum Kit's pad knobs) answers for it. */
inline bool slotHostTakesCable(const juce::AudioProcessor& module, const juce::String& paramId) {
    const auto* host = dynamic_cast<const synth::ModSlotHost*>(&module);
    return host != nullptr && host->canModulateParam(paramId);
}

} // namespace modcable_test
