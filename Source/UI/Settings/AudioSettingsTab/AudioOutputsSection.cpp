// Concern: the Audio tab's Outputs section -- the output channel ticks.
#include "UI/Settings/AudioSettingsTab/AudioOutputsSection.h"

AudioOutputsSection::AudioOutputsSection(juce::AudioDeviceManager& deviceManager)
    : deviceManager_(deviceManager) {
    none_.setText("No output device is open.", juce::dontSendNotification);
    addChildComponent(none_);
    refresh();
}

int AudioOutputsSection::preferredHeight(int) const {
    return kPad * 2 + juce::jmax(1, (int)ticks_.size()) * kRowHeight;
}

juce::String AudioOutputsSection::summary() const {
    int on = 0;
    for (const auto& tick : ticks_)
        on += tick->getToggleState() ? 1 : 0;
    return ticks_.empty() ? juce::String() : juce::String(on) + " of " + juce::String((int)ticks_.size()) + " on";
}

void AudioOutputsSection::refresh() {
    ticks_.clear();
    auto* device = deviceManager_.getCurrentAudioDevice();
    const auto names = device != nullptr ? device->getOutputChannelNames() : juce::StringArray();
    const auto active = device != nullptr ? device->getActiveOutputChannels() : juce::BigInteger();
    for (int channel = 0; channel < names.size(); ++channel) {
        auto tick = std::make_unique<juce::ToggleButton>(names[channel]);
        tick->setToggleState(active[channel], juce::dontSendNotification);
        tick->setTitle("Output " + names[channel]);
        tick->setTooltip("Play on " + names[channel]);
        tick->onClick = [this, channel, raw = tick.get()] { setChannel(channel, raw->getToggleState()); };
        addAndMakeVisible(*tick);
        ticks_.push_back(std::move(tick));
    }
    none_.setVisible(ticks_.empty());
    resized();
}

// The setup keeps explicit channels (not the device default) so the choice is what JUCE persists.
void AudioOutputsSection::setChannel(int channel, bool on) {
    auto setup = deviceManager_.getAudioDeviceSetup();
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        if (setup.useDefaultOutputChannels)
            setup.outputChannels = device->getActiveOutputChannels();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setBit(channel, on);
    if (setup.outputChannels.isZero()) {
        refresh(); // the last channel stays on
        return;
    }
    deviceManager_.setAudioDeviceSetup(setup, true);
}

void AudioOutputsSection::resized() {
    auto area = getLocalBounds().reduced(kPad);
    none_.setBounds(area.removeFromTop(kRowHeight));
    for (auto& tick : ticks_)
        tick->setBounds(area.removeFromTop(kRowHeight));
}
