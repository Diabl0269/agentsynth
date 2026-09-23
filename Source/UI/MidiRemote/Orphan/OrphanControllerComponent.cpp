// Concern: the orphan-controller view (see the header).

#include "UI/MidiRemote/Orphan/OrphanControllerComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

OrphanControllerComponent::OrphanControllerComponent() {
    titleLabel_.setComponentID("orphanTitleLabel");
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    bodyLabel_.setComponentID("orphanBodyLabel");
    bodyLabel_.setJustificationType(juce::Justification::topLeft);
    bodyLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(bodyLabel_);

    relinkButton_.setComponentID("orphanRelinkButton");
    relinkButton_.setTooltip("Point this project's assignments at a controller that is on this machine");
    relinkButton_.onClick = [this] {
        if (onRelinkRequested)
            onRelinkRequested(relinkButton_);
    };
    addAndMakeVisible(relinkButton_);

    recreateButton_.setComponentID("orphanRecreateButton");
    recreateButton_.setTooltip("Create a controller from this project's assignments");
    recreateButton_.onClick = [this] {
        if (onRecreateRequested)
            onRecreateRequested(recreateButton_);
    };
    addAndMakeVisible(recreateButton_);

    statusLabel_.setComponentID("orphanStatusLabel");
    statusLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(statusLabel_);
}

OrphanControllerComponent::~OrphanControllerComponent() = default;

void OrphanControllerComponent::setOrphan(const juce::String& name, int assignmentCount, bool canRecreate) {
    titleLabel_.setText(name + " (not on this machine)", juce::dontSendNotification);
    bodyLabel_.setText("This project has " + juce::String(assignmentCount) +
                           (assignmentCount == 1 ? " assignment" : " assignments") +
                           " for a controller that isn't set up here. Re-link them to a controller you have, "
                           "or Recreate it from what the project remembers.",
                       juce::dontSendNotification);
    recreateButton_.setEnabled(canRecreate);
    recreateButton_.setTooltip(canRecreate ? "Create a controller from this project's assignments"
                                           : "Connect the controller's MIDI device first");
    setStatusText({});
}

void OrphanControllerComponent::setStatusText(const juce::String& text) {
    statusLabel_.setText(text, juce::dontSendNotification);
}

void OrphanControllerComponent::resized() {
    auto bounds = getLocalBounds().reduced(10);
    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(6);
    bodyLabel_.setBounds(bounds.removeFromTop(96));
    auto buttons = bounds.removeFromTop(28);
    relinkButton_.setBounds(buttons.removeFromLeft(100));
    buttons.removeFromLeft(8);
    recreateButton_.setBounds(buttons.removeFromLeft(100));
    bounds.removeFromTop(8);
    statusLabel_.setBounds(bounds.removeFromTop(60));
}

void OrphanControllerComponent::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black);
}

} // namespace synth::ui
