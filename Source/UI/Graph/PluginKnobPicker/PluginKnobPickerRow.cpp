// PluginKnobPickerRow.cpp -- one row's controls. See the header for what this owns vs. what it
// reports to PluginKnobPickerComponent. docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerRow.h"

#include "UI/Layout/DragCursor.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kCheckboxWidth = 22;
constexpr int kDragHandleWidth = 18;
constexpr int kLabelFieldWidth = 90;
constexpr int kGap = 4;
} // namespace

PluginKnobPickerGrip::PluginKnobPickerGrip() { setMouseCursor(dragGrabCursor()); }

void PluginKnobPickerGrip::paint(juce::Graphics& g) {
    // Three short horizontal bars -- a plain, ASCII-free drag-handle glyph (Source/UI/CLAUDE.md:
    // no non-ASCII bytes in a string literal; this is drawn geometry, not text, so the rule does
    // not even apply, but it also sidesteps ever needing a Unicode glyph like U+2261 for one).
    g.setColour(findColour(juce::Label::textColourId).withAlpha(pressed_ ? 0.95f : 0.6f));
    const auto b = getLocalBounds().reduced(4, 8);
    for (int i = 0; i < 3; ++i)
        g.fillRect(b.getX(), b.getY() + i * (b.getHeight() / 2), b.getWidth(), 2);
}

void PluginKnobPickerGrip::mouseDown(const juce::MouseEvent& e) {
    pressed_ = true;
    repaint();
    if (onPress)
        onPress(e);
}

void PluginKnobPickerGrip::mouseDrag(const juce::MouseEvent& e) {
    if (onMove)
        onMove(e);
}

// The release may commit a reorder that rebuilds the whole row list, this grip included, so the
// callback runs from a copy and nothing here touches a member afterwards.
void PluginKnobPickerGrip::mouseUp(const juce::MouseEvent&) {
    pressed_ = false;
    repaint();
    const auto released = onRelease;
    if (released)
        released();
}

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

    dragHandle_.onPress = [this](const juce::MouseEvent& e) {
        if (onDragStarted)
            onDragStarted(e);
    };
    dragHandle_.onMove = [this](const juce::MouseEvent& e) {
        if (onDragUpdated)
            onDragUpdated(e);
    };
    dragHandle_.onRelease = [this] {
        const auto ended = onDragEnded;
        if (ended)
            ended();
    };
    addChildComponent(dragHandle_);

    setChecked(false);
}

void PluginKnobPickerRow::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
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
    if (lift_ <= 0.0f)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto surface = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    paintReorderLift(g, getLocalBounds().toFloat(), lift_, surface, accent);
}

void PluginKnobPickerRow::resized() {
    auto area = getLocalBounds().reduced(2, 0);
    checkbox_.setBounds(area.removeFromLeft(kCheckboxWidth));
    area.removeFromLeft(kGap);

    if (isChecked()) {
        labelEditor_.setBounds(area.removeFromRight(kLabelFieldWidth));
        area.removeFromRight(kGap);
        dragHandle_.setBounds(area.removeFromRight(kDragHandleWidth));
        area.removeFromRight(kGap);
    }
    dragHandle_.setVisible(isChecked());
    nameLabel_.setBounds(area);
}

} // namespace synth::ui
