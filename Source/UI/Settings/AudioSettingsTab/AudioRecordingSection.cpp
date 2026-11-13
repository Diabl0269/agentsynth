// Concern: the Audio tab's Recording section -- take format and automatic latency, read-only.
#include "UI/Settings/AudioSettingsTab/AudioRecordingSection.h"

#include "Timeline/TakeCaptureWriter.h"

AudioRecordingSection::AudioRecordingSection(AudioSettingsSource& source)
    : source_(source) {
    for (auto* label : {&formatLabel_, &latencyLabel_, &format_, &latency_, &caption_}) {
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    caption_.setText("The interface's delay is measured and taken off every take for you, so recorded audio lands on "
                     "the beat it was played on.",
                     juce::dontSendNotification);
    caption_.setFont(juce::Font(juce::FontOptions(11.5f)));
    caption_.setMinimumHorizontalScale(1.0f);
    caption_.setJustificationType(juce::Justification::topLeft);
    caption_.setColour(juce::Label::textColourId, juce::Colours::grey); // replaced by lookAndFeelChanged
    refresh();
}

// The take is shifted back by this much, hence the minus sign.
juce::String AudioRecordingSection::latencyText() const {
    const auto info = source_.currentInterface();
    const double ms = info.sampleRate > 0.0 ? 1000.0 * info.recordingLatencySamples / info.sampleRate : 0.0;
    const auto minus = juce::String::fromUTF8("\xE2\x88\x92");
    return "fixed automatically (" + (ms > 0.0 ? minus : juce::String()) + juce::String(ms, 1) + " ms)";
}

juce::String AudioRecordingSection::summary() const {
    return synth::TakeCaptureWriter::describeTakeFormat() + juce::String::fromUTF8(" \xC2\xB7 ") + "latency " +
           latencyText();
}

void AudioRecordingSection::refresh() {
    format_.setText(synth::TakeCaptureWriter::describeTakeFormat(), juce::dontSendNotification);
    latency_.setText(latencyText(), juce::dontSendNotification);
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
}

int AudioRecordingSection::preferredHeight(int width) const {
    juce::ignoreUnused(width);
    return kPad * 2 + 2 * kRowHeight + 36;
}

void AudioRecordingSection::resized() {
    auto area = getLocalBounds().reduced(kPad);
    for (const auto& pair : {std::pair{&formatLabel_, &format_}, std::pair{&latencyLabel_, &latency_}}) {
        auto row = area.removeFromTop(kRowHeight);
        pair.first->setBounds(row.removeFromLeft(kLabelWidth));
        pair.second->setBounds(row);
    }
    caption_.setBounds(area.removeFromTop(36));
}
