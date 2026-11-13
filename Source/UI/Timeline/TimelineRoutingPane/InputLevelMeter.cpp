// Concern: the routing pane's input level meter -- the gated level update and the banded bars.
#include "UI/Timeline/TimelineRoutingPane/InputLevelMeter.h"

#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/ChannelChipComponent.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kBarGap = 2.0f;

// The same effective colour stops as the mixer meters and the channel chip (a pinned override or the theme's own).
MeterColourStops stopsFor(const juce::Component& component) {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel()))
        return lf->getMeterColourStops();
    return MeterColourStops::fromTheme(synth::theme::Colors{});
}
} // namespace

InputLevelMeter::InputLevelMeter() {
    setComponentID("audioInputLevelMeter");
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
}

void InputLevelMeter::setChannelCount(int count) {
    const int clamped = count > 1 ? 2 : 1;
    if (clamped == channels_)
        return;
    channels_ = clamped;
    repaint();
}

// Gated like ChannelChipComponent::setMeterLevel: a move under its threshold repaints nothing, except a fall to
// silence, which is always drawn so a bar never sticks on a signal that is gone.
bool InputLevelMeter::setLevels(float left, float right) {
    bool moved = false;
    const float peaks[2] = {left, right};
    for (int i = 0; i < channels_; ++i) {
        const float db = meterLinearToDb(peaks[i]);
        const float fraction = meterDbToFraction(db);
        auto& shown = fractions_[(std::size_t)i];
        const bool toSilence = fraction <= 0.0f && shown > 0.0f;
        if (!toSilence && std::abs(fraction - shown) < ChannelChipComponent::kMeterRepaintThreshold)
            continue;
        shown = fraction;
        dbs_[(std::size_t)i] = db;
        moved = true;
    }
    if (moved)
        repaint();
    return moved;
}

void InputLevelMeter::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    auto area = getLocalBounds().toFloat();
    const float barHeight = (area.getHeight() - kBarGap * (float)(channels_ - 1)) / (float)channels_;
    const auto stops = stopsFor(*this);
    for (int i = 0; i < channels_; ++i) {
        const auto bar = area.removeFromTop(barHeight);
        area.removeFromTop(kBarGap);
        g.setColour(colours.border);
        g.fillRoundedRectangle(bar, 1.0f);
        if (fractions_[(std::size_t)i] <= 0.0f)
            continue;
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path shape;
        shape.addRoundedRectangle(bar, 1.0f);
        g.reduceClipRegion(shape);
        stops.forEachBand(kMeterMinDb, dbs_[(std::size_t)i], [&](float fromDb, float toDb, juce::Colour colour) {
            const float x0 = bar.getX() + bar.getWidth() * meterDbToFraction(fromDb);
            const float x1 = bar.getX() + bar.getWidth() * meterDbToFraction(toDb);
            g.setColour(colour);
            g.fillRect(juce::Rectangle<float>(x0, bar.getY(), x1 - x0, bar.getHeight()));
        });
    }
}

} // namespace synth::ui
