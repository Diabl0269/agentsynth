#pragma once

#include "MainComponent.h"

namespace synth {

/** The key a window receives while keyboard focus sits on nothing, or on the window itself: handed to the
 *  MainComponent it holds, whose keyPressed is the one place the shortcut table runs. False when `content` is not one
 *  (or the key is unbound). */
inline bool forwardWindowKeyToMainComponent(juce::Component* content, const juce::KeyPress& key) {
    auto* main = dynamic_cast<MainComponent*>(content);
    return main != nullptr && main->keyPressed(key);
}

} // namespace synth
