#pragma once

#include "Modules/CardLayout.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth {

/** One thing a card body shows: a parameter's widget or a view. */
struct CardBodyItem {
    /** The widget drawn; each kind lays out as its own run (CardBodyLayout.cpp). */
    enum class Kind { Choice, Knob, Toggle, View, KnobLarge, FaderV, FaderH, Segmented, Stepper };

    Kind kind = Kind::Knob;
    juce::RangedAudioParameter* param = nullptr; ///< Null for a view.
    CardView view = CardView::Scope;             ///< Meaningful for a view only.
    juce::Component* widget = nullptr;           ///< Null until built, and always in a measure-only plan.
    juce::Component* label = nullptr;            ///< The caption above a knob or choice; null otherwise.
    std::optional<juce::String> caption;         ///< The layout's label override; nullopt = the parameter's name.
    std::optional<CardCondition> when;           ///< The layout item's condition; nullopt = none.
    int section = -1;                            ///< Index into CardBodyPlan::sections; -1 = More or unplaced.
    int swapGroup = -1;                          ///< Index into CardBodyPlan::swapGroups; -1 = none.
    bool shown = true;                           ///< A `show` condition's current result.
    bool dimmed = false;                         ///< A `dim` condition or a code dim rule greys it out now.
    bool pill = false;                           ///< A footer toggle, drawn as the small pill.

    /** The text the card shows for this parameter: the override, else the parameter's name. */
    juce::String captionText() const;
};

/**
 * What a card body shows and where, built from a module and its resolved layout without creating
 * any component, so the same plan can be measured statically or built and laid out live.
 * docs/layout/module-card-layout.md#rendering.
 */
struct CardBodyPlan {
    struct Section {
        int columns = CardSection::kDefaultColumns;
        std::vector<int> items;            ///< Indices into `items`, in card order.
        std::optional<juce::String> title; ///< Drawn as a header row above the items; nullopt = none.
        juce::Component* header = nullptr; ///< The header row; null until built, and without a title.
        std::optional<CardCondition> visibleWhen;
        bool visible = true; ///< visibleWhen's current result.
        bool footer = false; ///< The footer row (CardSection::kFooterId), always laid out last.

        /** True when the section draws a header row: a non-empty title over at least one item. */
        bool hasHeader() const { return title.has_value() && title->trim().isNotEmpty() && !items.empty(); }
    };

    /** Consecutive `show` items testing one parameter: they share one cell, so a swap never resizes. */
    struct SwapGroup {
        std::vector<int> members;                               ///< Item indices, in layout order.
        CardBodyItem::Kind cellKind = CardBodyItem::Kind::Knob; ///< The member kind with the tallest cell.
        int span = 1;                                           ///< The widest member's span (spans are not drawn yet).
    };

    /** A code dim rule bound to this module's items and parameters. */
    struct DimRule {
        int item = -1;
        std::vector<juce::RangedAudioParameter*> watched;
        std::function<bool(juce::AudioProcessor&)> dims;
    };

    /** What a re-evaluation changed: any result at all, and whether a section appeared or went. */
    struct ConditionChange {
        bool any = false;
        bool sections = false;
    };

    std::vector<CardBodyItem> items; ///< Parameter items in declaration order (the build order), then views.
    std::vector<Section> sections;   ///< Placement, top to bottom; footer sections last.
    std::vector<int> more;           ///< Hidden or unplaced parameter items, in declaration order.
    std::vector<SwapGroup> swapGroups;
    std::vector<DimRule> dimRules;

    /** `layout` nullopt = the automatic layout; `dimRules` apply only with a layout. Conditions are
     *  evaluated against the module's current values. The module must outlive the plan. */
    static CardBodyPlan forModule(juce::AudioProcessor& module, const std::optional<CardLayout>& layout,
                                  const std::vector<CardDimRule>& dimRules = {});

    /** Re-reads every condition and dim rule against `module`'s current values. Message thread. */
    ConditionChange evaluateConditions(juce::AudioProcessor& module);
    /** Every parameter a condition or dim rule reads, once each. */
    std::vector<juce::RangedAudioParameter*> watchedParameters(juce::AudioProcessor& module) const;

    /** The parameter item for `paramId`, or -1. */
    int findParam(const juce::String& paramId) const;
    /** True when the item sits in a section shown now (a swapped-out member keeps its cell). */
    bool isOnCard(int item) const;
    bool hasFooter() const;
};

/** True when `condition` holds for `module` now: a choice's value string or a bool's "true"/"false"
 *  is in `is`. A parameter the module lacks, or of another type, never applies its effect (true). */
bool cardConditionHolds(const CardCondition& condition, juce::AudioProcessor& module);

/** True for the slider kinds (knob, large knob, faders): a modulation target lands on these. */
bool isContinuousKind(CardBodyItem::Kind kind);

/** The kind `widget` draws `param` as, or the parameter's automatic kind when the widget does not
 *  suit it (a fader on a choice, a segmented switch with too many values); nullopt = no widget. */
std::optional<CardBodyItem::Kind> cardBodyKindFor(const juce::RangedAudioParameter& param, CardWidget widget);

/** True when the card builds its parameter widgets through a CardBody (every built-in module). */
bool cardBodyBuildsWidgetsFor(juce::AudioProcessor& module);

/** True when the card's body is laid out from layout data (false for the bespoke cards). */
bool cardBodyLayoutIsDataDriven(juce::AudioProcessor& module);

} // namespace synth
