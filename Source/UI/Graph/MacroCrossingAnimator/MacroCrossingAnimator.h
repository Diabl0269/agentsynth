#pragma once

// MacroCrossingAnimator.h
//
// The interpolation state a Cmd+drag's mouseUp finalize
// (GraphEditor::finalizeMacroMembershipDrag) arms whenever crossing a macro's expanded hull border
// auto-created or removed a macro port and re-routed one or more cables through it.
// GraphEditor::buildVisibleCables() is memoized straight off live component/graph state with no
// interpolation of its own (docs/layout/cables.md) — this is the small, self-contained bolt-on
// that gives a handful of cables a "slide to the new anchor" tween for one time-bounded duration,
// and the dragged module itself a fading enter/exit ring, without teaching that memo anything
// about macros. See MacroCrossingAnimator.cpp for the matching rule.
//
// Pure state, same shape as synth::ui::PanelSlide (UI/Layout/UIAnimation.h): no juce_animation
// dependency, no AnimationDriver of its own. GraphEditor owns the driver (a plain
// synth::ui::AnimationDriver member, exactly like dropLandingAnim/modMatrixAnim/zoomSettleAnim)
// and calls applyTweenAt()/finish() from its onUpdate/onComplete callbacks — which is also what
// lets a test drive this class directly, with no VBlank and no Component peer required.

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class MacroCrossingAnimator {
public:
    using VisibleCable = graph_editor_types::VisibleCable;

    /** Diffs `before`/`after` (both GraphEditor::rebuildVisibleCables() snapshots, taken
     *  immediately before and after the membership mutation) and arms a tween for every cable,
     *  touching `crossingNodeUid` (the dragged module's own node id), whose CableId changed while
     *  it still shares its OTHER endpoint (same node id/channel/side) — a macro port spliced in or
     *  out on the crossing module's own cable. `flashBounds` is the dragged module's own canvas
     *  bounds (its enter/exit flash target); pass an empty rectangle to skip the flash.
     *
     *  Restricting to cables that touch the dragged module is deliberate, not just an
     *  optimisation: a port removal can also collapse a wholly-interior two-segment path between
     *  two OTHER members (e.g. A -> port -> F becoming direct A -> F once F itself joins) into one
     *  straight cable that never had a single well-defined "old anchor" to slide from — both its
     *  endpoints already sat where the new cable's endpoints are, only the bend in the middle
     *  disappeared. Scoping to the crossing module's own cables sidesteps that case entirely
     *  rather than mismatching it (see MacroCrossingAnimator.cpp's tests-adjacent comment for the
     *  concrete scenario that caught this).
     *
     *  Always arms the pure state when there is anything to animate (a matched cable pair, or a
     *  non-empty flashBounds) — never gated on whether a VBlank will actually reach the canvas, so
     *  a test can arm this class and then drive it with applyTweenAt()/finish() alone. Returns
     *  true when something was armed (the caller should then start its AnimationDriver); false is
     *  a complete no-op — isLive() stays false, nothing to apply, nothing to paint. */
    bool arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after, uint32_t crossingNodeUid,
             juce::Rectangle<int> flashBounds);

    /** Per-frame: `t` is the driver's eased 0->1 progress through the current tween. Also the
     *  manual test seam (GraphEditor::advanceMacroCrossingAnimForTest) — no VBlank required. */
    void applyTweenAt(float t) noexcept;

    /** Pins every tween at its exact final anchor and drops all state — a driver's last frame is
     *  not guaranteed to land exactly on t == 1. After this, isLive() is false and applyTo() is a
     *  no-op, so buildVisibleCables()'s own live-graph anchors show through unchanged (which are
     *  already the correct final positions — this class never has to remember them). */
    void finish() noexcept;

    /** True while a cable tween or the module flash is in flight. */
    bool isLive() const noexcept { return live_; }

    /** Called from GraphEditor::rebuildVisibleCables()'s tail, after every other post-pass:
     *  while live, overwrites each matched cable's moving endpoint (identified by its CURRENT
     *  post-splice CableId) with the lerp at the current progress. A tween whose `afterId` no
     *  longer appears in `cables` (undone, or the connection was deleted mid-tween) is silently
     *  skipped — the cable it would have moved is simply gone, nothing to anchor. */
    void applyTo(std::vector<VisibleCable>& cables) const;

    /** The dragged module's flash bounds and progress (0 = just landed, 1 = settled), or
     *  nullopt when no flash is armed. GraphEditor::GraphContentComponent::paintOverChildren reads
     *  this to draw the enter/exit ring on top of the module card. */
    std::optional<std::pair<juce::Rectangle<int>, float>> flashState() const noexcept;

private:
    struct CableTween {
        graph_editor_types::CableId afterId;
        juce::Point<float> fromP1, toP1, fromP2, toP2;
    };

    std::vector<CableTween> tweens_;
    juce::Rectangle<int> flashBounds_;
    float progress_ = 0.0f; // eased 0..1, shared by every tween + the flash
    bool live_ = false;
};
