#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Small zoomable overview map for the Graph Editor (issue #159).
//
// The minimap is a plain untransformed sibling overlay on top of GraphEditor's canvas — it never
// lives inside the panned/zoomed GraphContentComponent, so its own bounds are always in normal
// screen space (same pattern as ModMatrixComponent). Everything IT draws is expressed in CANVAS
// coordinates (the coordinate space ModuleComponents and cables already live in) and mapped down
// to the small map area with computeWorldToMap().
//
// Data flow: GraphEditor builds a MinimapModel snapshot (buildMinimapModel()) and hands it to
// setModel(). setModel() only repaints when the model actually changed — the owner ticks at
// 30 Hz and a static patch must not turn into a free-running repaint (see the no-continuous-
// repaint invariant in CLAUDE.md).
namespace synth::ui {

/** Everything the minimap draws, in CANVAS coordinates. Rebuilt by the owner only while the
 *  minimap is visible, and only repainted when it differs from the previous frame. */
struct MinimapModel {
    struct Node {
        juce::Rectangle<float> bounds; // canvas coords
        juce::Colour colour;
        bool selected = false;

        bool operator==(const Node& o) const noexcept {
            return bounds == o.bounds && colour == o.colour && selected == o.selected;
        }
        bool operator!=(const Node& o) const noexcept { return !(*this == o); }
    };

    struct Cable {
        juce::Point<float> p1, p2; // canvas coords
        juce::Colour colour;

        bool operator==(const Cable& o) const noexcept { return p1 == o.p1 && p2 == o.p2 && colour == o.colour; }
        bool operator!=(const Cable& o) const noexcept { return !(*this == o); }
    };

    std::vector<Node> nodes;
    std::vector<Cable> cables;
    juce::Rectangle<float> viewport; // the canvas rect currently visible in the editor

    bool operator==(const MinimapModel& o) const noexcept {
        return nodes == o.nodes && cables == o.cables && viewport == o.viewport;
    }
    bool operator!=(const MinimapModel& o) const noexcept { return !(*this == o); }
};

class MinimapComponent
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    MinimapComponent();

    /** Replaces the model. Repaints ONLY when the new model differs from the current one —
     *  the owner ticks at 30 Hz and a static patch must not cause a repaint storm. */
    void setModel(MinimapModel model);
    const MinimapModel& getModel() const noexcept { return model_; }

    /** Updates only the visible-canvas rect. Pan and zoom move the viewport without moving a
     *  single module or cable, so the owner uses this on every pan/zoom frame instead of
     *  rebuilding the whole model (which would re-walk the graph's edges per frame). Repaints
     *  only on an actual change, same as setModel(). */
    void setViewport(juce::Rectangle<float> viewport);

    /** Canvas point the user asked to centre the view on (click or drag on the map). */
    std::function<void(juce::Point<float>)> onNavigate;

    /** Wheel on the map zooms the main editor. Argument is a wheel deltaY (same sign/scale as
     *  juce::MouseWheelDetails::deltaY) so the owner can apply its own zoom curve. */
    std::function<void(float)> onZoom;

    // ---- Pure geometry helpers (static, no GUI state — unit-tested directly) ----

    /** World rect the map shows: the union of every node's bounds and the viewport, inflated by
     *  a small margin, and never narrower/shorter than kMinWorldSpan (so a single module doesn't
     *  blow up to fill the map). Falls back to the viewport when there are no nodes, and to a
     *  kMinWorldSpan square at the origin when the model is entirely empty. */
    static juce::Rectangle<float> computeWorldBounds(const MinimapModel& model) noexcept;

    /** Aspect-preserving world -> map-local mapping: the largest uniform scale that fits `world`
     *  inside `mapArea`, centred. Returns identity-ish safe values for degenerate inputs
     *  (zero/negative width or height on either rect) rather than dividing by zero. */
    static juce::AffineTransform computeWorldToMap(juce::Rectangle<float> world,
                                                   juce::Rectangle<float> mapArea) noexcept;

    /** Inverse of computeWorldToMap applied to a map-local point -> canvas point. */
    static juce::Point<float> mapToWorld(juce::Point<float> mapPoint, juce::Rectangle<float> world,
                                         juce::Rectangle<float> mapArea) noexcept;

    static constexpr float kMinWorldSpan = 1200.0f;
    static constexpr float kWorldMargin = 80.0f;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    /** The inset drawable area (4px in from the component bounds) that computeWorldToMap etc.
     *  should map into — shared by paint() and the mouse handlers so the map they draw and the
     *  map they hit-test agree. */
    juce::Rectangle<float> getMapArea() const;

    /** Shared by mouseDown/mouseDrag: converts the event position to a canvas point and fires
     *  onNavigate. */
    void navigateTo(juce::Point<float> localPos);

    MinimapModel model_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MinimapComponent)
};

} // namespace synth::ui
