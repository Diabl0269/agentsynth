#include "KeyGlideHost.h"
#include "UI/Timeline/CursorGlide/PhysicalKeyState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

namespace {
std::optional<ArrowKey> arrowOf(int keyCode) {
    if (keyCode == juce::KeyPress::leftKey)
        return ArrowKey::Left;
    if (keyCode == juce::KeyPress::rightKey)
        return ArrowKey::Right;
    if (keyCode == juce::KeyPress::upKey)
        return ArrowKey::Up;
    if (keyCode == juce::KeyPress::downKey)
        return ArrowKey::Down;
    return std::nullopt;
}

/** A held Cmd chord loses its key-up in JUCE's state on macOS, so the OS's own view wins where there is one. */
bool arrowPhysicallyDown(int keyCode) {
    std::optional<bool> physical;
    if (const auto arrow = arrowOf(keyCode))
        physical = physicalArrowKeyDown(*arrow);
    return physical.value_or(juce::KeyPress::isKeyCurrentlyDown(keyCode));
}
} // namespace

KeyGlideHost makeDefaultKeyGlideHost() {
    KeyGlideHost host;
    host.nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    host.isArrowDown = arrowPhysicallyDown;
    host.isShiftDown = [] { return juce::ModifierKeys::currentModifiers.isShiftDown(); };
    host.isAltDown = [] { return juce::ModifierKeys::currentModifiers.isAltDown(); };
    return host;
}

} // namespace synth::ui
