#pragma once

// LoadStageLine.h -- what a slow project load is waiting for ("Loading samples 1/3") above a slim line showing how
// much of it is in. Shown only once a load has taken 400 ms (a fast load never shows it); the counts are real items.
// Sits over the top of the canvas, follows it, takes no clicks and no focus; its title and description name it for a
// screen reader.

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class LoadStageLine final
    : public juce::Component
    , private juce::ComponentListener {
public:
    /** `canvas` must outlive this; the owner adds the line to an ancestor of it. */
    explicit LoadStageLine(juce::Component& canvas);
    ~LoadStageLine() override;

    /** `progress` 0..1. Fades in when `shown` turns on and out when it turns off (at once when not on screen). */
    void update(const juce::String& stage, float progress, bool shown);
    bool isShown() const noexcept { return shown_; }
    const juce::String& getStage() const noexcept { return stage_; }
    float getProgress() const noexcept { return progress_; }

    void paint(juce::Graphics& g) override;

private:
    void componentMovedOrResized(juce::Component&, bool, bool) override { follow(); }
    void follow();
    void landFade();

    juce::Component& canvas_;
    juce::String stage_;
    float progress_ = 0.0f;
    bool shown_ = false;
    float fade_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver fadeDriver_;
};

} // namespace synth::ui
