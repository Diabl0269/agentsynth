#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// MixerSectionViewport.h (docs/mixer/panel.md#shared-sections): the scrolling frame one column puts
// around its insert or send list, so a list longer than the shared section height scrolls with the
// wheel inside its own section. No scrollbar component: a 3 px thumb and a 10 px bottom fade are
// painted over the list instead, and the frame never takes keyboard focus.
namespace synth::ui {

class MixerSectionViewport : public juce::Viewport {
public:
    static constexpr int kThumbWidth = 3;
    static constexpr int kFadeHeight = 10;

    MixerSectionViewport();

    /** `list` must outlive this viewport; it is not owned. */
    void setList(juce::Component& list);
    /** The list's own content height (rows times row height); re-fits the list to the frame. */
    void setContentHeight(int contentHeight);

    /** True when the list is taller than the frame, i.e. the wheel scrolls it. */
    bool isScrollable() const noexcept;
    /** Scrolls the least that brings list rows `top` to `bottom` (list coordinates) into the frame. */
    void revealRange(int top, int bottom);

    void resized() override;
    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    void fitList();

    int contentHeight_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSectionViewport)
};

} // namespace synth::ui
