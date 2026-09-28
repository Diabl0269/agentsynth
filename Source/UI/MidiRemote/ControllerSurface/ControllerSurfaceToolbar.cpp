// ControllerSurfaceToolbar.cpp -- FRO134: see the header. The hint text is docs/control/
// midi-remote-ui.md#detect-mode's, word for word.

#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceToolbar.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

ControllerSurfaceToolbar::ControllerSurfaceToolbar() {
    detectButton_.setButtonText("Detect");
    detectButton_.setComponentID("detectButton");
    detectButton_.setClickingTogglesState(true);
    detectButton_.setTooltip("Touch your controller to add its controls to the surface");
    detectButton_.onClick = [this] {
        detectOn_ = detectButton_.getToggleState();
        resized();
        repaint();
        if (onDetectToggled)
            onDetectToggled(detectOn_);
    };
    addAndMakeVisible(detectButton_);

    assignButton_.setButtonText("Assign...");
    assignButton_.setComponentID("assignButton");
    assignButton_.setTooltip("Choose what the selected control drives");
    assignButton_.onClick = [this] {
        if (onAssignRequested)
            onAssignRequested(assignButton_);
    };
    addAndMakeVisible(assignButton_);

    templatesButton_.setButtonText(juce::String::fromUTF8("Templates \xe2\x96\xbe"));
    templatesButton_.setComponentID("templatesButton");
    templatesButton_.setTooltip("Start from a generic layout");
    templatesButton_.onClick = [this] {
        if (onTemplatesRequested)
            onTemplatesRequested(templatesButton_);
    };
    addAndMakeVisible(templatesButton_);

    moreButton_.setButtonText("...");
    moreButton_.setComponentID("moreButton");
    moreButton_.setTooltip("Import or export this controller");
    moreButton_.onClick = [this] {
        if (onMoreRequested)
            onMoreRequested(moreButton_);
    };
    addAndMakeVisible(moreButton_);

    hintLabel_.setComponentID("detectHintLabel");
    hintLabel_.setText("Touch each knob, fader and button once. Rename or retype them afterwards. "
                       "Turn an encoder left then right to detect its encoding.",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel_.setMinimumHorizontalScale(0.8f);
    addChildComponent(hintLabel_);

    undoHintLabel_.setComponentID("undoHintLabel");
    undoHintLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    undoHintLabel_.setJustificationType(juce::Justification::centredRight);
    undoHintLabel_.setMinimumHorizontalScale(0.7f);
    undoHintLabel_.setInterceptsMouseClicks(false, false);
    addChildComponent(undoHintLabel_);

    portHintLabel_.setComponentID("portHintLabel");
    portHintLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    portHintLabel_.setMinimumHorizontalScale(0.8f);
    portHintLabel_.setInterceptsMouseClicks(false, false);
    addChildComponent(portHintLabel_);

    setProfileSelected(false);
    setControlSelected(false);
}

ControllerSurfaceToolbar::~ControllerSurfaceToolbar() = default;

void ControllerSurfaceToolbar::setProfileSelected(bool selected) {
    detectButton_.setEnabled(selected);
    templatesButton_.setEnabled(selected);
    moreButton_.setEnabled(true); // Import needs no selection; the menu greys Export itself
    if (!selected && detectOn_)
        setDetectOn(false);
}

void ControllerSurfaceToolbar::setControlSelected(bool selected) { assignButton_.setEnabled(selected); }

void ControllerSurfaceToolbar::setDetectOn(bool on) {
    detectOn_ = on;
    detectButton_.setToggleState(on, juce::dontSendNotification);
    hintLabel_.setVisible(on);
    resized();
    repaint();
}

// Called on every focus change and every history change, so it must stay cheap: an unchanged
// text touches nothing (Source/UI/CLAUDE.md's repaint-only-on-change rule). The cue sits in the
// button row's leftover width, so showing it never changes getPreferredHeight() and never makes
// the panel re-layout the surface underneath.
void ControllerSurfaceToolbar::setUndoHint(const juce::String& text) {
    if (text == undoHintLabel_.getText() && undoHintLabel_.isVisible() == text.isNotEmpty())
        return;
    undoHintLabel_.setText(text, juce::dontSendNotification);
    undoHintLabel_.setVisible(text.isNotEmpty());
}

// Unlike setUndoHint/setDetectOn's hint row, this one has no other exception to Source/UI/CLAUDE.md's
// no-unconditional-repaint rule to share layout with -- it gets its own full-width row, so a change
// here (unlike the undo cue, which just repaints leftover space in the existing row) changes
// getPreferredHeight() and needs the same "caller re-layouts" contract setDetectOn() already has
// (see MidiRemotePanelHandshake.cpp's refreshPortHint()).
void ControllerSurfaceToolbar::setPortHint(const juce::String& text) {
    if (text == portHintLabel_.getText() && portHintLabel_.isVisible() == text.isNotEmpty())
        return;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        portHintLabel_.setColour(juce::Label::textColourId, lf->getTheme().colors.warning);
    portHintLabel_.setText(text, juce::dontSendNotification);
    portHintLabel_.setVisible(text.isNotEmpty());
    resized();
    repaint();
}

int ControllerSurfaceToolbar::getPreferredHeight() const noexcept {
    return kRowHeight + (portHintLabel_.isVisible() ? kHintHeight : 0) + (detectOn_ ? kHintHeight : 0);
}

void ControllerSurfaceToolbar::resized() {
    auto bounds = getLocalBounds();
    auto row = bounds.removeFromTop(kRowHeight).reduced(6, 3);
    detectButton_.setBounds(row.removeFromLeft(72));
    row.removeFromLeft(6);
    assignButton_.setBounds(row.removeFromLeft(84));
    row.removeFromLeft(6);
    templatesButton_.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(6);
    moreButton_.setBounds(row.removeFromLeft(40));
    row.removeFromLeft(8);
    undoHintLabel_.setBounds(row);
    if (portHintLabel_.isVisible())
        portHintLabel_.setBounds(bounds.removeFromTop(kHintHeight).reduced(8, 0));
    hintLabel_.setBounds(bounds.reduced(8, 0));
    hintLabel_.setVisible(detectOn_);
}

void ControllerSurfaceToolbar::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black);
}

} // namespace synth::ui
