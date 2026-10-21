// StepGridPaint.cpp -- drawing the step grid: the gutter, the Pitch lane as a mini piano roll (scale rows shaded, the
// key's root tinted, each bar at the pitch the step PLAYS with the written note dashed when scale snap moved it), the
// bar lanes, the Slide links, the Condition cells, the pads, the playhead column and the dimming past Steps. Colours
// are theme tokens. Nothing repaints per tick: the grid repaints when a value, the playhead or a motion changes.
#include "Music/MusicalScale.h"
#include "StepGridComponent.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

using namespace stepgrid;

namespace {

constexpr float kLabelFont = 9.5f;

bool isBlackKey(int pitch) {
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

const synth::theme::Colors& colorsOf(const juce::Component& comp) { return theme::themeOf(comp).colors; }

struct VisibleSteps {
    int first;
    int last;
};

VisibleSteps visibleSteps(float slidePos) {
    const int first = std::max(0, (int)std::floor((double)slidePos * kColumns));
    return {first, std::min(kMaxSequencerSteps - 1, first + kColumns)};
}

} // namespace

float StepGridComponent::shownNormal(SequencerLane lane, int step) const {
    const float to = laneNormal(lane, module_.getPattern().steps[(size_t)step]);
    if (!gliding_)
        return to;
    const float from = laneNormal(lane, glideFrom_.steps[(size_t)step]);
    return from + (to - from) * glideMix(step);
}

float StepGridComponent::shownPitch(int step) const {
    const float to = (float)playedPitchOf(step);
    if (!gliding_)
        return to;
    const int from = scale_.snapPitch(juce::jlimit(0, 127, (int)glideFrom_.steps[(size_t)step].pitch + transpose_));
    return (float)from + (to - (float)from) * glideMix(step);
}

juce::String StepGridComponent::chordLabel(int step, int playedRoot) const {
    const auto& entry = module_.getPattern().steps[(size_t)step];
    if (!module_.isPoly() || entry.chord == SequencerChord::Unison)
        return noteName(playedRoot);
    return noteName(playedRoot) + " " + chordId(entry.chord);
}

void StepGridComponent::paintGutter(juce::Graphics& g, const StepGridLayout& layout) {
    const auto& c = colorsOf(*this);
    g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    for (int i = 0; i < kSequencerLaneCount; ++i) {
        const auto lane = (SequencerLane)i;
        if (lane == SequencerLane::Pitch)
            continue;
        const auto box = layout.visibleLane(lane);
        if (box.getHeight() < 10)
            continue;
        g.setColour(lane == focusLane_ && hasKeyboardFocus(false) ? c.textPrimary : c.textMuted);
        g.drawText(laneName(lane), box.withWidth(kGutterWidth - 4), juce::Justification::centredRight);
    }
    g.setColour(c.textMuted);
    g.drawText("On", layout.pads.withWidth(kGutterWidth - 4), juce::Justification::centredRight);
}

// The playhead's column and a faint shading every other group of four steps, behind the lanes' bars.
void StepGridComponent::paintColumnBackdrop(juce::Graphics& g, const StepGridLayout& layout) {
    const auto& c = colorsOf(*this);
    const auto [first, last] = visibleSteps(slidePos_);
    const int active = runningStep();
    auto span = layout.pads;
    for (int i = 0; i < kSequencerLaneCount; ++i)
        span = span.getUnion(layout.visibleLane((SequencerLane)i));
    for (int s = first; s <= last; ++s) {
        const juce::Rectangle<float> column((float)columnLeft(s), (float)span.getY(), (float)columnWidth(),
                                            (float)span.getHeight());
        if (s == active && s < stepCount()) {
            g.setColour(c.accent.withAlpha(0.14f));
            g.fillRect(column);
        } else if ((s / 4) % 2 == 1) {
            g.setColour(c.textPrimary.withAlpha(0.03f));
            g.fillRect(column);
        }
    }
}

void StepGridComponent::paintPitchLane(juce::Graphics& g, juce::Rectangle<int> lane) {
    const auto& c = colorsOf(*this);
    const int rows = pitchRange_.rows();
    const float rowH = (float)lane.getHeight() / (float)rows;
    const float gridLeft = (float)kGutterWidth;
    const float gridWidth = (float)(getWidth() - kGutterWidth);
    auto rowTop = [&](float pitch) { return (float)lane.getY() + ((float)pitchRange_.hi - pitch) * rowH; };

    g.setColour(c.bg1);
    g.fillRect(lane.withTrimmedLeft(kGutterWidth));
    g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    for (int pitch = pitchRange_.lo; pitch <= pitchRange_.hi; ++pitch) {
        const juce::Rectangle<float> row(gridLeft, rowTop((float)pitch), gridWidth, rowH);
        const bool inScale = scale_.isChromatic() ? !isBlackKey(pitch) : scale_.contains(pitch);
        g.setColour(inScale ? c.surfaceHi : c.bg0);
        g.fillRect(row.withTrimmedBottom(0.5f));
        const bool root = !scale_.isChromatic() && pitch % 12 == scale_.rootPitchClass;
        if (root) {
            g.setColour(c.accent.withAlpha(0.14f));
            g.fillRect(row);
        }
        if (root || pitch % 12 == 0) {
            g.setColour(root ? c.textPrimary : c.textMuted);
            g.drawText(noteName(pitch),
                       juce::Rectangle<float>(0.0f, row.getY() + rowH * 0.5f - 6.0f, (float)kGutterWidth - 4.0f, 12.0f),
                       juce::Justification::centredRight);
        }
    }

    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    const float colW = (float)columnWidth();
    for (int s = first; s <= last; ++s) {
        const auto& entry = pattern.steps[(size_t)s];
        const float played = shownPitch(s);
        const float barH = std::max(rowH - 1.0f, 5.0f);
        const float centre = rowTop(juce::jlimit((float)pitchRange_.lo, (float)pitchRange_.hi, played)) + rowH * 0.5f;
        const juce::Rectangle<float> bar((float)columnLeft(s) + 2.0f, centre - barH * 0.5f, colW - 4.0f, barH);
        g.setColour(entry.on ? c.accent : c.textDisabled);
        g.fillRoundedRectangle(bar, 2.0f);
        if (colW >= 22.0f) {
            g.setColour(c.textPrimary);
            const float labelY = bar.getY() >= (float)lane.getY() + 11.0f ? bar.getY() - 11.0f : bar.getBottom();
            g.drawText(chordLabel(s, juce::roundToInt(played)),
                       juce::Rectangle<float>(bar.getX() - 4.0f, labelY, bar.getWidth() + 8.0f, 11.0f),
                       juce::Justification::centred);
        }
        const int written = entry.pitch;
        if (!gliding_ && written != juce::roundToInt(played) && written >= pitchRange_.lo &&
            written <= pitchRange_.hi) {
            juce::Path outline;
            outline.addRoundedRectangle(
                juce::Rectangle<float>(bar.getX(), rowTop((float)written) + 0.5f, bar.getWidth(), barH), 2.0f);
            juce::Path dashed;
            const float dashes[] = {3.0f, 2.0f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(c.textMuted);
            g.fillPath(dashed);
        }
    }
}

void StepGridComponent::paintBarLane(juce::Graphics& g, SequencerLane laneId, juce::Rectangle<int> lane) {
    const auto& c = colorsOf(*this);
    g.setColour(c.bg1);
    g.fillRect(lane.withTrimmedLeft(kGutterWidth));
    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    const float colW = (float)columnWidth();
    const float h = (float)lane.getHeight() - 4.0f;
    const float top = (float)lane.getY() + 2.0f;
    for (int s = first; s <= last; ++s) {
        const float n = shownNormal(laneId, s);
        const float x = (float)columnLeft(s) + 2.0f;
        g.setColour(pattern.steps[(size_t)s].on ? c.accent.withAlpha(0.85f) : c.textDisabled);
        if (laneId == SequencerLane::Nudge) {
            const float mid = top + h * 0.5f;
            const float y = top + h * (1.0f - n);
            g.fillRect(x, std::min(mid, y), colW - 4.0f, std::max(1.5f, std::abs(mid - y)));
        } else {
            const float barH = std::max(1.5f, h * n);
            g.fillRect(x, top + h - barH, colW - 4.0f, barH);
        }
    }
}

// A link sits on the boundary between a step and the next: a pill when the step glides into the next, a dot when not.
void StepGridComponent::paintSlideLane(juce::Graphics& g, juce::Rectangle<int> lane) {
    const auto& c = colorsOf(*this);
    g.setColour(c.bg1);
    g.fillRect(lane.withTrimmedLeft(kGutterWidth));
    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    const float cy = (float)lane.getCentreY();
    for (int s = first; s < std::min(last, kMaxSequencerSteps - 1); ++s) {
        const float x = (float)columnLeft(s + 1);
        if (pattern.steps[(size_t)s].slide) {
            g.setColour(c.accent);
            g.fillRoundedRectangle(x - 9.0f, cy - 4.0f, 18.0f, 8.0f, 4.0f);
            g.setColour(c.bg1);
            g.fillRoundedRectangle(x - 5.0f, cy - 1.5f, 10.0f, 3.0f, 1.5f);
        } else {
            g.setColour(c.textDisabled);
            g.fillEllipse(x - 2.0f, cy - 2.0f, 4.0f, 4.0f);
        }
    }
}

void StepGridComponent::paintConditionLane(juce::Graphics& g, juce::Rectangle<int> lane) {
    const auto& c = colorsOf(*this);
    g.setColour(c.bg1);
    g.fillRect(lane.withTrimmedLeft(kGutterWidth));
    g.setFont(theme::AppLookAndFeel::uiFont(10.0f));
    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    for (int s = first; s <= last; ++s) {
        const auto& entry = pattern.steps[(size_t)s];
        const bool conditional = entry.condOf > 1;
        g.setColour(conditional ? c.accent : c.textMuted);
        g.drawText(conditionText(entry), cellBounds(SequencerLane::Condition, s), juce::Justification::centred);
    }
}

void StepGridComponent::paintPads(juce::Graphics& g, juce::Rectangle<int> row) {
    const auto& c = colorsOf(*this);
    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    const int active = runningStep();
    g.setFont(theme::AppLookAndFeel::uiFont(kLabelFont));
    for (int s = first; s <= last; ++s) {
        const juce::Rectangle<float> pad((float)columnLeft(s) + 2.0f, (float)row.getY() + 1.0f,
                                         (float)columnWidth() - 4.0f, (float)row.getHeight() - 2.0f);
        const bool on = pattern.steps[(size_t)s].on;
        const bool playing = s == active && s < stepCount();
        if (on) {
            g.setColour(playing ? c.accent : c.accent.withAlpha(0.5f));
            g.fillRoundedRectangle(pad, 3.0f);
        } else {
            g.setColour(playing ? c.accent : c.surfaceHi);
            g.drawRoundedRectangle(pad.reduced(0.5f), 3.0f, 1.0f);
        }
        if (columnWidth() >= 22.0) {
            g.setColour(on ? c.bg0 : c.textMuted);
            g.drawText(juce::String(s + 1), pad.toNearestInt(), juce::Justification::centred);
        }
    }
}

void StepGridComponent::paintFocus(juce::Graphics& g, const StepGridLayout&) {
    if (!hasKeyboardFocus(false))
        return;
    const auto cell = focusLane_ == SequencerLane::Pitch || focusLane_ == SequencerLane::Gate ||
                              focusLane_ == SequencerLane::Velocity || focusLane_ == SequencerLane::Chance ||
                              focusLane_ == SequencerLane::Repeat || focusLane_ == SequencerLane::Slide ||
                              focusLane_ == SequencerLane::Nudge || focusLane_ == SequencerLane::Condition ||
                              focusLane_ == SequencerLane::Filter
                          ? cellBounds(focusLane_, focusStep_)
                          : juce::Rectangle<int>();
    if (!cell.isEmpty())
        paintFocusRingAlways(g, cell.toFloat().reduced(1.0f), *this, 2.0f);
}

void StepGridComponent::paint(juce::Graphics& g) {
    const auto layout = currentLayout();
    paintGutter(g, layout);

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedLeft(kGutterWidth));
        for (int i = 0; i < kSequencerLaneCount; ++i) {
            const auto lane = (SequencerLane)i;
            const auto visible = layout.visibleLane(lane);
            if (visible.isEmpty())
                continue;
            juce::Graphics::ScopedSaveState laneState(g);
            g.reduceClipRegion(visible);
            const auto full = layout.lanes[(size_t)i];
            switch (lane) {
            case SequencerLane::Pitch:
                paintPitchLane(g, full);
                break;
            case SequencerLane::Slide:
                paintSlideLane(g, full);
                break;
            case SequencerLane::Condition:
                paintConditionLane(g, full);
                break;
            default:
                paintBarLane(g, lane, full);
                break;
            }
        }
        paintPads(g, layout.pads);
        paintColumnBackdrop(g, layout);

        // Steps past Steps are dimmed over everything, never removed.
        const auto [first, last] = visibleSteps(slidePos_);
        auto span = layout.pads;
        for (int i = 0; i < kSequencerLaneCount; ++i)
            span = span.getUnion(layout.visibleLane((SequencerLane)i));
        for (int s = first; s <= last; ++s) {
            const float dim = dimAmount(s);
            if (dim <= 0.0f)
                continue;
            g.setColour(juce::Colours::black.withAlpha(0.45f * dim));
            g.fillRect(juce::Rectangle<float>((float)columnLeft(s), (float)span.getY(), (float)columnWidth(),
                                              (float)span.getHeight()));
        }
    }
    paintFocus(g, layout);
}

void StepGridComponent::paintOverChildren(juce::Graphics&) {}

} // namespace synth::ui
