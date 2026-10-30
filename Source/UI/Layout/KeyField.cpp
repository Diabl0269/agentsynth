#include "UI/Layout/KeyField.h"
#include "Music/SongKey.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

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

// A follower shows where its key comes from ("Song key" and a middle dot before "D Dorian") in the muted text colour,
// so following and an own key read differently at a glance; an own key uses the box's normal text colour.
void KeyField::setFollowing(bool following, const juce::String& what) {
    following_ = following;
    setTitle(what + (following ? ", following song key" : ", own key"));
    setTooltip(following ? "Following the song key. Click to give this its own key."
                         : "Own key. Choose Follow song key to follow again.");
    refreshText();
    refreshFollowColour();
}

void KeyField::refreshText() {
    auto text = key_.has_value() ? keyDisplayName(*key_) : noKeyText_;
    if (isFollowing())
        text = juce::String::fromUTF8("Song key \xc2\xb7 ") + text;
    else if (following_.has_value() && key_.has_value())
        text += juce::String::fromUTF8(" \xc2\xb7 own");
    if (getText() != text)
        setText(text, juce::dontSendNotification);
}

void KeyField::refreshFollowColour() {
    if (isFollowing())
        setColour(juce::ComboBox::textColourId, synth::theme::themeOf(*this).colors.textMuted);
    else
        removeColour(juce::ComboBox::textColourId);
}

// The muted colour is a theme token: a theme switch re-reads it.
void KeyField::lookAndFeelChanged() {
    juce::ComboBox::lookAndFeelChanged();
    if (following_.has_value())
        refreshFollowColour();
}

// The mouse path reaches here through ComboBox's own asynchronous open; there are no items, so the stock menu never
// shows.
void KeyField::showPopup() {
    // ComboBox raised its "menu active" latch before calling this and only the stock menu's close lowers it; with no
    // stock menu, left raised every later click is ignored as already open.
    hidePopup();
    if (isEnabled() && onOpen)
        onOpen();
}

// Space and Return open the picker; Up and Down are not ours (the combo would swallow them to step through items it
// does not have), so they bubble and the region around the field moves focus on.
bool KeyField::keyPressed(const juce::KeyPress& key) {
    if (isEnabled() && (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)) {
        showPopup();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        return false;
    return ComboBox::keyPressed(key);
}

} // namespace synth::ui
