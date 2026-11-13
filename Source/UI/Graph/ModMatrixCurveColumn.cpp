#include "ModMatrixCurveColumn.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"

namespace synth::ui::mod_matrix_curve {

void wireChip(ModCurveChip& chip, GraphEditor* editor, juce::AudioProcessorGraph::NodeID attenuverter) {
    if (editor == nullptr) {
        chip.setVisible(false);
        return;
    }
    wireModCurveChip(chip, *editor, attenuverter, {});
}

void layoutChip(ModCurveChip& chip, juce::Rectangle<int>& area) {
    const auto column = area.removeFromRight(kColumnWidth);
    chip.setBounds(column.withSizeKeepingCentre(ModCurveChip::kWidth, ModCurveChip::kHeight));
}

void nameChip(ModCurveChip& chip, int index) { chip.setTitle("Routing " + juce::String(index + 1) + " curve"); }

void drawHeader(juce::Graphics& g, juce::Rectangle<float> headerCell) {
    g.drawText("Curve", headerCell, juce::Justification::centred, true);
}

} // namespace synth::ui::mod_matrix_curve
