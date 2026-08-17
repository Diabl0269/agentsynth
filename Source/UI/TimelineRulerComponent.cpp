#include "TimelineRulerComponent.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Adaptive density thresholds (paint()). Deterministic and cheap — no font-width measurement —
// so behaviour is identical across platforms/fonts, which is what the guard tests below rely on.
constexpr double kMinLabelSpacingPx = 40.0;       // never draw two bar labels closer than this
constexpr double kMinBeatTickPixelsPerBeat = 8.0; // below this, per-beat ticks would just be noise
constexpr float kLoopBraceHeight = 4.0f;
constexpr float kLoopBraceTickHeight = 8.0f;
// Hover affordance: just enough tint to read which half is armed, not enough to fight the ticks.
constexpr float kHoverBandAlpha = 0.10f;

// Same formula as TransportService::getPosition() (a beat is always a quarter note, regardless of
// the file's notated denominator) — kept in sync there rather than shared, since that one lives on
// the audio-thread-facing side and this is message-thread-only UI.
double beatsPerBarFrom(int numerator, int denominator) noexcept {
    return (double)numerator * 4.0 / (double)std::max(1, denominator);
}
} // namespace

//==============================================================================
TimelineRulerComponent::TimelineRulerComponent(TimelineViewState& viewState)
    : viewState_(viewState) {}

//==============================================================================
double TimelineRulerComponent::currentBeatsPerBar() const noexcept {
    if (transport_ == nullptr)
        return 4.0;
    const auto snap = transport_->getPositionSnapshot();
    const double beatsPerBar = beatsPerBarFrom(snap.timeSigNumerator, snap.timeSigDenominator);
    return beatsPerBar > 0.0 ? beatsPerBar : 4.0;
}

double TimelineRulerComponent::snappedBeatAtX(double x) const noexcept {
    return viewState_.snapBeat(viewState_.xToBeat(x), currentBeatsPerBar());
}

TimelineRulerComponent::Zone TimelineRulerComponent::zoneAtY(float y) const noexcept {
    return y < (float)getHeight() * 0.5f ? Zone::Loop : Zone::Playhead;
}

//==============================================================================
void TimelineRulerComponent::mouseDown(const juce::MouseEvent& e) {
    if (transport_ == nullptr)
        return;

    if (e.mods.isCommandDown()) {
        // Cmd+click: toggle looping off, keeping the existing bounds — v1 keeps this simple
        // rather than also supporting re-enabling a brace from a plain click on it. Zone-agnostic.
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, false);
        repaint();
        return;
    }

    // Latch the zone for the whole gesture: mid-drag the pointer routinely leaves the band it
    // started in, and the gesture must not change meaning under the user's hand.
    gestureZone_ = zoneAtY(e.position.y);

    dragAnchorBeat_ = snappedBeatAtX((double)e.position.x);
    lastPostedLoopStart_ = -1.0; // nothing posted yet this gesture
    lastPostedLoopEnd_ = -1.0;
    lastPostedSeekBeat_ = -1.0;

    // Playhead zone seeks on press, not on release — the cursor lands where you clicked and then
    // follows the drag.
    if (gestureZone_ == Zone::Playhead)
        postSeekIfChanged(e);
}

void TimelineRulerComponent::mouseDrag(const juce::MouseEvent& e) {
    if (transport_ == nullptr || e.mods.isCommandDown())
        return;

    if (gestureZone_ == Zone::Playhead)
        postSeekIfChanged(e);
    else
        postLoopIfChanged(e);
}

void TimelineRulerComponent::mouseUp(const juce::MouseEvent& e) {
    if (transport_ == nullptr || e.mods.isCommandDown())
        return; // Cmd+click was already fully handled in mouseDown.

    // Both finalisers are the same "only post when the snapped value changed" throttle their drag
    // path used, so a release that adds no new information is a no-op.
    if (gestureZone_ == Zone::Playhead) {
        postSeekIfChanged(e);
        return;
    }

    // Loop zone: a click with no drag does nothing. Deliberate — the loop range is the only thing
    // this half owns, and a stray click must not clear or collapse it.
    if (e.mouseWasDraggedSinceMouseDown())
        postLoopIfChanged(e);
}

void TimelineRulerComponent::mouseEnter(const juce::MouseEvent& e) { setHoveredZone(zoneAtY(e.position.y)); }

void TimelineRulerComponent::mouseMove(const juce::MouseEvent& e) { setHoveredZone(zoneAtY(e.position.y)); }

void TimelineRulerComponent::mouseExit(const juce::MouseEvent&) { setHoveredZone(std::nullopt); }

void TimelineRulerComponent::setHoveredZone(std::optional<Zone> zone) {
    if (zone == hoveredZone_)
        return; // repaint on zone changes only — never once per pixel of mouse movement
    hoveredZone_ = zone;

    if (!hoveredZone_.has_value())
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (*hoveredZone_ == Zone::Playhead)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);

    repaint();
}

void TimelineRulerComponent::postSeekIfChanged(const juce::MouseEvent& e) {
    // Clamped the same way TransportService::locateBeat clamps, so the dedupe below can't be
    // fooled into re-posting identical seeks while the pointer drags left of beat 0.
    const double beat = std::max(0.0, snappedBeatAtX((double)e.position.x));
    if (beat == lastPostedSeekBeat_)
        return; // unchanged since the last post — the FIFO dedupes nothing, so don't spam it

    transport_->locateBeat(beat);
    lastPostedSeekBeat_ = beat;
    ++seekPostCount_;
    repaint();
}

void TimelineRulerComponent::postLoopIfChanged(const juce::MouseEvent& e) {
    const double current = snappedBeatAtX((double)e.position.x);
    const double start = std::min(dragAnchorBeat_, current);
    const double end = std::max(dragAnchorBeat_, current);
    if (end <= start)
        return; // degenerate/zero-length so far — wait for more drag distance
    if (start == lastPostedLoopStart_ && end == lastPostedLoopEnd_)
        return; // unchanged since the last post — the FIFO dedupes nothing, so don't spam it

    transport_->setLoop(start, end, true);
    lastPostedLoopStart_ = start;
    lastPostedLoopEnd_ = end;
    repaint();
}

//==============================================================================
void TimelineRulerComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour bg, border, textMuted, accent;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        border = c.border;
        textMuted = c.textMuted;
        accent = c.accent;
    } else {
        bg = juce::Colours::darkgrey.darker(0.3f);
        border = juce::Colours::grey;
        textMuted = juce::Colours::lightgrey;
        accent = juce::Colours::cyan;
    }

    const auto bounds = getLocalBounds();
    g.setColour(bg);
    g.fillRect(bounds);
    g.setColour(border);
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());

    const double widthPx = (double)getWidth();
    if (widthPx <= 0.0)
        return;

    double beatsPerBar = 4.0;
    bool looping = false;
    double loopStartBeat = 0.0;
    double loopEndBeat = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        beatsPerBar = beatsPerBarFrom(snap.timeSigNumerator, snap.timeSigDenominator);
        if (beatsPerBar <= 0.0)
            beatsPerBar = 4.0;
        looping = snap.looping;
        loopStartBeat = snap.loopStartPpq;
        loopEndBeat = snap.loopEndPpq;
    }

    const double startBeat = viewState_.firstVisibleBeat;
    const double endBeat = viewState_.xToBeat(widthPx);

    // Adaptive bar-label density: widen the stride by powers of two until labelled bars are at
    // least kMinLabelSpacingPx apart, so labels never overlap regardless of zoom.
    const double barWidthPx = beatsPerBar * viewState_.pixelsPerBeat;
    juce::int64 labelEveryNBars = 1;
    while (barWidthPx * (double)labelEveryNBars < kMinLabelSpacingPx)
        labelEveryNBars *= 2;

    const bool drawBeatTicks = viewState_.pixelsPerBeat >= kMinBeatTickPixelsPerBeat;
    const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));

    const juce::int64 firstBar = (juce::int64)std::floor(startBeat / beatsPerBar) - 1;
    const juce::int64 lastBar = (juce::int64)std::ceil(endBeat / beatsPerBar) + 1;

    g.setFont(juce::Font(11.0f));
    for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
        const double barBeat = (double)bar * beatsPerBar;
        const double x = viewState_.beatToX(barBeat);
        if (x < -1.0 || x > widthPx + 1.0)
            continue;

        g.setColour(border);
        g.drawVerticalLine((int)std::llround(x), 0.0f, (float)bounds.getHeight());

        if (bar >= 0 && (bar % labelEveryNBars) == 0) {
            g.setColour(textMuted);
            g.drawText(juce::String(bar + 1), (int)std::llround(x) + 3, 0, 40, bounds.getHeight(),
                       juce::Justification::centredLeft, false);
        }

        if (drawBeatTicks) {
            for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                const double beatX = viewState_.beatToX(barBeat + (double)beatInBar);
                if (beatX < 0.0 || beatX > widthPx)
                    continue;
                g.setColour(border.withAlpha(0.4f));
                g.drawVerticalLine((int)std::llround(beatX), (float)bounds.getHeight() * 0.5f,
                                   (float)bounds.getHeight());
            }
        }
    }

    // Hover affordance: tint the half the pointer is over, so which gesture is armed is visible
    // before pressing. Drawn under the loop brace so the brace stays legible.
    if (hoveredZone_.has_value()) {
        auto band = bounds.toFloat();
        if (*hoveredZone_ == Zone::Loop)
            band = band.removeFromTop(band.getHeight() * 0.5f);
        else
            band = band.removeFromBottom(band.getHeight() * 0.5f);
        g.setColour(accent.withAlpha(kHoverBandAlpha));
        g.fillRect(band);
    }

    // Loop brace: a bracket spanning [loopStartPpq, loopEndPpq] in the accent colour.
    if (looping && loopEndBeat > loopStartBeat) {
        const double xStart = viewState_.beatToX(loopStartBeat);
        const double xEnd = viewState_.beatToX(loopEndBeat);
        if (xEnd >= 0.0 && xStart <= widthPx) {
            const float clampedStart = (float)juce::jlimit(0.0, widthPx, xStart);
            const float clampedEnd = (float)juce::jlimit(0.0, widthPx, xEnd);
            g.setColour(accent);
            g.fillRect(clampedStart, 0.0f, std::max(1.0f, clampedEnd - clampedStart), kLoopBraceHeight);
            g.fillRect(clampedStart, 0.0f, 1.5f, kLoopBraceTickHeight);
            g.fillRect(clampedEnd - 1.5f, 0.0f, 1.5f, kLoopBraceTickHeight);
        }
    }
}

} // namespace synth::ui
