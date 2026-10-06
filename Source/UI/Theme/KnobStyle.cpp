#include "KnobStyle.h"

#include <array>

namespace synth::theme {

namespace {
struct StyleInfo {
    KnobStyle style;
    const char* id;
    const char* label;
};

constexpr std::array<StyleInfo, kKnobStyleCount> kStyles{{
    {KnobStyle::Classic, "classic", "Classic"},
    {KnobStyle::Hardware, "hardware", "Chunky"},
    {KnobStyle::Analog, "analog", "Analog"},
    {KnobStyle::Neon, "neon", "Neon"},
    {KnobStyle::Ring, "ring", "Ring"},
}};

// Styles that were removed, and the style a saved setting naming one now loads as.
struct RetiredId {
    const char* id;
    KnobStyle successor;
};

constexpr std::array<RetiredId, 2> kRetired{{
    {"polished", KnobStyle::Analog},
    {"soft", KnobStyle::Hardware},
}};

const StyleInfo& infoFor(KnobStyle style) noexcept {
    const auto index = static_cast<size_t>(style);
    return kStyles[index < kStyles.size() ? index : 0];
}
} // namespace

const char* knobStyleId(KnobStyle style) noexcept { return infoFor(style).id; }

const char* knobStyleLabel(KnobStyle style) noexcept { return infoFor(style).label; }

KnobStyle knobStyleFromId(const juce::String& id) noexcept {
    for (const auto& info : kStyles)
        if (id == info.id)
            return info.style;
    for (const auto& retired : kRetired)
        if (id == retired.id)
            return retired.successor;
    return KnobStyle::Classic;
}

KnobAppearance loadKnobAppearance(juce::PropertiesFile& props) {
    KnobAppearance appearance;
    if (props.containsKey(knobStyleKey()))
        appearance.style = knobStyleFromId(props.getValue(knobStyleKey()));
    appearance.colourByFamily = props.getBoolValue(knobColourByFamilyKey(), true);
    return appearance;
}

void writeKnobAppearance(juce::PropertiesFile& props, const KnobAppearance& appearance) {
    props.setValue(knobStyleKey(), juce::String(knobStyleId(appearance.style)));
    props.setValue(knobColourByFamilyKey(), appearance.colourByFamily);
}

} // namespace synth::theme
