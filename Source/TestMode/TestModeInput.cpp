// Concern: synthetic mouse and key input delivered through ComponentPeer.

#include "TestMode/TestModeInput.h"

namespace synth::testmode {

namespace {

juce::String normalisedKeyName(const juce::String& raw) {
    const auto name = raw.trim().toLowerCase();
    static const std::pair<const char*, const char*> aliases[] = {
        {"space", "spacebar"},   {"enter", "return"},       {"esc", "escape"},         {"up", "cursor up"},
        {"down", "cursor down"}, {"left", "cursor left"},   {"right", "cursor right"}, {"del", "delete"},
        {"pageup", "page up"},   {"pagedown", "page down"},
    };
    for (const auto& [from, to] : aliases)
        if (name == from)
            return to;
    return name;
}

} // namespace

juce::ModifierKeys modifiersFromNames(const juce::StringArray& names, juce::String& error) {
    int flags = 0;
    for (const auto& raw : names) {
        const auto name = raw.trim().toLowerCase();
        if (name == "cmd" || name == "command")
            flags |= juce::ModifierKeys::commandModifier;
        else if (name == "shift")
            flags |= juce::ModifierKeys::shiftModifier;
        else if (name == "alt" || name == "option")
            flags |= juce::ModifierKeys::altModifier;
        else if (name == "ctrl" || name == "control")
            flags |= juce::ModifierKeys::ctrlModifier;
        else
            error = "unknown modifier \"" + raw + "\"";
    }
    return juce::ModifierKeys(flags);
}

juce::KeyPress keyPressFromText(const juce::String& text, juce::String& error) {
    juce::StringArray parts;
    parts.addTokens(text, "+", "");
    parts.trim();
    if (parts.isEmpty() || parts[parts.size() - 1].isEmpty()) {
        // A lone "+" key arrives as "+" or "shift++": the last token is then empty.
        if (text.trim().endsWithChar('+')) {
            parts.removeEmptyStrings();
            parts.add("+");
        } else {
            error = "no key in \"" + text + "\"";
            return {};
        }
    }
    const auto keyName = parts[parts.size() - 1];
    parts.remove(parts.size() - 1);
    const auto mods = modifiersFromNames(parts, error);
    if (error.isNotEmpty())
        return {};

    // A single character is typed as itself, so a text field receives it; named keys go through JUCE's own table.
    if (keyName.length() == 1) {
        const auto c = keyName[0];
        return juce::KeyPress(static_cast<int>(juce::CharacterFunctions::toUpperCase(c)), mods,
                              mods.isCommandDown() || mods.isCtrlDown() ? 0 : c);
    }
    const auto name = normalisedKeyName(keyName);
    const auto key = juce::KeyPress::createFromDescription(name);
    // createFromDescription falls back to a character for a name it does not know; accept only a name it printed back.
    if (key.getKeyCode() == 0 || !key.getTextDescription().endsWithIgnoreCase(name)) {
        error = "unknown key \"" + keyName + "\"";
        return {};
    }
    return juce::KeyPress(key.getKeyCode(), mods, 0);
}

juce::Point<int> centreOf(const juce::Component& c) { return c.getLocalBounds().getCentre(); }

bool sendMouse(const MouseStep& step, juce::String& error) {
    if (step.target == nullptr) {
        error = "no target component";
        return false;
    }
    auto* peer = step.target->getPeer();
    if (peer == nullptr) {
        error = "the component is not on a native window";
        return false;
    }
    int flags = step.keys.getRawFlags();
    if (step.buttonDown)
        flags |= step.button == Button::left ? juce::ModifierKeys::leftButtonModifier
                                             : juce::ModifierKeys::rightButtonModifier;
    const auto inPeer = peer->getComponent().getLocalPoint(step.target, step.local).toFloat();
    // Which window is in front is not asked here: the automation launch tells JUCE to ignore the window order.
    if (!peer->getComponent().getLocalBounds().contains(peer->getComponent().getLocalPoint(step.target, step.local))) {
        error = "the point is outside the window";
        return false;
    }
    // JUCE reads the keyboard modifiers of a mouse event from this global, not from the event's own flags.
    const auto previous = juce::ModifierKeys::currentModifiers;
    juce::ModifierKeys::currentModifiers = step.keys.withoutMouseButtons();
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, inPeer, juce::ModifierKeys(flags),
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                           juce::Time::currentTimeMillis());
    juce::ModifierKeys::currentModifiers = previous;
    return true;
}

bool click(juce::Component& target, juce::Point<int> local, Button button, int clicks, juce::ModifierKeys keys,
           juce::String& error) {
    MouseStep step{&target, local, keys, false, button};
    if (!sendMouse(step, error))
        return false;
    for (int i = 0; i < juce::jlimit(1, 3, clicks); ++i) {
        step.buttonDown = true;
        if (!sendMouse(step, error))
            return false;
        step.buttonDown = false;
        if (!sendMouse(step, error))
            return false;
    }
    return true;
}

bool pressKey(juce::Component& target, const juce::KeyPress& key, bool& handled, juce::String& error) {
    auto* peer = target.getPeer();
    if (peer == nullptr) {
        error = "the component is not on a native window";
        return false;
    }
    handled = peer->handleKeyPress(key);
    return true;
}

} // namespace synth::testmode
