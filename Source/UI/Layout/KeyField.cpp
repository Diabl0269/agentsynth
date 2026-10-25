#include "UI/Layout/KeyField.h"
#include "Music/SongKey.h"

namespace synth::ui {

// Chrome on the timeline's transport bar and on cards: a click must not move keyboard focus, or the next Cmd+C/V
// would be routed away from the clips. Tabbing to it still focuses it.
KeyField::KeyField() {
    setMouseClickGrabsKeyboardFocus(false);
    setWantsKeyboardFocus(true);
    refreshText();
}

void KeyField::setKey(const std::optional<MusicalScale>& key) {
    if (sameKey(key_, key))
        return;
    key_ = key;
    refreshText();
}

void KeyField::setNoKeyText(const juce::String& text) {
    noKeyText_ = text;
    refreshText();
}

void KeyField::refreshText() {
    const auto text = key_.has_value() ? keyDisplayName(*key_) : noKeyText_;
    if (getText() != text)
        setText(text, juce::dontSendNotification);
}

// The mouse path reaches here through ComboBox's own asynchronous open; there are no items, so the stock menu never
// shows.
void KeyField::showPopup() {
    if (isEnabled() && onOpen)
        onOpen();
}

bool KeyField::keyPressed(const juce::KeyPress& key) {
    if (isEnabled() &&
        (key == juce::KeyPress::downKey || key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)) {
        showPopup();
        return true;
    }
    return ComboBox::keyPressed(key);
}

} // namespace synth::ui
