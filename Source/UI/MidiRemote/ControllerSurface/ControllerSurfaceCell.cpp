// ControllerSurfaceCell.cpp -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): paint,
// activity decode, and the cell's own mouse handling (select + drag-by-cells). See the header for
// the caller-facing contract; the mid-gesture-rebuild hazard this cell must never trip is documented
// on ControllerSurfaceComponent.cpp, which owns the rebuild.

#include "ControllerSurfaceCell.h"

#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <cmath>

namespace synth::ui {

namespace {
// Name above / assignment label below, per docs/control/midi-remote-ui.md#surface-centre.
constexpr int kNameHeight = 14;
constexpr int kLabelHeight = 12;
} // namespace

ControllerSurfaceCell::ControllerSurfaceCell() = default;
ControllerSurfaceCell::~ControllerSurfaceCell() = default;

void ControllerSurfaceCell::configure(const synth::Control& control, const juce::String& assignmentLabel,
                                      bool isWarningLabel, bool isMapped) {
    controlId_ = control.id;
    control_ = control;
    assignmentLabel_ = assignmentLabel;
    assignmentIsWarning_ = isWarningLabel;
    mapped_ = isMapped;
    lastValue_ = 0.0f;
    lastPressed_ = false;
    buildWidgetForKind();
    resized();
    repaint();
}

// Builds the ONE real widget this cell owns for its control's kind (docs/control/midi-remote-ui.md
// #surface-centre: "using the app's own widgets"). Both slider_ and button_ are torn down and
// rebuilt on every configure() call rather than reused/reshaped across kinds -- a cell is only ever
// reconfigured by ControllerSurfaceComponent::setControls() rebuilding the whole grid, never by
// changing one control's kind in place, so there is no state worth preserving across the switch.
void ControllerSurfaceCell::buildWidgetForKind() {
    slider_.reset();
    button_.reset();

    switch (control_.kind) {
    case synth::ControlKind::knob:
    case synth::ControlKind::encoder:
        slider_ = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        break;
    case synth::ControlKind::fader:
        slider_ = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        break;
    case synth::ControlKind::wheel:
        // No dedicated wheel painter in AppLookAndFeel (only rotary/linear/button) -- LinearHorizontal
        // is the honest fallback per the header's class comment.
        slider_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        break;
    case synth::ControlKind::pad:
    case synth::ControlKind::button:
        button_ = std::make_unique<juce::TextButton>();
        break;
    }

    if (slider_ != nullptr) {
        slider_->setRange(0.0, 1.0, 0.0);
        slider_->setValue(0.0, juce::dontSendNotification);
        slider_->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*slider_);
    }
    if (button_ != nullptr) {
        button_->setClickingTogglesState(false); // mouse-inert: this cell drives the toggle state, not clicks
        button_->setToggleState(false, juce::dontSendNotification);
        button_->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*button_);
    }
}

void ControllerSurfaceCell::setSelected(bool selected) {
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

// Decodes one activity event onto the display-only widget (docs/control/midi-remote-ui.md
// #surface-centre: "move with the hardware ... never dragged to send MIDI"). No-op (no repaint) if
// the decoded state doesn't actually change, per the header's contract and this codebase's
// no-unconditional-repaint rule (Source/UI/CLAUDE.md).
void ControllerSurfaceCell::noteActivity(synth::midi::RemoteEventKind kind, float value) {
    switch (kind) {
    case synth::midi::RemoteEventKind::absolute: {
        const float clamped = juce::jlimit(0.0f, 1.0f, value);
        if (juce::approximatelyEqual(clamped, lastValue_))
            return;
        lastValue_ = clamped;
        if (slider_ != nullptr)
            slider_->setValue((double)lastValue_, juce::dontSendNotification);
        else if (button_ != nullptr)
            button_->setToggleState(lastValue_ > 0.5f, juce::dontSendNotification);
        repaint();
        return;
    }
    case synth::midi::RemoteEventKind::relativeDelta: {
        const float next = juce::jlimit(0.0f, 1.0f, lastValue_ + value);
        if (juce::approximatelyEqual(next, lastValue_))
            return;
        lastValue_ = next;
        if (slider_ != nullptr)
            slider_->setValue((double)lastValue_, juce::dontSendNotification);
        else if (button_ != nullptr)
            button_->setToggleState(lastValue_ > 0.5f, juce::dontSendNotification);
        repaint();
        return;
    }
    case synth::midi::RemoteEventKind::buttonPress: {
        if (lastPressed_)
            return;
        lastPressed_ = true;
        if (button_ != nullptr)
            button_->setToggleState(true, juce::dontSendNotification);
        repaint();
        return;
    }
    case synth::midi::RemoteEventKind::buttonRelease: {
        if (!lastPressed_)
            return;
        lastPressed_ = false;
        if (button_ != nullptr)
            button_->setToggleState(false, juce::dontSendNotification);
        repaint();
        return;
    }
    case synth::midi::RemoteEventKind::learnCandidate:
        return; // not a display state -- the surface never shows an unassigned candidate's message
    }
}

void ControllerSurfaceCell::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour textColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour warningColour = lf != nullptr ? lf->getTheme().colors.warning : juce::Colours::orange;
    const juce::Colour selectedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
    const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);

    if (selected_) {
        g.setColour(selectedColour.withAlpha(0.15f));
        g.fillRect(bounds);
        g.setColour(selectedColour);
        g.drawRect(bounds, 1);
    }

    auto nameArea = bounds.removeFromTop(kNameHeight);
    g.setColour(textColour);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawFittedText(control_.name, nameArea, juce::Justification::centred, 1);

    auto labelArea = bounds.removeFromBottom(kLabelHeight);
    g.setColour(assignmentIsWarning_ ? warningColour : textColour.withAlpha(0.7f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawFittedText(assignmentLabel_, labelArea, juce::Justification::centred, 1);

    if (mapped_)
        synth::ui::midilearn::paintMidiMappedBadge(g, getLocalBounds(), badgeColour);
}

void ControllerSurfaceCell::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(kNameHeight);
    bounds.removeFromBottom(kLabelHeight);
    if (slider_ != nullptr)
        slider_->setBounds(bounds);
    if (button_ != nullptr)
        button_->setBounds(bounds.reduced(4));
}

namespace {
// Property keys used to remember the last DELTA (in whole cells) this cell actually fired during
// the live drag, so a slow drag across many pixels reports once per crossed cell boundary rather
// than once per pixel. Stashed on juce::Component::getProperties() rather than a new private
// member -- ControllerSurfaceCell.h is a locked contract for this ticket (see the header's own
// comment), and getProperties() is ordinary per-instance Component state, not shared/static.
const juce::Identifier kLastFiredDeltaColProperty("lastFiredDeltaCol");
const juce::Identifier kLastFiredDeltaRowProperty("lastFiredDeltaRow");
} // namespace

void ControllerSurfaceCell::mouseDown(const juce::MouseEvent& event) {
    dragStartMouse_ = event.getPosition();
    isDragging_ = false;
    getProperties().set(kLastFiredDeltaColProperty, 0);
    getProperties().set(kLastFiredDeltaRowProperty, 0);
    if (onSelected)
        onSelected();
}

// Reports a DELTA (in whole kCellSize units from the drag's start), not a running total, and only
// when that integer delta actually changes -- a plain click with no movement must never fire a
// spurious onDraggedByCells(0,0), and a slow drag across many pixels must not spam the owner once
// per pixel. The owner (ControllerSurfaceComponent) accumulates the delta onto the drag-start grid
// position and clamps once, per its own header's contract.
void ControllerSurfaceCell::mouseDrag(const juce::MouseEvent& event) {
    const auto offset = event.getPosition() - dragStartMouse_;
    const int dCols = (int)std::floor((float)offset.x / (float)kCellSize + 0.5f);
    const int dRows = (int)std::floor((float)offset.y / (float)kCellSize + 0.5f);

    const int lastCol = (int)getProperties().getWithDefault(kLastFiredDeltaColProperty, 0);
    const int lastRow = (int)getProperties().getWithDefault(kLastFiredDeltaRowProperty, 0);
    if (dCols == lastCol && dRows == lastRow)
        return; // no cell boundary crossed yet -- not "movement actually occurred" for the header's contract

    getProperties().set(kLastFiredDeltaColProperty, dCols);
    getProperties().set(kLastFiredDeltaRowProperty, dRows);
    // isDragging_ becomes true only once a real cell-crossing fires onDraggedByCells, not merely
    // because mouseDrag() was called -- so mouseUp below only fires onDragEnded for a drag that
    // actually moved the cell, never for a plain click or a sub-cell jiggle.
    isDragging_ = true;
    if (onDraggedByCells)
        onDraggedByCells(dCols, dRows);
}

void ControllerSurfaceCell::mouseUp(const juce::MouseEvent&) {
    const bool didDrag = isDragging_;
    isDragging_ = false;
    if (didDrag && onDragEnded)
        onDragEnded();
}

} // namespace synth::ui
