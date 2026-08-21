#include "TimelineRulerComponent.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Adaptive density thresholds (paint()). Deterministic and cheap — no font-width measurement —
// so behaviour is identical across platforms/fonts, which is what the guard tests below rely on.
constexpr double kMinLabelSpacingPx = 40.0; // never draw two bar labels closer than this
// The beat-tick / beat-label band edges live in the header next to rulerTickPlanFor(), so the tests
// assert against the same constants paint() reads.
constexpr float kBarLabelFontHeight = 11.0f;
// Sub-labels are the same row, two points smaller and faded: the bar number has to stay the thing
// the eye lands on when scanning the strip.
constexpr float kBeatLabelFontHeight = 9.0f;
constexpr float kBeatLabelAlpha = 0.65f;
constexpr int kBarLabelWidth = 40;
constexpr int kBeatLabelWidth = 34;
constexpr float kLoopBraceHeight = 4.0f;
constexpr float kLoopBraceTickHeight = 8.0f;
// Hover affordance: just enough tint to read which half is armed, not enough to fight the ticks.
constexpr float kHoverBandAlpha = 0.10f;
// A disarmed brace stays fully drawn, just greyed — see braceColourFor().
constexpr float kInactiveBraceAlpha = 0.7f;

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

double TimelineRulerComponent::mapBeatToX(double beat) const noexcept {
    return overrideView_ != nullptr ? (double)overrideOffsetPx_ + overrideView_->beatToX(beat)
                                    : viewState_.beatToX(beat);
}

double TimelineRulerComponent::mapXToBeat(double x) const noexcept {
    return overrideView_ != nullptr ? overrideView_->xToBeat(x - (double)overrideOffsetPx_) : viewState_.xToBeat(x);
}

double TimelineRulerComponent::mapPixelsPerBeat() const noexcept {
    return overrideView_ != nullptr ? overrideView_->pixelsPerBeat : viewState_.pixelsPerBeat;
}

double TimelineRulerComponent::mapFirstVisibleBeat() const noexcept {
    // The beat at this component's x == 0 — through the override that sits LEFT of the roll's
    // keys gutter, which is fine for paint()'s "which bars are visible" sweep (it culls per bar).
    return mapXToBeat(0.0);
}

double TimelineRulerComponent::snappedBeatAtX(double x) const noexcept {
    // Snap division from the SHARED state (the one snap setting); mapping via the override.
    return viewState_.snapBeat(mapXToBeat(x), currentBeatsPerBar());
}

TimelineRulerComponent::Zone TimelineRulerComponent::zoneAtY(float y) const noexcept {
    return y < (float)getHeight() * 0.5f ? Zone::Loop : Zone::Playhead;
}

TimelineRulerComponent::BraceState TimelineRulerComponent::braceStateFor(bool looping, double loopStartBeat,
                                                                         double loopEndBeat) noexcept {
    if (!(loopEndBeat > loopStartBeat))
        return BraceState::None;
    return looping ? BraceState::Active : BraceState::Inactive;
}

juce::Colour TimelineRulerComponent::braceColourFor(BraceState state, juce::Colour accent,
                                                    juce::Colour textMuted) noexcept {
    return state == BraceState::Active ? accent : textMuted.withAlpha(kInactiveBraceAlpha);
}

TimelineRulerComponent::BraceState TimelineRulerComponent::getBraceStateForTest() const noexcept {
    if (transport_ == nullptr)
        return BraceState::None;
    const auto snap = transport_->getPositionSnapshot();
    return braceStateFor(snap.looping, snap.loopStartPpq, snap.loopEndPpq);
}

//==============================================================================
void TimelineRulerComponent::mouseDown(const juce::MouseEvent& e) {
    if (transport_ == nullptr)
        return;

    if (e.mods.isCommandDown()) {
        // Cmd+click: toggle looping off, keeping the existing bounds. Zone-agnostic. The way back
        // on is a plain click on the resulting dimmed brace (see mouseUp).
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

    // Loop zone: a click with no drag does nothing, except on a dimmed brace — the loop range is
    // the only thing this half owns, and a stray click must not clear or collapse it.
    if (e.mouseWasDraggedSinceMouseDown()) {
        postLoopIfChanged(e);
        return;
    }
    reArmLoopIfClickOnInactiveBrace(e);
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

void TimelineRulerComponent::reArmLoopIfClickOnInactiveBrace(const juce::MouseEvent& e) {
    const auto snap = transport_->getPositionSnapshot();
    if (braceStateFor(snap.looping, snap.loopStartPpq, snap.loopEndPpq) != BraceState::Inactive)
        return;

    // The target is the brace's whole x-span across the loop half, not just the 4 px bar it draws:
    // a 4 px strip is not a click target. Anything outside the span stays inert.
    const double x = (double)e.position.x;
    if (x < mapBeatToX(snap.loopStartPpq) || x > mapBeatToX(snap.loopEndPpq))
        return;

    transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, true);
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

    const double startBeat = mapFirstVisibleBeat();
    const double endBeat = mapXToBeat(widthPx);

    // Adaptive bar-label density: widen the stride by powers of two until labelled bars are at
    // least kMinLabelSpacingPx apart, so labels never overlap regardless of zoom.
    const double barWidthPx = beatsPerBar * mapPixelsPerBeat();
    juce::int64 labelEveryNBars = 1;
    while (barWidthPx * (double)labelEveryNBars < kMinLabelSpacingPx)
        labelEveryNBars *= 2;

    // ONE decision per paint, from the pure helper the tests drive directly (see
    // rulerTickPlanFor) — never re-derived inside the loop.
    const auto tickPlan = rulerTickPlanFor(mapPixelsPerBeat(), beatsPerBar);
    const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));

    const juce::int64 firstBar = (juce::int64)std::floor(startBeat / beatsPerBar) - 1;
    const juce::int64 lastBar = (juce::int64)std::ceil(endBeat / beatsPerBar) + 1;

    // Both fonts built once per paint, outside the bar sweep — the loop itself allocates nothing.
    const juce::Font barFont{juce::FontOptions(kBarLabelFontHeight)};
    const juce::Font beatFont{juce::FontOptions(kBeatLabelFontHeight)};
    const juce::Colour beatLabelColour = textMuted.withAlpha(kBeatLabelAlpha);

    g.setFont(barFont);
    for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
        const double barBeat = (double)bar * beatsPerBar;
        const double x = mapBeatToX(barBeat);
        // Cull only the bar line and its number — never the whole bar. A bar whose line has
        // scrolled off the left edge still owns beat ticks/labels that ARE on screen; a whole-bar
        // `continue` here left the ruler blank to the left of the first visible bar line while
        // scrolling (the lanes-grid painter had the same bug, fixed the same way). The tick loop
        // below culls per tick.
        if (x >= -1.0 && x <= widthPx + 1.0) {
            g.setColour(border);
            g.drawVerticalLine((int)std::llround(x), 0.0f, (float)bounds.getHeight());

            if (bar >= 0 && (bar % labelEveryNBars) == 0) {
                g.setColour(textMuted);
                g.drawText(juce::String(bar + 1), (int)std::llround(x) + 3, 0, kBarLabelWidth, bounds.getHeight(),
                           juce::Justification::centredLeft, false);
            }
        }

        if (tickPlan.drawBeatTicks) {
            for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                const double beatX = mapBeatToX(barBeat + (double)beatInBar);
                if (beatX < 0.0 || beatX > widthPx)
                    continue;
                // Half-height and faded: a beat tick must read as subordinate to the full-height
                // bar line it sits between, at a glance and without reading the labels.
                g.setColour(border.withAlpha(0.4f));
                g.drawVerticalLine((int)std::llround(beatX), (float)bounds.getHeight() * 0.5f,
                                   (float)bounds.getHeight());

                // "bar.beat" (Cubase's "80.2"), 1-based on both halves so it matches the bar
                // numbers above and the transport bar's own readout. Only once there is real room
                // — see rulerTickPlanFor.
                if (tickPlan.drawBeatLabels && bar >= 0) {
                    g.setFont(beatFont);
                    g.setColour(beatLabelColour);
                    g.drawText(juce::String(bar + 1) + "." + juce::String(beatInBar + 1), (int)std::llround(beatX) + 3,
                               0, kBeatLabelWidth, bounds.getHeight(), juce::Justification::centredLeft, false);
                    g.setFont(barFont);
                }
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

    // Loop brace: a bracket spanning [loopStartPpq, loopEndPpq]. Drawn whenever a RANGE exists —
    // looping switched off greys it out rather than hiding it, so the locators stay findable (and
    // a click on the grey brace re-arms them).
    const auto braceState = braceStateFor(looping, loopStartBeat, loopEndBeat);
    if (braceState != BraceState::None) {
        const double xStart = mapBeatToX(loopStartBeat);
        const double xEnd = mapBeatToX(loopEndBeat);
        if (xEnd >= 0.0 && xStart <= widthPx) {
            const float clampedStart = (float)juce::jlimit(0.0, widthPx, xStart);
            const float clampedEnd = (float)juce::jlimit(0.0, widthPx, xEnd);
            g.setColour(braceColourFor(braceState, accent, textMuted));
            g.fillRect(clampedStart, 0.0f, std::max(1.0f, clampedEnd - clampedStart), kLoopBraceHeight);
            g.fillRect(clampedStart, 0.0f, 1.5f, kLoopBraceTickHeight);
            g.fillRect(clampedEnd - 1.5f, 0.0f, 1.5f, kLoopBraceTickHeight);
        }
    }
}

} // namespace synth::ui
