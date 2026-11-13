// LaneChipRow.cpp -- the chips' sizes and look (the card's toggle pill), the roving focus the row keeps for its one
// Tab stop, and the accessibility tree (a group of toggle buttons).
#include "LaneChipRow.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kChipGap = 3;
constexpr int kChipSidePadding = 7;
constexpr float kChipFontHeight = 12.0f;

} // namespace

LaneChipRow::LaneChipRow() {
    setTitle("Lanes");
    setTooltip("Lanes: choose which lanes show under the grid. Left and Right move, Space shows or hides one");
    setWantsKeyboardFocus(true);
}

void LaneChipRow::setLanes(const juce::StringArray& labels) {
    chips_.clear();
    for (int i = 0; i < labels.size(); ++i) {
        auto* chip = chips_.add(new juce::ToggleButton(labels[i]));
        chip->setTitle(labels[i] + " lane");
        chip->setTooltip("Show the " + labels[i] + " lane");
        chip->setClickingTogglesState(true);
        chip->setWantsKeyboardFocus(false);
        setTogglePillStyle(*chip, true);
        chip->onClick = [this, i] { toggled(i); };
        addAndMakeVisible(chip);
    }
    focused_ = juce::jlimit(0, juce::jmax(0, chips_.size() - 1), focused_);
    resized();
}

void LaneChipRow::setShown(int index, bool shown) {
    if (auto* chip = chips_[index])
        chip->setToggleState(shown, juce::dontSendNotification);
}

bool LaneChipRow::isShown(int index) const {
    const auto* chip = chips_[index];
    return chip != nullptr && chip->getToggleState();
}

void LaneChipRow::toggled(int index) {
    focused_ = index;
    if (onToggle)
        onToggle(index, isShown(index));
    repaint();
}

// Chips take their text's width plus a little padding, left to right; the row never wraps (it is as wide as the
// grid beneath it).
void LaneChipRow::resized() {
    int x = 0;
    for (auto* chip : chips_) {
        const int width =
            theme::AppLookAndFeel::uiTextWidth(chip->getButtonText(), kChipFontHeight) + 2 * kChipSidePadding;
        chip->setBounds(x, 0, width, getHeight());
        x += width + kChipGap;
    }
}

void LaneChipRow::paintOverChildren(juce::Graphics& g) {
    if (!hasKeyboardFocus(false))
        return;
    if (const auto* chip = chips_[focused_])
        paintFocusRingAlways(g, chip->getBounds().toFloat(), *this, (float)chip->getHeight() * 0.5f);
}

bool LaneChipRow::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown() || chips_.isEmpty())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        const int next = juce::jlimit(0, chips_.size() - 1, focused_ + (code == juce::KeyPress::rightKey ? 1 : -1));
        if (next != focused_) {
            focused_ = next;
            repaint();
        }
        return true;
    }
    if (code == juce::KeyPress::spaceKey) {
        chips_[focused_]->setToggleState(!chips_[focused_]->getToggleState(), juce::dontSendNotification);
        toggled(focused_);
        return true;
    }
    return false;
}

void LaneChipRow::focusGained(FocusChangeType) { repaint(); }
void LaneChipRow::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> LaneChipRow::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
