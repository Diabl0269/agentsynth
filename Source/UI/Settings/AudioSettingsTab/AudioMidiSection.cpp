// Concern: the Audio tab's MIDI section -- input ticks, the output choice and the controllers caption.
#include "UI/Settings/AudioSettingsTab/AudioMidiSection.h"

AudioMidiSection::AudioMidiSection(AudioSettingsSource& source, const juce::StringArray& profiledControllers)
    : source_(source) {
    inputsNone_.setText("No MIDI inputs available", juce::dontSendNotification);
    addChildComponent(inputsNone_);
    outputLabel_.attachToComponent(&output_, true);
    output_.setTitle("MIDI output");
    output_.setTooltip("MIDI output");
    output_.onChange = [this] {
        const int id = output_.getSelectedId();
        source_.setMidiOutput(id > 0 && id <= (int)outputs_.size() ? outputs_[(size_t)id - 1].identifier
                                                                   : juce::String());
    };
    addAndMakeVisible(output_);

    addChildComponent(caption_);
    if (!profiledControllers.isEmpty()) {
        caption_.setText("Controllers: " + profiledControllers.joinIntoString(", ") +
                             ". These are opened as controllers whether or not they are ticked above; "
                             "ticking one only decides whether it also plays the patch.",
                         juce::dontSendNotification);
        caption_.setFont(juce::Font(juce::FontOptions(11.5f)));
        caption_.setMinimumHorizontalScale(1.0f); // wrap, don't squeeze
        caption_.setJustificationType(juce::Justification::topLeft);
        caption_.setVisible(true);
    }
    lookAndFeelChanged();
    refresh();
}

void AudioMidiSection::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
}

int AudioMidiSection::captionHeight(int width) const {
    if (!caption_.isVisible())
        return 0;
    return wrappedHeight(caption_.getText(), caption_.getFont(), width - 2 * kPad) + 8;
}

int AudioMidiSection::preferredHeight(int width) const {
    return kPad * 2 + juce::jmax(1, (int)ticks_.size()) * kRowHeight + kRowHeight + captionHeight(width);
}

juce::String AudioMidiSection::summary() const {
    int on = 0;
    for (const auto& tick : ticks_)
        on += tick->getToggleState() ? 1 : 0;
    return ticks_.empty() ? juce::String("No MIDI inputs")
                          : juce::String(on) + " of " + juce::String((int)ticks_.size()) + " inputs on";
}

void AudioMidiSection::refresh() {
    ticks_.clear();
    for (const auto& device : source_.midiInputs()) {
        auto tick = std::make_unique<juce::ToggleButton>(device.name);
        tick->setToggleState(device.enabled, juce::dontSendNotification);
        tick->setTitle("MIDI input " + device.name);
        tick->setTooltip("Play the patch from " + device.name);
        tick->onClick = [this, id = device.identifier, raw = tick.get()] {
            source_.setMidiInputEnabled(id, raw->getToggleState());
        };
        addAndMakeVisible(*tick);
        ticks_.push_back(std::move(tick));
    }
    inputsNone_.setVisible(ticks_.empty());

    outputs_ = source_.midiOutputs();
    output_.clear(juce::dontSendNotification);
    output_.addItem("No MIDI output", 1000000);
    int selected = 1000000;
    for (size_t i = 0; i < outputs_.size(); ++i) {
        output_.addItem(outputs_[i].name, (int)i + 1);
        if (outputs_[i].enabled)
            selected = (int)i + 1;
    }
    output_.setSelectedId(selected, juce::dontSendNotification);
    resized();
}

void AudioMidiSection::resized() {
    auto area = getLocalBounds().reduced(kPad);
    inputsNone_.setBounds(area.removeFromTop(kRowHeight));
    for (auto& tick : ticks_)
        tick->setBounds(area.removeFromTop(kRowHeight));
    auto row = area.removeFromTop(kRowHeight);
    row.removeFromLeft(kLabelWidth);
    output_.setBounds(row.reduced(0, 2));
    if (caption_.isVisible())
        caption_.setBounds(area.removeFromTop(captionHeight(getWidth())).reduced(0, 4));
}
