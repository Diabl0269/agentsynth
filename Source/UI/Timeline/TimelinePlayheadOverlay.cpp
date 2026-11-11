#include "TimelinePlayheadOverlay.h"
#include "PlayheadExtrapolator.h"
#include "PlayheadLocateGlide.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace synth::ui {

namespace {
// Guard rail for the beat->pixel conversion: at kMinPixelsPerBeat with a far-out scroll position
// beatToX() is still a well-behaved double, but llround() of an unbounded double is not. Nothing
// beyond a few screen widths can ever be visible anyway, so clamping here costs nothing and keeps
// the conversion total.
constexpr double kMaxLineXMagnitude = 1.0e7;

// Whether a locate glide may play on a component that is not on screen (a headless test), since no display frame ever
// reaches one.
bool& glideOffScreen() {
    static bool value = false;
    return value;
}

// Reports the frame run to test mode (id "timelinePlayhead"); never painted, never focused, never heard. A child, so
// the overlay's header stays free of TestModeStateSource.h.
class PlayheadStateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit PlayheadStateProbe(const TimelinePlayheadOverlay& overlay)
        : overlay_(overlay) {
        setComponentID("timelinePlayhead");
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        into.setProperty("frameCount", overlay_.getFrameCount());
        into.setProperty("medianFrameIntervalMs", overlay_.getMedianFrameIntervalMs());
        into.setProperty("frameRunning", overlay_.isPlayheadFrameRunning());
    }

private:
    const TimelinePlayheadOverlay& overlay_;
};
} // namespace

//==============================================================================
TimelinePlayheadOverlay::TimelinePlayheadOverlay(TimelineViewState& viewState)
    : viewState_(viewState) {
    // Purely decorative: the ruler underneath owns drag-to-scrub and drag-to-loop, and the lanes
    // below it own their own gestures. The overlay must never swallow either.
    setInterceptsMouseClicks(false, false);
    extrapolator_ = std::make_unique<PlayheadExtrapolator>();
    glide_ = std::make_unique<PlayheadLocateGlide>();
    stateProbe_ = std::make_unique<PlayheadStateProbe>(*this);
    addAndMakeVisible(*stateProbe_);
    stateProbe_->setBounds(0, 0, 1, 1);
}

TimelinePlayheadOverlay::~TimelinePlayheadOverlay() = default;

double TimelinePlayheadOverlay::nowMs() const {
    return testClock_ ? testClock_() : juce::Time::getMillisecondCounterHiRes();
}

double TimelinePlayheadOverlay::getMedianFrameIntervalMs() const {
    const int n = std::min<int>(frameCount_, (int)frameTimesMs_.size());
    if (n < 2)
        return 0.0;
    // The ring holds the last n stamps; the oldest is where the next one will go once it is full.
    std::vector<double> gaps;
    gaps.reserve((size_t)n - 1);
    const int first = frameCount_ > (int)frameTimesMs_.size() ? frameCount_ % (int)frameTimesMs_.size() : 0;
    for (int i = 1; i < n; ++i) {
        const double a = frameTimesMs_[(size_t)((first + i - 1) % (int)frameTimesMs_.size())];
        const double b = frameTimesMs_[(size_t)((first + i) % (int)frameTimesMs_.size())];
        gaps.push_back(b - a);
    }
    std::nth_element(gaps.begin(), gaps.begin() + (std::ptrdiff_t)gaps.size() / 2, gaps.end());
    return gaps[gaps.size() / 2];
}

void TimelinePlayheadOverlay::setGlideOffScreenForTest(bool allow) { glideOffScreen() = allow; }

void TimelinePlayheadOverlay::armLocateGlide() { glide_->arm(nowMs()); }

bool TimelinePlayheadOverlay::wantsFrames() const noexcept {
    return playing_ || previewActive_ || glide_->isActive(evaluatedAtMs_);
}

void TimelinePlayheadOverlay::startFrames() {
    if (frames_ != nullptr && !framesEnded_)
        return;
    framesEnded_ = false;
    frameCount_ = 0;
    if (frames_ == nullptr)
        frames_ = std::make_unique<juce::VBlankAttachment>(this, [this] { onFrame(); });
}

void TimelinePlayheadOverlay::stopFrames() {
    frames_.reset();
    framesEnded_ = false;
}

// A run that finished inside its own frame (a glide that landed while stopped). The attachment is dropped from the
// message queue: destroying it from inside the callback it is running would pull that callback out from under itself.
void TimelinePlayheadOverlay::endFramesFromFrame() {
    framesEnded_ = true;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<TimelinePlayheadOverlay>(this)] {
        if (safe != nullptr && safe->framesEnded_)
            safe->stopFrames();
    });
}

//==============================================================================
double TimelinePlayheadOverlay::latencyBeats() const noexcept {
    return outputLatencySeconds_ * std::max(0.0, snapshot_.bpm) / 60.0;
}

void TimelinePlayheadOverlay::adoptSnapshot(const synth::TransportService::PositionSnapshot& snapshot) {
    const double now = nowMs();
    const double drawnBefore = getDrawnBeat();

    snapshot_ = snapshot;
    snapshot_.ppq = extrapolator_->extrapolate(snapshot, now);
    wrap_.observe(snapshot_, latencyBeats());

    const bool located = haveLocateCount_ && snapshot.locateCount != locateCount_;
    haveLocateCount_ = true;
    locateCount_ = snapshot.locateCount;
    evaluatedAtMs_ = now;

    if (!located)
        return;
    const bool armed = glide_->consumeArm(now);
    const bool mayGlide =
        (isShowing() || glideOffScreen()) && !synth::ui::animationsOff() && !synth::ui::prefersReducedMotion();
    if (armed && mayGlide) {
        glide_->start(drawnBefore, now);
        startFrames();
    } else {
        glide_->cancel(); // a scrub, a held cursor move, Reduce Motion: the line jumps with the sound
    }
}

double TimelinePlayheadOverlay::getTargetBeat() const noexcept { return wrap_.drawnBeat(snapshot_, latencyBeats()); }

double TimelinePlayheadOverlay::getDrawnBeat() const noexcept {
    // Draw where the audio being HEARD is, not where the block being rendered is, but only while
    // something IS being heard. Stopped, the raw position is the truth: a ruler click that snapped
    // exactly onto a bar must draw exactly ON that bar line. When the loop wraps, the transport is
    // already back at the loop start while the sound is still latency short of the loop end, so the
    // wrap helper keeps the line travelling to the loop end before it turns around.
    return glide_->apply(wrap_.drawnBeat(snapshot_, latencyBeats()), evaluatedAtMs_);
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

    // The frame source's lifecycle lives in these lines and the glide start above — it is started on the play
    // transition and dropped on the stop/pause one (unless a preview or a glide still needs frames).
    bool justStopped = false;
    if (snapshot.playing != playing_) {
        playing_ = snapshot.playing;
        if (playing_) {
            startFrames();
        } else {
            if (!previewActive_ && !glide_->isActive(evaluatedAtMs_))
                stopFrames();
            justStopped = true;
        }
    }

    // While stopped this is silent unless the position actually moved (a seek posted from the
    // ruler); the stop transition is the one case that always emits, so the line settles exactly on
    // the position playback ended at even if the last frame had already drawn it there.
    refreshLine(/*force=*/justStopped);
}

void TimelinePlayheadOverlay::setClipPreviewActive(bool active) {
    if (previewActive_ == active)
        return;
    previewActive_ = active;
    if (active) {
        startFrames();
    } else if (!playing_ && !glide_->isActive(evaluatedAtMs_)) {
        stopFrames();
        refreshLine(/*force=*/false); // the local client settles on the beat it is left at
    }
}

void TimelinePlayheadOverlay::onFrame() {
    frameTimesMs_[(size_t)(frameCount_ % (int)frameTimesMs_.size())] = nowMs();
    ++frameCount_;

    // Re-read the transport so the line moves smoothly between the owner's 10 Hz polls. It is deliberately NOT this
    // frame's job to notice that playback stopped: one owner (the low-rate poll) manages the frame source's
    // lifecycle, and a frame after the transport stopped simply finds an unchanged x and requests nothing. The one
    // exception is its own glide: frames that ran for nothing else end when it has landed.
    if (transport_ != nullptr)
        adoptSnapshot(transport_->getPositionSnapshot());
    else
        evaluatedAtMs_ = nowMs();

    refreshLine(/*force=*/false);

    if (!wantsFrames() && frames_ != nullptr && !framesEnded_)
        endFramesFromFrame();
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
    // A big move (a locate, a glide frame) asks for the two strips apart: the span between them did not change.
    const auto shared = getSharedRegion();
    if (std::abs(x - previousX) > 4 * kStripHalfWidth + 1) {
        for (const int stripX : {previousX, x})
            if (const auto strip = stripFor(stripX).getIntersection(shared); !strip.isEmpty())
                requestRepaintStrip(strip);
        return;
    }
    const auto strip = stripFor(previousX).getUnion(stripFor(x)).getIntersection(shared);
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
