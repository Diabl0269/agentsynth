// ModDotControllerPopover.cpp -- the controller's side of the dot's panel: opening it under a dot, following the
// graph while it is open, and the two actions that belong to the app window (remove a source, show it in the
// timeline), with their hooks and fallbacks. docs/modules/modulation.md#the-mod-dot-menu.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ModDotController.h"
#include "ModDotPanelFrame.h"
#include "ModDotPanelLaunch.h"
#include "ModDotPopover.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {
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
        open->stopPick();
        open->orphan(); // its window is deleted a turn later, by when this is gone
        open->dismiss();
    }
}

void ModDotController::openPopover(juce::AudioProcessorGraph::NodeID card, int destChannel, juce::Component& anchor) {
    // A dot clicked again while its panel is still fading out: the fade is cut short, the new panel takes the spot.
    if (auto* old = dynamic_cast<ModDotPanelFrame*>(frame_.getComponent()); old != nullptr && old->isClosing())
        old->finishClosingNow();
    frame_ = nullptr;
    closePopover();
    if (!knobModTarget(editor_, card, destChannel).valid())
        return;
    auto content = std::make_unique<ModDotPopover>(editor_, *this, card, destChannel, anchor);
    popover_ = content.get();
    if (popoverLauncher)
        popoverLauncher(std::move(content), anchor);
    else
        frame_ = launchPopoverFrame(std::move(content), anchor);
}

// The panel gets a window of its own beside the dot, held open while a canvas pick is under way.
ModDotPanelFrame* ModDotController::launchPopoverFrame(std::unique_ptr<juce::Component> content,
                                                       juce::Component& anchor) {
    auto* panel = dynamic_cast<ModDotPopover*>(content.get());
    ModDotPanelLaunch options;
    options.appProperties = host.appProperties;
    if (panel != nullptr) {
        options.setMaxHeight = [panel = juce::Component::SafePointer<ModDotPopover>(panel)](int height) {
            if (panel != nullptr)
                panel->setMaxHeight(height);
        };
        options.bindDismiss = [panel =
                                   juce::Component::SafePointer<ModDotPopover>(panel)](std::function<void()> close) {
            if (panel != nullptr)
                panel->onDismiss = std::move(close);
        };
        options.keepOpenOnOutsideClick = [panel = juce::Component::SafePointer<ModDotPopover>(panel)] {
            return panel != nullptr && panel->isPicking();
        };
    }
    return launchInFrame(std::move(content), anchor, anchor.getScreenBounds(), options);
}

ModDotPopover* ModDotController::getPopover() const { return static_cast<ModDotPopover*>(popover_.getComponent()); }

void ModDotController::endCanvasPick() {
    if (auto* open = getPopover())
        open->stopPick();
}

void ModDotController::closePopover() {
    if (auto* open = getPopover())
        open->dismiss();
    popover_ = nullptr;
}

void ModDotController::dotDoubleClicked(juce::AudioProcessorGraph::NodeID card, int destChannel,
                                        juce::Component& anchor) {
    const auto sources = knobModSources(editor_, card, destChannel, true);
    if (sources.empty())
        return;
    if (sources.size() == 1) {
        closePopover();
        editor_.removeModulationChain(sources.front().attenuverterId);
        return;
    }
    auto* open = getPopover();
    const bool reusable = open != nullptr && open->card() == card && open->destChannel() == destChannel;
    if (!reusable) {
        openPopover(card, destChannel, anchor);
        open = getPopover();
    }
    if (open != nullptr)
        open->setRemoveHighlighted(true);
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
