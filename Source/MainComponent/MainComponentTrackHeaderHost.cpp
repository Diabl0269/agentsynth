// MainComponentTrackHeaderHost.cpp — the synth::ui::TrackHeaderHost callback surface:
// available Track-In binding candidates, plugin automation-lane options, MIDI destination
// options, and note audition. This is the query/binding half of track-header support; track and
// channel *creation* lives in MainComponentTrackCreation.cpp. MainComponent is declared in
// MainComponent.h; the rest of its implementation lives in the sibling MainComponent*.cpp units
// next to this one.
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Modules/TimelineMidiSourceModule.h" // auditionTrackNote pushes into the bound Track In node
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/AutomationBinding.h"
#include <map>
#include <set>

namespace {

// Human-readable identity for a graph node — the binding chip's base label and the re-bind menu's
// starting point. Plain processor name only: appending "#id" unconditionally was itself the source
// of founder confusion (a chip reading "Track In #16" reads like a module name, not a binding), and
// the id means nothing outside a menu actually showing two same-named candidates at once.
// getAvailableTrackInNodes (below) is where that disambiguation happens, over the option list it is
// building — never here, and never on the chip.
juce::String describeNodeForBinding(juce::AudioProcessorGraph::Node* node) {
    if (node == nullptr || node->getProcessor() == nullptr)
        return {};
    return node->getProcessor()->getName();
}

} // namespace

// ---- TrackHeaderHost ----

std::vector<synth::ui::TrackHeaderHost::BindingOption>
MainComponent::getAvailableTrackInNodes(synth::TrackId forTrack) {
    std::vector<BindingOption> options;
    // A Track In node feeds exactly one track, so anything another track already claims is off the
    // menu. This track's own current binding stays on it (ticked), so the menu always shows where
    // it points today.
    std::set<juce::String> claimedByOtherTracks;
    for (const auto& track : timelineDoc.getTracks())
        if (!(track.id == forTrack) && track.bindingUuid.isNotEmpty())
            claimedByOtherTracks.insert(track.bindingUuid);

    // Which NODE TYPE can feed this track depends on the track's kind — a MIDI track wants a
    // Track In, an audio track wants a Track Audio. Offering the wrong one would let a user bind a
    // track to a node that structurally cannot play it (the modules match on kind as well as uuid,
    // so the result would be a track that silently plays nothing).
    const auto* track = timelineDoc.getTrack(forTrack);
    const ModuleType wantedType = (track != nullptr && track->kind == synth::TrackKind::Audio)
                                      ? ModuleType::TimelineAudioSource
                                      : ModuleType::TimelineMidiSource;

    std::vector<juce::AudioProcessorGraph::Node*> candidates;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || module->getModuleType() != wantedType)
            continue;

        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty() || claimedByOtherTracks.count(uuid) > 0)
            continue;

        candidates.push_back(node);
    }

    // "#id" is disambiguation, not identity: an option earns the suffix only when some OTHER
    // candidate in this same menu carries the same plain name. Two passes because no candidate
    // knows it needs one until the whole list is known.
    std::map<juce::String, int> nameOccurrences;
    for (auto* node : candidates)
        ++nameOccurrences[describeNodeForBinding(node)];

    for (auto* node : candidates) {
        const juce::String uuid = node->properties["uuid"].toString();
        const juce::String name = describeNodeForBinding(node);
        const juce::String display =
            nameOccurrences[name] > 1 ? name + " #" + juce::String((int)node->nodeID.uid) : name;
        options.push_back({uuid, display});
    }
    return options;
}

juce::String MainComponent::getNodeDisplayName(const juce::String& uuid) {
    return describeNodeForBinding(findNodeByUuid(uuid));
}

void MainComponent::bindTrackTo(synth::TrackId track, const juce::String& uuid) {
    if (uuid.isEmpty())
        return;
    // A user gesture, so it goes on the undo stack (unlike reconciliation, which derives runtime
    // state and is deliberately not undoable).
    undoManager.recordTimelineChange(timelineDoc, [this, track, uuid] { timelineDoc.setTrackBinding(track, uuid); });
    // Derive the orphan flag against the live graph immediately, so the chip stops reading
    // "Missing" the moment the user picks a node rather than at the next graph change.
    reconcileTimelineAfterGraphChange();
}

void MainComponent::createAndBindTrackInNode(synth::TrackId track) {
    // Kind-aware for the same reason getAvailableTrackInNodes() is — the chip's "new node"
    // entry must create the node type the track can actually be fed by.
    const auto* existing = timelineDoc.getTrack(track);
    const bool wantsAudio = existing != nullptr && existing->kind == synth::TrackKind::Audio;

    undoManager.recordCombinedChange(audioEngine.getGraph(), timelineDoc, [this, track, wantsAudio] {
        const juce::String uuid = wantsAudio ? createTrackAudioNode() : createTrackInNode();
        if (uuid.isNotEmpty())
            timelineDoc.setTrackBinding(track, uuid);
    });
    reconcileTimelineAfterGraphChange();
}

void MainComponent::selectNodeInGraph(const juce::String& uuid) {
    if (auto* node = findNodeByUuid(uuid))
        graphEditor.selectModule(node->nodeID, /*additive=*/false);
}

// The automation strip lane picker's "Add lane..." entries — the minimal creation surface
// for a hosted plugin's own parameters, which have no ModuleComponent knob to right-click (the
// plugin has its own editor; see docs/modules/modulation.md#hosted-plugin-parameters-as-automation-lanes's Hosted Plugin table). Every live
// HostedPluginModule with a published instance offers every parameter that doesn't already have a
// lane; a bare or still-loading one offers nothing, same as it renders nothing elsewhere in the UI.
std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> MainComponent::getAvailablePluginLaneOptions() const {
    std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> options;
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor());
        if (hosted == nullptr || !hosted->hasInstance())
            continue;

        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isEmpty())
            continue; // ensure-uuid runs at automate time, same as automateParameter() — nothing to offer yet

        const juce::String moduleLabel = describeNodeForBinding(node);
        for (const auto& param : hosted->getInstanceParameters()) {
            if (timelineDoc.getLaneForParam(uuid, param.paramId) != nullptr)
                continue; // already automated
            synth::ui::TrackHeaderHost::PluginLaneOption option;
            option.nodeUuid = uuid;
            option.paramId = param.paramId;
            option.paramIndex = param.index;
            option.label = moduleLabel + juce::String::fromUTF8(" \xC2\xB7 ") + param.displayName;
            options.push_back(std::move(option));
        }
    }
    return options;
}

synth::LaneId MainComponent::addPluginAutomationLane(const synth::ui::TrackHeaderHost::PluginLaneOption& option) {
    if (option.nodeUuid.isEmpty() || option.paramId.isEmpty())
        return {};

    auto* node = findNodeByUuid(option.nodeUuid);
    auto* hosted = node != nullptr ? dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) : nullptr;
    if (hosted == nullptr)
        return {}; // the node disappeared (or stopped being a plugin) between offering and choosing

    // Hosted-plugin parameters are always normalised (a hosted AudioProcessorParameter has no
    // NormalisableRange, and JUCE's own host contract is 0..1 regardless of format) — the lane's
    // RangeSnapshot IS {0, 1, default}, never something read off a live NormalisableRange.
    const auto resolved = synth::resolveLaneParameter(hosted, option.paramId, option.paramIndex);
    if (!resolved.resolved())
        return {}; // the parameter vanished between offering and choosing

    synth::LaneId laneId;
    const juce::String uuidCopy = option.nodeUuid;
    const juce::String paramIdCopy = option.paramId;
    const int paramIndexCopy = option.paramIndex;
    auto mutate = [this, &laneId, uuidCopy, paramIdCopy, paramIndexCopy, &resolved] {
        synth::TrackId trackId;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.kind == synth::TrackKind::Automation) {
                trackId = track.id;
                break;
            }
        }
        if (!trackId.isValid())
            trackId = timelineDoc.addTrack(synth::TrackKind::Automation, "Automation");
        if (!trackId.isValid())
            return;

        synth::AutomationLane::RangeSnapshot range;
        const auto bounds = synth::laneValueBoundsFor(resolved);
        range.minValue = static_cast<float>(bounds.minValue);
        range.maxValue = static_cast<float>(bounds.maxValue);
        range.defaultValue = static_cast<float>(synth::laneDefaultValueFor(resolved));
        laneId = timelineDoc.addLane(trackId, uuidCopy, paramIdCopy, range, paramIndexCopy);
    };
    undoManager.recordTimelineChange(timelineDoc, mutate);
    if (!laneId.isValid())
        return {};

    if (!isTimelineVisible && toggleTimelineButton.onClick)
        toggleTimelineButton.onClick();
    timelinePanel.showAutomationLane(laneId);
    return laneId;
}

// The MIDI-destinations picker's row list — every node in the live graph that actually CONSUMES
// MIDI in its processBlock (ModuleBase::acceptsMidi(), corrected per module by an audit of every
// module's processBlock — see Tests/Modules/ModuleMidiFlagsTests.cpp for the full table — so this is no
// longer a hardcoded allowlist, and no module can advertise a MIDI jack that silently did
// nothing). MIDI SOURCES are still deliberately excluded: a pure source
// (Track In / External MIDI / MIDI Keyboard) has acceptsMidi()==false by construction — see
// isMidiInstrumentType's own comment for why a source must never be offered as a destination — so
// checking acceptsMidi() alone already leaves them out without a separate "is it a source" test.
// Disambiguated exactly like getAvailableTrackInNodes(): "#id" appears only when some other
// candidate shares its plain display name.
std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption>
MainComponent::getMidiDestinationOptions(synth::TrackId forTrack) {
    std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption> options;
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return options; // unbound/orphaned: nothing resolvable to wire FROM

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return options; // the bound node is gone — reconcile marks this orphaned elsewhere

    auto& graph = audioEngine.getGraph();
    std::vector<juce::AudioProcessorGraph::Node*> candidates;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr || !module->acceptsMidi())
            continue;
        candidates.push_back(node);
    }

    std::map<juce::String, int> nameOccurrences;
    for (auto* node : candidates)
        ++nameOccurrences[describeNodeForBinding(node)];

    for (auto* node : candidates) {
        const juce::String name = describeNodeForBinding(node);
        const juce::String display =
            nameOccurrences[name] > 1 ? name + " #" + juce::String((int)node->nodeID.uid) : name;
        const bool connected = graph.isConnected({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                                  {node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
        options.push_back({display, node->nodeID.uid, connected, detail::isMidiInstrumentNode(node->getProcessor())});
    }
    return options;
}

void MainComponent::setMidiDestinationConnected(synth::TrackId forTrack, juce::uint32 nodeUid, bool connect) {
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return; // stale popup: the track lost its binding since the list was built — no-op, never crash

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return; // stale popup: the bound node is gone

    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::Node* targetNode = nullptr;
    for (auto* node : graph.getNodes()) {
        if (node != nullptr && node->nodeID.uid == nodeUid) {
            targetNode = node;
            break;
        }
    }
    if (targetNode == nullptr)
        return; // stale popup: the target node no longer resolves

    const juce::AudioProcessorGraph::Connection connection{
        {trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
        {targetNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}};

    undoManager.recordStructuralChange(graph, [&graph, connection, connect] {
        if (connect)
            graph.addConnection(connection);
        else
            graph.removeConnection(connection);
    });
    graphEditor.updateComponents();
    reconcileTimelineAfterGraphChange();
}

void MainComponent::auditionTrackNote(synth::TrackId forTrack, int pitch, int velocity, bool noteOn) {
    // Deliberately the SAME resolution the two functions above use — track -> bindingUuid -> live
    // node — because that node's MIDI output is where the track's destinations are wired FROM. Going
    // anywhere else (AudioEngine's own midiMessageCollector, say) would reach the global MIDI-in
    // path instead of this track's instruments, so the preview would play the wrong thing or nothing.
    const auto* track = timelineDoc.getTrack(forTrack);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return; // an unbound track plays nowhere, so a preview on it is silence

    auto* trackInNode = findNodeByUuid(track->bindingUuid);
    if (trackInNode == nullptr)
        return; // orphaned binding — the chip already says so; a preview must not crash on it

    auto* source = dynamic_cast<TimelineMidiSourceModule*>(trackInNode->getProcessor());
    if (source == nullptr)
        return; // the uuid resolves to something that is not a Track In node

    // NO structural change, NO undo step and NO doc mutation: a preview is not an edit. The push is
    // wait-free and a full FIFO simply drops the event (see pushAuditionNote) — nothing here may
    // block, because this is the mouse-down that is still unwinding.
    source->pushAuditionNote(pitch, velocity, noteOn);
}
