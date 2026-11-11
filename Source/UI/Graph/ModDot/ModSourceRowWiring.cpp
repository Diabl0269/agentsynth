// ModSourceRowWiring.cpp -- what a modulation row's controls do to the graph, the same on every surface.

#include "ModSourceRowWiring.h"

#include "AudioEngine/AudioEngine.h"
#include "KnobModSources.h"
#include "ModDotController.h"
#include "ModDotSourceRow.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"

namespace synth::ui {

void applyModAmount(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverterId, float amount) {
    const float current = attenuverterAmount(editor.getAudioEngine().getGraph(), attenuverterId, amount);
    editor.adjustModAmount(attenuverterId, amount - current);
}

// A drag is one undo step (begin on press, commit on release); a typed value is one step on its own. "Show in
// timeline" goes through the mod dot's controller, the one reveal every surface uses.
void wireModSourceRow(ModDotSourceRow& row, GraphEditor& editor, juce::AudioProcessorGraph::NodeID card,
                      int destChannel, std::function<void()> afterTypedAmount) {
    wireModCurveChip(row.curveChip(), editor, row.attenuverterId(), row.source().spokenName());
    row.onGestureBegin = [&editor](ModDotSourceRow&) { editor.beginModAmountGesture(); };
    row.onAmountDragged = [&editor](ModDotSourceRow& r, float amount) {
        applyModAmount(editor, r.attenuverterId(), amount);
    };
    row.onGestureEnd = [&editor](ModDotSourceRow&) { editor.commitModAmountGesture(); };
    row.onAmountTyped = [&editor, after = std::move(afterTypedAmount)](ModDotSourceRow& r, float amount) {
        editor.beginModAmountGesture();
        applyModAmount(editor, r.attenuverterId(), amount);
        editor.commitModAmountGesture();
        if (after)
            after();
    };
    row.onShowInTimeline = [&editor, card, destChannel](ModDotSourceRow& r) {
        editor.getModDot().revealSource(card, destChannel, r.attenuverterId());
    };
}

} // namespace synth::ui
