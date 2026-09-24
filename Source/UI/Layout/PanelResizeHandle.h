#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// PanelResizeHandle.h -- FRO231: a top-edge drag strip that resizes a bottom-anchored panel.
//
// A standalone child the panel's owner places along the panel's TOP edge (setBounds(0, 0, width,
// kHeight), added last so it wins the hit test over whatever chrome it overlaps). It never sizes
// anything itself: dragging reports the height the user is asking for, measured from the OWNER's
// fixed bottom edge, and whoever owns the layout clamps it, re-lays out and persists it.
class PanelResizeHandle : public juce::Component {
public:
    /** Thickness of the strip. It overlaps whatever chrome the owner lays out beneath it, so the
     *  owner keeps its own controls clear of the top kHeight px. */
    static constexpr int kHeight = 5;

    /** `owner`'s BOTTOM edge is the fixed reference every drag is measured from, so it must be
     *  the component whose top edge the owner moves (typically this handle's own parent). */
    explicit PanelResizeHandle(juce::Component& owner);

    /** Fired on every drag step with the desired owner height, UNCLAMPED. Message thread only. */
    std::function<void(int desiredHeight)> onResize;

    /** Fired once on mouse-up with the last desired height, and only if the gesture moved: a
     *  stray click never fires it (the owner's cue to persist). */
    std::function<void(int desiredHeight)> onResizeCommitted;

    /** Whether the strip is painting its brighter hairline; it repaints only when this changes. */
    bool isHovered() const noexcept { return hovered_; }

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    bool isHighlighted() const noexcept { return hovered_ || dragging_; }
    int desiredHeightFor(const juce::MouseEvent& e) const;

    juce::Component& owner_;
    bool hovered_ = false;
    bool dragging_ = false;
    bool moved_ = false;
    int grabOffsetY_ = 0;
    int lastDesiredHeight_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelResizeHandle)
};

} // namespace synth::ui
