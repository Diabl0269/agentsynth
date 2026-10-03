#pragma once

// The right-click quick path on a module card's controls: hide a control from the card, put it back,
// or switch a continuous control between a knob and a fader. Each click is one write of the node's
// "cardLayout" override (one undo step) starting from the layout the card draws now.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "Modules/CardLayout.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;

namespace synth {

class CardBody;

enum class CardQuickEdit { Hide, ShowOnCard, ShowAsFader, ShowAsKnob };

/** `layout` with `paramId` changed by `edit`. Pure. */
CardLayout applyCardQuickEdit(CardLayout layout, const juce::String& paramId, CardQuickEdit edit);

/**
 * Applies `edit` to node `nodeId`'s card as ONE undo step through `undo` (null applies without
 * recording): writes the override, rebuilds the card and makes room for (or gives back) any change in
 * its height. False when the node has no data-driven card or nothing changes. Message thread only.
 */
bool performCardQuickEdit(GraphEditor& editor, ::AppUndoManager* undo, juce::AudioProcessorGraph::NodeID nodeId,
                          const juce::String& paramId, CardQuickEdit edit);

/**
 * Appends a separator and the layout items for `paramId`'s control on `body`'s card ("Hide from card"
 * or "Show on card", "Show as fader" or "Show as knob", "Edit Layout...", "Layout List..."). Nothing when `body` is
 * null, its card is not drawn from layout data, or it shows no control for `paramId`.
 */
void appendCardLayoutMenuItems(juce::PopupMenu& menu, GraphEditor& editor, ::AppUndoManager* undo,
                               juce::AudioProcessorGraph::NodeID nodeId, const CardBody* body,
                               const juce::String& paramId);

/** The card layout editor's entry ("Edit Layout..."); `open` runs after the menu closes. */
void appendEditLayoutMenuItem(juce::PopupMenu& menu, std::function<void()> open);

/** The layout list's entry ("Layout List..."), the list editor beside the card. */
void appendLayoutListMenuItem(juce::PopupMenu& menu, std::function<void()> open);

} // namespace synth
