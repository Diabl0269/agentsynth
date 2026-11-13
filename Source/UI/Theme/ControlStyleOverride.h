#pragma once

#include "UI/Theme/KnobStyle.h"
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

namespace synth::theme {

// A module's own knob style and fader style; an unset kind follows the next level down.
struct ControlStyleOverride {
    std::optional<KnobStyle> knob;
    std::optional<KnobStyle> fader;

    bool isEmpty() const noexcept { return !knob && !fader; }
    bool operator==(const ControlStyleOverride& other) const noexcept = default;
};

// {"knob":"neon","fader":"ring"} with only the set keys; void when nothing is set.
juce::var toVar(const ControlStyleOverride& style);
// Ids this build does not know are read as unset.
ControlStyleOverride controlStyleOverrideFromVar(const juce::var& value);

// The style a control paints: its module's own, else the one set for every module of its type, else the app's.
KnobStyle resolveControlStyle(std::optional<KnobStyle> instance, std::optional<KnobStyle> type, KnobStyle app) noexcept;

// User-settings key holding {"<factory type name>": {"knob":..,"fader":..}}.
inline const char* controlStyleByTypeKey() noexcept { return "controlStyleByType"; }

ControlStyleOverride loadTypeControlStyle(juce::PropertiesFile& props, const juce::String& type);
// An empty override removes the type's entry. Does not flush; call props.saveIfNeeded() at the commit point.
void writeTypeControlStyle(juce::PropertiesFile& props, const juce::String& type, const ControlStyleOverride& style);

} // namespace synth::theme
