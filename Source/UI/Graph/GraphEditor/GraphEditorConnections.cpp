// GraphEditorConnections.cpp
//
// Poly-link resolution (resolvePolyLink and its scoring helpers) and connection-drag
// begin/drag/end. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "Modules/MacroMidiInletModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"

using namespace detail;

// Works out which raw-channel fan a cable dropped between two *visible* jacks should wire.
// Port hit-testing yields visible jack indices, which are not raw channel numbers once a
// module goes poly (a poly VCA's CV jack is jack 1 but raw channel 8). Pure — no graph access,
// headless-testable. See the note below for the fan-width/mod-CV-broadcast rules.
//
// When both ends front equally wide fans the cable covers all N voices; otherwise it degrades to
// one head-to-head wire. The one exception is a mono source landing on a per-voice *mod-CV* fan:
// that is broadcast to every voice (one LFO shakes all eight), which is what sourceStride == 0
// means. Where a jack fronts more than one fan (Poly MIDI's "Poly Out" carries both Pitch and
// Gate) the pairing whose roles agree wins. A null end (the graph's audio I/O nodes, which have no
// logical ports) is treated as a plain mono jack whose index is its raw channel.
GraphEditor::PolyLink GraphEditor::resolvePolyLink(const ModuleBase* source, int sourceVisibleJack,
                                                   const ModuleBase* dest, int destVisibleJack) {
    PolyLink link{sourceVisibleJack, destVisibleJack, 1};

    // A non-ModuleBase end (the graph's audio I/O nodes) has no logical ports, so treat its jack
    // index as the raw channel — the pre-logical-port behaviour, still correct for a plain mono jack.
    const auto identity = [](int jack) { return std::vector<ModuleBase::JackTarget>{{jack, PortRole::Other, 1}}; };
    const auto sourceTargets =
        source != nullptr ? source->getJackTargets(sourceVisibleJack, false) : identity(sourceVisibleJack);
    const auto destTargets = dest != nullptr ? dest->getJackTargets(destVisibleJack, true) : identity(destVisibleJack);

    int bestScore = -1;
    for (const auto& s : sourceTargets) {
        for (const auto& d : destTargets) {
            const int score = scoreJackPair(s, d);
            if (score <= bestScore)
                continue;
            bestScore = score;

            int voiceCount = std::min(s.voiceSpan, d.voiceSpan);
            int sourceStride = 1;

            // One mono modulator patched onto a per-voice mod-CV fan drives every voice, the way a
            // single LFO shakes all the voices of a hardware poly synth. Deliberately limited to
            // ModCV: broadcasting Pitch or Gate would make all eight voices sound the same note at
            // the same time, and broadcasting audio onto a poly *voice* fan would be a paraphonic
            // instrument (identical signal through a shared cutoff — bit-identical for 8x the DSP).
            if (s.voiceSpan == 1 && d.voiceSpan > 1 && d.role == PortRole::ModCV) {
                voiceCount = d.voiceSpan;
                sourceStride = 0;
            }

            // Mono audio into a collapsed stereo pair (voiceSpan == 2, PortRole::Audio) duplicates
            // onto L and R — the usual mono→FX insert. Distinct from poly-voice broadcast above.
            if (s.voiceSpan == 1 && d.voiceSpan == 2 && d.role == PortRole::Audio) {
                voiceCount = 2;
                sourceStride = 0;
            }

            // ...but a DUAL I/O FX's LEFT jack is not mono — it is one half of a split pair sitting
            // on raw0/raw1, and duplicating it onto both destination legs drops the right channel
            // entirely (a dual Reverb landing on a collapsed Chorus wired only Left). Wire the real
            // pair instead: L -> raw0, R -> raw1.
            //
            // Deliberately limited to an ADJACENT right leg, i.e. the FX layout. The split-block
            // voice modules (Oscillator, Filter, VCA, Wavetable) put Audio R on its own kRightBase
            // block far from ch0, and for those the established behaviour is the mono broadcast
            // above — ResolvePolyLinkBroadcastsMonoIntoCollapsedStereoPair and
            // TogglingDualIOKeepsBothStereoLegs both encode it, the latter explaining that the
            // right leg gets picked up separately from the module's own Audio R block. Widening
            // this to non-adjacent legs is a deliberate behaviour change for manual cable drags
            // too, not something to slip in behind a smart-connect fix.
            if (s.voiceSpan == 1 && d.voiceSpan == 2 && d.role == PortRole::Audio && s.role == PortRole::Audio &&
                source != nullptr && source->isDualIO() && source->rightAudioLegChannel() == s.rawHeadChannel + 1) {
                voiceCount = 2;
                sourceStride = 1;
            }

            // Collapsed stereo source (span 2) dropped on dest jack 0: fan L→L / R→R when the
            // dest actually has a second audio channel (Audio Output, Dual I/O). A mono Filter
            // jack 0 must not steal its Cutoff CV on ch1.
            if (s.voiceSpan == 2 && s.role == PortRole::Audio && d.voiceSpan == 1 && destVisibleJack == 0) {
                bool destHasStereoPair = dest == nullptr;
                if (dest != nullptr) {
                    const auto other = dest->mapInputChannel(d.rawHeadChannel + 1);
                    destHasStereoPair = other.role == PortRole::Audio;
                }
                if (destHasStereoPair) {
                    voiceCount = 2;
                    sourceStride = 1;
                }
            }

            link = {s.rawHeadChannel, d.rawHeadChannel, voiceCount, sourceStride};
        }
    }

    // Graph I/O nodes are not ModuleBase: a collapsed stereo source dropped on Audio Output Left
    // should fan L→L and R→R rather than leave the right leg silent.
    if (dest == nullptr && destVisibleJack == 0) {
        for (const auto& s : sourceTargets) {
            if (s.role == PortRole::Audio && s.voiceSpan == 2) {
                return PolyLink{s.rawHeadChannel, 0, 2, 1};
            }
        }
    }

    return link;
}

void GraphEditor::beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                                      juce::Point<int> screenPos) {
    isDraggingConnection = true;
    dragSourceModule = sourceModule;
    dragSourceChannel = channelIndex;
    dragSourceIsInput = isInput;
    dragSourceIsMidi = isMidi;
    dragCurrentPos = screenPos;
    repaintCanvas();
}

void GraphEditor::dragConnection(juce::Point<int> screenPos) {
    if (!isDraggingConnection)
        return;
    dragCurrentPos = screenPos;

    // Ring whichever knob the cable would land on, so a Serum-style mod drop is aimed rather
    // than guessed at. Only a cable dragged FROM an output can land on a knob.
    for (auto* comp : content.getModules()) {
        int target = -1;
        if (!dragSourceIsInput && !dragSourceIsMidi && comp != dragSourceModule) {
            const auto localPos = comp->getLocalPoint(nullptr, screenPos);
            if (!comp->getPortForPoint(localPos))
                if (auto port = comp->getModTargetPortForPoint(localPos))
                    target = port->index;
        }
        comp->setModDropTargetChannel(target);
    }

    repaintCanvas();
}

// Drops the pending modulation drop-target highlight on every card.
void GraphEditor::clearModDropTargets() {
    for (auto* comp : content.getModules())
        comp->setModDropTargetChannel(-1);
}

// Wires two visible jacks the same way a completed cable-drag does (poly fan, MIDI,
// attenuverter for mono mod CV). When recordUndo is false the caller owns the transaction
// (e.g. inside an existing recordStructuralChange). Also GraphCanvasHost::connectPorts().
void GraphEditor::connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                               juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi, bool recordUndo) {
    auto& graph = audioEngine.getGraph();
    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    if (srcNode == nullptr || dstNode == nullptr)
        return;

    auto* srcModuleBase = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
    auto* dstModuleBase = dynamic_cast<ModuleBase*>(dstNode->getProcessor());

    PolyLink link{srcJack, dstJack, 1};
    if (!isMidi)
        link = resolvePolyLink(srcModuleBase, srcJack, dstModuleBase, dstJack);

    // An attenuverter sits in the path of a single mod wire; a poly fan stays direct so
    // AudioEngine can collapse its N raw edges into one PolyBus. Structural pitch/gate
    // sources are never wrapped either — see carriesStructuralSignal.
    bool isCV = false;
    if (!isMidi && link.voiceCount == 1 && dstModuleBase != nullptr &&
        !carriesStructuralSignal(srcModuleBase, link.sourceRawChannel)) {
        for (const auto& t : dstModuleBase->getModulationTargets()) {
            if (t.channelIndex == link.destRawChannel) {
                isCV = true;
                break;
            }
        }
    }

    auto doConnect = [this, &graph, srcId, dstId, link, isMidi, isCV] {
        if (isMidi) {
            graph.addConnection({{srcId, juce::AudioProcessorGraph::midiChannelIndex},
                                 {dstId, juce::AudioProcessorGraph::midiChannelIndex}});
        } else if (isCV) {
            audioEngine.addModRouting(srcId, link.sourceRawChannel, dstId, link.destRawChannel);
        } else {
            for (int v = 0; v < link.voiceCount; ++v)
                graph.addConnection(
                    {{srcId, link.sourceRawChannel + v * link.sourceStride}, {dstId, link.destRawChannel + v}});
        }
    };

    if (recordUndo && undoManager)
        undoManager->recordStructuralChange(graph, doConnect);
    else
        doConnect();
}

void GraphEditor::endConnectionDrag(juce::Point<int> screenPos) {
    if (!isDraggingConnection)
        return;

    bool connectedToAModule = false; // gates the macro-card fallback below — unchanged loop otherwise
    for (auto* comp : content.getModules()) {
        // A hidden member of a collapsed macro is not "on the canvas" as a drop target (same
        // reasoning as collectModuleBoxes' marquee/group-drag exclusion) — it is kept alive only
        // so its position tracks the card, and its ORIGINAL bounds (group-bounds top-left, see
        // groupSelectionIntoMacro) sit directly underneath the card, including its own jacks at
        // the same left/right inset a card jack now uses (T141). Without this guard a release on
        // a card jack could hit-test straight through to the hidden member's real jack instead.
        if (comp == nullptr || !comp->isVisible())
            continue;
        auto localPos = comp->getLocalPoint(nullptr, screenPos);
        auto port = comp->getPortForPoint(localPos);

        // Serum-style modulation drop: a cable released on a KNOB connects to that parameter's
        // CV jack. Only as a fallback, so an actual jack under the cursor still wins, and only
        // for a cable coming from an output — a mod source drives a destination, not the reverse.
        if (!port && !dragSourceIsInput && !dragSourceIsMidi && comp != dragSourceModule) {
            port = comp->getModTargetPortForPoint(localPos);
            // A knob names its CV jack by RAW channel (the ModulationTarget's channelIndex), while
            // everything below speaks in VISIBLE jack indices. On a collapsed stereo pair the two
            // differ by one (raw ch0/ch1 are the single "Audio" jack), so without this mapping a
            // Distortion's Drive knob (raw ch2) wired visible jack 2, which is Mix.
            if (port)
                if (auto* mb = dynamic_cast<ModuleBase*>(comp->getModule()))
                    port->index = mb->mapInputChannel(port->index).visibleJackIndex;
        }

        if (port) {
            if (comp == dragSourceModule)
                continue;
            if (port->isInput == dragSourceIsInput)
                continue;
            if (port->isMidi != dragSourceIsMidi)
                continue;

            auto& graph = audioEngine.getGraph();
            juce::AudioProcessorGraph::Node* srcNode = nullptr;
            juce::AudioProcessorGraph::Node* dstNode = nullptr;

            for (auto* n : graph.getNodes()) {
                if (n->getProcessor() == dragSourceModule->getModule())
                    srcNode = n;
                if (n->getProcessor() == comp->getModule())
                    dstNode = n;
            }

            if (srcNode && dstNode) {
                auto* realSrc = dragSourceIsInput ? dstNode : srcNode;
                auto* realDst = dragSourceIsInput ? srcNode : dstNode;
                const int srcJack = dragSourceIsInput ? port->index : dragSourceChannel;
                const int dstJack = dragSourceIsInput ? dragSourceChannel : port->index;

                // T184 (docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow"): a MIDI cable from a
                // Track In node landing here may newly make some audio reach the output with no channel — build one, in
                // the SAME undo step as the connection itself (and as any T148 macro port the connection below also
                // mints, so a Track In dragged across a macro boundary straight onto an unchanneled instrument gets ALL
                // of it undone by one Cmd+Z). Gated by autoCreateChannelOnConnectEnabled (Preferences); OFF (or not a
                // MIDI drag from a Track In) falls straight through to the T148/plain-connect branch below, byte for
                // byte as it was before T184.
                if (autoCreateChannelOnConnectEnabled && dragSourceIsMidi &&
                    nodeIsTimelineMidiSource(realSrc->nodeID)) {
                    const auto realSrcId = realSrc->nodeID;
                    const auto realDstId = realDst->nodeID;
                    auto doMutation = [this, realSrcId, srcJack, realDstId, dstJack] {
                        if (!autoCreateMacroPortsOnDragEnabled ||
                            !macroController_.maybeAutoCreateMacroPortsForDrag(realSrcId, srcJack, realDstId, dstJack,
                                                                               /*isMidi=*/true, /*recordUndo=*/false))
                            connectPorts(realSrcId, srcJack, realDstId, dstJack, /*isMidi=*/true,
                                         /*recordUndo=*/false);
                        // realDstId is always the real destination node, whether or not either
                        // side just got a minted macro port above — maybeAutoCreateMacroPortsForDrag
                        // wires any port it mints straight through to this same node — so the
                        // search for un-channeled output feeds always starts here.
                        maybeAutoCreateChannelAfterConnect(realDstId);
                        updateComponents();
                    };
                    if (undoManager)
                        undoManager->recordGraphAndMacroChange(graph, macros, doMutation);
                    else
                        doMutation();
                } else if (!autoCreateMacroPortsOnDragEnabled ||
                           !macroController_.maybeAutoCreateMacroPortsForDrag(realSrc->nodeID, srcJack, realDst->nodeID,
                                                                              dstJack, dragSourceIsMidi)) {
                    // T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): if this completed drag crosses a macro
                    // boundary (an EXPANDED macro's member on one side, something outside that same
                    // macro on the other — the collapsed-card drop above is a different code path),
                    // mint and wire a matching port instead of the plain direct connection. Gated by
                    // autoCreateMacroPortsOnDragEnabled (Preferences); when it handles the drag it
                    // returns true and the plain connectPorts below is skipped entirely.
                    connectPorts(realSrc->nodeID, srcJack, realDst->nodeID, dstJack, dragSourceIsMidi, true);
                }
                connectedToAModule = true;
            }
        }
    }

    // Macro card drop (docs/macros/ports.md#how-a-port-is-drawn): nothing above matched (no module jack under the
    // cursor), so check whether the release point is over a COLLAPSED macro's card.
    if (!connectedToAModule && dragSourceModule != nullptr) {
        for (auto* card : content.getMacroCards()) {
            if (card == nullptr || !card->isVisible()) // visible exactly while its macro is collapsed
                continue;
            const auto cardLocal = card->getLocalPoint(nullptr, screenPos);
            if (!card->getLocalBounds().contains(cardLocal))
                continue;

            auto& graph = audioEngine.getGraph();
            juce::AudioProcessorGraph::Node* srcNode = nullptr;
            for (auto* n : graph.getNodes())
                if (n->getProcessor() == dragSourceModule->getModule()) {
                    srcNode = n;
                    break;
                }
            if (srcNode == nullptr)
                break;

            // dragSourceIsInput == false: the drag started at an OUTPUT looking for a
            // destination -> the macro offers an INPUT (MacroInlet/MacroMidiInlet) to receive
            // it. dragSourceIsInput == true: started at an INPUT looking for a source -> the
            // macro offers an OUTPUT.
            const bool newPortIsInput = !dragSourceIsInput;

            // T141: an existing port's jack under the cursor wires directly into that port's own
            // node, rather than always minting a fresh one — "one jack per port" makes a jack a
            // real, precise drop target, not just the card as a whole. A jack whose direction or
            // kind doesn't match the drag is refused silently, the same way an ordinary mismatched
            // module-jack drop is refused a few lines above
            // (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed: not silently adapted). DEFERRED
            // (not this stage): raw channel 0 only, same as createMacroPortFromDroppedCable below. For a Mono port that
            // IS the port's one visible jack; for an existing Stereo port this wires the Left leg only and leaves Right
            // unconnected — a card jack summarises the whole port as one dot
            // (docs/macros/ports.md#how-a-port-is-drawn), so there is no separate "Right" drop target to land on yet.
            // Widening this to resolvePolyLink-style fan-out for an existing Stereo/Poly-N port is future work, not a
            // regression: the modal (docs/macros/configure-io.md#renaming-and-reordering-ports) remains the reliable
            // way to wire a non-Mono port completely.
            if (auto hitPort = macroController_.macroCardPortForPoint(card->getMacroId(), cardLocal)) {
                if (hitPort->isInput == newPortIsInput &&
                    (hitPort->kind == synth::MacroPortKind::Midi) == dragSourceIsMidi) {
                    const auto portNodeId = macroController_.resolveMemberNodeId(hitPort->nodeUuid);
                    if (graph.getNodeForId(portNodeId) != nullptr) {
                        const auto connSrcId = newPortIsInput ? srcNode->nodeID : portNodeId;
                        const auto connDstId = newPortIsInput ? portNodeId : srcNode->nodeID;
                        const int connSrcJack = newPortIsInput ? dragSourceChannel : 0;
                        const int connDstJack = newPortIsInput ? 0 : dragSourceChannel;

                        // T184: the same auto-channel trigger as the direct-jack branch above,
                        // for a Track In dropped straight onto an EXISTING port jack on a
                        // collapsed macro's card. The port node (a MacroMidiInletModule) is a
                        // plain pass-through — findUnchanneledOutputFeeds (via
                        // maybeAutoCreateChannelAfterConnect) reaches whatever it forwards to on
                        // its own, so searching from the port node itself is enough.
                        if (autoCreateChannelOnConnectEnabled && dragSourceIsMidi &&
                            nodeIsTimelineMidiSource(connSrcId)) {
                            auto doMutation = [this, connSrcId, connSrcJack, connDstId, connDstJack] {
                                connectPorts(connSrcId, connSrcJack, connDstId, connDstJack, /*isMidi=*/true,
                                             /*recordUndo=*/false);
                                maybeAutoCreateChannelAfterConnect(connDstId);
                                updateComponents();
                            };
                            if (undoManager)
                                undoManager->recordGraphAndMacroChange(graph, macros, doMutation);
                            else
                                doMutation();
                        } else {
                            connectPorts(connSrcId, connSrcJack, connDstId, connDstJack, dragSourceIsMidi, true);
                        }
                        connectedToAModule = true;
                    }
                }
                break;
            }

            // No jack under the cursor: fall back to the "shape from a dropped cable" convenience
            // (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed, T140) — the whole card is still
            // a valid drop target, and a fresh Mono port is created to receive the cable. T184 does NOT apply here:
            // createMacroPortFromDroppedCable wires no interior leg (the freshly-minted port has nothing behind it
            // yet), so there is nothing for findUnchanneledOutputFeeds to find.
            macroController_.createMacroPortFromDroppedCable(card->getMacroId(), newPortIsInput, dragSourceIsMidi,
                                                             srcNode->nodeID, dragSourceChannel);
            break;
        }
    }

    isDraggingConnection = false;
    dragSourceModule = nullptr;
    clearModDropTargets();
    repaintCanvas();
}
