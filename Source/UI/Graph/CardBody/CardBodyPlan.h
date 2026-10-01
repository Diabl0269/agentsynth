#pragma once

#include "Modules/CardLayout.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth {

/** One thing a card body shows: a parameter's widget or a view. */
struct CardBodyItem {
    enum class Kind { Choice, Knob, Toggle, View };

    Kind kind = Kind::Knob;
    juce::RangedAudioParameter* param = nullptr; ///< Null for a view.
    CardView view = CardView::Scope;             ///< Meaningful for a view only.
    juce::Component* widget = nullptr;           ///< Null until built, and always in a measure-only plan.
    juce::Component* label = nullptr;            ///< The caption above a knob or choice; null otherwise.
};

/**
 * What a card body shows and where, built from a module and its resolved layout without creating
 * any component, so the same plan can be measured statically or built and laid out live.
 * docs/layout/module-card-layout.md#rendering.
 */
struct CardBodyPlan {
    struct Section {
        int columns = CardSection::kDefaultColumns;
        std::vector<int> items; ///< Indices into `items`, in card order.
    };

    std::vector<CardBodyItem> items; ///< Parameter items in declaration order (the build order), then views.
    std::vector<Section> sections;   ///< Placement, top to bottom.
    std::vector<int> more;           ///< Hidden or unplaced parameter items, in declaration order.

    /** `layout` nullopt = the automatic layout. The module must outlive the plan. */
    static CardBodyPlan forModule(juce::AudioProcessor& module, const std::optional<CardLayout>& layout);

    /** The parameter item for `paramId`, or -1. */
    int findParam(const juce::String& paramId) const;
};

/** True when the card builds its parameter widgets through a CardBody (every built-in module). */
bool cardBodyBuildsWidgetsFor(juce::AudioProcessor& module);

/** True when the card's body is laid out from layout data (false for the bespoke cards). */
bool cardBodyLayoutIsDataDriven(juce::AudioProcessor& module);

} // namespace synth
