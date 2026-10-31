#pragma once

// Opens the small curve box beside an anchor (a chip, or the pointer on a cable) in the shared panel window, and wires
// a chip to it. One box is open at a time; opening another closes the first.

#include "ModCurveBox.h"
#include "ModCurveChip.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include <functional>

class GraphEditor;

namespace synth::ui {

/** Opens the box for `attenuverter` with its arrow at `anchorScreenBounds`. `anchorIsRegion`: the anchor is a spot on a
 *  bigger component (a cable), so only a press inside those bounds counts as the owner's own click. Focus goes back to
 *  `anchor` when the box closes. Null when a test launcher took the box. */
ModDotPanelFrame* openModCurvePopover(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter,
                                      juce::Component& anchor, juce::Rectangle<int> anchorScreenBounds,
                                      bool anchorIsRegion);

bool isModCurvePopoverOpen();
/** The attenuverter of the open box (uid 0 when none). */
juce::AudioProcessorGraph::NodeID openModCurvePopoverFor();
ModCurveBox* openModCurveBox();
void closeModCurvePopover();
/** True when the screen point lies inside the open box's window (the dot's panel stays open for presses there). */
bool modCurvePopoverContains(juce::Point<int> screenPoint);
/** Run from the editor's 30 Hz tick: closes the open box when its attenuverter has vanished. */
void tickModCurvePopover();

/** Makes `chip` edit `attenuverter` and open the box on a click (a second click closes it). */
void wireModCurveChip(ModCurveChip& chip, GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter,
                      const juce::String& sourceName);

namespace test_hooks {
/** When set, the box is handed here instead of opening a window; the hook owns it. */
std::function<void(std::unique_ptr<ModCurveBox>, juce::Component& anchor)>& modCurvePopoverLauncherForTest();
} // namespace test_hooks

} // namespace synth::ui
