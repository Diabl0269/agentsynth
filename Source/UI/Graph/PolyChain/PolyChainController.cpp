// Concern: the Poly pill's click -> plan -> (question) -> one undo step -> feedback flow, and the join-on-first-cable
// rule. The graph change itself is synth::applyPolyVoiceGraph (Mixer/ChannelFlows/PolyVoiceGraph.cpp).
#include "PolyChainController.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/NewModulePlacement/NewModulePlacement.h"
#include <algorithm>

namespace synth::ui {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

bool isPolyMidiNode(juce::AudioProcessor* processor) {
    auto* module = dynamic_cast<ModuleBase*>(processor);
    return module != nullptr && module->getModuleType() == ModuleType::PolyMidi;
}
} // namespace

PolyChainController::PolyChainController(GraphEditor& editor)
    : editor_(editor) {}

PolyChainController::~PolyChainController() { *alive_ = false; }

// The click. The plan is made now only to know whether to ask; the change re-plans nothing -- it applies the node set
// the user chose, and a node that vanished while the question was open is simply skipped by applyPolyVoiceGraph.
void PolyChainController::polyPillClicked(NodeID clicked, bool poly) {
    auto& graph = editor_.getAudioEngine().getGraph();
    const auto tracks = trackProvider ? trackProvider() : std::vector<synth::PolyTrackRef>{};
    auto plan = synth::planPolyVoiceGraph(graph, clicked, tracks);
    if (plan.thisTrackNodes.empty())
        plan.thisTrackNodes.push_back(clicked);

    if (plan.otherTrackNodes.empty()) {
        commit(plan.thisTrackNodes, poly);
        return;
    }

    std::weak_ptr<bool> alive = alive_;
    ask(plan, [this, alive, plan, poly](PolyChainChoice choice) {
        if (alive.expired() || choice == PolyChainChoice::cancel)
            return;
        auto nodes = plan.thisTrackNodes;
        if (choice == PolyChainChoice::includeConnectedTracks)
            nodes.insert(nodes.end(), plan.otherTrackNodes.begin(), plan.otherTrackNodes.end());
        commit(nodes, poly);
    });
}

// "This track only" is the first button, so Return picks it; the last button is Escape and answers 0, the same
// 1 / 2 / 0 mapping LookAndFeel_V2::createAlertWindow gives any three-button MessageBoxOptions.
void PolyChainController::ask(const synth::PolyVoiceGraphPlan& plan, std::function<void(PolyChainChoice)> answer) {
    PolyChainPrompt prompt;
    prompt.otherModuleCount = (int)plan.otherTrackNodes.size();
    prompt.trackNames = synth::describeTrackNames(plan.otherTrackNames);
    prompt.message = "This also changes " + juce::String(prompt.otherModuleCount) +
                     (prompt.otherModuleCount == 1 ? " module on " : " modules on ") + prompt.trackNames + ".";
    if (confirmForTest) {
        confirmForTest(prompt, std::move(answer));
        return;
    }
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Switch connected tracks too?")
                       .withMessage(prompt.message)
                       .withButton("This track only")
                       .withButton("Include connected tracks")
                       .withButton("Cancel")
                       .withAssociatedComponent(&editor_);
    juce::AlertWindow::showAsync(options, [answer = std::move(answer)](int result) {
        answer(result == 1   ? PolyChainChoice::thisTrackOnly
               : result == 2 ? PolyChainChoice::includeConnectedTracks
                             : PolyChainChoice::cancel);
    });
}

// ONE undo step for everything: the parameter flips (each card re-anchors its own cables as it hears them), the
// inserted or removed Poly MIDI node with its cables, and any macro membership. The pill's own attachment opens no
// gesture because the pill does not toggle itself (CardBody::installPolyPill), so no second or half step exists.
void PolyChainController::commit(const std::vector<NodeID>& nodes, bool poly) {
    auto& graph = editor_.getAudioEngine().getGraph();
    int changed = 0;
    auto change = [&] {
        synth::PolyVoiceGraphOptions options;
        options.macros = &editor_.getMacros();
        options.leavePolyMidiUnplaced = true;
        options.removeNodes = [this](const std::vector<NodeID>& ids) {
            editor_.removeNodesNow(ids, /*healChain=*/false, /*narrowDetach=*/false);
        };
        const auto result = synth::applyPolyVoiceGraph(graph, nodes, poly, options);
        changed = (int)result.flipped.size();
        // Placed from the model (the inserted node has no card yet), then its card is built where it landed.
        synth::placeNewModulesBesideConnections(editor_, result.addedPolyMidi);
        editor_.updateComponents();
    };

    auto* undo = editor_.getUndoManager();
    if (undo != nullptr)
        undo->recordGraphAndMacroChange(graph, editor_.getMacros(), change);
    else
        change();
    announce(changed, poly, undo != nullptr ? undo->getEditSerial() : 0);
}

// Only a change that reached beyond the clicked module is worth a message: the pill itself already shows the click.
// The action undoes the step only while it is still the last thing done.
void PolyChainController::announce(int changed, bool poly, int editSerialAfter) {
    if (changed <= 1 || !showToast)
        return;
    std::weak_ptr<bool> alive = alive_;
    showToast(
        "Made " + juce::String(changed) + (poly ? " modules poly" : " modules mono"), "Undo",
        [this, alive, editSerialAfter] {
            if (alive.expired())
                return;
            if (auto* undo = editor_.getUndoManager(); undo != nullptr && undo->getEditSerial() == editSerialAfter)
                undo->undo();
        },
        "Undo this change");
}

bool PolyChainController::hasCables(NodeID node) const {
    for (const auto& c : editor_.getAudioEngine().getGraph().getConnections())
        if (c.source.nodeID == node || c.destination.nodeID == node)
            return true;
    return false;
}

// Decided from the state BEFORE the cable exists, for both ends at once, so the first cable of two fresh modules
// cannot make them chase each other. Only a module with no cables at all joins; one that already has any is left alone.
void PolyChainController::joinConnectedModules(NodeID source, NodeID destination) {
    if (suppressJoin_ > 0 || source == destination)
        return;
    auto& graph = editor_.getAudioEngine().getGraph();
    auto* sourceProcessor =
        graph.getNodeForId(source) != nullptr ? graph.getNodeForId(source)->getProcessor() : nullptr;
    auto* destProcessor =
        graph.getNodeForId(destination) != nullptr ? graph.getNodeForId(destination)->getProcessor() : nullptr;
    if (sourceProcessor == nullptr || destProcessor == nullptr)
        return;

    auto pushesPoly = [](juce::AudioProcessor* p) { return synth::isProcessorPoly(p) || isPolyMidiNode(p); };
    auto joinable = [&](juce::AudioProcessor* p, NodeID id) {
        return synth::hasPolyParameter(p) && !synth::isProcessorPoly(p) && !hasCables(id);
    };
    const bool sourceJoins = joinable(sourceProcessor, source) && pushesPoly(destProcessor);
    const bool destJoins = joinable(destProcessor, destination) && pushesPoly(sourceProcessor);
    if (sourceJoins)
        synth::setProcessorPoly(sourceProcessor, true);
    if (destJoins)
        synth::setProcessorPoly(destProcessor, true);
}

} // namespace synth::ui
