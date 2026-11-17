#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// A 1x1 child that takes no clicks and only answers test-mode getState with what `fill` adds, so the owning surface's
// own header stays free of TestModeStateSource.h. Add it to the surface and give it `id`. Defined in
// AutomationLanes/LaneToolCursors.cpp.
std::unique_ptr<juce::Component> makeLaneStateProbe(const juce::String& id,
                                                    std::function<void(juce::DynamicObject&)> fill);

} // namespace synth::ui
