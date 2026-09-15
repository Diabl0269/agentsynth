#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// MixerMeter.h -- FRO11 (P9-5, docs/mixer.md §5.10): a column's peak meter. Reads
// ChannelStripModule/MasterModule::getMeterPeak via `peakProvider`, painted with the reserved
// Theme::Colors::meterFill token (its first consumer -- see docs/theming.md).
//
// Driven by MixerPanelComponent's refresh(), itself polled from MainComponent's existing 10 Hz
// timer while the mixer tab is showing (docs/layout_visuals_animation.md §2's precedent for the
// Timeline panel's own tick-riding transport poll) -- NOT a new AnimationDriver/timer of its own.
// refresh() applies simple peak-hold-with-decay ballistics and repaints only past a coarse
// quantization step, same gated-repaint shape ChannelChipComponent::setMeterLevel() already uses
// for the track header's channel chip.

namespace synth::ui {

class MixerMeter : public juce::Component {
public:
    MixerMeter() = default;

    /** Displayed-level delta below which a tick is dropped without repainting -- the meter is a
     *  handful of px tall, so anything finer is invisible. */
    static constexpr float kRepaintThreshold = 0.01f;

    /** Per-tick multiplicative decay applied to the held peak before the new one is taken (a
     *  simple peak-hold, not a true VU ballistic curve -- adequate at the shared 10 Hz poll rate,
     *  same class of approximation the codebase's other coarse-tick meters already use). */
    static constexpr float kDecayPerTick = 0.85f;

    /** `leg`: 0 = Left, 1 = Right, matching ChannelStripModule/MasterModule::getMeterPeak. Left
     *  null (the default) for a meter with nothing to read yet (an orphan column mid-teardown). */
    std::function<float(int leg)> peakProvider;

    /** One tick: reads both legs, applies decay, and repaints only past kRepaintThreshold. */
    void refresh() {
        const float rawL = peakProvider ? juce::jlimit(0.0f, 1.0f, peakProvider(0)) : 0.0f;
        const float rawR = peakProvider ? juce::jlimit(0.0f, 1.0f, peakProvider(1)) : 0.0f;
        const float raw = juce::jmax(rawL, rawR);
        const float held = juce::jmax(raw, displayedLevel_ * kDecayPerTick);
        const bool crossedToSilence = held <= 0.0f && displayedLevel_ > 0.0f;
        if (!crossedToSilence && std::abs(held - displayedLevel_) < kRepaintThreshold)
            return;
        displayedLevel_ = held;
        repaint();
    }

    float getDisplayedLevelForTest() const noexcept { return displayedLevel_; }

    void paint(juce::Graphics& g) override;

    /** FRO18: a read-only staticText handler reporting the current displayed level (0..1) as a
     *  percentage -- the meter has nothing for VoiceOver to act on, only to read. */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    float displayedLevel_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMeter)
};

} // namespace synth::ui
