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

//==============================================================================
void TimelineRulerComponent::mouseDown(const juce::MouseEvent& e) {
    if (transport_ == nullptr)
        return;

    if (e.mods.isCommandDown()) {
        // Cmd+click: toggle looping off, keeping the existing bounds — v1 keeps this simple
        // rather than also supporting re-enabling a brace from a plain click on it.
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, false);
        repaint();
        return;
    }

    dragAnchorBeat_ = snappedBeatAtX((double)e.position.x);
    lastPostedLoopStart_ = -1.0; // nothing posted yet this gesture
    lastPostedLoopEnd_ = -1.0;
}

void TimelineRulerComponent::mouseDrag(const juce::MouseEvent& e) {
    if (transport_ == nullptr || e.mods.isCommandDown())
        return;
    postLoopIfChanged(e);
}

void TimelineRulerComponent::mouseUp(const juce::MouseEvent& e) {
    if (transport_ == nullptr || e.mods.isCommandDown())
        return; // Cmd+click was already fully handled in mouseDown.

    if (!e.mouseWasDraggedSinceMouseDown()) {
        transport_->locateBeat(snappedBeatAtX((double)e.position.x));
        repaint();
        return;
    }

    // Finalise the drag. postLoopIfChanged is the same "only post when the snapped pair changed"
    // throttle mouseDrag used, so this naturally becomes a no-op if the last drag update already
    // posted today's final [start,end] — satisfying "post on mouseUp OR on change" with one path.
    postLoopIfChanged(e);
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
