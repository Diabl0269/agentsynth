#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// Concern: the closed note-length box -- its items, the name API and opening the popup.

namespace synth::ui {

NoteLengthBox::NoteLengthBox() = default;

NoteLengthBox::~NoteLengthBox() {
    if (popup_ != nullptr) {
        popup_->onPick = nullptr;
        popup_->onClosed = nullptr;
    }
}

void NoteLengthBox::setCompact(bool compact) {
    getProperties().set(theme::AppLookAndFeel::kCompactComboProperty, compact);
    resized();
    repaint();
}

bool NoteLengthBox::isCompact() const noexcept {
    return (bool)getProperties().getWithDefault(theme::AppLookAndFeel::kCompactComboProperty, false);
}

void NoteLengthBox::setChoices(const juce::StringArray& names, const juce::String& textPrefix) {
    const juce::String keep = getCurrentName();
    clear(juce::dontSendNotification);
    names_ = names;
    for (int i = 0; i < names.size(); ++i)
        addItem(textPrefix + names[i], i + 1);
    if (keep.isNotEmpty())
        setCurrentName(keep, juce::dontSendNotification);
}

juce::String NoteLengthBox::getCurrentName() const {
    const int index = getSelectedItemIndex();
    return index >= 0 && index < names_.size() ? names_[index] : juce::String();
}

void NoteLengthBox::setCurrentName(const juce::String& name, juce::NotificationType notification) {
    const int index = names_.indexOf(name);
    if (index >= 0)
        setSelectedItemIndex(index, notification);
}

// A key opens the popup at once (the mouse path goes through ComboBox's own asynchronous open and lands here too).
// Left / Right keep the combo's step through the choices.
bool NoteLengthBox::keyPressed(const juce::KeyPress& key) {
    if (isEnabled() &&
        (key == juce::KeyPress::downKey || key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)) {
        showPopup();
        return true;
    }
    return ComboBox::keyPressed(key);
}

// The popup is owned here and handed to the message loop to delete once it has closed: its own close callback is
// still running when onClosed fires.
void NoteLengthBox::showPopup() {
    if (popup_ != nullptr || !isEnabled() || names_.isEmpty())
        return;
    NoteLengthPopup::Options options;
    options.choices = names_;
    options.current = getCurrentName();
    options.title = getTitle().isNotEmpty() ? getTitle() + " note length" : juce::String("Note length");
    options.unavailableTip = unavailableTip_;
    options.appProperties = appProperties_;
    popup_ = std::make_unique<NoteLengthPopup>(*this, std::move(options));
    juce::Component::SafePointer<NoteLengthBox> self(this);
    popup_->onPick = [self](const juce::String& name) {
        if (self != nullptr)
            self->setCurrentName(name, juce::sendNotificationSync);
    };
    popup_->onClosed = [self] {
        if (self != nullptr)
            self->popupClosed(self->popup_ != nullptr && self->popup_->restoresFocus());
    };
    popup_->showOnDesktop();
    repaint();
}

void NoteLengthBox::popupClosed(bool restoreFocus) {
    auto doomed = std::make_shared<std::unique_ptr<NoteLengthPopup>>(std::move(popup_));
    juce::MessageManager::callAsync([doomed] { doomed->reset(); });
    hidePopup(); // lets ComboBox open again (it marks itself busy while a popup is up)
    if (restoreFocus)
        grabKeyboardFocus();
    repaint();
}

} // namespace synth::ui
