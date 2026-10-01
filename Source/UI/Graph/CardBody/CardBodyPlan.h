#pragma once

#include "Modules/CardLayout.h"
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

        /** True when the section draws a header row: a non-empty title over at least one item. */
        bool hasHeader() const { return title.has_value() && title->trim().isNotEmpty() && !items.empty(); }
    };

    std::vector<CardBodyItem> items; ///< Parameter items in declaration order (the build order), then views.
    std::vector<Section> sections;   ///< Placement, top to bottom.
    std::vector<int> more;           ///< Hidden or unplaced parameter items, in declaration order.

    /** `layout` nullopt = the automatic layout. The module must outlive the plan. */
    static CardBodyPlan forModule(juce::AudioProcessor& module, const std::optional<CardLayout>& layout);

    /** The parameter item for `paramId`, or -1. */
    int findParam(const juce::String& paramId) const;
};

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
