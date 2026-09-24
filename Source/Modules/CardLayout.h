#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

namespace synth {

/** How a slot's widget is drawn. Auto = derive from the bound parameter (deriveSlotKind). */
enum class CardSlotKind { Auto, Knob, Toggle, Choice };

/** One parameter on a card. Plain data, plugin-agnostic (docs/control/plugin-card-layout.md). */
struct CardSlot {
    juce::String paramId;              ///< The parameter's stable id (or "legacy:<index>").
    int indexHint = -1;                ///< Index at creation; the rescue key for id-less parameters.
    std::optional<juce::String> label; ///< User override; nullopt = the parameter's own name.
    CardSlotKind kind = CardSlotKind::Auto;

    bool operator==(const CardSlot& other) const noexcept {
        return paramId == other.paramId && indexHint == other.indexHint && label == other.label && kind == other.kind;
    }
};

struct CardLayoutParseResult;

/** Ordered list of slots a card shows. Persisted as JSON; version 1 is the only one understood. */
struct CardLayout {
    static constexpr int kCurrentVersion = 1;

    int version = kCurrentVersion;
    std::vector<CardSlot> slots;

    enum class ParseStatus { Ok, Malformed, UnsupportedVersion };

    juce::var toVar() const;
    static CardLayoutParseResult fromVar(const juce::var& json);

    bool operator==(const CardLayout& other) const noexcept { return version == other.version && slots == other.slots; }
};

struct CardLayoutParseResult {
    CardLayout::ParseStatus status = CardLayout::ParseStatus::Malformed;
    CardLayout layout;
};

/** isBoolean -> Toggle; isDiscrete with value strings -> Choice; otherwise Knob. Never Auto. */
CardSlotKind deriveSlotKind(const juce::AudioProcessorParameter& param);

/** The slot's own kind unless it is Auto, in which case derived from `param` (Knob when null). */
CardSlotKind effectiveSlotKind(const CardSlot& slot, const juce::AudioProcessorParameter* param);

} // namespace synth
