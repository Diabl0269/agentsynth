#pragma once

#include "Modules/CardLayout.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth {

/**
 * The view registry: view id -> how to build the existing ModuleViews component for a module and
 * how tall it lays out. Only the views a card body places are registered; a layout naming any
 * other view leaves it out. docs/layout/module-card-layout.md#rendering.
 */
struct CardViewFactory {
    /** Null when the module cannot show this view. Message thread only. */
    std::unique_ptr<juce::Component> (*create)(juce::AudioProcessor& module) = nullptr;
    /** The height the view lays out at; needs no component, so a static measure can ask it too. */
    int (*preferredHeight)(juce::AudioProcessor& module) = nullptr;
    /** The parameter the view edits itself, or empty; it never also gets a widget of its own. */
    juce::String (*ownedParamId)(juce::AudioProcessor& module) = nullptr;
};

/** Null for a view nothing registered. */
const CardViewFactory* findCardViewFactory(CardView view);

/** True when `module` can show `view` (the factory exists and accepts the module). */
bool cardViewAvailableFor(CardView view, juce::AudioProcessor& module);

} // namespace synth
