// CurveEditorGridLines.cpp -- where the grid's lines are and what snaps to them (even cells, a fixed step or an
// explicit list), and the grid's reading aids: the guides switch and the line the pointer (or the dragged point) is
// on, lit in the accent with its name so the exact line is easy to find.
#include "CurveEditorComponent.h"

namespace synth::ui {

namespace {
constexpr float kHighlightThickness = 2.0f;
constexpr float kLabelHeight = 16.0f;
constexpr float kLabelPad = 6.0f;
} // namespace

bool CurveEditorComponent::gridHasX() const {
    return grid_.has_value() && (!grid_->xLines.empty() || grid_->xStep > 0.0 || grid_->xDivisions > 0);
}

bool CurveEditorComponent::gridHasY() const { return grid_.has_value() && grid_->yDivisions > 0; }

std::vector<double> CurveEditorComponent::gridXPositions() const {
    std::vector<double> out;
    if (!gridHasX())
        return out;
    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    if (!grid_->xLines.empty()) {
        for (const auto& line : grid_->xLines)
            if (line.x >= minX - 1e-9 && line.x <= maxX + 1e-9)
                out.push_back(line.x);
        return out;
    }
    if (grid_->xStep > 0.0) {
        const double first = std::ceil((minX - grid_->xOrigin) / grid_->xStep - 1e-9);
        for (double k = first; grid_->xOrigin + k * grid_->xStep <= maxX + 1e-9; k += 1.0)
            out.push_back(grid_->xOrigin + k * grid_->xStep);
        return out;
    }
    const int xDiv = grid_->xDivisions;
    for (int i = 0; i <= xDiv; ++i)
        out.push_back(minX + (maxX - minX) * ((double)i / (double)xDiv));
    return out;
}

// Explicit lines: the nearest one that snaps (an out-of-key line never does); none in range leaves x alone.
double CurveEditorComponent::snapGridX(double x) const {
    if (!gridHasX() || !grid_->snapX)
        return x;
    const double minX = model_.getMinX();
    const double maxX = model_.getMaxX();
    if (!grid_->xLines.empty()) {
        std::optional<double> best;
        for (const auto& line : grid_->xLines) {
            if (line.tier == GridLine::Tier::outOfKey || line.x < minX - 1e-9 || line.x > maxX + 1e-9)
                continue;
            if (!best.has_value() || std::abs(line.x - x) < std::abs(*best - x))
                best = line.x;
        }
        return best.value_or(x);
    }
    if (grid_->xStep > 0.0) {
        const double origin = grid_->xOrigin;
        return juce::jlimit(minX, maxX, origin + std::round((x - origin) / grid_->xStep) * grid_->xStep);
    }
    if (maxX <= minX)
        return x;
    const double xDiv = (double)grid_->xDivisions;
    const double frac = juce::jlimit(0.0, 1.0, (x - minX) / (maxX - minX));
    return minX + (std::round(frac * xDiv) / xDiv) * (maxX - minX);
}

std::optional<double> CurveEditorComponent::explicitLineStep(double x, int side) const {
    if (!grid_.has_value() || grid_->xLines.empty() || !grid_->snapX || side == 0)
        return std::nullopt;
    std::optional<double> best;
    for (const auto& line : grid_->xLines) {
        if (line.tier == GridLine::Tier::outOfKey || line.x < model_.getMinX() || line.x > model_.getMaxX())
            continue;
        const double distance = (line.x - x) * (double)side;
        if (distance > 1e-9 && (!best.has_value() || distance < *best))
            best = distance;
    }
    return best;
}

void CurveEditorComponent::setGuidesVisible(bool visible) {
    if (guidesVisible_ == visible)
        return;
    guidesVisible_ = visible;
    repaint();
}

/** Every named line the user can point at: the snap grid's (while it is drawn) and the guides' (while shown). The
    nearest one within kHoverLinePx wins; a vertical and a horizontal tie goes to whichever is closer. */
std::optional<CurveEditorComponent::LineHit> CurveEditorComponent::lineNear(const CurveEditorGeometry& geometry,
                                                                            juce::Point<float> point) const {
    std::optional<LineHit> best;
    float bestDistance = kHoverLinePx;
    const auto consider = [&](bool vertical, float pixel, const juce::String& name) {
        if (name.isEmpty())
            return;
        const float distance = std::abs(pixel - (vertical ? point.x : point.y));
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = LineHit{vertical, pixel, name};
        }
    };
    const bool gridDrawn = grid_.has_value() && (snapToGrid_ || !guides_.has_value());
    if (gridDrawn) {
        if (!grid_->xLines.empty()) {
            for (const auto& line : grid_->xLines)
                if (line.x >= model_.getMinX() && line.x <= model_.getMaxX())
                    consider(true, std::floor(geometry.xForTime(line.x)),
                             line.name.isNotEmpty() ? line.name
                             : grid_->xName         ? grid_->xName(line.x)
                                                    : juce::String());
        } else if (grid_->xName) {
            for (const double t : gridXPositions())
                consider(true, std::floor(geometry.xForTime(t)), grid_->xName(t));
        }
        if (grid_->yName && gridHasY()) {
            const int yDiv = grid_->yDivisions;
            for (int i = 0; i <= yDiv; ++i) {
                const float level = (float)i / (float)yDiv;
                consider(false, std::floor(geometry.yForLevel(level)), grid_->yName(level));
            }
        }
    }
    if (guides_.has_value() && guidesVisible_) {
        for (const auto& guide : guides_->x)
            consider(true, std::floor(geometry.xForTime(guide.position)), guide.label);
        for (const auto& guide : guides_->y)
            consider(false, std::floor(geometry.yForLevel((float)guide.position)),
                     grid_ && grid_->yName ? grid_->yName((float)guide.position) : guide.label);
    }
    return best;
}

/** While a point is dragged the line it snapped to; otherwise the line under the pointer. */
std::optional<CurveEditorComponent::LineHit>
CurveEditorComponent::highlightedLine(const CurveEditorGeometry& geometry) const {
    if (dragKind_ == DragKind::Node && dragIndex_ >= 0 && dragIndex_ < model_.getNumNodes())
        return lineNear(geometry, geometry.nodePosition(dragIndex_));
    if (pointer_.has_value())
        return lineNear(geometry, *pointer_);
    return std::nullopt;
}

juce::String CurveEditorComponent::getHighlightedLineName() const {
    const auto hit = highlightedLine(currentGeometry());
    return hit.has_value() ? hit->name : juce::String();
}

void CurveEditorComponent::paintLineHighlight(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                              const ThemeColours& colours) const {
    const auto hit = highlightedLine(geometry);
    if (!hit.has_value())
        return;
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    g.setColour(colours.accent);
    const float start = hit->pixel - (kHighlightThickness - 1.0f) * 0.5f;
    if (hit->vertical)
        g.fillRect(start, 0.0f, kHighlightThickness, h);
    else
        g.fillRect(0.0f, start, w, kHighlightThickness);

    const juce::Font font(juce::FontOptions(10.0f));
    g.setFont(font);
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, hit->name);
    const float boxWidth = textWidth + 2.0f * kLabelPad;
    const float x = juce::jlimit(0.0f, juce::jmax(0.0f, w - boxWidth), hit->vertical ? hit->pixel + 4.0f : 4.0f);
    const float y =
        juce::jlimit(0.0f, juce::jmax(0.0f, h - kLabelHeight), hit->vertical ? 2.0f : hit->pixel - kLabelHeight - 2.0f);
    const juce::Rectangle<float> box(x, y, boxWidth, kLabelHeight);
    g.setColour(colours.background.withAlpha(0.92f));
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(colours.accent);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    g.drawText(hit->name, box, juce::Justification::centred, false);
}

} // namespace synth::ui
