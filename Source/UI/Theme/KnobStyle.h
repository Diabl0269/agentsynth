#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace synth::theme {

// The look every rotary knob in the app takes (Settings > Appearance > Knobs).
enum class KnobStyle { Classic, Polished, Hardware, Neon, Ring, Soft };

inline constexpr int kKnobStyleCount = 6;

// Persisted ids and picker labels: never rename a shipped id. An unknown id loads as Polished.
const char* knobStyleId(KnobStyle style) noexcept;
const char* knobStyleLabel(KnobStyle style) noexcept;
KnobStyle knobStyleFromId(const juce::String& id) noexcept;

struct KnobAppearance {
    KnobStyle style = KnobStyle::Polished;
    bool colourByFamily = true; // value colour follows the module family when the knob sits in a card

    bool operator==(const KnobAppearance& other) const noexcept = default;
};

inline const char* knobStyleKey() noexcept { return "knobStyle"; }
inline const char* knobColourByFamilyKey() noexcept { return "knobColourByFamily"; }

// Missing or unknown keys fall back to the defaults above.
KnobAppearance loadKnobAppearance(juce::PropertiesFile& props);
// Writes both keys without flushing; call props.saveIfNeeded() at the commit point.
void writeKnobAppearance(juce::PropertiesFile& props, const KnobAppearance& appearance);

} // namespace synth::theme
