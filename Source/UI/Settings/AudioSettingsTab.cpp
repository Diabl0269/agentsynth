#include "AudioSettingsTab.h"

namespace {
// Gives `target` the caption's text as its screen-reader title and tooltip, unless it already has one.
void nameFromCaption(juce::Component& target, const juce::String& caption) {
    if (target.getTitle().isEmpty())
        target.setTitle(caption);
    if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&target))
        if (tip->getTooltip().isEmpty())
            tip->setTooltip(caption);
    // The MIDI output caption is attached to a wrapper around its drop-down, not to the drop-down.
    for (auto* child : target.getChildren())
        if (dynamic_cast<juce::ComboBox*>(child) != nullptr)
            nameFromCaption(*child, caption);
}

void nameDescendants(juce::Component& parent) {
    for (auto* child : parent.getChildren()) {
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            if (auto* attached = label->getAttachedComponent())
                nameFromCaption(*attached, label->getText().trimCharactersAtEnd(": "));
        } else if (auto* button = dynamic_cast<juce::Button*>(child)) {
            if (button->getTooltip().isEmpty())
                button->setTooltip(button->getButtonText());
        }
        nameDescendants(*child);
    }
}
} // namespace

AudioSettingsTab::AudioSettingsTab(juce::AudioDeviceManager& deviceManager,
                                   const std::vector<juce::String>& midiRemoteDeviceNames)
    : deviceManager_(deviceManager) {
    deviceSelector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager, 0, 2, // min/max inputs
                                                                           0, 2,                // min/max outputs
                                                                           true, true,          // midi
                                                                           false, false         // bit depths
    );
    addAndMakeVisible(*deviceSelector_);
    nameSelectorControls();
    deviceManager_.addChangeListener(this);

    juce::StringArray names;
    for (const auto& name : midiRemoteDeviceNames)
        if (name.isNotEmpty())
            names.addIfNotAlreadyThere(name);

    addChildComponent(caption_);
    if (names.isEmpty())
        return;
    caption_.setText("Controllers: " + names.joinIntoString(", ") +
                         ". These are opened as controllers whether or not they are ticked above; "
                         "ticking one only decides whether it also plays the patch.",
                     juce::dontSendNotification);
    caption_.setFont(juce::Font(juce::FontOptions(11.5f)));
    caption_.setMinimumHorizontalScale(1.0f); // wrap, don't squeeze
    caption_.setJustificationType(juce::Justification::topLeft);
    caption_.setVisible(true);
    lookAndFeelChanged();
}

AudioSettingsTab::~AudioSettingsTab() { deviceManager_.removeChangeListener(this); }

void AudioSettingsTab::nameSelectorControls() { nameDescendants(*deviceSelector_); }

void AudioSettingsTab::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
}

void AudioSettingsTab::resized() {
    auto bounds = getLocalBounds();
    if (caption_.isVisible())
        caption_.setBounds(bounds.removeFromBottom(kCaptionHeight).reduced(8, 4));
    deviceSelector_->setBounds(bounds);
}
