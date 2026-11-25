// StepGridPaint.cpp -- drawing the step grid: the gutter, the Pitch lane as a mini piano roll (scale rows shaded, the
// key's root tinted, each bar at the pitch the step PLAYS with the written note outlined when scale snap moved it), the
// bar lanes, the Slide links, the Condition cells, the pads, the playhead column and the dimming past Steps. Colours
// are theme tokens. Nothing repaints per tick: the grid repaints when a value, the playhead or a motion changes.
#include "Music/MusicalScale.h"
#include "StepGridComponent.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>
#include <vector>

namespace synth::ui {

using namespace stepgrid;

namespace {

constexpr float kLabelFont = 9.5f;
constexpr float kWrittenOutlineWidth = 1.5f; ///< The solid outline of a written note a scale moved.

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
    const float from =
        fromDrawn_ ? fromNormal_[(size_t)lane][(size_t)step] : laneNormal(lane, glideFrom_.steps[(size_t)step]);
    return from + (to - from) * glideMix(step);
}

float StepGridComponent::shownPitch(int step) const {
    const float to = (float)playedPitchOf(step);
    if (!gliding_)
        return to;
    const float from = fromDrawn_ ? fromPitch_[(size_t)step]
                                  : (float)glideFromScale_.snapPitch(juce::jlimit(
                                        0, 127, (int)glideFrom_.steps[(size_t)step].pitch + glideFromTranspose_));
    return from + (to - from) * glideMix(step);
}

// The label over a bar: the note for a mono step or a single note, the chord (and its numeral) on the Poly Sequencer.
stepgrid::ChordLabel StepGridComponent::labelOf(SequencerChord chord, int playedRoot, int step) const {
    if (!module_.isPoly())
        return {noteName(playedRoot), {}};
    return chordLabelFor(chord, playedRoot, scale_, module_.getPattern().seed, step);
}

// The bar of `step` at pitch `played` in the Pitch lane `lane` (its full box), the one place both painting and the
// chord label's hit test get it from.
juce::Rectangle<float> StepGridComponent::pitchBarRect(int step, float played, juce::Rectangle<int> lane) const {
    const float rowH = (float)lane.getHeight() / (float)pitchRange_.rows();
    const float barH = std::max(rowH - 1.0f, 5.0f);
    const float rowTop =
        (float)lane.getY() +
        ((float)pitchRange_.hi - juce::jlimit((float)pitchRange_.lo, (float)pitchRange_.hi, played)) * rowH;
    return {(float)columnLeft(step) + 2.0f, rowTop + rowH * 0.5f - barH * 0.5f, (float)columnWidth() - 4.0f, barH};
}

// Above the bar, or below it for the top row; as wide as the column plus a little. A written-note outline beside the
// bar counts as part of it, so the label never crosses the outline. Drawn only in columns 22 px wide or more.
juce::Rectangle<float> StepGridComponent::chordLabelRect(const juce::Rectangle<float>& playedBar,
                                                         juce::Rectangle<int> lane, int step) const {
    auto bar = playedBar;
    for (const auto& outline : writtenOutlines(step, lane))
        if (outline.alpha > 0.0f)
            bar = bar.getUnion(outline.rect);
    const float labelY = bar.getY() >= (float)lane.getY() + 11.0f ? bar.getY() - 11.0f : bar.getBottom();
    return {bar.getX() - 4.0f, labelY, bar.getWidth() + 8.0f, 11.0f};
}

// The name, and beside it the numeral smaller and muted when both fit the column; otherwise the name alone.
void StepGridComponent::paintLabel(juce::Graphics& g, const stepgrid::ChordLabel& label, juce::Rectangle<float> area,
                                   float alpha) const {
    const auto& c = colorsOf(*this);
    const auto nameFont = theme::AppLookAndFeel::uiFont(11.0f);
    const auto numeralFont = theme::AppLookAndFeel::uiFont(9.0f);
    const float nameWidth = (float)theme::AppLookAndFeel::uiTextWidth(label.name, 11.0f);
    const float numeralWidth = (float)theme::AppLookAndFeel::uiTextWidth(label.numeral, 9.0f);
    const bool both = label.numeral.isNotEmpty() && nameWidth + 3.0f + numeralWidth <= area.getWidth();
    g.setFont(nameFont);
    g.setColour(c.textPrimary.withMultipliedAlpha(alpha));
    if (!both) {
        g.drawText(label.name, area, juce::Justification::centred);
        return;
    }
    const float left = area.getCentreX() - (nameWidth + 3.0f + numeralWidth) * 0.5f;
    g.drawText(label.name, juce::Rectangle<float>(left, area.getY(), nameWidth, area.getHeight()),
               juce::Justification::centredLeft);
    g.setFont(numeralFont);
    g.setColour(c.textMuted.withMultipliedAlpha(alpha));
    g.drawText(label.numeral,
               juce::Rectangle<float>(left + nameWidth + 3.0f, area.getY(), numeralWidth + 1.0f, area.getHeight()),
               juce::Justification::centredLeft);
}

// A Poly chord label is a button: its outline is the border colour at rest on the focused step (so the keyboard user
// sees what Enter opens) and eases to the accent while the pointer is over it.
void StepGridComponent::paintChordLabelOutline(juce::Graphics& g, int step, juce::Rectangle<float> label) const {
    if (!module_.isPoly())
        return;
    const auto& c = colorsOf(*this);
    const bool focused = step == focusStep_ && focusLane_ == SequencerLane::Pitch && hasKeyboardFocus(false);
    const float hover = step == hoverDrawnStep_ ? chordHover_.value() : 0.0f;
    if (!focused && hover <= 0.0f)
        return;
    const auto rest = focused ? c.border : c.accent.withAlpha(0.0f);
    g.setColour(rest.interpolatedWith(c.accent, hover));
    g.drawRoundedRectangle(label.reduced(0.5f), label.getHeight() * 0.5f, 1.0f);
}

// A 2 px accent line where the pattern ends: on the pads and on every shown lane, never across the chip row between.
void StepGridComponent::paintStepsMarker(juce::Graphics& g, const StepGridLayout& layout) {
    const auto markers = stepsMarkers();
    for (int k = 0; k < markers.count; ++k) {
        const float x = markers.x[(size_t)k];
        g.setColour(colorsOf(*this).accent.withMultipliedAlpha(markers.alpha[(size_t)k]));
        const auto mark = [&](juce::Rectangle<int> box) {
            if (!box.isEmpty())
                g.fillRect(x - 1.0f, (float)box.getY(), kStepsMarkerWidth, (float)box.getHeight());
        };
        mark(layout.pads);
        for (int i = 0; i < kSequencerLaneCount; ++i)
            mark(layout.visibleLane((SequencerLane)i));
    }
}

// A step whose chord just changed cross-fades from its old label to the new one.
void StepGridComponent::paintStepLabel(juce::Graphics& g, int step, int playedRoot, juce::Rectangle<float> area) const {
    const auto& entry = module_.getPattern().steps[(size_t)step];
    const auto now = labelOf(entry.chord, playedRoot, step);
    const auto& before = glideFrom_.steps[(size_t)step];
    const float strength = columnStrength(step);
    if (labelFading_ && module_.isPoly() && before.chord != entry.chord) {
        paintLabel(g, labelOf(before.chord, playedRoot, step), area, (1.0f - labelFadeT_) * strength);
        paintLabel(g, now, area, labelFadeT_ * strength);
        return;
    }
    paintLabel(g, now, area, strength);
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
    // The pads and every shown lane, each on its own: the chip row between them is never tinted.
    std::vector<juce::Rectangle<int>> boxes{layout.pads};
    for (int i = 0; i < kSequencerLaneCount; ++i)
        boxes.push_back(layout.visibleLane((SequencerLane)i));
    for (const auto& box : boxes) {
        if (box.isEmpty())
            continue;
        for (int s = first; s <= last; ++s) {
            const juce::Rectangle<float> column((float)columnLeft(s), (float)box.getY(), (float)columnWidth(),
                                                (float)box.getHeight());
            if (s == active && s < stepCount()) {
                g.setColour(c.accent.withAlpha(0.14f));
                g.fillRect(column);
            } else if ((s / 4) % 2 == 1) {
                g.setColour(c.textPrimary.withAlpha(0.03f * columnStrength(s)));
                g.fillRect(column);
            }
        }
    }
}

// Where the note a step was written at sits when a scale moved it elsewhere; empty when nothing moved it or the
// written row is not shown. The stroke is solid and centred on the returned rectangle, inset so it stays in its row.
juce::Rectangle<float> StepGridComponent::writtenOutlineBounds(int step) const {
    const auto outlines = writtenOutlines(step, currentLayout().lanes[(size_t)SequencerLane::Pitch]);
    return outlines[1].alpha > 0.0f   ? outlines[1].rect
           : outlines[0].alpha > 0.0f ? outlines[0].rect
                                      : juce::Rectangle<float>();
}

float StepGridComponent::writtenOutlineAlpha(int step, bool fromSide) const {
    return writtenOutlines(step, currentLayout().lanes[(size_t)SequencerLane::Pitch])[fromSide ? 0 : 1].alpha;
}

juce::Rectangle<float> StepGridComponent::outlineRectFor(int step, int written, int played,
                                                         juce::Rectangle<int> lane) const {
    if (written == played || written < pitchRange_.lo || written > pitchRange_.hi)
        return {};
    const float rowH = (float)lane.getHeight() / (float)pitchRange_.rows();
    const float rowTop = (float)lane.getY() + ((float)pitchRange_.hi - (float)written) * rowH;
    const float inset = kWrittenOutlineWidth * 0.5f + 0.5f;
    const auto bar = pitchBarRect(step, (float)written, lane);
    return bar.withY(rowTop + inset).withHeight(std::max(rowH - 2.0f * inset, 3.0f));
}

// While a glide runs the outline of the OLD note fades out as the one of the new note fades in, so an undo, redo or Fit
// never drops it for the span of the glide. A note that did not move keeps one outline at full strength.
std::array<StepGridComponent::WrittenOutline, 2> StepGridComponent::writtenOutlines(int step,
                                                                                    juce::Rectangle<int> lane) const {
    const int s = juce::jlimit(0, kMaxSequencerSteps - 1, step);
    const auto to = outlineRectFor(step, module_.getPattern().steps[(size_t)s].pitch, playedPitchOf(step), lane);
    const WrittenOutline now{to, to.isEmpty() ? 0.0f : 1.0f};
    if (!gliding_)
        return {WrittenOutline{}, now};
    const int fromWritten = glideFrom_.steps[(size_t)s].pitch;
    const int fromPlayed = glideFromScale_.snapPitch(juce::jlimit(0, 127, fromWritten + glideFromTranspose_));
    const auto from = outlineRectFor(step, fromWritten, fromPlayed, lane);
    if (from == to)
        return {WrittenOutline{}, now};
    const float mix = glideMix(step);
    return {WrittenOutline{from, from.isEmpty() ? 0.0f : 1.0f - mix}, WrittenOutline{to, to.isEmpty() ? 0.0f : mix}};
}

StepGridComponent::RowShade StepGridComponent::rowShadeOf(const MusicalScale& scale, int pitch) {
    const bool inScale = scale.isChromatic() ? !isBlackKey(pitch) : scale.contains(pitch);
    const bool root = !scale.isChromatic() && pitch % 12 == scale.rootPitchClass;
    return {inScale ? 1.0f : 0.0f, root ? 1.0f : 0.0f};
}

// A key change fades the row shading to the new key's with the bars' progress (the root's wash and label with it); a
// second change mid-glide starts from the shade drawn then (`fromRowShade_`). Reduce Motion lands it at once.
StepGridComponent::RowShade StepGridComponent::rowShade(int pitch) const {
    const auto to = rowShadeOf(scale_, pitch);
    if (!gliding_)
        return to;
    const auto from = fromDrawn_ ? fromRowShade_[(size_t)(pitch % 12)] : rowShadeOf(glideFromScale_, pitch);
    const float mix = rowShadeMix();
    return {from.inScale + (to.inScale - from.inScale) * mix, from.root + (to.root - from.root) * mix};
}

juce::Colour StepGridComponent::rowFill(int pitch) const {
    const auto& c = colorsOf(*this);
    const auto shade = rowShade(pitch);
    return c.bg0.interpolatedWith(c.surfaceHi, shade.inScale).overlaidWith(c.accent.withAlpha(0.14f * shade.root));
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
        const auto shade = rowShade(pitch);
        g.setColour(c.bg0.interpolatedWith(c.surfaceHi, shade.inScale));
        g.fillRect(row.withTrimmedBottom(0.5f));
        if (shade.root > 0.0f) {
            g.setColour(c.accent.withAlpha(0.14f * shade.root));
            g.fillRect(row);
        }
        if (shade.root > 0.0f || pitch % 12 == 0) {
            g.setColour(c.textMuted.interpolatedWith(c.textPrimary, shade.root));
            g.drawText(noteName(pitch),
                       juce::Rectangle<float>(0.0f, row.getY() + rowH * 0.5f - 6.0f, (float)kGutterWidth - 4.0f, 12.0f),
                       juce::Justification::centredRight);
        }
    }

    const auto [first, last] = visibleSteps(slidePos_);
    const auto& pattern = module_.getPattern();
    const float colW = (float)columnWidth();
    // Steps past Steps: the row shading washes toward the lane fill (never a black cover), and the bars draw lighter.
    for (int s = first; s <= last; ++s) {
        if (const float dim = dimAmount(s); dim > 0.0f) {
            g.setColour(c.bg1.withAlpha(kDimWashAlpha * dim));
            g.fillRect(juce::Rectangle<float>((float)columnLeft(s), (float)lane.getY(), (float)columnWidth(),
                                              (float)lane.getHeight()));
        }
    }
    for (int s = first; s <= last; ++s) {
        const auto& entry = pattern.steps[(size_t)s];
        const float played = shownPitch(s);
        const auto bar = pitchBarRect(s, played, lane);
        g.setColour((entry.on ? c.accent : c.textDisabled).withMultipliedAlpha(columnStrength(s)));
        g.fillRoundedRectangle(bar, 2.0f);
        if (colW >= 22.0f)
            paintStepLabel(g, s, juce::roundToInt(played), chordLabelRect(bar, lane, s));
        if (colW >= 22.0f)
            paintChordLabelOutline(g, s, chordLabelRect(bar, lane, s));
        for (const auto& outline : writtenOutlines(s, lane)) {
            if (outline.alpha <= 0.0f)
                continue;
            g.setColour(c.textPrimary.withAlpha(outline.alpha * columnStrength(s)));
            g.drawRoundedRectangle(outline.rect, 2.0f, kWrittenOutlineWidth);
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
        g.setColour((pattern.steps[(size_t)s].on ? c.accent.withAlpha(0.85f) : c.textDisabled)
                        .withMultipliedAlpha(columnStrength(s)));
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
        const float strength = columnStrength(s);
        if (pattern.steps[(size_t)s].slide) {
            g.setColour(c.accent.withMultipliedAlpha(strength));
            g.fillRoundedRectangle(x - 9.0f, cy - 4.0f, 18.0f, 8.0f, 4.0f);
            g.setColour(c.bg1);
            g.fillRoundedRectangle(x - 5.0f, cy - 1.5f, 10.0f, 3.0f, 1.5f);
        } else {
            g.setColour(c.textDisabled.withMultipliedAlpha(strength));
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
        g.setColour((conditional ? c.accent : c.textMuted).withMultipliedAlpha(columnStrength(s)));
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
        const float strength = columnStrength(s);
        if (on) {
            g.setColour((playing ? c.accent : c.accent.withAlpha(0.5f)).withMultipliedAlpha(strength));
            g.fillRoundedRectangle(pad, 3.0f);
        } else {
            g.setColour((playing ? c.accent : c.surfaceHi).withMultipliedAlpha(strength));
            g.drawRoundedRectangle(pad.reduced(0.5f), 3.0f, 1.0f);
        }
        if (columnWidth() >= 22.0) {
            g.setColour((on ? c.bg0 : c.textMuted).withMultipliedAlpha(on ? 1.0f : strength));
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

        paintStepsMarker(g, layout);
    }
    paintFocus(g, layout);
}

void StepGridComponent::paintOverChildren(juce::Graphics&) {}

} // namespace synth::ui
