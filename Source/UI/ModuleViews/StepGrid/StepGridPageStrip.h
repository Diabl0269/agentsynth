#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * The step grid's page strip: one small bar per page of 16 steps ("1-16", "17-32", ...) and a Follow pill. The page
 * holding the playhead pulses when the playhead moves; a click views a page. Hidden (its row kept) while there is
 * one page. One Tab stop: Left/Right move between pages, Enter views the page. docs/modules/sequencer-design.md#card.
 */
class StepGridPageStrip
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    StepGridPageStrip();

    /** `playing` is the page holding the playhead, or -1. Shows or hides the strip with `count > 1`. */
    void setPages(int count, int viewed, int playing, int steps);
    int getPageCount() const noexcept { return count_; }
    int getViewedPage() const noexcept { return viewed_; }
    int getFocusedPage() const noexcept { return focused_; }
    juce::Rectangle<int> pageBounds(int page) const;
    juce::ToggleButton& getFollowPill() noexcept { return follow_; }

    /** Pulses the bar of `page` once (the playhead stepped within it); time-bounded. */
    void pulse(int page);

    std::function<void(int page)> onViewPage;
    std::function<void(bool follow)> onFollow;

    juce::String getTooltip() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::String describe() const;
    int pageAt(juce::Point<int> p) const;

    int count_ = 1;
    int viewed_ = 0;
    int playing_ = -1;
    int steps_ = 8;
    int focused_ = 0;
    int pulsePage_ = -1;
    float pulseAmount_ = 0.0f;
    juce::ToggleButton follow_{"Follow"};
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver pulseAnim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridPageStrip)
};

} // namespace synth::ui
