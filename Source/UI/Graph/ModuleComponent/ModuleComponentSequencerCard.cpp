// ModuleComponentSequencerCard.cpp -- the Sequencer and Poly Sequencer card's tie to its step grid view and its Key
// field: the grid edits the pattern itself, the card gives it the undo record and the height hooks, and the existing
// 15 Hz timer feeds it the playhead and the song key (no new Timer). docs/modules/sequencer-design.md#card.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "UI/ModuleViews/StepGrid/StepGridComponent.h"

namespace {

synth::ui::StepGridComponent* gridOf(synth::CardBody* body) {
    return body != nullptr ? dynamic_cast<synth::ui::StepGridComponent*>(body->findView(synth::CardView::StepGrid))
                           : nullptr;
}

synth::ui::SequencerKeyField* keyFieldOf(synth::CardBody* body) {
    return body != nullptr
               ? dynamic_cast<synth::ui::SequencerKeyField*>(body->findWidget(synth::sequencer_ids::kScaleRoot))
               : nullptr;
}

} // namespace

// A drag, key or menu pick in the grid is one undo step; a lane folding in or out re-measures the card every frame
// and asks the neighbours to make room once per direction.
void ModuleComponent::wireSequencerCard() {
    auto* grid = gridOf(cardBody_.get());
    if (grid == nullptr)
        return;
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    synth::ui::StepGridHooks hooks;
    hooks.recordEdit = [safeThis](const juce::var& before, const juce::var& after) {
        if (safeThis != nullptr && safeThis->undoManager != nullptr)
            safeThis->undoManager->recordNodeExtraStateChange(safeThis->owner.getAudioEngine().getGraph(),
                                                              safeThis->nodeId, before, after);
    };
    hooks.recordEditWithParameter = [safeThis](const juce::var& before, const juce::var& after,
                                               const juce::String& paramId, float oldValue, float newValue) {
        if (safeThis != nullptr && safeThis->undoManager != nullptr)
            safeThis->undoManager->recordNodeExtraStateAndParameterChange(safeThis->owner.getAudioEngine().getGraph(),
                                                                          safeThis->nodeId, before, after, paramId,
                                                                          oldValue, newValue);
    };
    hooks.layoutChanged = [safeThis] {
        if (safeThis != nullptr)
            safeThis->updateLayout();
    };
    hooks.makeRoom = [safeThis] {
        if (safeThis != nullptr)
            safeThis->owner.handleModuleResized(safeThis.getComponent());
    };
    grid->setHooks(std::move(hooks));
}

// The card body can be rebuilt under a new layout, which makes a new grid and key field: wire each again the first
// tick it exists. The key field's pick is one graph snapshot around all of its writes. The grid reads the song key
// from the same transport, so a key change repaints both on the same tick.
void ModuleComponent::pollSequencerCard() {
    auto& transport = owner.getAudioEngine().getTransport();
    if (auto* key = keyFieldOf(cardBody_.get())) {
        if (!key->hasRecordEdit()) {
            juce::Component::SafePointer<ModuleComponent> safeThis(this);
            key->setTransport(&transport);
            key->setRecordEdit([safeThis](const std::function<void()>& edit) {
                if (safeThis == nullptr || safeThis->undoManager == nullptr) {
                    edit();
                    return;
                }
                auto& graph = safeThis->owner.getAudioEngine().getGraph();
                safeThis->undoManager->captureBeforeState(graph);
                edit();
                safeThis->undoManager->pushSnapshotFromCapture(graph);
            });
        }
        key->refresh();
    }
    auto* grid = gridOf(cardBody_.get());
    if (grid == nullptr)
        return;
    if (!grid->hasHooks())
        wireSequencerCard();
    grid->setSongKey(transport.getDocumentState().songKey);
    grid->poll();
}
