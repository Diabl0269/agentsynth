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

    setProfileSelected(false);
}

ControllerSurfaceToolbar::~ControllerSurfaceToolbar() = default;

void ControllerSurfaceToolbar::setProfileSelected(bool selected) {
    detectButton_.setEnabled(selected);
    templatesButton_.setEnabled(selected);
    moreButton_.setEnabled(true); // Import needs no selection; the menu greys Export itself
    if (!selected && detectOn_)
        setDetectOn(false);
}

void ControllerSurfaceToolbar::setDetectOn(bool on) {
    detectOn_ = on;
    detectButton_.setToggleState(on, juce::dontSendNotification);
    hintLabel_.setVisible(on);
    resized();
    repaint();
}

int ControllerSurfaceToolbar::getPreferredHeight() const noexcept { return kRowHeight + (detectOn_ ? kHintHeight : 0); }

void ControllerSurfaceToolbar::resized() {
    auto bounds = getLocalBounds();
    auto row = bounds.removeFromTop(kRowHeight).reduced(6, 3);
    detectButton_.setBounds(row.removeFromLeft(72));
    row.removeFromLeft(6);
    templatesButton_.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(6);
    moreButton_.setBounds(row.removeFromLeft(40));
    hintLabel_.setBounds(bounds.reduced(8, 0));
    hintLabel_.setVisible(detectOn_);
}

void ControllerSurfaceToolbar::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black);
}

} // namespace synth::ui
