#include "CurveEditorComponent.h"
#include "UI/Layout/FocusRing.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr float kNodeRadius = 5.0f;
constexpr float kBendHandleRadius = 4.0f;
constexpr int kCurveStepsPerSegment = 24;
constexpr float kLandmarkThickness = 1.5f;
constexpr float kLandmarkAlpha = 0.55f;
constexpr float kOutOfKeyAlphaScale = 0.5f;
constexpr float kKindHueShare = 0.35f;
constexpr float kLabelGapPx = 3.0f;

/** A full-length line at pixel `position`. A landmark is 1.5 px, centred on the pixel column it shares with a 1 px line
 *  so the two stay comparable in a pixel test. */
void drawGridLine(juce::Graphics& g, bool vertical, float position, float length, bool landmark) {
    const int p = (int)position;
    if (!landmark) {
        vertical ? g.drawVerticalLine(p, 0.0f, length) : g.drawHorizontalLine(p, 0.0f, length);
        return;
    }
    const float start = (float)p - (kLandmarkThickness - 1.0f) * 0.5f;
    vertical ? g.fillRect(start, 0.0f, kLandmarkThickness, length)
             : g.fillRect(0.0f, start, length, kLandmarkThickness);
}
} // namespace

CurveEditorComponent::ThemeColours CurveEditorComponent::resolveThemeColours() const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr) {
        const auto& colors = lf->getTheme().colors;
        const auto grid = colors.border.withAlpha(0.4f);
        return {colors.bg1,
                grid,
                colors.textMuted,
                colors.accent,
                colors.textMuted.withAlpha(kLandmarkAlpha),
                grid.withMultipliedAlpha(kOutOfKeyAlphaScale),
                colors.accent,
                colors.warning};
    }
    return {juce::Colour(0xff1a1a2e),
            juce::Colour(0xff2a2a3e),
            juce::Colour(0xff6a6a7e),
            juce::Colour(0xff00b4d8),
            juce::Colour(0xff6a6a7e).withAlpha(kLandmarkAlpha),
            juce::Colour(0xff2a2a3e).withMultipliedAlpha(kOutOfKeyAlphaScale),
            juce::Colour(0xff00b4d8),
            juce::Colour(0xffe0a33d)};
}

// The grid's own lines, unlabelled: the vertical cells (or the explicit lines) and the horizontal levels, one level a
// landmark (the middle one, or the level the grid names).
void CurveEditorComponent::paintGridLines(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                          const ThemeColours& colours) const {
    const float h = (float)getHeight();
    if (!grid_->xLines.empty()) {
        paintExplicitXLines(g, geometry, colours);
    } else {
        g.setColour(colours.grid);
        for (const double t : gridXPositions())
            drawGridLine(g, true, geometry.xForTime(t), h, false);
    }
    if (!gridHasY())
        return;
    const int yDiv = grid_->yDivisions;
    for (int i = 0; i <= yDiv; ++i) {
        const float level = (float)i / (float)yDiv;
        const float y = geometry.yForLevel(level);
        const bool landmark = grid_->yLandmark.has_value() ? std::abs(level - *grid_->yLandmark) < 1.0e-4f
                                                           : (yDiv % 2 == 0) && (i == yDiv / 2);
        g.setColour(landmark ? colours.landmark : colours.grid);
        drawGridLine(g, false, y, (float)getWidth(), landmark);
    }
}

// A note line leans toward the accent and a harmonic line toward the second hue by a fixed share, keeping its tier's
// alpha, so brightness still says how important a line is and the hue only says what kind it is.
juce::Colour CurveEditorComponent::gridLineColour(const ThemeColours& colours, GridLine::Tier tier,
                                                  GridLine::Kind kind) const {
    const auto base = tier == GridLine::Tier::landmark ? colours.landmark
                      : tier == GridLine::Tier::snap   ? colours.grid
                                                       : colours.outOfKey;
    if (kind == GridLine::Kind::neutral)
        return base;
    const auto hue = kind == GridLine::Kind::note ? colours.noteHue : colours.harmonicHue;
    return base.interpolatedWith(hue.withAlpha(base.getFloatAlpha()), kKindHueShare);
}

// Landmarks always draw; a fainter line closer than kMinLineGapPx to the last line drawn is skipped (it still snaps),
// so a fine grid never turns into a solid wash. Out-of-key lines are dotted as well as dimmer: colour is never the
// only cue.
void CurveEditorComponent::paintExplicitXLines(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                               const ThemeColours& colours) const {
    const float h = (float)getHeight();
    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    std::vector<float> landmarks;
    for (const auto& line : grid_->xLines)
        if (line.tier == GridLine::Tier::landmark && line.x >= minX && line.x <= maxX)
            landmarks.push_back(std::floor(geometry.xForTime(line.x)));
    float lastDrawn = -1.0e9f;
    std::size_t nextLandmark = 0;
    for (const auto& line : grid_->xLines) {
        if (line.x < minX - 1e-9 || line.x > maxX + 1e-9)
            continue;
        const float px = std::floor(geometry.xForTime(line.x));
        const bool landmark = line.tier == GridLine::Tier::landmark;
        while (nextLandmark < landmarks.size() && landmarks[nextLandmark] < px)
            ++nextLandmark;
        if (!landmark) {
            const bool crowdsLast = px - lastDrawn < kMinLineGapPx;
            const bool crowdsLandmark = nextLandmark < landmarks.size() && landmarks[nextLandmark] - px < kMinLineGapPx;
            if (crowdsLast || crowdsLandmark)
                continue;
        }
        g.setColour(gridLineColour(colours, line.tier, line.kind));
        if (line.tier == GridLine::Tier::outOfKey) {
            const float dashes[] = {2.0f, 3.0f};
            g.drawDashedLine(juce::Line<float>(px + 0.5f, 0.0f, px + 0.5f, h), dashes, 2, 1.0f);
        } else {
            drawGridLine(g, true, px, h, landmark);
        }
        lastDrawn = px;
    }
}

// The explicit lines' labels along the top: the landmarks' first, then the rest, each skipped when it would overlap a
// label already placed or run off the edge.
void CurveEditorComponent::paintGridLabels(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                           const ThemeColours& colours) const {
    if (!grid_.has_value() || grid_->xLines.empty())
        return;
    const juce::Font font(juce::FontOptions(9.0f));
    g.setFont(font);
    g.setColour(colours.mutedText);
    const float w = (float)getWidth();
    std::vector<juce::Rectangle<float>> placed;
    for (const bool landmarks : {true, false}) {
        for (const auto& line : grid_->xLines) {
            if (line.label.isEmpty() || (line.tier == GridLine::Tier::landmark) != landmarks)
                continue;
            if (line.x < model_.getMinX() || line.x > model_.getMaxX())
                continue;
            const float px = std::floor(geometry.xForTime(line.x));
            const float textWidth = juce::GlyphArrangement::getStringWidth(font, line.label);
            const juce::Rectangle<float> box(px + 2.0f, 2.0f, textWidth + 1.0f, 11.0f);
            if (box.getRight() > w)
                continue;
            const auto padded = box.expanded(kLabelGapPx, 0.0f);
            if (std::any_of(placed.begin(), placed.end(), [&](const auto& r) { return r.intersects(padded); }))
                continue;
            placed.push_back(box);
            g.drawText(line.label, box, juce::Justification::left, false);
        }
    }
}

void CurveEditorComponent::paintGrid(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                     const ThemeColours& colours) const {
    const float h = (float)getHeight();

    // guides_ set (the Filter shape editor): exactly its labelled lines, wherever the caller put them. While the
    // editor snaps, the snap lines are drawn first, under the guides; the guides are the landmarks (a zero line, the
    // labelled frequencies), and a switch hides them without touching the snap lines.
    if (guides_.has_value()) {
        if (grid_.has_value() && snapToGrid_) {
            paintGridLines(g, geometry, colours);
            paintGridLabels(g, geometry, colours);
        }
        if (!guidesVisible_)
            return;
        g.setFont(juce::Font(9.0f));
        for (const auto& guide : guides_->x) {
            const int x = (int)geometry.xForTime(guide.position);
            g.setColour(colours.landmark);
            drawGridLine(g, true, (float)x, h, true);
            g.setColour(colours.mutedText);
            g.drawText(guide.label, x + 2, (int)h - 12, 48, 11, juce::Justification::left, false);
        }
        for (const auto& guide : guides_->y) {
            const int y = (int)geometry.yForLevel((float)guide.position);
            g.setColour(guide.emphasised ? colours.landmark : colours.grid);
            drawGridLine(g, false, (float)y, (float)getWidth(), guide.emphasised);
            g.setColour(colours.mutedText);
            g.drawText(guide.label, 3, y - 12, 56, 11, juce::Justification::left, false);
        }
        return;
    }

    // grid_ set (the LFO card): a fixed xDivisions x yDivisions grid, in place of the nice-tick
    // time grid every other caller (the envelope card) still gets below.
    if (grid_.has_value()) {
        paintGridLines(g, geometry, colours);
        g.setFont(juce::Font(9.0f));
        g.setColour(colours.mutedText);
        for (const double t : gridXPositions()) {
            const float x = geometry.xForTime(t);
            const juce::String label = timeLabelFormatter_(t); // LFO's own formatter returns {}
            g.drawText(label, (int)x + 2, (int)h - 12, 48, 11, juce::Justification::left, false);
        }
        return;
    }

    const auto ticks = geometry.computeGridTicks();

    g.setColour(colours.grid);
    for (const double t : ticks)
        g.drawVerticalLine((int)geometry.xForTime(t), 0.0f, h);

    g.setFont(juce::Font(9.0f));
    g.setColour(colours.mutedText);
    for (const double t : ticks) {
        const float x = geometry.xForTime(t);
        const juce::String label = timeLabelFormatter_(t);
        g.drawText(label, (int)x + 2, (int)h - 12, 48, 11, juce::Justification::left, false);
    }
}

// How far the point at `progress` along a segment is drawn from its model place: the x offset blends linearly between
// the two nodes' offsets and the y offset by how far along the level change the point is, so the line meets both
// gliding nodes.
juce::Point<float> CurveEditorComponent::segmentTweenOffset(int seg, float progress) const {
    const auto start = pointTween_.offsetFor(seg);
    const auto end = pointTween_.offsetFor(seg + 1);
    const float from = model_.getNode(seg).y;
    const float to = model_.getNode(seg + 1).y;
    const float shape = to == from ? progress : (model_.valueAt(seg, progress) - from) / (to - from);
    return {start.x * (1.0f - progress) + end.x * progress, start.y * (1.0f - shape) + end.y * shape};
}

juce::Path CurveEditorComponent::getDrawnCurvePathForTest() const {
    juce::Path curvePath, fillPath;
    buildCurvePaths(currentGeometry(), curvePath, fillPath);
    return curvePath;
}

void CurveEditorComponent::paintCurve(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                      const ThemeColours& colours) const {
    juce::Path curvePath, fillPath;
    buildCurvePaths(geometry, curvePath, fillPath);
    if (curvePath.isEmpty())
        return;
    const float fade = morphTween_.alphaFor(0); // Reduce Motion: a restored line lands and fades in
    g.setColour(colours.accent.withAlpha(0.20f * fade));
    g.fillPath(fillPath);
    g.setColour(colours.accent.withMultipliedAlpha(fade));
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));
}

// The model's line in pixels, sampled kCurveStepsPerSegment times per segment, with each point drawn where the node
// glide has it now.
std::vector<juce::Point<float>> CurveEditorComponent::modelCurvePoints(const CurveEditorGeometry& geometry) const {
    std::vector<juce::Point<float>> points;
    const int numSegments = model_.getNumSegments();
    for (int seg = 0; seg < numSegments; ++seg) {
        const juce::Point<float> segStart = geometry.nodePosition(seg);
        const juce::Point<float> segEnd = geometry.nodePosition(seg + 1);
        for (int step = 0; step <= kCurveStepsPerSegment; ++step) {
            if (step == 0 && seg > 0)
                continue; // shared with the previous segment's last point
            const float progress = (float)step / (float)kCurveStepsPerSegment;
            const auto drift = segmentTweenOffset(seg, progress);
            points.push_back({segStart.x + (segEnd.x - segStart.x) * progress + drift.x,
                              geometry.yForLevel(model_.valueAt(seg, progress)) + drift.y});
        }
    }
    return points;
}

// The fill follows the drawn line, so a morphing line takes its fill with it.
void CurveEditorComponent::buildCurvePaths(const CurveEditorGeometry& geometry, juce::Path& curvePath,
                                           juce::Path& fillPath) const {
    const auto points = drawnCurvePoints(geometry);
    if (points.empty())
        return;
    const float baselineY = geometry.yForLevel(fillBaselineLevel_);
    curvePath.startNewSubPath(points.front());
    fillPath.startNewSubPath(points.front().x, baselineY);
    fillPath.lineTo(points.front());
    for (size_t i = 1; i < points.size(); ++i) {
        curvePath.lineTo(points[i]);
        fillPath.lineTo(points[i]);
    }
    fillPath.lineTo(points.back().x, baselineY);
    fillPath.closeSubPath();
}

void CurveEditorComponent::paintHandles(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                        const ThemeColours& colours) const {
    for (int seg = 0; seg < model_.getNumSegments(); ++seg) {
        auto handle = geometry.bendHandlePosition(seg);
        if (!handle.has_value())
            continue;
        *handle += segmentTweenOffset(seg, 0.5f);
        const bool active = hoveredKind_ == CurveHitKind::BendHandle && hoveredIndex_ == seg;
        const float radius = active ? kBendHandleRadius + 1.5f : kBendHandleRadius;
        g.setColour(
            colours.accent.withAlpha((active ? 0.9f : 0.55f) * (1.0f - morphWeight()) * morphTween_.alphaFor(0)));
        g.drawEllipse(handle->x - radius, handle->y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    }

    for (int i = 0; i < model_.getNumNodes(); ++i) {
        const auto& node = model_.getNode(i);
        const juce::Point<float> pos = geometry.nodePosition(i) + pointTween_.offsetFor(i);
        const bool active = isSelected(i) || (hoveredKind_ == CurveHitKind::Node && hoveredIndex_ == i);
        const float radius = active ? kNodeRadius + 1.5f : kNodeRadius;

        if (active) {
            g.setColour(colours.accent.withAlpha(0.25f));
            g.fillEllipse(pos.x - radius - 4.0f, pos.y - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);
        }
        const float fade = pointTween_.alphaFor(i) * (1.0f - morphWeight()) * morphTween_.alphaFor(0);
        g.setColour(colours.background.withMultipliedAlpha(fade));
        g.fillEllipse(pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour((node.xMovable || node.yMovable ? colours.accent : colours.mutedText).withMultipliedAlpha(fade));
        g.drawEllipse(pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);
    }
}

void CurveEditorComponent::paintPlayheadMarker(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                               const ThemeColours& colours) const {
    if (!playhead_.has_value())
        return;
    const juce::Point<float> pos = geometry.playheadPosition(*playhead_);
    g.setColour(colours.mutedText.withAlpha(0.5f));
    g.drawVerticalLine((int)pos.x, 0.0f, (float)getHeight());
    g.setColour(juce::Colours::white);
    g.fillEllipse(pos.x - 4.0f, pos.y - 4.0f, 8.0f, 8.0f);
}

void CurveEditorComponent::paint(juce::Graphics& g) {
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    if (w <= 0.0f || h <= 0.0f)
        return;

    const ThemeColours colours = resolveThemeColours();
    g.fillAll(colours.background);

    const CurveEditorGeometry geometry = currentGeometry();
    if (backdrop_)
        backdrop_(g, geometry);
    paintGrid(g, geometry, colours);
    paintLineHighlight(g, geometry, colours);
    paintCurve(g, geometry, colours);
    paintHandles(g, geometry, colours);
    paintPlayheadMarker(g, geometry, colours);
    boxSelect_.paintMarquee(g);
    paintFocusRing(g, getLocalBounds().toFloat(), *this);
}

} // namespace synth::ui
