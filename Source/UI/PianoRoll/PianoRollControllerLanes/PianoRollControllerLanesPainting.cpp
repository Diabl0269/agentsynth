// PianoRollControllerLanes — drawing and hover (declared in PianoRollControllerLanes.h): the lane
// header in the left gutter, the guide lines, the velocity bars (coloured exactly like the notes
// they belong to) or the CC curve with its soft accent fill, handle and bar hover states, the value
// readout next to the pointer during a drag, and the roll's playhead carried down into the strip.
//
// Theme tokens only, and no timers: hover repaints just the old and new hovered rects, a drag step
// repaints the strip (the preview can change anywhere in it). Paint only reads — the gesture
// preview when one is live, the doc otherwise.

#include "PianoRollControllerLanes.h"

#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

constexpr float kBarWidth = 4.0f;
constexpr float kDotRadius = 3.5f;
constexpr float kHandleRadius = 3.5f;
constexpr float kHoverGrow = 1.5f;

synth::theme::Colors themeColours(juce::Component& c) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        return lf->getTheme().colors;
    return {};
}

juce::Font smallFont(float size) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
}

} // namespace

void PianoRollControllerLanes::paint(juce::Graphics& g) {
    const auto colours = themeColours(*this);
    g.fillAll(colours.bg1);
    // A slightly raised seam where the strip meets the note canvas.
    g.setColour(colours.border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    g.setColour(colours.surfaceHi.withAlpha(0.6f));
    g.drawHorizontalLine(1, 0.0f, (float)getWidth());

    const auto area = getValueArea();
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(area.withY(2).withBottom(getHeight()));
        paintGuides(g);
        if (const auto* clip = openClip()) {
            const int clipEnd = xForClipBeat(clip->lengthBeats);
            if (clipEnd < area.getRight()) {
                g.setColour(colours.bg0.withAlpha(0.55f));
                g.fillRect(juce::Rectangle<int>::leftTopRightBottom(std::max(clipEnd, area.getX()), 2, area.getRight(),
                                                                    getHeight()));
            }
            if (selectedLane_ == kVelocityLane)
                paintVelocityLane(g);
            else
                paintCcLane(g);
        }
        if (roll_.hasPlayheadPosition()) {
            g.setColour(colours.textPrimary.withAlpha(0.8f));
            g.fillRect((float)roll_.getPlayheadLineX() - PianoRollComponent::kPlayheadLineWidth * 0.5f, 2.0f,
                       PianoRollComponent::kPlayheadLineWidth, (float)getHeight());
        }
        paintReadout(g);
    }
    paintSelector(g);
}

// Velocity: 32 / 64 / 96 (quarter, half, three-quarter force). CC: 0 / 64 / 127 (floor, centre /
// pedal threshold, ceiling). Faint lines, with the number at the lane's left edge.
void PianoRollControllerLanes::paintGuides(juce::Graphics& g) {
    const auto colours = themeColours(*this);
    const auto area = getValueArea();
    const int velocityGuides[] = {32, 64, 96};
    const int ccGuides[] = {0, 64, 127};
    const int* guides = selectedLane_ == kVelocityLane ? velocityGuides : ccGuides;
    g.setFont(smallFont(8.5f));
    for (int i = 0; i < 3; ++i) {
        const int y = yForValue(guides[i]);
        g.setColour(colours.border.withAlpha(guides[i] == 64 ? 0.8f : 0.5f));
        g.drawHorizontalLine(y, (float)area.getX(), (float)area.getRight());
        g.setColour(colours.textDisabled);
        g.drawText(juce::String(guides[i]), area.getX() + 3, juce::jlimit(2, getHeight() - 11, y - 10), 24, 10,
                   juce::Justification::centredLeft, false);
    }
}

// The lane header: name, the value range it edits, and a drawn caret (a path, never a Unicode glyph
// through a themed font), on a surface that lifts on hover like the roll's header chips.
void PianoRollControllerLanes::paintSelector(juce::Graphics& g) {
    const auto colours = themeColours(*this);
    const auto bounds = getSelectorBounds();
    if (bounds.getWidth() <= 0)
        return;
    g.setColour(colours.surface);
    g.fillRect(bounds.withTrimmedTop(2));
    g.setColour(colours.border);
    g.drawVerticalLine(bounds.getRight() - 1, 2.0f, (float)getHeight());

    auto pill = bounds.reduced(4, 0).withTrimmedTop(7).withHeight(18);
    g.setColour(selectorHovered_ ? colours.surfaceHi.brighter(0.12f) : colours.surfaceHi);
    g.fillRoundedRectangle(pill.toFloat(), 4.0f);
    g.setColour(colours.border);
    g.drawRoundedRectangle(pill.toFloat(), 4.0f, 1.0f);

    auto label = pill.reduced(5, 0);
    const auto caret = label.removeFromRight(7).toFloat().withSizeKeepingCentre(7.0f, 4.0f);
    juce::Path path;
    path.addTriangle(caret.getX(), caret.getY(), caret.getRight(), caret.getY(), caret.getCentreX(), caret.getBottom());
    g.setColour(colours.textMuted);
    g.fillPath(path);
    g.setColour(colours.textPrimary);
    g.setFont(smallFont(10.0f));
    g.drawFittedText(lanes::laneName(selectedLane_), label, juce::Justification::centredLeft, 1, 0.7f);

    g.setColour(colours.textDisabled);
    g.setFont(smallFont(8.5f));
    g.drawText(selectedLane_ == kVelocityLane ? "1 - 127" : "0 - 127",
               bounds.reduced(6, 0).withTrimmedTop(28).withHeight(12), juce::Justification::centredLeft, false);
}

// One bar per note at its start, filled in the note's OWN colour (resolveNoteColour through the roll,
// at the previewed velocity), with a rounded cap and a dot handle. Selected bars get the selection
// ring, the hovered bar grows, muted notes come out dimmed because their note colour already is.
void PianoRollControllerLanes::paintVelocityLane(juce::Graphics& g) {
    const auto colours = themeColours(*this);
    const auto* clip = openClip();
    const auto area = getValueArea();
    for (const auto& note : clip->notes) {
        if (note.startBeat >= clip->lengthBeats)
            break;
        const int x = xForClipBeat(note.startBeat);
        if (x < area.getX() - 8 || x > area.getRight() + 8)
            continue;
        auto shown = note;
        shown.velocity = previewVelocityFor(note.id).value_or(note.velocity);
        const auto notePaint = roll_.notePaintFor(shown);
        const bool selected = roll_.getSelection().contains(note.id);
        const bool hovered = hoveredBarBeat_.has_value() && *hoveredBarBeat_ == note.startBeat;
        const bool live = previewVelocities_.count(note.id.value) > 0;

        const float top = (float)yForValue(shown.velocity);
        const float width = kBarWidth + (hovered || live ? 1.0f : 0.0f);
        const juce::Rectangle<float> bar((float)x - width * 0.5f, top, width, (float)area.getBottom() - top + 1.0f);
        g.setColour(notePaint.fill);
        g.fillRoundedRectangle(bar, width * 0.5f);

        const float radius = kDotRadius + (hovered || live ? kHoverGrow : 0.0f);
        const auto dot = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre({(float)x, top});
        g.setColour(notePaint.fill.withAlpha(1.0f));
        g.fillEllipse(dot);
        g.setColour(selected || live ? colours.noteSelected : notePaint.border);
        g.drawEllipse(dot, selected ? 1.6f : 1.0f);
    }
}

// The curve exactly as the audio thread plays it (AutomationKernel): flat before the first point and
// after the last, a step for a Hold segment, a straight line for a Linear one — stroked in the
// theme accent over a soft accent gradient that fades toward the lane floor.
void PianoRollControllerLanes::paintCcLane(juce::Graphics& g) {
    const auto colours = themeColours(*this);
    const auto& points = shownPoints();
    const auto area = getValueArea();
    if (points.empty()) {
        g.setColour(colours.textDisabled);
        g.setFont(smallFont(10.0f));
        g.drawText("Drag to draw " + lanes::laneName(selectedLane_) + "  -  Shift: line, Alt: erase", area,
                   juce::Justification::centred, false);
        return;
    }
    juce::Path curve;
    curve.startNewSubPath((float)area.getX(), (float)yForValue(points.front().value));
    for (size_t i = 0; i < points.size(); ++i) {
        const float x = (float)xForClipBeat(points[i].beat);
        if (i > 0 && points[i - 1].curve == static_cast<int>(synth::BreakpointCurve::Hold))
            curve.lineTo(x, (float)yForValue(points[i - 1].value));
        curve.lineTo(x, (float)yForValue(points[i].value));
    }
    curve.lineTo((float)area.getRight(), (float)yForValue(points.back().value));

    juce::Path fill(curve);
    fill.lineTo((float)area.getRight(), (float)area.getBottom());
    fill.lineTo((float)area.getX(), (float)area.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colours.accent.withAlpha(0.32f), 0.0f, (float)area.getY(),
                                           colours.accent.withAlpha(0.03f), 0.0f, (float)area.getBottom(), false));
    g.fillPath(fill);
    g.setColour(colours.accent);
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool moving = gesture_ == Gesture::CcMove;
    for (size_t i = 0; i < points.size(); ++i) {
        const bool hovered = hoveredHandle_.has_value() && *hoveredHandle_ == i;
        const float radius = kHandleRadius + (hovered ? kHoverGrow : 0.0f);
        const auto centre = juce::Point<float>((float)xForClipBeat(points[i].beat), (float)yForValue(points[i].value));
        const auto dot = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
        g.setColour(hovered ? colours.accent : colours.bg1);
        g.fillEllipse(dot);
        g.setColour(hovered || moving ? colours.textPrimary : colours.accent);
        g.drawEllipse(dot, 1.4f);
    }
}

// A small pill with the value under the pointer, beside it, only while a gesture is live.
void PianoRollControllerLanes::paintReadout(juce::Graphics& g) {
    const auto text = readoutText();
    if (text.isEmpty())
        return;
    const auto colours = themeColours(*this);
    const auto box = readoutBoundsFor(lastPos_).toFloat();
    g.setColour(colours.surfaceHi.withAlpha(0.92f));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(colours.border);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    g.setColour(colours.textPrimary);
    g.setFont(smallFont(9.5f));
    g.drawText(text, box, juce::Justification::centred, false);
}

juce::String PianoRollControllerLanes::readoutText() const {
    if (gesture_ == Gesture::None || gesture_ == Gesture::CcErase)
        return {};
    const double value = valueForY(lastPos_.y);
    if (selectedLane_ == kVelocityLane)
        return juce::String(juce::jlimit(1, 127, (int)std::lround(value)));
    return juce::String(juce::jlimit(0, 127, (int)std::lround(value)));
}

juce::Rectangle<int> PianoRollControllerLanes::readoutBoundsFor(juce::Point<int> pos) const noexcept {
    const auto area = getValueArea();
    juce::Rectangle<int> box(pos.x + 10, pos.y - 18, 30, 14);
    if (box.getRight() > area.getRight())
        box.setX(pos.x - 40);
    return box.withY(juce::jlimit(2, std::max(2, getHeight() - 15), box.getY()));
}

// ---- Hover -----------------------------------------------------------------------------------------

void PianoRollControllerLanes::mouseMove(const juce::MouseEvent& e) { updateHover(e.getPosition()); }

void PianoRollControllerLanes::mouseExit(const juce::MouseEvent&) { updateHover({-1000, -1000}); }

// State-change gated: moving within the same bar / handle / selector costs nothing, and a change
// repaints only the old and new rects (the repaint budget in Source/UI/CLAUDE.md).
void PianoRollControllerLanes::updateHover(juce::Point<int> pos) {
    if (gesture_ != Gesture::None)
        return;
    const bool selector = getSelectorBounds().contains(pos);
    std::optional<size_t> handle;
    std::optional<double> bar;
    if (!selector && openClip() != nullptr && getValueArea().expanded(0, 4).contains(pos)) {
        if (selectedLane_ == kVelocityLane) {
            const auto ids = barNotesAt(pos.x);
            if (!ids.empty())
                if (const auto* note = roll_.getTimelineDoc()->getNote(ids.front()))
                    bar = note->startBeat;
        } else {
            handle = handleAt(pos);
        }
    }
    if (selector != selectorHovered_) {
        selectorHovered_ = selector;
        repaint(getSelectorBounds());
    }
    if (handle != hoveredHandle_) {
        if (hoveredHandle_)
            repaint(handleRectFor(*hoveredHandle_));
        hoveredHandle_ = handle;
        if (hoveredHandle_)
            repaint(handleRectFor(*hoveredHandle_));
    }
    if (bar != hoveredBarBeat_) {
        if (hoveredBarBeat_)
            repaint(barColumnFor(*hoveredBarBeat_));
        hoveredBarBeat_ = bar;
        if (hoveredBarBeat_)
            repaint(barColumnFor(*hoveredBarBeat_));
    }
}

juce::Rectangle<int> PianoRollControllerLanes::barColumnFor(double clipBeat) const {
    return {xForClipBeat(clipBeat) - 8, 0, 16, getHeight()};
}

juce::Rectangle<int> PianoRollControllerLanes::handleRectFor(size_t index) const {
    const auto& points = shownPoints();
    if (index >= points.size())
        return {};
    return juce::Rectangle<int>(14, 14).withCentre({xForClipBeat(points[index].beat), yForValue(points[index].value)});
}

std::optional<size_t> PianoRollControllerLanes::getHoveredHandleForTest() const noexcept { return hoveredHandle_; }
std::optional<double> PianoRollControllerLanes::getHoveredBarBeatForTest() const noexcept { return hoveredBarBeat_; }
juce::String PianoRollControllerLanes::getReadoutTextForTest() const { return readoutText(); }

} // namespace synth::ui
