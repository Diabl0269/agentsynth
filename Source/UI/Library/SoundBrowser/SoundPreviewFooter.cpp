// SoundPreviewFooter.cpp -- the waveform, Stop, preview volume and Auto-play controls under the Sounds list.
#include "UI/Library/SoundBrowser/SoundPreviewFooter.h"
#include "SoundBrowser/SoundPreviewPlayer.h"

namespace synth::ui {

SoundPreviewFooter::SoundPreviewFooter() {
    addAndMakeVisible(wave_);

    stop_.setTitle("Stop preview");
    stop_.setTooltip("Stop the preview (Space)");
    stop_.onClick = [this] {
        if (onStop)
            onStop();
    };
    addAndMakeVisible(stop_);

    autoPlay_.setTitle("Auto-play");
    autoPlay_.setTooltip("Play a sound when you select it");
    autoPlay_.setToggleState(true, juce::dontSendNotification);
    autoPlay_.onClick = [this] {
        if (onAutoPlayChanged)
            onAutoPlayChanged(autoPlay_.getToggleState());
    };
    addAndMakeVisible(autoPlay_);

    using Player = synth::soundbrowser::SoundPreviewPlayer;
    volume_.setTitle("Preview volume");
    volume_.setTooltip("Preview volume, only for previews");
    volume_.setRange(Player::kMinGainDb, Player::kMaxGainDb, 1.0);
    volume_.setValue(Player::kDefaultGainDb, juce::dontSendNotification);
    volume_.setDoubleClickReturnValue(true, Player::kDefaultGainDb);
    volume_.setTextBoxIsEditable(false);
    volume_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    volume_.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " dB"; };
    volume_.onValueChange = [this] {
        if (onGainChanged)
            onGainChanged((float)volume_.getValue());
    };
    addAndMakeVisible(volume_);
}

void SoundPreviewFooter::showSound(const std::optional<synth::soundbrowser::SoundEntry>& entry) {
    wave_.setFile(entry.has_value() ? juce::File(entry->path) : juce::File());
}

void SoundPreviewFooter::resized() {
    auto area = getLocalBounds().reduced(12, 6);
    wave_.setBounds(area.removeFromTop(40));
    area.removeFromTop(6);
    auto row = area.removeFromTop(24);
    stop_.setBounds(row.removeFromLeft(56));
    autoPlay_.setBounds(row.removeFromRight(juce::jmin(110, row.getWidth() - 8)));
    area.removeFromTop(4);
    volume_.setBounds(area.removeFromTop(22));
}

} // namespace synth::ui
