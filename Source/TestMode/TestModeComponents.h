#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// TestModeComponents.h: finding components by a stable id and describing them for the test-mode client
// (docs/development/test-mode.md#ids). Message thread only.
namespace synth::testmode {

/** The main DocumentWindow: the top-level whose content is a MainComponent, else the first top-level; null if none. */
juce::Component* findMainWindow();

/** The component an id names. Order: componentID, then name, then accessibility title, each depth first over every
 *  top-level component, preferring one that is showing. "window" is findMainWindow(). "a > b" looks for b inside the
 *  first match of a. `error` is set when nothing matches. */
struct Lookup {
    juce::Component* component = nullptr;
    juce::String error;
};
Lookup findComponent(const juce::String& id);

/** The class name of a component, demangled. */
juce::String typeNameOf(const juce::Component& component);

/** {id,name,title,type,bounds,visible,showing} of one component; bounds are relative to the main window. */
juce::var describeComponent(const juce::Component& component);

/** The tree below `root`, `depth` levels deep, skipping components that are not showing unless `all`. */
juce::var describeTree(const juce::Component& root, int depth, bool all);

/** describeComponent plus enabled, hasFocus, the stock widget fields and any StateSource's own. */
juce::var describeState(const juce::Component& component);

} // namespace synth::testmode
