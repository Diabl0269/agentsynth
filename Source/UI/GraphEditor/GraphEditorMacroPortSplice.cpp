// GraphEditorMacroPortSplice.cpp
//
// Macro bypass/mute state (bypassFanOutMembers and friends), the macro port crossing-plan math
// (buildMacroPortCrossingPlan and its add/remove-member variants, spliceMacroPorts/
// spliceOutMacroPort), and auto-create-port-on-drag / auto-delete-on-last-cable. GraphEditor is
// declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of
// the class.

#include "GraphEditor.h"

#include "../../AI/AIStateMapper.h"
#include "../../Modules/AttenuverterModule.h"
#include "../../Modules/MacroInletModule.h"
#include "../../Modules/MacroOutletModule.h"
#include "../MacroCardComponent.h"
#include "../ModuleComponent/ModuleComponent.h"

void GraphEditor::deleteMacroAndMembers(const juce::String& macroId) {
    auto* m = macros.find(macroId);
    if (m == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    // Reuses the single delete-selection path exactly, rather than a parallel "delete a macro"
    // mutation: selecting every member and calling deleteSelection() gets the same undo/dirty/
    // timeline-reconcile handling deleting any other multi-selection gets, and
    // updateComponents() dissolves the now-empty macro as part of that same step.
    setSelectedNodes(memberIds);
    deleteSelection();
}

// ---- Macro bypass/mute (P8-15d, T142, docs/macros_ports.md §5.6) -------------------------------------

std::vector<juce::AudioProcessorGraph::NodeID>
GraphEditor::resolvedMacroMemberModuleNodes(const juce::String& macroId) const {
    std::vector<juce::AudioProcessorGraph::NodeID> result;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return result;

    auto& graph = audioEngine.getGraph();
    for (const auto& uuid : macro->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;
        auto* node = graph.getNodeForId(nodeId);
        if (node != nullptr && dynamic_cast<ModuleBase*>(node->getProcessor()) != nullptr)
            result.push_back(nodeId);
    }
    return result;
}

bool GraphEditor::macroHasMuteEligibleMember(const juce::String& macroId) const {
    auto& graph = audioEngine.getGraph();
    for (auto nodeId : resolvedMacroMemberModuleNodes(macroId)) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (mb != nullptr && mb->hasMuteParameter())
            return true;
    }
    return false;
}

namespace {
// The members a macro's Bypass fan-out touches. For an ordinary macro that is every member. For a
// CHANNEL macro — one containing a Channel Strip — the source node(s) and the strip itself are
// skipped (docs/mixer.md §5.5): "bypass" on a channel means "bypass the inserts", so the chain's
// effects go dry while the source keeps producing and the strip keeps passing signal. Mute has no
// such carve-out — muting a channel macro mutes the strip too.
std::vector<juce::AudioProcessorGraph::NodeID>
bypassFanOutMembers(juce::AudioProcessorGraph& graph, std::vector<juce::AudioProcessorGraph::NodeID> members) {
    auto typeOf = [&graph](juce::AudioProcessorGraph::NodeID nodeId) -> std::optional<ModuleType> {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        return mb != nullptr ? std::optional<ModuleType>(mb->getModuleType()) : std::nullopt;
    };
    const bool isChannelMacro = std::any_of(members.begin(), members.end(),
                                            [&](auto nodeId) { return typeOf(nodeId) == ModuleType::ChannelStrip; });
    if (!isChannelMacro)
        return members;
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](auto nodeId) {
                                     const auto type = typeOf(nodeId);
                                     return type == ModuleType::ChannelStrip ||
                                            type == ModuleType::TimelineMidiSource ||
                                            type == ModuleType::TimelineAudioSource;
                                 }),
                  members.end());
    return members;
}
} // namespace

GraphEditor::MacroToggleState GraphEditor::macroBypassState(const juce::String& macroId) const {
    auto& graph = audioEngine.getGraph();
    bool anyOn = false;
    bool anyOff = false;
    // Reports over exactly the members the fan-out below toggles, so a channel macro whose inserts
    // are all bypassed reads AllOn even though its strip and source never are.
    for (auto nodeId : bypassFanOutMembers(graph, resolvedMacroMemberModuleNodes(macroId))) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (mb == nullptr)
            continue;
        (mb->isBypassed() ? anyOn : anyOff) = true;
    }
    if (anyOn && anyOff)
        return MacroToggleState::Mixed;
    return anyOn ? MacroToggleState::AllOn : MacroToggleState::AllOff;
}

GraphEditor::MacroToggleState GraphEditor::macroMuteState(const juce::String& macroId) const {
    auto& graph = audioEngine.getGraph();
    bool anyOn = false;
    bool anyOff = false;
    for (auto nodeId : resolvedMacroMemberModuleNodes(macroId)) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        // Skip members with no "muted" parameter (Macro In/Out and their MIDI variants among
        // them, §7 item 1's note) — they have nothing to report either way.
        if (mb == nullptr || !mb->hasMuteParameter())
            continue;
        (mb->isMuted() ? anyOn : anyOff) = true;
    }
    if (anyOn && anyOff)
        return MacroToggleState::Mixed;
    return anyOn ? MacroToggleState::AllOn : MacroToggleState::AllOff;
}

void GraphEditor::setMacroBypassed(const juce::String& macroId, bool bypassed) {
    // A channel macro skips its source and strip — see bypassFanOutMembers.
    const auto memberNodes = bypassFanOutMembers(audioEngine.getGraph(), resolvedMacroMemberModuleNodes(macroId));
    if (memberNodes.empty())
        return;

    // The fan-out is an ORDINARY parameter change (ModuleBase::setBypassed is already
    // setValueNotifyingHost under the hood, docs/macros_ports.md §5.6) batched into ONE undo step via
    // the same before/after graph-JSON snapshot applySmartSuggestions uses to land several
    // connections as one step — never a new mutation mechanism, and never a macro-level
    // reinterpretation of what bypass means. Each member's own processBlock keeps honouring the
    // two-branch bypass/mute contract exactly as it does for a per-module toggle.
    auto& graph = audioEngine.getGraph();
    auto doBypass = [this, memberNodes, bypassed] {
        auto& g = audioEngine.getGraph();
        for (auto nodeId : memberNodes) {
            auto* node = g.getNodeForId(nodeId);
            if (auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr)
                mb->setBypassed(bypassed);
        }
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, doBypass);
    else
        doBypass();

    // A collapsed macro's own card is not one of the members whose parameterValueChanged listener
    // would otherwise schedule this repaint (its members are hidden ModuleComponents, and
    // MacroCardComponent listens to no parameters) -- it reads macroBypassState() fresh on every
    // paint, so it has to be told a repaint is due. repaintCanvas() (not a bare repaint()) matches
    // every other macro-scoped mutation in this file (e.g. finalizeMacroCardDrag).
    repaintCanvas();
}

void GraphEditor::setMacroMuted(const juce::String& macroId, bool muted) {
    if (!macroHasMuteEligibleMember(macroId))
        return;

    const auto memberNodes = resolvedMacroMemberModuleNodes(macroId);
    auto& graph = audioEngine.getGraph();
    auto doMute = [this, memberNodes, muted] {
        auto& g = audioEngine.getGraph();
        for (auto nodeId : memberNodes) {
            auto* node = g.getNodeForId(nodeId);
            auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
            // A member with no "muted" parameter (ModuleBase::hasMuteParameter()) is left alone
            // rather than calling setMuted, which dereferences an unset mutedParam unconditionally
            // — the pre-existing gap §7 item 1 flagged this fan-out would need to guard against.
            if (mb != nullptr && mb->hasMuteParameter())
                mb->setMuted(muted);
        }
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, doMute);
    else
        doMute();

    // See setMacroBypassed's matching comment: the collapsed card reads macroMuteState() fresh on
    // every paint and has no parameter listener of its own to trigger that repaint on its own.
    repaintCanvas();
}

void GraphEditor::toggleMacroBypassed(const juce::String& macroId) {
    if (macros.find(macroId) == nullptr) {
        if (onStatusMessage)
            onStatusMessage("Select a macro to bypass or enable it.");
        return;
    }

    // Converge toward bypassing everything first (Mixed or AllOff -> bypass all; AllOn -> clear
    // all) — the same direction toggleSelectionMacrosCollapsed converges a mixed selection
    // toward collapsed, for the same reason: it is the direction a user reaching for this command
    // almost always wants, and it makes the command settle rather than oscillate once every
    // member agrees.
    setMacroBypassed(macroId, macroBypassState(macroId) != MacroToggleState::AllOn);
}

void GraphEditor::toggleMacroMuted(const juce::String& macroId) {
    if (!macroHasMuteEligibleMember(macroId)) {
        if (onStatusMessage)
            onStatusMessage("This macro has no member that can be muted.");
        return;
    }

    setMacroMuted(macroId, macroMuteState(macroId) != MacroToggleState::AllOn);
}

void GraphEditor::beginMacroCardDrag(const juce::String& macroId) {
    selectMacro(macroId, false);
    beginSelectionDrag();
}

void GraphEditor::dragMacroCardBy(const juce::String&, juce::Point<int> delta) {
    dragSelectionBy(delta, nullptr);
    // Matches ModuleComponent::mouseDrag's own per-frame repaint call exactly (one repaint per
    // drag tick), but goes through repaintCanvas() rather than a bare Component::repaint(): once
    // rebuildVisibleCables() anchors a collapsed macro's boundary cables on the LIVE
    // MacroCardComponent bounds (macroCableAnchorBounds), a bare repaint() would just re-paint
    // whatever cable geometry is already cached rather than recomputing it against the card's new
    // position. MacroCardComponent::mouseDrag deliberately does NOT also call
    // getParentComponent()->repaint() — this is the one repaint call for the gesture.
    repaintCanvas();
}

void GraphEditor::finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft) {
    auto& graph = audioEngine.getGraph();
    auto doFinalize = [this, macroId, newCardTopLeft] {
        finalizeSelectionDrag();
        if (auto* m = macros.find(macroId))
            m->bounds.setPosition(synth::LayoutUtil::snap(newCardTopLeft));
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doFinalize);
    else
        doFinalize();

    repaintCanvas();
}

void GraphEditor::cancelMacroCardDrag(const juce::String&) { cancelSelectionDrag(); }

// ---- Macro I/O (P8-15b, T140): the "Configure I/O" modal + the cable-drop convenience ---------

juce::String GraphEditor::macroPortNodeTypeName(bool isInput, synth::MacroPortKind kind) {
    if (kind == synth::MacroPortKind::Midi)
        return isInput ? "Macro MIDI In" : "Macro MIDI Out";
    return isInput ? "Macro In" : "Macro Out";
}

juce::String GraphEditor::defaultMacroPortName(bool isInput, synth::MacroPortKind kind) {
    if (kind == synth::MacroPortKind::Midi)
        return isInput ? "MIDI In" : "MIDI Out";
    return isInput ? "Input" : "Output";
}

int GraphEditor::nextMacroPortOrder(const synth::Macro& macro, bool isInput) {
    int next = 0;
    for (const auto& p : macro.ports)
        if (p.isInput == isInput)
            next = std::max(next, p.order + 1);
    return next;
}

// ---- Auto-create-ports-on-group (founder-review fix F5, docs/macros_implementation.md §7 item 6.1) -------------

std::vector<GraphEditor::MacroPortCrossingGroup>
GraphEditor::buildMacroPortCrossingPlan(const std::vector<juce::AudioProcessorGraph::NodeID>& memberNodeIds) const {
    std::vector<MacroPortCrossingGroup> groups;
    auto& graph = audioEngine.getGraph();

    std::set<uint32_t> memberUids;
    for (const auto& id : memberNodeIds)
        if (id.uid != 0)
            memberUids.insert(id.uid);
    // No `size() < 2` floor: groupSelectionIntoMacro() already refuses a selection of fewer than
    // two modules before it ever gets here (its own, earlier check), but the incremental add/
    // remove callers (buildMacroPortCrossingPlanForNewMembers/ForRemovedMembers, T138) legitimately
    // need a crossing plan for a one-member "inside" set — e.g. removing one of a macro's two
    // ordinary members leaves exactly one remaining member whose newly-external cable still needs
    // a port. The loop below is correct for any size, including 0 or 1: a connection with both
    // ends outside `memberUids` is skipped either way.
    auto isMember = [&](juce::AudioProcessorGraph::NodeID id) { return memberUids.count(id.uid) != 0; };

    for (const auto& c : graph.getConnections()) {
        const bool srcInside = isMember(c.source.nodeID);
        const bool dstInside = isMember(c.destination.nodeID);
        if (srcInside == dstInside)
            continue; // both inside (nothing to splice) or both outside (not this macro's concern)

        const bool isMidi = c.source.isMIDI() || c.destination.isMIDI();
        const auto internalId = srcInside ? c.source.nodeID : c.destination.nodeID;
        const auto externalId = srcInside ? c.destination.nodeID : c.source.nodeID;
        const int internalRaw = srcInside ? c.source.channelIndex : c.destination.channelIndex;
        const int externalRaw = srcInside ? c.destination.channelIndex : c.source.channelIndex;
        // Signal LEAVING the macro (internal end is the connection's source) -> an outlet
        // (isInput=false); signal ENTERING it (internal end is the destination) -> an inlet.
        const bool portIsInput = !srcInside;

        // A crossing connection whose EXTERNAL endpoint is an AttenuverterModule needs one more
        // check before it can be treated like any other crossing (founder review: "mod
        // connections don't get routed - they should"). AudioEngine::addModRouting always wraps a
        // single-slot CV routing as source -> attenuverter(ch0) -> destination, and the
        // attenuverter itself can NEVER be a macro member — it never gets a ModuleComponent
        // (GraphEditor::updateComponents() skips it outright), so it can never be part of a canvas
        // selection. So when this crossing's external node is an attenuverter, the mod chain's
        // OTHER endpoint — the attenuverter's own other ch0 connection — decides what is actually
        // happening:
        //
        //  - That far endpoint is ALSO about to become a member (the user selected both the real
        //    mod source and its real target together, e.g. an ADSR and the VCA it drives). The
        //    hidden attenuverter sitting nominally "outside" is then just an artefact of its own
        //    invisibility, not a real boundary crossing — splicing here would spawn TWO spurious
        //    ports (an outlet off the source, an inlet onto the target) for a routing the user is
        //    grouping wholly inside the macro. Left un-ported, both edges, exactly like any other
        //    fully-internal connection (MacroAutoPortTests.cpp's
        //    ModRoutingWithBothRealEndpointsInsideStaysWhollyInternal pins this).
        //  - The far endpoint is genuinely external (or the chain is only half-wired) — this IS a
        //    real crossing. Splicing a port here does NOT desync AudioEngine's DirectCV/
        //    AttenuverterChain classification: `getModulationRoutings()`'s AttenuverterChain pass
        //    is keyed purely on the ATTENUVERTER's own node identity (it walks every
        //    AttenuverterModule node and reads whichever connections currently sit on its ch0 in/
        //    out), never on what is wired to the other end. Retargeting the attenuverter's own
        //    edge onto the new port — with the attenuverter itself standing in as the "external"
        //    node for the splice below — keeps the knob in the mod matrix (`getActiveModRoutings`
        //    still reports it, now with the port as the reported source/dest, matching how any
        //    other boundary-crossing cable reports the port it passes through) and keeps the
        //    modulation signal flowing, since the port is a pure pass-through.
        auto* externalNode = graph.getNodeForId(externalId);
        if (externalNode != nullptr && dynamic_cast<AttenuverterModule*>(externalNode->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::NodeID farNode;
            bool foundFar = false;
            // AudioEngine::addModRouting always uses channel 0 on both of the attenuverter's ch0
            // in/out edges (mirrors AudioEngine::getModulationRoutings' own hardcoded "channel 0"
            // convention when it walks an attenuverter's connections) — the far side of THIS
            // connection's leg is the attenuverter's other ch0 edge. This holds even for a
            // mod-of-mod chain (one attenuverter's Amount CV, ch1, driven by another): the ONLY
            // things that ever wire directly into an attenuverter are AudioEngine::addModRouting
            // and ModMatrixComponent::ModRow::comboBoxChanged, and both always source that wire
            // from ANOTHER attenuverter's ch0 output — a genuinely selectable (member-eligible)
            // module can therefore never land on an attenuverter's ch1 directly, so the crossing
            // edge examined here is always on ch0 on the attenuverter side too.
            for (const auto& oc : graph.getConnections()) {
                if (portIsInput ? (oc.destination.nodeID == externalId && oc.destination.channelIndex == 0)
                                : (oc.source.nodeID == externalId && oc.source.channelIndex == 0)) {
                    farNode = portIsInput ? oc.source.nodeID : oc.destination.nodeID;
                    foundFar = true;
                    break;
                }
            }
            if (foundFar && isMember(farNode))
                continue; // both real endpoints of this mod chain are members; nothing crosses
            // else: fall through and splice exactly like any other crossing, treating the
            // attenuverter itself as the external node.
        }

        auto* internalNode = graph.getNodeForId(internalId);
        if (internalNode == nullptr)
            continue;
        auto* internalMb = dynamic_cast<ModuleBase*>(internalNode->getProcessor());

        int visibleJack = -1;
        int headRaw = internalRaw;
        MacroPortShape thisShape = MacroPortShape::Mono;
        int thisSpan = 1;
        if (!isMidi) {
            if (internalMb == nullptr)
                continue; // defensive: a non-ModuleBase node (graph I/O) can't be a macro member
            const auto p =
                portIsInput ? internalMb->mapInputChannel(internalRaw) : internalMb->mapOutputChannel(internalRaw);
            visibleJack = p.visibleJackIndex;

            // The jack's real span/role lives on its HEAD raw channel, not necessarily this one —
            // a follower channel (e.g. the second leg of a collapsed stereo jack) reports
            // polyVoiceSpan == 1 on itself. Same technique rebuildVisibleCables()'s
            // channelExposedOnJack uses to read a jack's full shape.
            const int totalCh = portIsInput ? internalNode->getProcessor()->getTotalNumInputChannels()
                                            : internalNode->getProcessor()->getTotalNumOutputChannels();
            PortRole headRole = p.role;
            int headSpan = 1;
            for (int raw = 0; raw < totalCh; ++raw) {
                const auto hp = portIsInput ? internalMb->mapInputChannel(raw) : internalMb->mapOutputChannel(raw);
                if (hp.visibleJackIndex == visibleJack && hp.isPolyGroupHead) {
                    headRaw = raw;
                    headSpan = std::max(1, hp.polyVoiceSpan);
                    headRole = hp.role;
                    break;
                }
            }
            thisSpan = headSpan;
            // headSpan > 1 with Audio role is a COLLAPSED stereo jack (an FX module's single
            // "Audio" jack fanning both raw legs, docs/macros_ports.md §5.3 / docs/macros_implementation.md §7 item 7)
            // — the port must present the SAME one visible jack the module does (MacroPortShape::StereoCollapsed),
            // never the two-jack MacroPortShape::Stereo a hand-picked Configure I/O choice means.
            // A Dual-I/O-ON module's separately-jacked Left/Right pair never reaches this branch:
            // each leg is its own visible jack with headSpan == 1 here, so it starts life as two
            // Mono groups and only becomes Stereo in the merge pass below.
            thisShape = (headSpan > 1 && headRole == PortRole::Audio) ? MacroPortShape::StereoCollapsed
                        : (headSpan > 1)                              ? MacroPortShape::Poly
                                                                      : MacroPortShape::Mono;
        }

        MacroPortCrossingGroup* group = nullptr;
        for (auto& g : groups) {
            if (g.internalNodeId == internalId && g.isInput == portIsInput && g.isMidi == isMidi &&
                (isMidi || g.visibleJack == visibleJack)) {
                group = &g;
                break;
            }
        }
        if (group == nullptr) {
            MacroPortCrossingGroup newGroup;
            newGroup.internalNodeId = internalId;
            newGroup.internalUuid = nodeUuidFor(internalId);
            newGroup.isInput = portIsInput;
            newGroup.isMidi = isMidi;
            newGroup.visibleJack = visibleJack;
            newGroup.headRawChannel = headRaw;
            newGroup.shape = thisShape;
            newGroup.voiceCount = thisSpan;
            groups.push_back(newGroup);
            group = &groups.back();
        }

        MacroPortCrossingEdge edge;
        edge.externalNodeId = externalId;
        edge.externalRawChannel = externalRaw;
        edge.internalRawChannel = internalRaw;
        edge.legIndex = isMidi ? 0 : (internalRaw - group->headRawChannel);
        group->edges.push_back(edge);
    }

    // Merge a Dual-I/O-on module's separately-jacked Left/Right crossings into one Stereo group.
    // A collapsed jack's own two-raw-channel stereo pair is already ONE group above (grouped by
    // visible jack, span read off the head channel); this handles the OTHER stereo shape — Dual
    // I/O on puts Left and Right on two DIFFERENT visible jacks, so if a crossing connection
    // reaches both, two Mono groups would otherwise silently collapse a stereo signal into two
    // independent mono cables. Pairs legs via ModuleBase::rightAudioLegChannel(), never jack index
    // 0/1 (Source/Modules/CLAUDE.md) — correct for both the adjacent (FX) and split-block (voice
    // module) layouts.
    bool mergedAny = true;
    while (mergedAny) {
        mergedAny = false;
        for (size_t i = 0; i < groups.size() && !mergedAny; ++i) {
            auto& left = groups[i];
            if (left.isMidi || left.shape != MacroPortShape::Mono || left.headRawChannel != 0)
                continue;
            auto* internalNode = graph.getNodeForId(left.internalNodeId);
            auto* internalMb =
                internalNode != nullptr ? dynamic_cast<ModuleBase*>(internalNode->getProcessor()) : nullptr;
            if (internalMb == nullptr || !internalMb->hasDualIOParameter() || !internalMb->isDualIO())
                continue;
            const int rightRaw = internalMb->rightAudioLegChannel();
            if (rightRaw < 0)
                continue;

            for (size_t j = 0; j < groups.size(); ++j) {
                if (j == i)
                    continue;
                auto& right = groups[j];
                if (right.isMidi || right.shape != MacroPortShape::Mono ||
                    right.internalNodeId != left.internalNodeId || right.isInput != left.isInput ||
                    right.headRawChannel != rightRaw)
                    continue;

                for (auto edge : right.edges) {
                    edge.legIndex = 1;
                    left.edges.push_back(edge);
                }
                left.shape = MacroPortShape::Stereo;
                left.voiceCount = 1;
                groups.erase(groups.begin() + (long)j);
                mergedAny = true;
                break;
            }
        }
    }

    return groups;
}

std::vector<GraphEditor::MacroPortCrossingGroup>
GraphEditor::buildMacroPortCrossingPlan(const std::vector<juce::String>& memberUuids) const {
    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    ids.reserve(memberUuids.size());
    for (const auto& uuid : memberUuids) {
        const auto id = resolveMemberNodeId(uuid);
        if (id.uid != 0)
            ids.push_back(id);
    }
    return buildMacroPortCrossingPlan(ids);
}

void GraphEditor::spliceMacroPorts(const juce::String& macroId, const std::vector<MacroPortCrossingGroup>& plan) {
    auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;
    auto& graph = audioEngine.getGraph();

    // The right leg of a Stereo macro port node's own raw layout (docs/macros_ports.md §5.3's
    // implementation note) — identical on MacroInletModule and MacroOutletModule.
    constexpr int kMacroPortRightBase = MacroInletModule::kRightBase;
    static_assert(MacroOutletModule::kRightBase == kMacroPortRightBase,
                  "MacroInletModule/MacroOutletModule must agree on the Stereo right-leg raw channel");

    for (const auto& group : plan) {
        // 1. Disconnect every original crossing edge in this group, external<->internal.
        for (const auto& edge : group.edges) {
            const int externalChannel =
                group.isMidi ? juce::AudioProcessorGraph::midiChannelIndex : edge.externalRawChannel;
            const int internalChannel =
                group.isMidi ? juce::AudioProcessorGraph::midiChannelIndex : edge.internalRawChannel;
            const juce::AudioProcessorGraph::Connection c =
                group.isInput ? juce::AudioProcessorGraph::Connection{{edge.externalNodeId, externalChannel},
                                                                      {group.internalNodeId, internalChannel}}
                              : juce::AudioProcessorGraph::Connection{{group.internalNodeId, internalChannel},
                                                                      {edge.externalNodeId, externalChannel}};
            graph.removeConnection(c);
        }

        // 2. Construct the port node with the derived shape/kind, named from the internal module +
        //    jack it fronts, BEFORE it is wired into the live graph (§5.3's construction-time rule).
        const auto kind = group.isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
        const juce::String typeName = macroPortNodeTypeName(group.isInput, kind);
        auto newProcessor = synth::AIStateMapper::createModule(typeName);
        if (!newProcessor)
            continue; // defensive: leave this group's cables disconnected rather than crash

        if (!group.isMidi) {
            if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
                inlet->setPortShape(group.shape, group.voiceCount);
            else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
                outlet->setPortShape(group.shape, group.voiceCount);
        }

        auto* internalNodeForName = graph.getNodeForId(group.internalNodeId);
        auto* internalMbForName =
            internalNodeForName != nullptr ? dynamic_cast<ModuleBase*>(internalNodeForName->getProcessor()) : nullptr;
        const juce::String portName =
            autoMacroPortName(internalMbForName, group.isInput, group.visibleJack, group.isMidi);

        auto node = graph.addNode(std::move(newProcessor));
        if (!node)
            continue;
        node->properties.set("x", macro->bounds.getX());
        node->properties.set("y", macro->bounds.getY());
        const juce::String portUuid = synth::AIStateMapper::ensureNodeUuid(node);

        macro->members.push_back(portUuid);
        synth::MacroPort mp;
        mp.nodeUuid = portUuid;
        mp.isInput = group.isInput;
        mp.name = portName;
        mp.order = nextMacroPortOrder(*macro, group.isInput);
        mp.kind = kind;
        macro->ports.push_back(mp);

        // 3. Reconnect: external -> port -> internal (an inlet), or internal -> port -> external
        //    (an outlet), on exactly the raw channels the original edges used.
        for (const auto& edge : group.edges) {
            const int portRaw = group.isMidi                            ? 0
                                : group.shape == MacroPortShape::Stereo ? (edge.legIndex == 0 ? 0 : kMacroPortRightBase)
                                                                        : edge.legIndex;
            const int externalChannel =
                group.isMidi ? juce::AudioProcessorGraph::midiChannelIndex : edge.externalRawChannel;
            const int internalChannel =
                group.isMidi ? juce::AudioProcessorGraph::midiChannelIndex : edge.internalRawChannel;
            const int portChannel = group.isMidi ? juce::AudioProcessorGraph::midiChannelIndex : portRaw;

            if (group.isInput) {
                graph.addConnection({{edge.externalNodeId, externalChannel}, {node->nodeID, portChannel}});
                graph.addConnection({{node->nodeID, portChannel}, {group.internalNodeId, internalChannel}});
            } else {
                graph.addConnection({{group.internalNodeId, internalChannel}, {node->nodeID, portChannel}});
                graph.addConnection({{node->nodeID, portChannel}, {edge.externalNodeId, externalChannel}});
            }
        }
    }
}

// ---- Auto-create/delete ports on incremental Add/Remove Selection to/from Macro (T138) -----------

std::vector<GraphEditor::MacroPortCrossingGroup>
GraphEditor::buildMacroPortCrossingPlanForNewMembers(const juce::String& macroId,
                                                     const std::vector<juce::String>& addedUuids) const {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || addedUuids.empty())
        return {};

    std::vector<juce::AudioProcessorGraph::NodeID> insideIds;
    for (const auto& uuid : macro->members)
        if (!macro->memberIsPort(uuid)) {
            const auto id = resolveMemberNodeId(uuid);
            if (id.uid != 0)
                insideIds.push_back(id);
        }
    std::set<uint32_t> addedUids;
    for (const auto& uuid : addedUuids) {
        const auto id = resolveMemberNodeId(uuid);
        if (id.uid != 0) {
            insideIds.push_back(id);
            addedUids.insert(id.uid);
        }
    }

    auto plan = buildMacroPortCrossingPlan(insideIds);

    // Only groups fronting a NEWLY added member are this add's own work — see this method's header
    // comment for why a group fronting an already-established member is left untouched.
    plan.erase(
        std::remove_if(plan.begin(), plan.end(),
                       [&](const MacroPortCrossingGroup& g) { return addedUids.count(g.internalNodeId.uid) == 0; }),
        plan.end());

    // Drop any edge whose external endpoint is one of macroId's OWN existing ports — see this
    // method's header comment; macroPortsThatBecomeInteriorOnAdd handles that port instead.
    for (auto& g : plan)
        g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                                     [&](const MacroPortCrossingEdge& e) {
                                         const juce::String extUuid = nodeUuidFor(e.externalNodeId);
                                         return extUuid.isNotEmpty() && macro->memberIsPort(extUuid);
                                     }),
                      g.edges.end());
    plan.erase(
        std::remove_if(plan.begin(), plan.end(), [](const MacroPortCrossingGroup& g) { return g.edges.empty(); }),
        plan.end());
    return plan;
}

std::vector<juce::String>
GraphEditor::macroPortsThatBecomeInteriorOnAdd(const juce::String& macroId,
                                               const std::vector<juce::String>& addedUuids) const {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || addedUuids.empty())
        return {};

    std::set<juce::String> interiorAfterAdd;
    for (const auto& uuid : macro->members)
        if (!macro->memberIsPort(uuid))
            interiorAfterAdd.insert(uuid);
    for (const auto& uuid : addedUuids)
        interiorAfterAdd.insert(uuid);

    auto& graph = audioEngine.getGraph();
    std::vector<juce::String> result;
    for (const auto& port : macro->ports) {
        const auto portId = resolveMemberNodeId(port.nodeUuid);
        if (portId.uid == 0)
            continue;
        bool anyEdge = false;
        bool anyExternal = false;
        for (const auto& c : graph.getConnections()) {
            juce::AudioProcessorGraph::NodeID other;
            if (c.source.nodeID == portId)
                other = c.destination.nodeID;
            else if (c.destination.nodeID == portId)
                other = c.source.nodeID;
            else
                continue;
            anyEdge = true;
            const juce::String otherUuid = nodeUuidFor(other);
            if (otherUuid.isEmpty() || interiorAfterAdd.count(otherUuid) == 0) {
                anyExternal = true;
                break;
            }
        }
        if (anyEdge && !anyExternal)
            result.push_back(port.nodeUuid);
    }
    return result;
}

std::vector<GraphEditor::MacroPortCrossingGroup>
GraphEditor::buildMacroPortCrossingPlanForRemovedMembers(const juce::String& macroId,
                                                         const std::vector<juce::String>& removedUuids) const {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr || removedUuids.empty())
        return {};

    std::set<juce::String> removedSet(removedUuids.begin(), removedUuids.end());
    std::vector<juce::AudioProcessorGraph::NodeID> remainingIds;
    for (const auto& uuid : macro->members)
        if (!macro->memberIsPort(uuid) && removedSet.count(uuid) == 0) {
            const auto id = resolveMemberNodeId(uuid);
            if (id.uid != 0)
                remainingIds.push_back(id);
        }

    auto plan = buildMacroPortCrossingPlan(remainingIds);

    // Keep only edges whose external endpoint is actually one of the departing members — see this
    // method's header comment for why a remaining member's pre-existing, already-ported connection
    // would otherwise look like a brand-new crossing too.
    std::set<uint32_t> removedUids;
    for (const auto& uuid : removedUuids) {
        const auto id = resolveMemberNodeId(uuid);
        if (id.uid != 0)
            removedUids.insert(id.uid);
    }
    for (auto& g : plan)
        g.edges.erase(std::remove_if(
                          g.edges.begin(), g.edges.end(),
                          [&](const MacroPortCrossingEdge& e) { return removedUids.count(e.externalNodeId.uid) == 0; }),
                      g.edges.end());
    plan.erase(
        std::remove_if(plan.begin(), plan.end(), [](const MacroPortCrossingGroup& g) { return g.edges.empty(); }),
        plan.end());
    return plan;
}

void GraphEditor::spliceOutMacroPort(synth::Macro& macro, const juce::String& portNodeUuid) {
    const auto nodeId = resolveMemberNodeId(portNodeUuid);
    if (nodeId.uid != 0) {
        auto& graph = audioEngine.getGraph();

        // Every connection currently touching the port, split by which side of it they land on.
        // `portChannel` is the port node's OWN channel index for that edge — the axis the cross
        // product below groups on, since MacroInlet/MacroOutlet's per-channel pass-through
        // guarantee (this method's header comment) is exactly "whatever entered on channel c
        // leaves on channel c".
        struct InEdge {
            juce::AudioProcessorGraph::NodeID otherNode;
            int otherChannel = 0;
            int portChannel = 0;
        };
        struct OutEdge {
            juce::AudioProcessorGraph::NodeID otherNode;
            int otherChannel = 0;
            int portChannel = 0;
        };
        std::vector<InEdge> ins;
        std::vector<OutEdge> outs;
        for (const auto& c : graph.getConnections()) {
            if (c.destination.nodeID == nodeId)
                ins.push_back({c.source.nodeID, c.source.channelIndex, c.destination.channelIndex});
            else if (c.source.nodeID == nodeId)
                outs.push_back({c.destination.nodeID, c.destination.channelIndex, c.source.channelIndex});
        }

        for (const auto& in : ins)
            for (const auto& out : outs)
                if (in.portChannel == out.portChannel)
                    graph.addConnection({{in.otherNode, in.otherChannel}, {out.otherNode, out.otherChannel}});

        // Same cleanup every other node-removal site in this file performs first (deleteSelection,
        // requestDeleteModule, changeMacroPortShape's own node swap): removeNode() drops the port's
        // own connections as part of removing it (juce::AudioProcessorGraph::removeNode calls
        // connections.disconnectNode() before erasing the node), so nothing above needs an explicit
        // removeConnection pass — including the edges left unmatched by the cross product above
        // (a port wired on only one side simply disappears here, with nothing to reconnect).
        modMatrix.clearRows();
        graph.removeNode(nodeId);
    }

    macro.members.erase(std::remove(macro.members.begin(), macro.members.end(), portNodeUuid), macro.members.end());
    macro.ports.erase(std::remove_if(macro.ports.begin(), macro.ports.end(),
                                     [&](const synth::MacroPort& p) { return p.nodeUuid == portNodeUuid; }),
                      macro.ports.end());
}

juce::String GraphEditor::autoMacroPortName(ModuleBase* internalMb, bool isInput, int visibleJack, bool isMidi) {
    const juce::String base = internalMb != nullptr ? internalMb->getName() : juce::String("Module");
    if (isMidi)
        return base + " MIDI";
    if (internalMb == nullptr || visibleJack < 0)
        return base;
    const juce::String jackLabel =
        isInput ? internalMb->getInputPortLabel(visibleJack) : internalMb->getOutputPortLabel(visibleJack);
    return jackLabel.isNotEmpty() ? base + " " + jackLabel : base;
}

// ---- Auto-create-port-on-drag / auto-delete-on-last-cable (T148, docs/macros_implementation.md §7 item 9) -----

bool GraphEditor::nodeIsMacroPort(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return false;
    const auto* m = macros.findByMember(uuid);
    return m != nullptr && m->memberIsPort(uuid);
}

juce::AudioProcessorGraph::NodeID
GraphEditor::mintMacroPortForAutoCreate(const juce::String& macroId, bool isInput, bool isMidi,
                                        juce::AudioProcessorGraph::NodeID internalNodeId, int internalVisibleJack) {
    auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return {};

    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const juce::String typeName = macroPortNodeTypeName(isInput, kind);
    auto newProcessor = synth::AIStateMapper::createModule(typeName);
    if (!newProcessor)
        return {};
    if (!isMidi) {
        // Always Mono — the same scope cut createMacroPortFromDroppedCable already applies (§5.3):
        // no inference from the dragged cable's own poly/stereo fan.
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(MacroPortShape::Mono, 1);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(MacroPortShape::Mono, 1);
    }

    auto& graph = audioEngine.getGraph();
    auto* internalNode = graph.getNodeForId(internalNodeId);
    auto* internalMb = internalNode != nullptr ? dynamic_cast<ModuleBase*>(internalNode->getProcessor()) : nullptr;

    auto node = graph.addNode(std::move(newProcessor));
    if (!node)
        return {};
    node->properties.set("x", macro->bounds.getX());
    node->properties.set("y", macro->bounds.getY());
    const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);

    macro->members.push_back(uuid);
    synth::MacroPort mp;
    mp.nodeUuid = uuid;
    mp.isInput = isInput;
    mp.name = autoMacroPortName(internalMb, isInput, internalVisibleJack, isMidi);
    mp.order = nextMacroPortOrder(*macro, isInput);
    mp.kind = kind;
    macro->ports.push_back(mp);

    return node->nodeID;
}

bool GraphEditor::maybeAutoCreateMacroPortsForDrag(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                                   juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi,
                                                   bool recordUndo) {
    // T155: a mod/CV-routed drag goes through this SAME mint-and-wire path as a plain audio drag —
    // no separate scope cut. connectPorts() (below) has its own CV detection (isCV, from the real
    // destination's getModulationTargets()) and wraps that leg in a hidden AttenuverterModule via
    // addModRouting() exactly as it always has; a freshly-minted MacroInletModule/MacroOutletModule
    // is never itself a modulation target, so only the leg whose real endpoint is the genuine
    // mod-target parameter can ever attract the wrap (docs/macros_implementation.md §7 item 9).
    const juce::String srcUuid = nodeUuidFor(srcId);
    const juce::String dstUuid = nodeUuidFor(dstId);
    auto* srcMacro = srcUuid.isNotEmpty() ? macros.findByMember(srcUuid) : nullptr;
    auto* dstMacro = dstUuid.isNotEmpty() ? macros.findByMember(dstUuid) : nullptr;

    // An endpoint needs a NEW port on its own macro iff it is an ORDINARY member there (not
    // already a port itself) and the OTHER endpoint is not a member of that same macro at all —
    // "not a member" covers both "an ordinary member of the same macro" (nothing crosses, wire
    // directly) and "already a port of the same macro" (wire straight into the existing jack,
    // never mint a second one), since a port's own uuid is always also a `members` entry
    // (MacroSet's own invariant).
    const bool srcNeedsPort = srcMacro != nullptr && !srcMacro->memberIsPort(srcUuid) && !srcMacro->hasMember(dstUuid);
    const bool dstNeedsPort = dstMacro != nullptr && !dstMacro->memberIsPort(dstUuid) && !dstMacro->hasMember(srcUuid);

    if (!srcNeedsPort && !dstNeedsPort)
        return false;

    auto& graph = audioEngine.getGraph();
    const juce::String srcMacroId = srcNeedsPort ? srcMacro->id : juce::String();
    const juce::String dstMacroId = dstNeedsPort ? dstMacro->id : juce::String();

    auto doMutation = [this, srcId, srcJack, dstId, dstJack, isMidi, srcMacroId, dstMacroId, srcNeedsPort,
                       dstNeedsPort] {
        auto effectiveSrc = srcId;
        int effectiveSrcJack = srcJack;
        auto effectiveDst = dstId;
        int effectiveDstJack = dstJack;

        if (srcNeedsPort) {
            const auto portId = mintMacroPortForAutoCreate(srcMacroId, /*isInput=*/false, isMidi, srcId, srcJack);
            if (portId.uid != 0) {
                connectPorts(srcId, srcJack, portId, 0, isMidi, /*recordUndo=*/false);
                effectiveSrc = portId;
                effectiveSrcJack = 0;
            }
        }
        if (dstNeedsPort) {
            const auto portId = mintMacroPortForAutoCreate(dstMacroId, /*isInput=*/true, isMidi, dstId, dstJack);
            if (portId.uid != 0) {
                connectPorts(portId, 0, dstId, dstJack, isMidi, /*recordUndo=*/false);
                effectiveDst = portId;
                effectiveDstJack = 0;
            }
        }

        // The final leg: direct member<->member if neither side needed a port (never actually
        // reached — see the early return above), member<->port if only one side did, or
        // port<->port for a genuine cross-macro-boundary crossing.
        connectPorts(effectiveSrc, effectiveSrcJack, effectiveDst, effectiveDstJack, isMidi, /*recordUndo=*/false);
    };

    if (!recordUndo) {
        // T184's auto-channel hook: the caller already owns an outer recordGraphAndMacroChange
        // transaction and will call updateComponents() itself once, after its own further
        // mutations — calling it here too would fire onGraphStructureChanged (and the timeline
        // reconcile pass it drives) twice for one user gesture.
        doMutation();
        return true;
    }

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, [&] {
            doMutation();
            updateComponents();
        });
    else {
        doMutation();
        updateComponents();
    }

    return true;
}
