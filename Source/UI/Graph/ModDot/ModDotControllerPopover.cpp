// ModDotControllerPopover.cpp -- the controller's side of the dot's panel: opening it under a dot, following the
// graph while it is open, and the two actions that belong to the app window (remove a source, show it in the
// timeline), with their hooks and fallbacks. docs/modules/modulation.md#the-mod-dot-menu.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ModDotController.h"
#include "ModDotPopover.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {
void launchInCallout(std::unique_ptr<juce::Component> content, juce::Component& anchor) {
    juce::CallOutBox::launchAsynchronously(std::move(content), anchor.getScreenBounds(), nullptr);
}

// The timeline names a routing by uuids (a node id does not survive an undo), so the source, the target and the
// hidden attenuverter each get one here, the same ensure-uuid step the timeline's own lookup takes.
ModulatorInfo modulatorInfoFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card, int destChannel,
                               const KnobModSource& source, const KnobModTarget& target) {
    auto& graph = editor.getAudioEngine().getGraph();
    ModulatorInfo info;
    auto uuidOf = [&graph](juce::AudioProcessorGraph::NodeID id) {
        auto* node = graph.getNodeForId(id);
        return node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
    };
    info.sourceUuid = uuidOf(source.sourceNodeId);
    info.sourceTitle = source.sourceName;
    info.sourceChannel = source.sourceChannel;
    if (auto* node = graph.getNodeForId(source.sourceNodeId))
        info.isLfo = dynamic_cast<LFOModule*>(node->getProcessor()) != nullptr;
    info.attenuverterUuid = uuidOf(source.attenuverterId);
    info.targetUuid = uuidOf(card);
    info.paramId = target.paramId;
    info.targetChannel = destChannel;
    info.colour = editor.modulationWireColour(source.sourceNodeId);
    return info;
}
} // namespace

ModDotController::~ModDotController() {
    if (auto* open = getPopover()) {
        open->orphan(); // its callout is deleted a turn later, by when this is gone
        open->dismiss();
    }
}

void ModDotController::openPopover(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor) {
    closePopover();
    if (!knobModTarget(editor_, card, destChannel).valid())
        return;
    auto content = std::make_unique<ModDotPopover>(editor_, *this, card, destChannel, anchor);
    popover_ = content.get();
    if (popoverLauncher)
        popoverLauncher(std::move(content), anchor);
    else
        launchInCallout(std::move(content), anchor);
}

ModDotPopover* ModDotController::getPopover() const { return static_cast<ModDotPopover*>(popover_.getComponent()); }

void ModDotController::closePopover() {
    if (auto* open = getPopover())
        open->dismiss();
    popover_ = nullptr;
}

void ModDotController::tickPopover() {
    if (auto* open = getPopover())
        open->syncFromGraph();
}

void ModDotController::popoverClosed(ModDotPopover* popover) {
    if (popover_.getComponent() == static_cast<juce::Component*>(popover))
        popover_ = nullptr;
}

void ModDotController::removeSource(juce::AudioProcessorGraph::NodeID card, int destChannel,
                                    juce::AudioProcessorGraph::NodeID attenuverterId) {
    const auto target = knobModTarget(editor_, card, destChannel);
    for (const auto& source : knobModSources(editor_, card, destChannel, true)) {
        if (source.attenuverterId != attenuverterId)
            continue;
        if (host.removeModulator && target.valid())
            host.removeModulator(modulatorInfoFor(editor_, card, destChannel, source, target));
        else
            editor_.removeModulationChain(attenuverterId);
        return;
    }
}

void ModDotController::revealSource(juce::AudioProcessorGraph::NodeID card, int destChannel,
                                    juce::AudioProcessorGraph::NodeID attenuverterId) {
    const auto target = knobModTarget(editor_, card, destChannel);
    if (!host.revealModulator || !target.valid())
        return;
    for (const auto& source : knobModSources(editor_, card, destChannel, true))
        if (source.attenuverterId == attenuverterId)
            host.revealModulator(modulatorInfoFor(editor_, card, destChannel, source, target));
}

} // namespace synth::ui
