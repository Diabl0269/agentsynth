#pragma once

// Opens the instrument map in its own window. The window grows out of the Sampler card that asked for it and shrinks
// back into it when it closes (PopupMotion).

#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include <functional>
#include <memory>

namespace synth::ui {

/** Opens a window around `view`, centred on `anchor`'s window and growing out of `anchor`. Returns it, or null when a
 *  test hook took the view instead. Escape or Done closes it. Message thread only. */
juce::DialogWindow* openSamplerMapWindow(std::unique_ptr<SamplerMapView> view, juce::Component* anchor);

namespace test_hooks {
/** When set, opening hands the view to this instead of creating a window (which cannot open on a headless runner).
 *  The test keeps the pointer alive. */
std::function<void(std::unique_ptr<SamplerMapView>)>& samplerMapWindowHookForTest();
} // namespace test_hooks

} // namespace synth::ui
