#pragma once

// ShapeSavePrompts.h -- the questions about a saved shape: what to call it, whether to replace a saved shape of that
// name and whether to delete one. Both are async alert windows in the app's usual idiom (as for saving a snippet); a
// test answers them through the hooks instead, since a modal cannot run headless.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Asks for a shape name, starting from `suggested`; `problem` (may be empty) is shown above the field. `done` gets the
 *  typed text, or is never called when the user cancels. */
void askShapeName(const juce::String& suggested, const juce::String& problem, std::function<void(juce::String)> done);

/** Asks whether to replace the saved shape `name`; `done` gets true only for Replace. */
void askShapeReplace(const juce::String& name, std::function<void(bool)> done);

/** Asks whether to delete the saved shape `name` from disk; `done` gets true only for Delete. */
void askShapeDelete(const juce::String& name, std::function<void(bool)> done);

namespace test_hooks {
/** When set, askShapeName calls this with its arguments instead of opening a window. */
std::function<void(const juce::String& suggested, const juce::String& problem, std::function<void(juce::String)>)>&
shapeNamePromptHookForTest();
/** When set, askShapeReplace calls this instead of opening a window. */
std::function<void(const juce::String& name, std::function<void(bool)>)>& shapeReplacePromptHookForTest();
/** When set, askShapeDelete calls this instead of opening a window. */
std::function<void(const juce::String& name, std::function<void(bool)>)>& shapeDeletePromptHookForTest();
} // namespace test_hooks

} // namespace synth::ui
