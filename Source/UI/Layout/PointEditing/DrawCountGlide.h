// DrawCountGlide.h -- the drawn preview of a Draw box easing from the wave it showed into the wave a new cycle count
// makes (Option+drag up or down, `[` and `]`), instead of popping to it. Shared by the shape windows' pen and the
// timeline lane's Draw box. The owner hands over the line it draws right now (drawn()) just before the count changes;
// from then on paint passes the new resting line through drawn(), which blends the two with line_morph on the shared
// tween (weight 1 -> 0 over the app's glide time). A second step mid-glide starts from the line drawn at that moment,
// so a held `]` retargets and never queues. Paint only: the committed curve and the chip's number are never touched.
// Plain (the resting line, at once) under Reduce Motion, Animations Off or an off-screen owner, as KeyedTween is; the
// tween repaints the whole owner on every frame. Message thread only.
#pragma once

#include "UI/Layout/KeyGlide/KeyedPointTween.h"
#include "UI/Layout/PointEditing/LineMorph.h"

namespace synth::ui {

class DrawCountGlide {
public:
    using Line = line_morph::Line;

    /** `owner` is repainted whole each frame; it must outlive this. */
    explicit DrawCountGlide(juce::Component& owner)
        : tween_(owner) {}

    /** The count is about to change: ease from `drawnNow` (what is painted right now) into whatever drawn() is next
     *  given. An empty line (nothing drawn yet) starts nothing. */
    void begin(Line drawnNow, bool lineIsFunctionOfX);
    /** Stops at once (a press, a release, an Escape): the next paint is the resting line. */
    void clear();
    /** `resting` as it is drawn now: blended from the line the glide began at while it runs, else `resting` itself. */
    Line drawn(const Line& resting) const;
    /** 1 at the start of a glide, 0 when none runs. */
    float weight() const;
    bool isGliding() const { return weight() > 0.0f; }

    KeyedPointTween& tweenForTest() noexcept { return tween_; }

private:
    KeyedPointTween tween_;
    Line from_;
    bool stepped_ = false; ///< the lane's line is a function of x with steps: blendAcrossSteps, not blend
};

} // namespace synth::ui
