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
constexpr float kCornerRadius = 2.0f;

// Themed colours with literal fallbacks -- the headless test path installs no AppLookAndFeel, the
// same pattern TimelineTrackHeaderComponent's own coloursFor() uses.
struct ChipColours {
    juce::Colour surface{juce::Colour(0xff1B1F26)};
    juce::Colour border{juce::Colour(0xff2A2F38)};
    juce::Colour text{juce::Colour(0xffEAEEF3)};
    // FRO146: the meter's colour now steps through MeterColourStops' four zones (same scale/zones
    // as MixerMeter) instead of a single fixed accent -- default-constructed from
    // synth::theme::Colors{}'s own field defaults, which already equal Obsidian's values.
    synth::ui::MeterColourStops meterStops = synth::ui::MeterColourStops::fromTheme(synth::theme::Colors{});
};

ChipColours coloursFor(const juce::Component& component) {
    ChipColours result;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        result.surface = c.surface;
        result.border = c.border;
        result.text = c.textPrimary;
        result.meterStops = synth::ui::MeterColourStops::fromTheme(c);
    }
    return result;
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
    // FRO146: linear amplitude -> dBFS -> a 0..1 fraction of the -60..+3 dB scale (same scale and
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

void ChannelChipComponent::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto colours = coloursFor(*this);
    auto bounds = getLocalBounds().toFloat();

    g.setColour(highlighted ? colours.surface.brighter(0.15f) : colours.surface);
    g.fillRoundedRectangle(bounds, kCornerRadius);
    g.setColour(colours.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kCornerRadius, 1.0f);

    auto content = getLocalBounds().reduced(4, 1);
    auto meterArea = content.removeFromRight(kMeterWidth);
    content.removeFromRight(kMeterGap);

    g.setColour(colours.text);
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
        colours.meterStops.forEachBand(
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
