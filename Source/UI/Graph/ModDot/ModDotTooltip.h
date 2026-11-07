#pragma once

// The live readout of a mod-dot drag (or a key step): which source is being adjusted on which knob and its amount
// right now, with the fade that brings it in and out. It paints nothing itself: the chip under the knob
// (ModuleComponentModChip.cpp) draws the dragged source's row from it. It fades in over 160 ms and out over
// 110 ms (a plain 80 ms fade under Reduce Motion). docs/layout/animation.md ("What moves, and how").

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** What the readout says: the source being adjusted on (`card`, `destChannel`) and its live amount. */
struct ModDotReadout {
    juce::AudioProcessorGraph::NodeID card;
    int destChannel = 0;
    juce::AudioProcessorGraph::NodeID attenuverter;
    juce::String name;
    float amount = 0.0f;
};

class ModDotTooltip {
public:
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;
    static constexpr double kReducedFadeMs = ShownFade::kReducedMs;

    /** `canvas` only supplies the vblank the fade runs on and whether it is on screen; it must outlive this. */
    explicit ModDotTooltip(juce::Component& canvas);
    ~ModDotTooltip();

    /** Shows `name` at `amount` for the source `attenuverter` on (`card`, `destChannel`). Fades in when hidden,
     *  otherwise re-texts at the current opacity. */
    void show(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::AudioProcessorGraph::NodeID attenuverter,
              const juce::String& name, float amount);
    /** Fades out; still readable (readout()) until it has gone. */
    void hide();

    /** Called whenever isShown() changes. */
    std::function<void()> onShownChanged;
    /** Called with the card the readout is on whenever what it draws changes (text, opacity, gone). */
    std::function<void(juce::AudioProcessorGraph::NodeID card)> onChanged;

    bool isShown() const noexcept { return wanted_; }
    float getOpacity() const noexcept { return fade_.alpha(); }
    /** "<source> . <signed percent>%" of the readout, empty once it has gone. */
    juce::String getText() const;
    /** The readout while it is up or fading out, null once it has gone. */
    const ModDotReadout* readout() const noexcept { return content_ ? &*content_ : nullptr; }

private:
    void setOpacity(float value);

    std::optional<ModDotReadout> content_; // kept through a fade-out so it keeps drawing
    bool wanted_ = false;
    ShownFade fade_;
};

} // namespace synth::ui
