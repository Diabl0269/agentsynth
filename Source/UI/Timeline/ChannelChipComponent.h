#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ChannelChipComponent.h -- FRO14 (P9-4, docs/mixer.md §5.2): the track header's CHANNEL chip.
//
// Shown on every track header whose notes/audio play into a channel, linked or not: the channel's
// name plus a compact level meter. Clicking it reveals that channel (P9-5: its mixer column). It is
// a pointer, not a signal path -- this is how a MIDI track shows where its audio went, without
// anyone creating an extra audio track for it.
//
// Its own sibling, the BINDING chip, names the track's Track In node (upstream); this one names the
// channel strip its audio ends up in (downstream). Two different questions, two chips.
//
// NO TIMER OF ITS OWN. With up to TimelineDoc::kMaxTracks rows, one timer per chip would be 256
// independent timers; TimelinePanelComponent owns ONE 15 Hz timer and ticks its headers, and each
// chip's setMeterLevel() decides whether that tick is worth a repaint at all (a coarse threshold).
// That is what keeps this compliant with the "no unconditional per-tick repaint" invariant
// (docs/layout/rendering.md) -- the repaint is gated on the drawn value actually
// moving, exactly like ModuleComponent's own 15 Hz meter poll.

namespace synth::ui {

class ChannelChipComponent : public juce::Button {
public:
    /** Level change (0..1) below which a tick is dropped without repainting. Coarse on purpose: the
     *  meter is ~40px wide, so anything finer is a repaint nobody can see. */
    static constexpr float kMeterRepaintThreshold = 0.02f;

    ChannelChipComponent();

    /** The channel's name, as the chip draws it. Repaints only when it actually changed, so a
     *  refreshFromDoc() that changed nothing else costs nothing here either. */
    void setChannelName(const juce::String& name);
    juce::String getChannelName() const { return channelName_; }

    /** Feeds one meter tick with a LINEAR amplitude peak (0..1-ish; a real over can exceed 1) --
     *  converted internally to dBFS and a 0..1 fraction of the -60..+3 dB scale (FRO146,
     *  MixerMeterScale.h), same scale and colour zones (MeterColourStops.h) as the mixer's own
     *  MixerMeter, so the chip turns the clip colour on the same overs the mixer column shows red.
     *  Returns TRUE only when the displayed fraction moved far enough to be worth a repaint (and
     *  one was issued) -- the gate itself, exposed so a test can prove a below-threshold tick
     *  repaints nothing without driving a real juce::Timer. */
    bool setMeterLevel(float peakLinear);
    /** The displayed level as a 0..1 fraction of the dB scale (NOT the raw linear peak passed to
     *  setMeterLevel() -- see that method's own comment). */
    float getMeterLevel() const noexcept { return meterFraction_; }
    /** The displayed level in dBFS -- what getMeterLevel()'s fraction is derived from. */
    float getMeterDbForTest() const noexcept { return meterDb_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::String channelName_;
    float meterFraction_ = 0.0f;
    float meterDb_ = -60.0f; // kMeterMinDb -- kept in sync with MixerMeterScale.h's constant

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelChipComponent)
};

} // namespace synth::ui
