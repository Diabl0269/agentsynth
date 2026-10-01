#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <variant>
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

/** How a v2 item is drawn. The first four map onto the v1 kinds; the rest need version 2. */
enum class CardWidget { Auto, Knob, KnobLarge, FaderV, FaderH, Toggle, Choice, Segmented, Stepper };

/** A read-only visual a v2 section can hold. */
enum class CardView {
    Scope,
    Response,
    Spectrum,
    Envelope,
    LfoShape,
    LfoCurve,
    Waveform,
    Wavetable,
    EqCurve,
    Threshold,
    GainReduction
};

enum class CardPresentation { Grid, Tab };
enum class CardConditionEffect { Show, Dim };

/** Dim or show an item by another parameter's value: choice value strings, or "true"/"false". */
struct CardCondition {
    juce::String param;
    juce::StringArray is;
    CardConditionEffect effect = CardConditionEffect::Dim;

    bool operator==(const CardCondition& other) const noexcept {
        return param == other.param && is == other.is && effect == other.effect;
    }
};

/** One parameter in a v2 section. `node` is reserved (a member uuid for a future custom module). */
struct CardParamItem {
    juce::String paramId;
    int indexHint = -1; ///< Kept from v1 slots: the rescue key for id-less hosted parameters.
    std::optional<juce::String> node;
    CardWidget widget = CardWidget::Auto;
    std::optional<juce::String> label;
    int span = 1; ///< Grid cells taken, 1..6.
    std::optional<CardCondition> when;

    bool operator==(const CardParamItem& other) const noexcept {
        return paramId == other.paramId && indexHint == other.indexHint && node == other.node &&
               widget == other.widget && label == other.label && span == other.span && when == other.when;
    }
};

struct CardViewItem {
    CardView view = CardView::Scope;
    bool open = true; ///< Collapsible views: open by default or not.

    bool operator==(const CardViewItem& other) const noexcept { return view == other.view && open == other.open; }
};

using CardItem = std::variant<CardParamItem, CardViewItem>;

/** A titled (or untitled) group of items; consecutive Tab sections render as one tab strip. */
struct CardSection {
    static constexpr int kDefaultColumns = 3;

    juce::String id;
    std::optional<juce::String> title;
    int columns = kDefaultColumns; ///< 1..6.
    CardPresentation presentation = CardPresentation::Grid;
    std::optional<CardCondition> visibleWhen;
    std::vector<CardItem> items;

    bool operator==(const CardSection& other) const noexcept {
        return id == other.id && title == other.title && columns == other.columns &&
               presentation == other.presentation && visibleWhen == other.visibleWhen && items == other.items;
    }
};

struct CardLayoutParseResult;

/**
 * What a card shows, persisted as JSON. Plain value type, no UI or plugin knowledge.
 *
 * Exactly one body is in use: `slots` (the flat v1 form, also the implicit single untitled grid
 * section) or `sections` (v2). Reading v1 fills `slots`, reading v2 fills `sections`; `upgradeV1`
 * converts the former. Out-of-range numbers and unknown enum strings refuse the whole layout
 * (Malformed); unknown keys are ignored. docs/layout/module-card-layout.md.
 */
struct CardLayout {
    static constexpr int kLegacyVersion = 1;
    static constexpr int kCurrentVersion = 2;

    int version = kCurrentVersion; ///< As read; toVar() picks 1 or 2 from usesV2Features().
    std::vector<CardSlot> slots;
    std::optional<juce::String> basedOn; ///< "<ModuleType>@<defaultRevision>"; v2 only.
    std::vector<CardSection> sections;
    juce::StringArray hidden; ///< paramIds the user hid; v2 only.

    enum class ParseStatus { Ok, Malformed, UnsupportedVersion };

    /** Writes v1 unless usesV2Features(), so older builds keep reading hosted-plugin cards. */
    juce::var toVar() const;
    static CardLayoutParseResult fromVar(const juce::var& json);

    /** True when the layout cannot be expressed as a flat v1 slot list. */
    bool usesV2Features() const;
    /** `slots`, or every param item across `sections` mapped to the nearest v1 kind. */
    std::vector<CardSlot> flatSlots() const;

    bool operator==(const CardLayout& other) const noexcept {
        return slots == other.slots && basedOn == other.basedOn && sections == other.sections && hidden == other.hidden;
    }
};

struct CardLayoutParseResult {
    CardLayout::ParseStatus status = CardLayout::ParseStatus::Malformed;
    CardLayout layout;
};

/**
 * v1 -> v2: `slots` become one untitled grid section and every id in `allParamIds` that no slot
 * names goes into `hidden` (the v1 rule "absent = hidden", made explicit). Apply once, on read, by
 * whoever has the parameter list. A layout already in sections form is returned unchanged.
 */
CardLayout upgradeV1(const CardLayout& layout, const juce::StringArray& allParamIds);

/** Node property that carries a built-in module's per-instance layout JSON. */
constexpr const char* kCardLayoutNodeProperty = "cardLayout";

/** isBoolean -> Toggle; isDiscrete with value strings -> Choice; otherwise Knob. Never Auto. */
CardSlotKind deriveSlotKind(const juce::AudioProcessorParameter& param);

/** The slot's own kind unless it is Auto, in which case derived from `param` (Knob when null). */
CardSlotKind effectiveSlotKind(const CardSlot& slot, const juce::AudioProcessorParameter* param);

} // namespace synth
