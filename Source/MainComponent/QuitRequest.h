#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/** What the app does when the system asks it to quit (the Quit menu item, Cmd+Q): with unsaved changes in the project
 *  `mainContent` shows, asks Save / Discard / Cancel first and calls `quit` only on Save (once written) or Discard;
 *  with none, or with no main component, calls `quit` at once. The question is asynchronous, so `quit` may run later or
 *  never. */
void requestQuit(juce::Component* mainContent, std::function<void()> quit);

} // namespace synth
