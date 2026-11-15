#include "GraphEditorCableCurveChips.h"

#include "GraphEditor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/ModCurve/ModCurveChip.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui::cable_curve_chips {

namespace {
constexpr float kGapFromKnob = 16.0f; // the knob's grab radius is 15
} // namespace

juce::Rectangle<float> chipRect(const graph_editor_types::VisibleCable& cable) {
    const auto mid = (cable.p1 + cable.p2) / 2.0f;
    return {mid.x + kGapFromKnob, mid.y - (float)ModCurveChip::kHeight * 0.5f, (float)ModCurveChip::kWidth,
            (float)ModCurveChip::kHeight};
}

void paint(juce::Graphics& g, GraphEditor& editor, const graph_editor_types::VisibleCable& cable) {
    if (!editor.showCurvesOnCables || cable.kind != graph_editor_types::VisibleCable::Kind::AttenuverterChain)
        return;
    ModCurveEditTarget target(editor, juce::AudioProcessorGraph::NodeID{cable.id.attenUid});
    const auto curve = target.read().value_or(synth::ModCurve::linear());
    const auto& colors = synth::theme::themeOf(editor).colors;
    const auto rect = chipRect(cable);
    const bool bent = !curve.isLinear();
    g.setColour(colors.surface);
    g.fillRoundedRectangle(rect, 4.0f);
    g.setColour(bent ? colors.accent : colors.border);
    g.drawRoundedRectangle(rect.reduced(0.5f), 4.0f, bent ? 1.5f : 1.0f);
    paintModCurveGlyph(g, rect.reduced(5.0f, 4.0f), curve, bent ? colors.accent : colors.textMuted);
}

juce::AudioProcessorGraph::NodeID chipAt(GraphEditor& editor, juce::Point<float> canvasPos) {
    if (!editor.showCurvesOnCables)
        return {};
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.kind == graph_editor_types::VisibleCable::Kind::AttenuverterChain &&
            chipRect(cable).contains(canvasPos))
            return juce::AudioProcessorGraph::NodeID{cable.id.attenUid};
    return {};
}

bool press(GraphEditor& editor, juce::Component& canvas, juce::Point<float> canvasPos) {
    const auto atten = chipAt(editor, canvasPos);
    if (atten.uid == 0)
        return false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.attenUid == atten.uid) {
            const auto screen = canvas.localAreaToGlobal(chipRect(cable).toNearestInt());
            toggleModCurvePopover(editor, atten, editor, screen, true);
            break;
        }
    return true;
}

juce::Rectangle<float> polyBadgeRect(const graph_editor_types::VisibleCable& cable, float width,
                                     const std::vector<juce::Rectangle<int>>& cards) {
    constexpr float kHeight = 16.0f, kBeside = 5.0f, kStep = 0.03f;
    const auto mid = (cable.p1 + cable.p2) / 2.0f;
    const juce::Rectangle<float> atMid((float)((int)mid.x + (int)kBeside), mid.y - kHeight * 0.5f, width, kHeight);
    auto covered = [&](const juce::Rectangle<float>& r) {
        for (const auto& card : cards)
            if (r.intersects(card.toFloat()))
                return true;
        return false;
    };
    if (!covered(atMid))
        return atMid;
    const auto wire = GraphEditor::buildCablePath(cable.p1, cable.p2);
    const float length = wire.getLength();
    for (int k = 1; k * kStep < 0.5f; ++k)
        for (const float t : {0.5f + (float)k * kStep, 0.5f - (float)k * kStep}) {
            const auto at = wire.getPointAlongPath(length * t);
            const juce::Rectangle<float> r(at.x + kBeside, at.y - kHeight * 0.5f, width, kHeight);
            if (!covered(r))
                return r;
        }
    return atMid;
}

} // namespace synth::ui::cable_curve_chips
