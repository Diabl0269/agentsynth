// CardSegmentedSwitch.cpp -- a choice as joined segments: selection, keys, clicks and the accessibility
// tree (a group of radio buttons). The segments are plain TextButtons with connected edges, so they
// draw exactly like the ADSR MS|BPM switch; they take no clicks or focus themselves, the switch does.
#include "CardSegmentedSwitch.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"

namespace synth::ui {

namespace {

// Shared by every segment of one switch; JUCE's radio grouping makes each report as a radio button.
constexpr int kSegmentRadioGroup = 0x53454731; // 'SEG1'

} // namespace

CardSegmentedSwitch::CardSegmentedSwitch(const juce::String& name, const juce::StringArray& values) {
    setTitle(name);
    setWantsKeyboardFocus(true);
    for (int i = 0; i < values.size(); ++i) {
        auto* segment = segments_.add(new juce::TextButton(values[i]));
        segment->setTitle(values[i]);
        segment->setTooltip(name + ": " + values[i]);
        segment->setRadioGroupId(kSegmentRadioGroup);
        segment->setWantsKeyboardFocus(false);
        segment->setInterceptsMouseClicks(false, false);
        int edges = 0;
        if (i > 0)
            edges |= juce::Button::ConnectedOnLeft;
        if (i < values.size() - 1)
            edges |= juce::Button::ConnectedOnRight;
        segment->setConnectedEdges(edges);
        // An accessibility press (the only way a segment is clicked: it takes no mouse) selects it.
        segment->onClick = [this, i] { setSelectedIndex(i, juce::sendNotificationSync); };
        addAndMakeVisible(segment);
    }
}

void CardSegmentedSwitch::setSelectedIndex(int index, juce::NotificationType notification) {
    if (index < 0 || index >= segments_.size())
        return;
    const bool changed = index != selected_;
    selected_ = index;
    for (int i = 0; i < segments_.size(); ++i)
        segments_[i]->setToggleState(i == index, juce::dontSendNotification);
    if (changed && notification != juce::dontSendNotification && onChange)
        onChange(index);
}

void CardSegmentedSwitch::resized() {
    const int count = segments_.size();
    if (count == 0)
        return;
    const int width = getWidth();
    for (int i = 0; i < count; ++i) {
        const int left = width * i / count;
        const int right = width * (i + 1) / count;
        segments_[i]->setBounds(left, 0, right - left, getHeight());
    }
}

void CardSegmentedSwitch::paintOverChildren(juce::Graphics& g) {
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
}

bool CardSegmentedSwitch::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int code = key.getKeyCode();
    int next = selected_;
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey)
        next = std::max(0, selected_ - 1);
    else if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)
        next = std::min(segments_.size() - 1, selected_ + 1);
    else if (code == juce::KeyPress::homeKey)
        next = 0;
    else if (code == juce::KeyPress::endKey)
        next = segments_.size() - 1;
    else
        return false;
    setSelectedIndex(next, juce::sendNotificationSync);
    return true;
}

int CardSegmentedSwitch::segmentAt(juce::Point<int> local) const {
    for (int i = 0; i < segments_.size(); ++i)
        if (segments_[i]->getBounds().contains(local))
            return i;
    return -1;
}

void CardSegmentedSwitch::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu())
        return; // a right click is the card's control menu
    setSelectedIndex(segmentAt(e.getPosition()), juce::sendNotificationSync);
}

void CardSegmentedSwitch::focusGained(FocusChangeType) { repaint(); }
void CardSegmentedSwitch::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> CardSegmentedSwitch::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
