// Concern (docs/control/midi-remote-ui.md#surface-centre): the surface's pan/zoom view
// transform. Mirrors GraphEditor's own canvas transform (Source/UI/Graph/GraphEditor/
// GraphEditorCanvas.cpp's updateTransform()/applyZoomAt()) but is an independent, from-scratch
// implementation: this directory never includes GraphEditor.h (Source/UI/CLAUDE.md's header-cost
// rule), and a control-surface grid has a much smaller natural zoom range than an open-ended patch
// canvas. `Content` (declared on ControllerSurfaceComponent.h) is the one place the transform is
// ever applied -- every cell is its child, so cell-local code (grid snapping, marquee hit-testing,
// drag deltas -- ControllerSurfaceCell.cpp, ControllerSurfaceMarquee.cpp,
// ControllerSurfaceGroupDrag.cpp) works in one fixed content-local coordinate space and never has
// to know the current pan/zoom at all.

#include "ControllerSurfaceComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <cmath>

namespace synth::ui {

namespace {
// content_'s fixed size (content-local units, i.e. before the view transform) -- generous enough
// that panning never runs out of room for any controller template shipped or user-authored. A fixed
// "just make it big" extent rather than sizing to the current profile's actual bounding box (the
// patch canvas grows instead, docs/layout/layout.md#canvas-frame, but a controller surface is small), which would have
// to be recomputed on every setControls() and every group move.
constexpr int kContentExtent = 6000;

// Wheel-to-pixels and wheel-to-zoom tuning, the same shape (a plain multiplier on the raw wheel
// delta, no exponential curve) TimelinePanelComponent::mouseWheelMove uses for its own two
// branches -- there is no existing shared helper for this (that file's constants are private to
// its own .cpp), so the numbers are re-tuned here for this surface's 56 px grid rather than
// shared.
constexpr float kWheelPanPixelsPerUnit = 40.0f;
constexpr float kWheelZoomSensitivity = 0.1f; // matches GraphEditor::applyZoomAt's own factor
} // namespace

ControllerSurfaceComponent::Content::Content(ControllerSurfaceComponent& owner)
    : owner_(owner) {}

// The dotted background grid, moved here from ControllerSurfaceComponent::paint(): drawn
// in CONTENT-local space so the dots stay attached to the grid under pan/zoom instead of to the
// viewport, the same "drawn in canvas space" reasoning GraphEditorCables.cpp's marquee band
// comment gives. Skipped entirely while no controller is selected -- the owner's paint() shows its
// own "No controller selected" text instead, and dots under that text would look like a rendering
// glitch, not a second, deliberately empty surface. Only the clip bounds are walked (not the whole
// kContentExtent square) -- otherwise a zoomed-out or panned frame walks tens of thousands of
// fillRect calls for dots nowhere near the screen.
void ControllerSurfaceComponent::Content::paint(juce::Graphics& g) {
    if (owner_.cells_.isEmpty())
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour gridLine = lf != nullptr ? lf->getTheme().colors.border : juce::Colours::darkgrey;
    g.setColour(gridLine.withAlpha(0.3f));

    const auto clip = g.getClipBounds();
    const int step = ControllerSurfaceComponent::kCellSize + ControllerSurfaceComponent::kCellMargin;
    const int startX = ControllerSurfaceComponent::kCellMargin +
                       ((juce::jmax(0, clip.getX() - ControllerSurfaceComponent::kCellMargin) / step) * step);
    const int startY = ControllerSurfaceComponent::kCellMargin +
                       ((juce::jmax(0, clip.getY() - ControllerSurfaceComponent::kCellMargin) / step) * step);
    for (int x = startX; x < clip.getRight(); x += step)
        for (int y = startY; y < clip.getBottom(); y += step)
            g.fillRect(x, y, 1, 1);
}

// The marquee band:
// marqueeRect_ is content-local, so painting it here -- rather than on the untransformed
// owner -- is what keeps the band locked to the cells it is selecting under pan/zoom, exactly the
// reasoning GraphEditorCables.cpp's own marquee-paint comment gives for GraphContentComponent.
void ControllerSurfaceComponent::Content::paintOverChildren(juce::Graphics& g) {
    if (!owner_.marqueeActive_)
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
    g.setColour(accent.withAlpha(0.12f));
    g.fillRect(owner_.marqueeRect_);
    g.setColour(accent);
    g.drawRect(owner_.marqueeRect_, 1);
}

// The one place content_'s transform is written -- called after every panOffset_/zoomLevel_ change
// (a drag-to-pan step, a wheel/pinch zoom, a profile-switch view restore/reset).
void ControllerSurfaceComponent::updateTransform() {
    content_.setBounds(0, 0, kContentExtent, kContentExtent);
    content_.setTransform(juce::AffineTransform::scale(zoomLevel_).translated(panOffset_));
    repaint();
}

// Shared zoom math for mouseWheelMove and mouseMagnify: `screenAnchor` is the point (in this
// component's own local/screen coordinates) whose underlying content point must stay fixed under
// the cursor/pinch centre. Clamped to [kMinZoom, kMaxZoom]; a tick that would move outside the
// clamp and lands exactly on it is still applied (the clamp is on the RESULT, not a refusal to
// move at all), and a tick already AT the clamp is a no-op (oldZoom == clamped guard below).
void ControllerSurfaceComponent::applyZoom(float newZoomLevel, juce::Point<float> screenAnchor) {
    const float oldZoom = zoomLevel_;
    const float clamped = juce::jlimit(kMinZoom, kMaxZoom, newZoomLevel);
    if (juce::approximatelyEqual(clamped, oldZoom))
        return;

    // Solve for the content point currently under screenAnchor from the OLD transform, then pick
    // the new panOffset_ that puts that same content point back under screenAnchor at the NEW
    // zoom: screenAnchor == contentPoint * zoom + panOffset_ (same equation as
    // GraphEditor::applyZoomAt's own comment).
    const auto oldTransform = juce::AffineTransform::scale(oldZoom).translated(panOffset_);
    const auto contentPoint = screenAnchor.transformedBy(oldTransform.inverted());

    zoomLevel_ = clamped;
    panOffset_ = screenAnchor - contentPoint * zoomLevel_;
    updateTransform();
}

void ControllerSurfaceComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (!event.mods.isCommandDown()) {
        // Plain wheel/trackpad-scroll pans -- what makes an overflowing grid reachable without a
        // drag. No platform branch needed for the zoom modifier below: isCommandDown() already
        // resolves to Cmd on macOS and Ctrl everywhere else (TimelinePanelComponent::
        // mouseWheelMove's own comment).
        panOffset_ += juce::Point<float>(wheel.deltaX, wheel.deltaY) * kWheelPanPixelsPerUnit;
        updateTransform();
        return;
    }
    applyZoom(zoomLevel_ + wheel.deltaY * kWheelZoomSensitivity * zoomLevel_, event.position);
}

void ControllerSurfaceComponent::mouseMagnify(const juce::MouseEvent& event, float scaleFactor) {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    applyZoom(zoomLevel_ * scaleFactor, event.position);
}

juce::Rectangle<float> ControllerSurfaceComponent::getVisibleContentRect() const noexcept {
    const auto t = juce::AffineTransform::scale(zoomLevel_).translated(panOffset_);
    return getLocalBounds().toFloat().transformedBy(t.inverted());
}

// Trivial, in-memory, session-only view-per-controller memory. Persisting it to disk
// (ControllerProfile/the project file) is deliberately out of scope -- the surface
// otherwise has no per-profile UI state at all today, so there is no existing read/write path to
// extend, and adding one is a real schema/migration decision, not a "while we're here" addition.
void ControllerSurfaceComponent::restoreOrResetView(const juce::String& newProfileId) {
    if (!profileId_.isEmpty())
        savedViewByProfileId_[profileId_] = {panOffset_, zoomLevel_};

    if (const auto it = savedViewByProfileId_.find(newProfileId); it != savedViewByProfileId_.end()) {
        panOffset_ = it->second.first;
        zoomLevel_ = it->second.second;
    } else {
        panOffset_ = {};
        zoomLevel_ = 1.0f;
    }
    updateTransform();
}

} // namespace synth::ui
