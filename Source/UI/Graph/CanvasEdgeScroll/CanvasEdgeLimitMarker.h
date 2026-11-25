#pragma once

// CanvasEdgeLimitMarker.h
//
// A non-interactive overlay over the patch canvas view: while a drag held at an edge cannot pan on any further, it
// marks that side of the view with an accent line, a faint strip inward and an "Edge of canvas" chip. Owned by
// CanvasEdgeScrollDriver; reports which sides show for test mode (id "canvasEdgeLimitMarker"). See
// docs/layout/layout.md "Edge scroll while dragging".

#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/UIAnimation.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CanvasEdgeLimitMarker final
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    enum Side : unsigned { Left = 1, Top = 2, Right = 4, Bottom = 8 };

    explicit CanvasEdgeLimitMarker(juce::Component& view);

    /** The sides to mark now (a bit set of Side); zero hides it. The sides in `beyond` (a subset) are marked at the
     *  border of `frame` (the canvas frame in the view's coordinates) instead of at the view's own edge. */
    void setSides(unsigned sides, unsigned beyond = 0, juce::Rectangle<float> frame = {});
    unsigned sides() const noexcept { return sides_; }

    void paint(juce::Graphics& g) override;
    void addTestModeState(juce::DynamicObject& into) const override;

    /** Test seam: lands the fade. */
    void settleForTest() { fade_.jumpTo(sides_ != 0 ? 1.0f : 0.0f); }
    float alphaForTest() const noexcept { return fade_.alpha(); }

private:
    juce::Component& view_;
    ShownFade fade_;
    unsigned sides_ = 0;
    unsigned painted_ = 0;
    unsigned beyond_ = 0;
    unsigned beyondPainted_ = 0;
    juce::Rectangle<float> frame_;
};

} // namespace synth::ui
