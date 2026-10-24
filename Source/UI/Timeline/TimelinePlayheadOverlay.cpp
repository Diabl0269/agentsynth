#include "TimelinePlayheadOverlay.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Guard rail for the beat->pixel conversion: at kMinPixelsPerBeat with a far-out scroll position
// beatToX() is still a well-behaved double, but llround() of an unbounded double is not. Nothing
// beyond a few screen widths can ever be visible anyway, so clamping here costs nothing and keeps
// the conversion total.
constexpr double kMaxLineXMagnitude = 1.0e7;
} // namespace

//==============================================================================
TimelinePlayheadOverlay::TimelinePlayheadOverlay(TimelineViewState& viewState)
    : viewState_(viewState) {
    // Purely decorative: the ruler underneath owns drag-to-scrub and drag-to-loop, and the lanes
    // below it own their own gestures. The overlay must never swallow either.
    setInterceptsMouseClicks(false, false);
}

//==============================================================================
double TimelinePlayheadOverlay::latencyBeats() const noexcept {
    return outputLatencySeconds_ * std::max(0.0, snapshot_.bpm) / 60.0;
}

void TimelinePlayheadOverlay::adoptSnapshot(const synth::TransportService::PositionSnapshot& snapshot) noexcept {
    snapshot_ = snapshot;
    wrap_.observe(snapshot_, latencyBeats());
}

double TimelinePlayheadOverlay::getDrawnBeat() const noexcept {
    // Draw where the audio being HEARD is, not where the block being rendered is, but only while
    // something IS being heard. Stopped, the raw position is the truth: a ruler click that snapped
    // exactly onto a bar must draw exactly ON that bar line. When the loop wraps, the transport is
    // already back at the loop start while the sound is still latency short of the loop end, so the
    // wrap helper keeps the line travelling to the loop end before it turns around.
    return wrap_.drawnBeat(snapshot_, latencyBeats());
}

int TimelinePlayheadOverlay::getLineX() const noexcept {
    const double x = std::clamp(viewState_.beatToX(getDrawnBeat()), -kMaxLineXMagnitude, kMaxLineXMagnitude);
    return (int)std::llround(x);
}

juce::Rectangle<int> TimelinePlayheadOverlay::getSharedRegion() const noexcept {
    auto bounds = getLocalBounds();
    if (localClient_ == nullptr || !localClient_->isLocalPlayheadActive() || localRegion_.isEmpty())
        return bounds;
    // The client's region is always a bottom-anchored band (the lanes rect below the ruler), so
    // "not the client's rows" is a single setBottom rather than a rectangle subtraction.
    bounds.setBottom(juce::jlimit(bounds.getY(), bounds.getBottom(), localRegion_.getY()));
    return bounds;
}

juce::Rectangle<int> TimelinePlayheadOverlay::stripFor(int x) const noexcept {
    return {x - kStripHalfWidth, 0, 2 * kStripHalfWidth + 1, getHeight()};
}

void TimelinePlayheadOverlay::requestRepaintStrip(juce::Rectangle<int> strip) { repaint(strip); }

//==============================================================================
void TimelinePlayheadOverlay::updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot,
                                                  double outputLatencySeconds) {
    outputLatencySeconds_ = std::max(0.0, outputLatencySeconds);
    adoptSnapshot(snapshot);

    // The 30 Hz timer's ENTIRE lifecycle lives in these three lines — it is started on the play
    // transition and stopped on the stop/pause one, and nothing else ever touches it.
    bool justStopped = false;
    if (snapshot.playing != playing_) {
        playing_ = snapshot.playing;
        if (playing_) {
            startTimerHz(kFrameRateHz);
        } else {
            if (!previewActive_)
                stopTimer();
            justStopped = true;
        }
    }

    // While stopped this is silent unless the position actually moved (a seek posted from the
    // ruler); the stop transition is the one case that always emits, so the line settles exactly on
    // the position playback ended at even if the last 30 Hz tick had already drawn it there.
    refreshLine(/*force=*/justStopped);
}

void TimelinePlayheadOverlay::setClipPreviewActive(bool active) {
    if (previewActive_ == active)
        return;
    previewActive_ = active;
    if (active) {
        if (!isTimerRunning())
            startTimerHz(kFrameRateHz);
    } else if (!playing_) {
        stopTimer();
        refreshLine(/*force=*/false); // the local client settles on the beat it is left at
    }
}

void TimelinePlayheadOverlay::timerCallback() {
    // Re-read the transport so the line moves smoothly between the owner's 10 Hz polls. It is
    // deliberately NOT this tick's job to notice that playback stopped: one owner (the low-rate
    // poll) manages the timer's lifecycle, and a tick after the transport stopped simply finds an
    // unchanged x and requests nothing.
    if (transport_ != nullptr)
        adoptSnapshot(transport_->getPositionSnapshot());

    refreshLine(/*force=*/false);
}

void TimelinePlayheadOverlay::refreshLine(bool force) {
    if (onDrawnBeat)
        onDrawnBeat(getDrawnBeat());

    // The local client is told the beat on EVERY refresh, before this overlay's own "did my x
    // move?" gate: its mapping is zoomed differently, so its line can move on a frame where the
    // shared one didn't. It runs the identical no-move-no-repaint gate on its own side.
    if (localClient_ != nullptr && localClient_->isLocalPlayheadActive())
        localClient_->setPlayheadBeat(getDrawnBeat());

    const int x = getLineX();

    if (!hasLineX_) {
        hasLineX_ = true;
        lineX_ = x;
        if (!force)
            return; // nothing was ever drawn, so there are no stale pixels to erase
    }

    if (x == lineX_ && !force)
        return; // zoomed far out, or a tick that landed inside the same pixel — repaint nothing

    const int previousX = lineX_;
    lineX_ = x;

    // The union of where the line WAS (in pixels — see getLastRequestedLineX's note on why the old
    // position is remembered in pixel space, not re-derived from the old beat) and where it now is,
    // clipped to the rows this overlay still owns (a local client owns the rest — and repaints it).
    const auto strip = stripFor(previousX).getUnion(stripFor(x)).getIntersection(getSharedRegion());
    if (!strip.isEmpty())
        requestRepaintStrip(strip);
}

//==============================================================================
void TimelinePlayheadOverlay::paint(juce::Graphics& g) {
    // Only the rows the shared mapping still owns: while the piano roll is open it draws the line
    // inside its own rect, at its own x.
    const auto bounds = getSharedRegion();
    if (bounds.isEmpty())
        return;

    // The line is drawn whenever this component paints — including while STOPPED, when the panel
    // repaints for some other reason. Painting is not what the confinement contract restricts;
    // ASKING for a repaint is.
    const int x = getLineX();
    if (x < -kStripHalfWidth || x > bounds.getWidth() + kStripHalfWidth)
        return;

    const juce::Colour accent = synth::theme::themeOf(*this).colors.accent;

    g.setColour(accent);
    g.fillRect((float)x - kLineWidth * 0.5f, 0.0f, kLineWidth, (float)bounds.getHeight());
}

} // namespace synth::ui
