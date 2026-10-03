#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::test_hooks {

/** When set, a lane header, modulator row or modulator band hands its menu here with the options placing it
 *  instead of showing it (a popup menu cannot run in a headless test). */
std::function<void(const juce::PopupMenu&, const juce::PopupMenu::Options&)>& laneMenuHookForTest();

} // namespace synth::ui::test_hooks
