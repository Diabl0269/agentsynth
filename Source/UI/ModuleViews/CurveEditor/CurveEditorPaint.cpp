#include "CurveEditorComponent.h"

namespace synth::ui {

namespace {
constexpr float kNodeRadius = 5.0f;
constexpr float kBendHandleRadius = 4.0f;
constexpr int kCurveStepsPerSegment = 24;
} // namespace

CurveEditorComponent::ThemeColours CurveEditorComponent::resolveThemeColours() const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr) {
        const auto& colors = lf->getTheme().colors;
        return {colors.bg1, colors.border.withAlpha(0.4f), colors.textMuted, colors.accent};
    }
    return {juce::Colour(0xff1a1a2e), juce::Colour(0xff2a2a3e), juce::Colour(0xff6a6a7e), juce::Colour(0xff00b4d8)};
}

void CurveEditorComponent::paintGrid(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                     const ThemeColours& colours) const {
    const float h = (float)getHeight();
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

void CurveEditorComponent::paintCurve(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                      const ThemeColours& colours) const {
    const int numSegments = model_.getNumSegments();
    if (numSegments <= 0)
        return;

    const float baselineY = geometry.yForLevel(0.0f);
    juce::Path curvePath;
    juce::Path fillPath;
    bool first = true;

    for (int seg = 0; seg < numSegments; ++seg) {
        const juce::Point<float> segStart = geometry.nodePosition(seg);
        const juce::Point<float> segEnd = geometry.nodePosition(seg + 1);
        for (int step = 0; step <= kCurveStepsPerSegment; ++step) {
            if (step == 0 && seg > 0)
                continue; // shared with the previous segment's last point
            const float progress = (float)step / (float)kCurveStepsPerSegment;
            const float x = segStart.x + (segEnd.x - segStart.x) * progress;
            const float y = geometry.yForLevel(model_.valueAt(seg, progress));
            if (first) {
                curvePath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, baselineY);
                fillPath.lineTo(x, y);
                first = false;
            } else {
                curvePath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }
    }
    fillPath.lineTo(geometry.nodePosition(numSegments).x, baselineY);
    fillPath.closeSubPath();

    g.setColour(colours.accent.withAlpha(0.20f));
    g.fillPath(fillPath);
    g.setColour(colours.accent);
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));
}

void CurveEditorComponent::paintHandles(juce::Graphics& g, const CurveEditorGeometry& geometry,
                                        const ThemeColours& colours) const {
    for (int seg = 0; seg < model_.getNumSegments(); ++seg) {
        const auto handle = geometry.bendHandlePosition(seg);
        if (!handle.has_value())
            continue;
        const bool active = hoveredKind_ == CurveHitKind::BendHandle && hoveredIndex_ == seg;
        const float radius = active ? kBendHandleRadius + 1.5f : kBendHandleRadius;
        g.setColour(colours.accent.withAlpha(active ? 0.9f : 0.55f));
        g.drawEllipse(handle->x - radius, handle->y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    }

    for (int i = 0; i < model_.getNumNodes(); ++i) {
        const auto& node = model_.getNode(i);
        const juce::Point<float> pos = geometry.nodePosition(i);
        const bool active = (i == selectedIndex_) || (hoveredKind_ == CurveHitKind::Node && hoveredIndex_ == i);
        const float radius = active ? kNodeRadius + 1.5f : kNodeRadius;

        if (active) {
            g.setColour(colours.accent.withAlpha(0.25f));
            g.fillEllipse(pos.x - radius - 4.0f, pos.y - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);
        }
        g.setColour(colours.background);
        g.fillEllipse(pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(node.xMovable || node.yMovable ? colours.accent : colours.mutedText);
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
    paintGrid(g, geometry, colours);
    paintCurve(g, geometry, colours);
    paintHandles(g, geometry, colours);
    paintPlayheadMarker(g, geometry, colours);
}

} // namespace synth::ui
