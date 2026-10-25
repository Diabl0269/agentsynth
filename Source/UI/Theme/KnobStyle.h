#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace synth::theme {

// The look a rotary knob or a fader in the app takes (Settings > Appearance > Controls): knobs and faders
// each pick one of these independently.
// The enum order is the picker order.
enum class KnobStyle { Classic, Hardware, Analog, Neon, Ring };

inline constexpr int kKnobStyleCount = 5;

// Persisted ids and picker labels: never rename a shipped id (Hardware keeps "hardware" under the label
// Chunky). The retired ids load as their successors ("polished" as Analog, "soft" as Hardware); any
// other unknown id loads as Classic.
const char* knobStyleId(KnobStyle style) noexcept;
const char* knobStyleLabel(KnobStyle style) noexcept;
KnobStyle knobStyleFromId(const juce::String& id) noexcept;
// Like knobStyleFromId, but nullopt for an id that is neither current nor retired.
std::optional<KnobStyle> knobStyleFromKnownId(const juce::String& id) noexcept;

struct KnobAppearance {
    KnobStyle style = KnobStyle::Classic;      // every rotary knob
    bool colourByFamily = true;                // value colour follows the module family when the control sits in a card
    KnobStyle faderStyle = KnobStyle::Classic; // every linear slider (fader)

    bool operator==(const KnobAppearance& other) const noexcept = default;
};

inline const char* knobStyleKey() noexcept { return "knobStyle"; }
inline const char* knobColourByFamilyKey() noexcept { return "knobColourByFamily"; }
inline const char* faderStyleKey() noexcept { return "faderStyle"; }

// Missing or unknown keys fall back to the defaults above, except that a missing fader style takes the
// loaded knob style: settings saved when one style drove both keep their faders unchanged.
KnobAppearance loadKnobAppearance(juce::PropertiesFile& props);
// Writes all three keys without flushing; call props.saveIfNeeded() at the commit point.
void writeKnobAppearance(juce::PropertiesFile& props, const KnobAppearance& appearance);

} // namespace synth::theme
