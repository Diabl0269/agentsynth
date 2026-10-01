// MainComponentAutomationLanes.cpp -- creating an automation lane on a chosen track, and listing what a
// track's "Add automation..." picker offers. The ownership rule (which track plays which module) is in
// MainComponentAutomationOwner.cpp; the knob's "Automate" and the lane choices reach addLaneUndoable() from
// MainComponentTimeline.cpp and MainComponentTrackHeaderHost.cpp.
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Mixer/ChannelFlows/ChannelFlows.h" // isMacroPortNode
#include "Modules/ChannelStripModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/AutomationBinding.h"

namespace {

// Modules that are plumbing, not something a person automates: the Mod Matrix's attenuverters, macro port
// nodes, and the timeline's own Track In / Track Audio sources.
bool isHiddenHelperModule(const ModuleBase& module) {
    const auto type = module.getModuleType();
    return type == ModuleType::Attenuverter || type == ModuleType::TimelineMidiSource ||
           type == ModuleType::TimelineAudioSource || synth::isMacroPortNode(&module);
}

// "send2Level" / "send3Pan": a Channel Strip send slot's parameters. They are offered through the send-slot
// options instead, which label them by target and leave inactive slots out.
bool isSendSlotParameter(const juce::String& paramId) {
    return paramId.startsWith("send") && (paramId.endsWith("Level") || paramId.endsWith("Pan"));
}

// The parameters a knob on the module's card automates: float and int ones. A hosted plugin's own are
// offered through the plugin options (they have no RangedAudioParameter), and its wrapper's are not
// something a person sees.
void appendModuleParameters(const juce::AudioProcessorGraph::Node& node, ModuleBase& module, const juce::String& title,
                            const synth::TimelineDoc& doc,
                            std::vector<synth::ui::TrackHeaderHost::AutomatableParameter>& out) {
    if (dynamic_cast<synth::HostedPluginModule*>(&module) != nullptr)
        return;
    const bool isStrip = dynamic_cast<ChannelStripModule*>(&module) != nullptr;
    const juce::String uuid = node.properties["uuid"].toString();
    for (auto* param : module.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        const bool automatable = dynamic_cast<juce::AudioParameterFloat*>(param) != nullptr ||
                                 dynamic_cast<juce::AudioParameterInt*>(param) != nullptr;
        if (ranged == nullptr || !automatable || (isStrip && isSendSlotParameter(ranged->paramID)))
            continue;
        if (uuid.isNotEmpty() && doc.getLaneForParam(uuid, ranged->paramID) != nullptr)
            continue; // already automated
        synth::ui::TrackHeaderHost::AutomatableParameter entry;
        entry.nodeUid = node.nodeID.uid;
        entry.nodeUuid = uuid;
        entry.paramId = ranged->paramID;
        entry.moduleTitle = title;
        entry.parameterName = ranged->getName(64);
        out.push_back(std::move(entry));
    }
}

} // namespace

// The one place a lane is bound. The track is picked INSIDE the mutation (when the caller named none), so a
// track created for an unowned module and its lane are one undo step, not two. addLane dedupes doc-wide: a
// repeat for an already-automated parameter mutates nothing and returns the existing lane's id.
synth::LaneId MainComponent::addLaneUndoable(const juce::String& uuid, const juce::String& paramId, int paramIndex,
                                             const synth::AutomationLane::RangeSnapshot& range,
                                             std::optional<synth::TrackId> target) {
    synth::LaneId laneId;
    undoManager.recordTimelineChange(timelineDoc, [&] {
        const synth::TrackId trackId = target.has_value() ? *target : trackForNewLane(uuid);
        if (!trackId.isValid())
            return; // kMaxTracks reached -- nothing to bind onto
        laneId = timelineDoc.addLane(trackId, uuid, paramId, range, paramIndex);
    });
    return laneId;
}

// A parameter on a live node, own module or hosted plugin alike: resolveLaneParameter branches internally on
// which (a hosted one is normalised 0..1 with an index-hint rescue), and laneValueBoundsFor /
// laneDefaultValueFor read the range off whichever resolved.
synth::LaneId MainComponent::addLaneForOption(const synth::ui::TrackHeaderHost::PluginLaneOption& option,
                                              std::optional<synth::TrackId> target) {
    if (option.nodeUuid.isEmpty() || option.paramId.isEmpty())
        return {};
    auto* node = findNodeByUuid(option.nodeUuid);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return {}; // the node disappeared between offering and choosing

    const auto resolved = synth::resolveLaneParameter(processor, option.paramId, option.paramIndex);
    if (!resolved.resolved())
        return {}; // the parameter vanished between offering and choosing

    synth::AutomationLane::RangeSnapshot range;
    const auto bounds = synth::laneValueBoundsFor(resolved);
    range.minValue = static_cast<float>(bounds.minValue);
    range.maxValue = static_cast<float>(bounds.maxValue);
    range.defaultValue = static_cast<float>(synth::laneDefaultValueFor(resolved));
    return addLaneUndoable(option.nodeUuid, option.paramId, option.paramIndex, range, target);
}

// What the "Add automation..." picker lists for `track`: every automatable parameter of the modules it
// plays, or -- for the Automation track -- of the modules no single track plays. Modules come in graph order
// with a module's parameters adjacent, so the picker's headers read module by module.
std::vector<synth::ui::TrackHeaderHost::AutomatableParameter>
MainComponent::getAutomatableParameters(synth::TrackId track) {
    std::vector<synth::ui::TrackHeaderHost::AutomatableParameter> result;
    const auto* trackInfo = timelineDoc.getTrack(track);
    if (trackInfo == nullptr)
        return result;

    const auto owners = resolveAutomationOwners();
    const bool unassigned = trackInfo->kind == synth::TrackKind::Automation;
    const auto belongs = [&](const juce::String& key) {
        const auto owner = owners.find(key);
        return unassigned ? owner == owners.end() : (owner != owners.end() && owner->second == track);
    };

    for (auto* node : audioEngine.getGraph().getNodes()) {
        auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module == nullptr || isHiddenHelperModule(*module) || !belongs(detail::ownershipKey(*node)))
            continue;
        appendModuleParameters(*node, *module, synth::moduleTitle(*node), timelineDoc, result);
    }

    // Hosted-plugin instance parameters and Channel Strip send slots: the existing "Add lane..." sources,
    // already free of parameters that have a lane. Their nodes always carry a uuid, which is the ownership key.
    for (const auto& option : getAvailablePluginLaneOptions()) {
        if (!belongs(option.nodeUuid))
            continue;
        synth::ui::TrackHeaderHost::AutomatableParameter entry;
        if (auto* node = findNodeByUuid(option.nodeUuid))
            entry.nodeUid = node->nodeID.uid;
        entry.nodeUuid = option.nodeUuid;
        entry.paramId = option.paramId;
        entry.paramIndex = option.paramIndex;
        entry.moduleTitle = option.moduleTitle;
        entry.parameterName = option.parameterName;
        result.push_back(std::move(entry));
    }
    return result;
}

// Creates the lane on `track` (the one the person asked about, not the ownership rule's pick). A node that has
// never been automated has no uuid yet; it gets one here, mirrored into the processor like every uuid write
// (ModuleBase::setNodeUuid), the same ensure-uuid step automateParameter() takes.
synth::LaneId MainComponent::addAutomationLane(synth::TrackId track,
                                               const synth::ui::TrackHeaderHost::AutomatableParameter& parameter) {
    if (timelineDoc.getTrack(track) == nullptr)
        return {};
    auto* node = parameter.nodeUid != 0
                     ? audioEngine.getGraph().getNodeForId(juce::AudioProcessorGraph::NodeID(parameter.nodeUid))
                     : findNodeByUuid(parameter.nodeUuid);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return {};

    juce::String uuid = node->properties["uuid"].toString();
    if (uuid.isEmpty()) {
        uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        module->setNodeUuid(uuid);
    }

    synth::ui::TrackHeaderHost::PluginLaneOption option;
    option.nodeUuid = uuid;
    option.paramId = parameter.paramId;
    option.paramIndex = parameter.paramIndex;
    return addLaneForOption(option, track);
}
