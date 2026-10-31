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
            openModCurvePopover(editor, atten, editor, screen, true);
            break;
        }
    return true;
}

} // namespace synth::ui::cable_curve_chips
