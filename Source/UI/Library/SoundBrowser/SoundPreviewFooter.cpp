// SoundPreviewFooter.cpp -- the waveform, Stop, preview volume and Auto-play controls under the Sounds list.
#include "UI/Library/SoundBrowser/SoundPreviewFooter.h"
#include "SoundBrowser/SoundPreviewPlayer.h"

#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

SoundPreviewFooter::SoundPreviewFooter() {
    addAndMakeVisible(wave_);
    addChildComponent(curve_);

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

    matchKey_.setTitle("Match key");
    matchKey_.setTooltip("Play loops in the song's key");
    matchKey_.onClick = [this] {
        if (onMatchKeyChanged)
            onMatchKeyChanged(matchKey_.getToggleState());
    };
    addAndMakeVisible(matchKey_);

    using Player = synth::soundbrowser::SoundPreviewPlayer;
    volume_.setTitle("Preview volume");
    volume_.setWantsKeyboardFocus(true); // juce::Slider turns it off; the volume is a keyboard stop
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

SoundPreviewFooter::~SoundPreviewFooter() { shapeAnim_.stop(vblank_); }

void SoundPreviewFooter::showSound(const std::optional<synth::soundbrowser::SoundEntry>& entry) {
    wave_.setFile(entry.has_value() ? juce::File(entry->path) : juce::File());
}

void SoundPreviewFooter::setCompact(bool compact) {
    if (compact_ == compact)
        return;
    compact_ = compact;
    applyShapeMix();
    resized();
}

void SoundPreviewFooter::showShape(const std::optional<synth::shapes::LibraryShape>& shape) {
    if (shape.has_value())
        curve_.setShape(shape);
    if (shape.has_value() == shapeTarget_)
        return;
    shapeTarget_ = shape.has_value();
    const float from = shapeMix_;
    const float to = shapeTarget_ ? 1.0f : 0.0f;
    shapeAnim_.start(
        vblank_, motionMs(120.0, 80.0), easeOutCubic,
        [this, from, to](float t) {
            shapeMix_ = from + (to - from) * t;
            applyShapeMix();
        },
        [this, to] {
            shapeMix_ = to;
            applyShapeMix();
        });
}

void SoundPreviewFooter::finishShapeMotionForTest() {
    shapeAnim_.completeForTest();
    shapeMix_ = shapeTarget_ ? 1.0f : 0.0f;
    applyShapeMix();
}

// The transport (and the waveform) is on screen while any of it is left; the shape's picture while any of it shows.
// A control that is faded out entirely is hidden, so it is no focus stop and no screen reader reaches it.
void SoundPreviewFooter::applyShapeMix() {
    const bool transport = shapeMix_ < 1.0f;
    const bool picture = shapeMix_ > 0.0f && !compact_;
    const float transportAlpha = 1.0f - shapeMix_;
    for (juce::Component* c : {static_cast<juce::Component*>(&stop_), static_cast<juce::Component*>(&volume_)}) {
        c->setVisible(transport);
        c->setAlpha(transportAlpha);
    }
    for (juce::Component* c : {static_cast<juce::Component*>(&autoPlay_), static_cast<juce::Component*>(&matchKey_),
                               static_cast<juce::Component*>(&wave_)}) {
        c->setVisible(transport && !compact_);
        c->setAlpha(transportAlpha);
    }
    curve_.setVisible(picture);
    curve_.setAlpha(shapeMix_);
}

void SoundPreviewFooter::resized() {
    if (compact_) {
        auto row = getLocalBounds().withTrimmedTop(1).reduced(12, 4);
        stop_.setBounds(row.removeFromLeft(56));
        const int volumeWidth = juce::jmax(64, juce::jmin(100, row.getWidth() - 60));
        volume_.setBounds(row.removeFromRight(juce::jmin(volumeWidth, row.getWidth())));
        compactName_ = row.reduced(6, 0);
        return;
    }
    compactName_ = {};
    curve_.setBounds(getLocalBounds().reduced(12, 6));
    auto area = getLocalBounds().reduced(12, 6);
    wave_.setBounds(area.removeFromTop(40));
    area.removeFromTop(6);
    auto row = area.removeFromTop(24);
    stop_.setBounds(row.removeFromLeft(56));
    const int switchWidth = juce::jmax(0, juce::jmin(110, (row.getWidth() - 8) / 2));
    autoPlay_.setBounds(row.removeFromRight(switchWidth));
    matchKey_.setBounds(row.removeFromRight(switchWidth));
    area.removeFromTop(4);
    volume_.setBounds(area.removeFromTop(22));
}

} // namespace synth::ui
