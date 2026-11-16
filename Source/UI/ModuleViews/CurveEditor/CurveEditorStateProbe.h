#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class CurveEditorComponent;

// A 1x1 child for the curve editor that takes no clicks and only answers test-mode getState: `points` (the node
// count), `selected` (the selected count), `cursor` (the cursor node, -1 none) and `xs` (every node's x, in order, two
// decimals, space-separated). The editor's own header stays free of TestModeStateSource.h.
std::unique_ptr<juce::Component> makeCurveEditorStateProbe(const CurveEditorComponent& editor);

} // namespace synth::ui
