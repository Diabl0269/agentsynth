// MacroGroupControllerPortSplice.cpp
//
// The macro port crossing-plan math (buildMacroPortCrossingPlan and its add/remove-member
// variants), spliceMacroPorts/spliceOutMacroPort, and auto-create-port-on-drag. MacroGroupController
// is declared in MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory
// hold the rest of the class.

#include "MacroGroupController.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"

juce::String MacroGroupController::macroPortNodeTypeName(bool isInput, synth::MacroPortKind kind) {
    if (kind == synth::MacroPortKind::Midi)
        return isInput ? "Macro MIDI In" : "Macro MIDI Out";
    return isInput ? "Macro In" : "Macro Out";
}

juce::String MacroGroupController::defaultMacroPortName(bool isInput, synth::MacroPortKind kind) {
    if (kind == synth::MacroPortKind::Midi)
        return isInput ? "MIDI In" : "MIDI Out";
    return isInput ? "Input" : "Output";
}

int MacroGroupController::nextMacroPortOrder(const synth::Macro& macro, bool isInput) {
    int next = 0;
    for (const auto& p : macro.ports)
        if (p.isInput == isInput)
            next = std::max(next, p.order + 1);
    return next;
}

// ---- Auto-create-ports-on-group (founder-review fix F5, docs/macros_implementation.md §7 item 6.1) -------------

std::vector<MacroGroupController::MacroPortCrossingGroup> MacroGroupController::buildMacroPortCrossingPlan(
    const std::vector<juce::AudioProcessorGraph::NodeID>& memberNodeIds) const {
    std::vector<MacroPortCrossingGroup> groups;
    auto& graph = host_.graph();

    std::set<uint32_t> memberUids;
    for (const auto& id : memberNodeIds)
        if (id.uid != 0)
            memberUids.insert(id.uid);
    // No `size() < 2` floor: groupSelectionIntoMacro() already refuses a selection of fewer than
    // two modules before it ever gets here, but the incremental add/remove callers (T138)
    // legitimately need a crossing plan for a one-member "inside" set. The loop below is correct
    // for any size, including 0 or 1.
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
        // attenuverter itself can NEVER be a macro member — it never gets a ModuleComponent, so it
        // can never be part of a canvas selection. So when this crossing's external node is an
        // attenuverter, the mod chain's OTHER endpoint decides what is actually happening: if it
        // is ALSO about to become a member, both edges stay wholly internal (unported); otherwise
        // this IS a real crossing and the attenuverter itself stands in as the "external" node.
        auto* externalNode = graph.getNodeForId(externalId);
        if (externalNode != nullptr && dynamic_cast<AttenuverterModule*>(externalNode->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::NodeID farNode;
            bool foundFar = false;
            // AudioEngine::addModRouting always uses channel 0 on both of the attenuverter's ch0
            // in/out edges — the far side of THIS connection's leg is the attenuverter's other
            // ch0 edge.
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
            // polyVoiceSpan == 1 on itself.
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
            // "Audio" jack fanning both raw legs) — the port must present the SAME one visible
            // jack the module does (MacroPortShape::StereoCollapsed), never the two-jack
            // MacroPortShape::Stereo a hand-picked Configure I/O choice means. A Dual-I/O-ON
            // module's separately-jacked Left/Right pair never reaches this branch: each leg is
            // its own visible jack with headSpan == 1 here, so it starts life as two Mono groups
            // and only becomes Stereo in the merge pass below.
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
    // Pairs legs via ModuleBase::rightAudioLegChannel(), never jack index 0/1.
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

std::vector<MacroGroupController::MacroPortCrossingGroup>
MacroGroupController::buildMacroPortCrossingPlan(const std::vector<juce::String>& memberUuids) const {
    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    ids.reserve(memberUuids.size());
    for (const auto& uuid : memberUuids) {
        const auto id = resolveMemberNodeId(uuid);
        if (id.uid != 0)
            ids.push_back(id);
    }
    return buildMacroPortCrossingPlan(ids);
}

void MacroGroupController::spliceMacroPorts(const juce::String& macroId,
                                            const std::vector<MacroPortCrossingGroup>& plan) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return;
    auto& graph = host_.graph();

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

std::vector<MacroGroupController::MacroPortCrossingGroup>
MacroGroupController::buildMacroPortCrossingPlanForNewMembers(const juce::String& macroId,
                                                              const std::vector<juce::String>& addedUuids) const {
    const auto* macro = host_.getMacros().find(macroId);
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

    // Only groups fronting a NEWLY added member are this add's own work.
    plan.erase(
        std::remove_if(plan.begin(), plan.end(),
                       [&](const MacroPortCrossingGroup& g) { return addedUids.count(g.internalNodeId.uid) == 0; }),
        plan.end());

    // Drop any edge whose external endpoint is one of macroId's OWN existing ports —
    // macroPortsThatBecomeInteriorOnAdd handles that port instead.
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
MacroGroupController::macroPortsThatBecomeInteriorOnAdd(const juce::String& macroId,
                                                        const std::vector<juce::String>& addedUuids) const {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || addedUuids.empty())
        return {};

    std::set<juce::String> interiorAfterAdd;
    for (const auto& uuid : macro->members)
        if (!macro->memberIsPort(uuid))
            interiorAfterAdd.insert(uuid);
    for (const auto& uuid : addedUuids)
        interiorAfterAdd.insert(uuid);

    auto& graph = host_.graph();
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

std::vector<MacroGroupController::MacroPortCrossingGroup>
MacroGroupController::buildMacroPortCrossingPlanForRemovedMembers(const juce::String& macroId,
                                                                  const std::vector<juce::String>& removedUuids) const {
    const auto* macro = host_.getMacros().find(macroId);
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

    // Keep only edges whose external endpoint is actually one of the departing members —
    // otherwise a remaining member's pre-existing, already-ported connection would otherwise look
    // like a brand-new crossing too.
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

void MacroGroupController::spliceOutMacroPort(synth::Macro& macro, const juce::String& portNodeUuid) {
    const auto nodeId = resolveMemberNodeId(portNodeUuid);
    if (nodeId.uid != 0) {
        auto& graph = host_.graph();

        // Every connection currently touching the port, split by which side of it they land on.
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

        // Same cleanup every other node-removal site performs first: removeNode() drops the
        // port's own connections as part of removing it, so nothing above needs an explicit
        // removeConnection pass.
        host_.clearModMatrixRows();
        graph.removeNode(nodeId);
    }

    macro.members.erase(std::remove(macro.members.begin(), macro.members.end(), portNodeUuid), macro.members.end());
    macro.ports.erase(std::remove_if(macro.ports.begin(), macro.ports.end(),
                                     [&](const synth::MacroPort& p) { return p.nodeUuid == portNodeUuid; }),
                      macro.ports.end());
}

juce::String MacroGroupController::autoMacroPortName(ModuleBase* internalMb, bool isInput, int visibleJack,
                                                     bool isMidi) {
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

bool MacroGroupController::nodeIsMacroPort(juce::AudioProcessorGraph::NodeID nodeId) const {
    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return false;
    const auto* m = host_.getMacros().findByMember(uuid);
    return m != nullptr && m->memberIsPort(uuid);
}

juce::AudioProcessorGraph::NodeID
MacroGroupController::mintMacroPortForAutoCreate(const juce::String& macroId, bool isInput, bool isMidi,
                                                 juce::AudioProcessorGraph::NodeID internalNodeId,
                                                 int internalVisibleJack) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return {};

    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const juce::String typeName = macroPortNodeTypeName(isInput, kind);
    auto newProcessor = synth::AIStateMapper::createModule(typeName);
    if (!newProcessor)
        return {};
    if (!isMidi) {
        // Always Mono — the same scope cut createMacroPortFromDroppedCable already applies (§5.3).
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(MacroPortShape::Mono, 1);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(MacroPortShape::Mono, 1);
    }

    auto& graph = host_.graph();
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

bool MacroGroupController::maybeAutoCreateMacroPortsForDrag(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                                            juce::AudioProcessorGraph::NodeID dstId, int dstJack,
                                                            bool isMidi, bool recordUndo) {
    // T155: a mod/CV-routed drag goes through this SAME mint-and-wire path as a plain audio drag —
    // no separate scope cut. host_.connectPorts() has its own CV detection and wraps that leg in a
    // hidden AttenuverterModule via addModRouting() exactly as it always has.
    const juce::String srcUuid = nodeUuidFor(srcId);
    const juce::String dstUuid = nodeUuidFor(dstId);
    auto* srcMacro = srcUuid.isNotEmpty() ? host_.getMacros().findByMember(srcUuid) : nullptr;
    auto* dstMacro = dstUuid.isNotEmpty() ? host_.getMacros().findByMember(dstUuid) : nullptr;

    // An endpoint needs a NEW port on its own macro iff it is an ORDINARY member there (not
    // already a port itself) and the OTHER endpoint is not a member of that same macro at all.
    const bool srcNeedsPort = srcMacro != nullptr && !srcMacro->memberIsPort(srcUuid) && !srcMacro->hasMember(dstUuid);
    const bool dstNeedsPort = dstMacro != nullptr && !dstMacro->memberIsPort(dstUuid) && !dstMacro->hasMember(srcUuid);

    if (!srcNeedsPort && !dstNeedsPort)
        return false;

    auto& graph = host_.graph();
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
                host_.connectPorts(srcId, srcJack, portId, 0, isMidi, /*recordUndo=*/false);
                effectiveSrc = portId;
                effectiveSrcJack = 0;
            }
        }
        if (dstNeedsPort) {
            const auto portId = mintMacroPortForAutoCreate(dstMacroId, /*isInput=*/true, isMidi, dstId, dstJack);
            if (portId.uid != 0) {
                host_.connectPorts(portId, 0, dstId, dstJack, isMidi, /*recordUndo=*/false);
                effectiveDst = portId;
                effectiveDstJack = 0;
            }
        }

        // The final leg: direct member<->member if neither side needed a port (never actually
        // reached — see the early return above), member<->port if only one side did, or
        // port<->port for a genuine cross-macro-boundary crossing.
        host_.connectPorts(effectiveSrc, effectiveSrcJack, effectiveDst, effectiveDstJack, isMidi,
                           /*recordUndo=*/false);
    };

    if (!recordUndo) {
        // T184's auto-channel hook: the caller already owns an outer recordGraphAndMacroChange
        // transaction and will call updateComponents() itself once, after its own further
        // mutations.
        doMutation();
        return true;
    }

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), [&] {
            doMutation();
            host_.updateComponents();
        });
    else {
        doMutation();
        host_.updateComponents();
    }

    return true;
}
