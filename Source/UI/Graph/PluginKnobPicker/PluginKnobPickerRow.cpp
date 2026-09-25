// PluginKnobPickerRow.cpp -- one row's controls. See the header for what this owns vs. what it
// reports to PluginKnobPickerComponent. docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerRow.h"

namespace synth::ui {

namespace {
constexpr int kCheckboxWidth = 22;
constexpr int kDragHandleWidth = 18;
constexpr int kLabelFieldWidth = 90;
constexpr int kGap = 4;
} // namespace

PluginKnobPickerRow::PluginKnobPickerRow(juce::String paramId, juce::String displayName)
    : paramId_(std::move(paramId))
    , nameLabel_("name", displayName) {
    checkbox_.setComponentID("knobPickerCheck:" + paramId_);
    addAndMakeVisible(checkbox_);
    checkbox_.onClick = [this] {
        if (onToggled)
            onToggled(checkbox_.getToggleState());
    };

    nameLabel_.setInterceptsMouseClicks(false, false);
    nameLabel_.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(nameLabel_);

    labelEditor_.setComponentID("knobPickerLabel:" + paramId_);
    labelEditor_.setTextToShowWhenEmpty(displayName, juce::Colours::grey);
    labelEditor_.setJustification(juce::Justification::centredLeft);
    labelEditor_.onFocusLost = [this] { commitLabel(); };
    labelEditor_.onReturnKey = [this] { commitLabel(); };
    addAndMakeVisible(labelEditor_);

    setChecked(false);
}

void PluginKnobPickerRow::setChecked(bool checked) {
    checkbox_.setToggleState(checked, juce::dontSendNotification);
    labelEditor_.setVisible(checked);
    resized();
    repaint();
}

void PluginKnobPickerRow::setLabelText(const juce::String& text) { labelEditor_.setText(text, false); }

// Empty text commits as "no override" (the field's placeholder already shows the parameter's own
// name for that case) -- trimmed so a label of pure whitespace is treated the same way.
void PluginKnobPickerRow::commitLabel() {
    if (onLabelCommitted)
        onLabelCommitted(labelEditor_.getText().trim());
}

void PluginKnobPickerRow::paint(juce::Graphics& g) {
    if (isChecked() && !dragHandleBounds_.isEmpty()) {
        // Three short horizontal bars -- a plain, ASCII-free drag-handle glyph (Source/UI/CLAUDE.md:
        // no non-ASCII bytes in a string literal; this is drawn geometry, not text, so the rule does
        // not even apply, but it also sidesteps ever needing a Unicode glyph like U+2261 for one).
        g.setColour(findColour(juce::Label::textColourId).withAlpha(0.6f));
        const auto b = dragHandleBounds_.reduced(4, 8);
        for (int i = 0; i < 3; ++i) {
            const int y = b.getY() + i * (b.getHeight() / 2);
            g.fillRect(b.getX(), y, b.getWidth(), 2);
        }
    }
}

void PluginKnobPickerRow::resized() {
    auto area = getLocalBounds().reduced(2, 0);
    checkbox_.setBounds(area.removeFromLeft(kCheckboxWidth));
    area.removeFromLeft(kGap);

    if (isChecked()) {
        labelEditor_.setBounds(area.removeFromRight(kLabelFieldWidth));
        area.removeFromRight(kGap);
        dragHandleBounds_ = area.removeFromRight(kDragHandleWidth);
        area.removeFromRight(kGap);
    } else {
        dragHandleBounds_ = {};
    }
    nameLabel_.setBounds(area);
}

void PluginKnobPickerRow::mouseDown(const juce::MouseEvent& e) {
    if (!isChecked() || !dragHandleBounds_.contains(e.getPosition()))
        return;
    dragging_ = true;
    dragStartScreenPos_ = e.getScreenPosition();
    if (onDragStarted)
        onDragStarted();
}

void PluginKnobPickerRow::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    if (onDragUpdated)
        onDragUpdated(e.getScreenPosition().y - dragStartScreenPos_.y);
}

void PluginKnobPickerRow::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (onDragEnded)
        onDragEnded();
}

} // namespace synth::ui
