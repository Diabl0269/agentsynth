#include "AudioSettingsTab.h"

AudioSettingsTab::AudioSettingsTab(juce::AudioDeviceManager& deviceManager,
                                   const std::vector<juce::String>& midiRemoteDeviceNames) {
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager, 0, 2, // min/max inputs
                                                                           0, 2,                // min/max outputs
                                                                           true, true,          // midi
                                                                           false, false         // bit depths
    );
    addAndMakeVisible(*deviceSelector_);

    juce::StringArray names;
    for (const auto& name : midiRemoteDeviceNames)
        if (name.isNotEmpty())
            names.addIfNotAlreadyThere(name);

    addChildComponent(caption_);
    if (names.isEmpty())
        return;
    caption_.setText("MIDI Remote controllers: " + names.joinIntoString(", ") +
                         ". These are opened for MIDI Remote whether or not they are ticked above; "
                         "ticking one only decides whether it also plays the patch.",
                     juce::dontSendNotification);
    caption_.setFont(juce::Font(juce::FontOptions(11.5f)));
    caption_.setMinimumHorizontalScale(1.0f); // wrap, don't squeeze
    caption_.setJustificationType(juce::Justification::topLeft);
    caption_.setVisible(true);
    lookAndFeelChanged();
}

void AudioSettingsTab::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
}

void AudioSettingsTab::resized() {
    auto bounds = getLocalBounds();
    if (caption_.isVisible())
        caption_.setBounds(bounds.removeFromBottom(kCaptionHeight).reduced(8, 4));
    deviceSelector_->setBounds(bounds);
}
