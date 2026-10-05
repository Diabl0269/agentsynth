// GraphEditorMinimapSlide.cpp
//
// The mini map's show/hide: it fades in and slides out of its bottom-left corner, and fades back and
// slides into it, through PanelSlide + AnimationDriver (docs/layout/animation.md). resized() and every
// frame call layoutMinimap(), the single geometry authority, so a resize mid-slide stays correct.

#include "GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
constexpr double kMinimapInMs = 160.0;     // small reveal in (Motion rules)
constexpr double kMinimapOutMs = 110.0;    // and out
constexpr double kMinimapReducedMs = 80.0; // Reduce Motion: plain fade, no slide
constexpr int kMinimapSlidePx = 12;        // distance travelled from its corner
} // namespace

void GraphEditor::layoutMinimap() {
    // Bottom-LEFT with a 12px margin — the mod-matrix panel occupies a 600px panel on the right.
    // Auto-hide when the editor is too small to show it without swallowing the view, but never
    // clobber the user's preference: `minimapVisible` still reflects what they asked for, and
    // layout just recomputes whether that preference currently fits.
    constexpr int kMargin = 12;
    // Absolute floors, not a fraction of the editor: a fraction-of-self test is always
    // satisfied (w/4 * 2 <= w for any w), so it would never actually hide anything.
    constexpr int kMinEditorW = 480, kMinEditorH = 360;
    const int mmW = juce::jmin(220, getWidth() / 4);
    const int mmH = juce::jmin(150, getHeight() / 4);
    const bool fits = getWidth() >= kMinEditorW && getHeight() >= kMinEditorH;

    // The map emerges from its corner: at fraction 0 it sits minimapSlideDistance_ px toward it.
    const float p = minimapSlide_.getProgress();
    const int back = juce::roundToInt(static_cast<float>(minimapSlideDistance_) * (1.0f - p));
    minimap.setBounds(kMargin - back, getHeight() - mmH - kMargin + back, mmW, mmH);
    minimap.setAlpha(p);
    // Closing keeps it visible until the fade ends; it ignores the mouse meanwhile.
    minimap.setVisible(fits && (minimapVisible || p > 0.0f));
    minimap.setInterceptsMouseClicks(minimapVisible, false);
}

void GraphEditor::setMinimapVisible(bool shouldBeVisible) {
    minimapVisible = shouldBeVisible;
    const float target = shouldBeVisible ? 1.0f : 0.0f;

    // No VBlank reaches an off-screen component: a headless toggle, or a restore before the window
    // exists, lands synchronously.
    const bool canAnimate = isShowing() || minimapAnimateOffScreen_;
    const bool reduced = synth::ui::prefersReducedMotion();
    minimapSlideDistance_ = reduced ? 0 : kMinimapSlidePx;

    if (!minimapSlide_.retarget(target, canAnimate)) {
        finishMinimapSlide();
        return;
    }

    layoutMinimap(); // frame 0, before the first VBlank
    if (shouldBeVisible && minimap.isVisible())
        minimap.setModel(buildMinimapModel()); // seed it: the map would be empty until the next tick

    const double ms = reduced ? kMinimapReducedMs : (shouldBeVisible ? kMinimapInMs : kMinimapOutMs);
    minimapAnim_.start(
        vblankUpdater, ms, shouldBeVisible ? synth::ui::easeOutCubic : synth::ui::easeInCubic,
        [this](float t) {
            minimapSlide_.applyTweenAt(t);
            layoutMinimap();
        },
        [this] { finishMinimapSlide(); });
}

void GraphEditor::finishMinimapSlide() {
    minimapAnim_.stop(vblankUpdater);
    minimapSlide_.finish();
    layoutMinimap();
    // updateTransform() only pushes the viewport, so seed the full model on the way in.
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());
}

void GraphEditor::toggleMinimapVisibility() { setMinimapVisible(!minimapVisible); }

void GraphEditor::advanceMinimapSlideForTest(float t) {
    minimapSlide_.applyTweenAt(t);
    layoutMinimap();
}

void GraphEditor::finishMinimapSlideForTest() { finishMinimapSlide(); }
