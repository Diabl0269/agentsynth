#include "ChannelChipComponent.h"

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kMeterWidth = 26;
constexpr int kMeterGap = 4;

// The meter's colour steps through MeterColourStops' four zones (same scale/zones as MixerMeter): the
// SAME cached effective stops MixerMeter reads (the user's pinned override, or the theme's own tokens),
// not a fresh fromTheme() rebuild, so this chip and the mixer's own meters can never show two different
// colour sets for the same channel. Outside any AppLookAndFeel (a headless test) it follows a default Theme.
synth::ui::MeterColourStops meterStopsFor(const juce::Component& component) {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel()))
        return lf->getMeterColourStops();
    return synth::ui::MeterColourStops::fromTheme(synth::theme::Colors{});
}

} // namespace

ChannelChipComponent::ChannelChipComponent()
    : juce::Button("timelineChannelChip") {
    setComponentID("trackChannelChip");
    // Same focus opt-out as the header's other toggles: clicking the chip must not silently move
    // real keyboard focus off the track row it belongs to.
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void ChannelChipComponent::setChannelName(const juce::String& name) {
    if (channelName_ == name)
        return;
    channelName_ = name;
    repaint();
}

bool ChannelChipComponent::setMeterLevel(float peakLinear) {
    // Linear amplitude -> dBFS -> a 0..1 fraction of the -60..+3 dB scale (same scale and
    // colour zones as the mixer's own MixerMeter -- MixerMeterScale.h/MeterColourStops.h).
    const float db = synth::ui::meterLinearToDb(peakLinear);
    const float fraction = synth::ui::meterDbToFraction(db);
    // The gate. A tick whose level did not move a visible amount repaints nothing at all -- except
    // when it lands exactly on silence, which must always be drawn (a decaying tail that stops
    // short of the threshold would otherwise leave the bar stuck showing a signal that is gone).
    const bool crossedToSilence = fraction <= 0.0f && meterFraction_ > 0.0f;
    if (!crossedToSilence && std::abs(fraction - meterFraction_) < kMeterRepaintThreshold)
        return false;
    meterFraction_ = fraction;
    meterDb_ = db;
    repaint();
    return true;
}

void ChannelChipComponent::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colours = theme.colors;
    synth::theme::ChipState state;
    state.hovered = highlighted;
    state.down = down;
    synth::theme::paintChip(g, getLocalBounds().toFloat(), theme, state);

    auto content = getLocalBounds().reduced(4, 1);
    auto meterArea = content.removeFromRight(kMeterWidth);
    content.removeFromRight(kMeterGap);

    g.setColour(colours.textPrimary);
    g.setFont(juce::Font(juce::FontOptions((float)std::min(11, std::max(8, content.getHeight() - 2)))));
    g.drawText(channelName_, content, juce::Justification::centredLeft, true);

    // The meter: a track plus the filled portion. Drawn from the gated meterFraction_/meterDb_,
    // never from a live atomic read -- see the header's timer note.
    const auto meterBounds = meterArea.toFloat().reduced(0.0f, (float)meterArea.getHeight() * 0.3f);
    g.setColour(colours.border);
    g.fillRoundedRectangle(meterBounds, 1.0f);
    if (meterFraction_ > 0.0f) {
        // Positional bands, left to right (this chip is horizontal, MixerMeter's bars are
        // vertical -- same banding idea either way, see MeterColourStops::forEachBand's own
        // comment). Clipped to the track's own rounded shape so the banded rects still read as one
        // rounded bar rather than square-cornered slices.
        juce::Graphics::ScopedSaveState clipState(g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle(meterBounds, 1.0f);
        g.reduceClipRegion(clipPath);
        meterStopsFor(*this).forEachBand(
            synth::ui::kMeterMinDb, meterDb_, [&](float bandFromDb, float bandToDb, juce::Colour colour) {
                const float xFrom =
                    meterBounds.getX() + meterBounds.getWidth() * synth::ui::meterDbToFraction(bandFromDb);
                const float xTo = meterBounds.getX() + meterBounds.getWidth() * synth::ui::meterDbToFraction(bandToDb);
                g.setColour(colour);
                g.fillRect(juce::Rectangle<float>(xFrom, meterBounds.getY(), xTo - xFrom, meterBounds.getHeight()));
            });
    }
}

} // namespace synth::ui
