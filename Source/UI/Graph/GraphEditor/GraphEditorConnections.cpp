// GraphEditorConnections.cpp
//
// Poly-link resolution (resolvePolyLink and its scoring helpers) and connection-drag
// begin/drag/end. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "Modules/MacroMidiInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include "UI/Graph/PortPanel/PortConnector.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UserSettings.h"

#include <algorithm>
#include <utility>

using namespace detail;

namespace {

// The raw channel carrying the Right leg of the stereo pair whose Left leg is `destVisibleJack` (raw
// `leftRaw`) on `dest`, or -1 when that jack is not the Left of a pair. A null `dest` is the graph's
// Audio Output, whose jack 0 is Left over a contiguous Right. A module's pair is its jack 0 (the FX
// and voice modules), or any jack labelled Left/Right as a pair (Master's Mix L / Mix R and
// Direct L / Direct R). The Right leg is asked of the module -- adjacent for the FX, a dedicated
// kRightBase block for the split-block modules -- never assumed to be leftRaw + 1.
int stereoPairRightLeg(const ModuleBase* dest, int leftRaw, int destVisibleJack) {
    if (dest == nullptr)
        return destVisibleJack == 0 ? leftRaw + 1 : -1;

    const auto adjacent = dest->mapInputChannel(leftRaw + 1);
    if (adjacent.role == PortRole::Audio && destVisibleJack == 0)
        return leftRaw + 1;

    const bool labelledPair = jackIsLeftOfSplitPair(*dest, destVisibleJack, /*isInput=*/true);
    const int rightLeg = dest->rightAudioLegChannel();
    if (destVisibleJack == 0 && rightLeg > leftRaw && labelledPair &&
        dest->mapInputChannel(rightLeg).role == PortRole::Audio)
        return rightLeg;
    if (labelledPair && adjacent.role == PortRole::Audio && adjacent.visibleJackIndex == destVisibleJack + 1)
        return leftRaw + 1;
    return -1;
}

} // namespace

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
// logical ports) is treated as a plain mono jack whose index is its raw channel — except dropping
// a genuinely STEREO source onto its Left jack specifically (index 0), where it fans onto raw 0
// AND 1 (the null-dest block at the end of this function), so neither leaves the Right leg silent.
// A MONO source dropped there lands on Left alone (one leg, sourceStride 1) — render-time
// L/Mono normalling (AudioEngine::refreshNormalling) fills Right for as long as it stays unpatched,
// which is also what the "reconnect the chain" heal lands on for a mono chain healed
// straight into Audio Output.
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
            int destStride = d.rawStride;

            // One mono modulator patched onto a per-voice mod-CV fan drives every voice, the way a
            // single LFO shakes all the voices of a hardware poly synth. Deliberately limited to
            // ModCV: broadcasting Pitch or Gate would make all eight voices sound the same note at
            // the same time, and broadcasting audio onto a poly *voice* fan would be a paraphonic
            // instrument (identical signal through a shared cutoff — bit-identical for 8x the DSP).
            if (s.voiceSpan == 1 && d.voiceSpan > 1 && d.role == PortRole::ModCV) {
                voiceCount = d.voiceSpan;
                sourceStride = 0;
            }

            // One mono oscillator's audio on a poly Restart jack syncs every voice: it carries no note, so nothing
            // about the chord changes, and syncing only the first voice would leave the others free-running.
            if (s.voiceSpan == 1 && d.voiceSpan > 1 && d.role == PortRole::Gate && s.role == PortRole::Audio) {
                voiceCount = d.voiceSpan;
                sourceStride = 0;
            }

            // Mono audio into a collapsed stereo pair (voiceSpan == 2, PortRole::Audio) duplicates
            // onto L and R — the usual mono→FX insert. Distinct from poly-voice broadcast above.
            // A collapsed Key jack (PortRole::Sidechain) is the same stereo pair shape.
            const bool destIsStereoPair = d.role == PortRole::Audio || d.role == PortRole::Sidechain;
            if (s.voiceSpan == 1 && d.voiceSpan == 2 && destIsStereoPair) {
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
            if (s.voiceSpan == 1 && d.voiceSpan == 2 && destIsStereoPair && s.role == PortRole::Audio &&
                source != nullptr && source->isDualIO() && source->rightAudioLegChannel() == s.rawHeadChannel + 1) {
                voiceCount = 2;
                sourceStride = 1;
            }

            // Collapsed stereo source (span 2) dropped on dest jack 0: fan L→L / R→R when the
            // dest actually has a second audio channel (Audio Output, Dual I/O). A mono Filter
            // jack 0 must not steal its Cutoff CV on ch1.
            if (s.voiceSpan == 2 && s.role == PortRole::Audio && d.voiceSpan == 1) {
                const int rightLeg = stereoPairRightLeg(dest, d.rawHeadChannel, destVisibleJack);
                if (rightLeg >= 0) {
                    voiceCount = 2;
                    sourceStride = 1;
                    destStride = rightLeg - d.rawHeadChannel;
                }
            }

            link = {s.rawHeadChannel, d.rawHeadChannel, voiceCount, sourceStride, destStride};
        }
    }

    // Graph I/O nodes are not ModuleBase: a collapsed stereo source dropped on Audio Output Left
    // should fan L→L and R→R rather than leave the right leg silent.
    //
    // A genuinely MONO source dropped on Audio Output Left would otherwise broadcast onto both raw
    // legs here (sourceStride 0). Render-time
    // L/Mono normalling (AudioEngine::refreshNormalling) replaces that broadcast: a mono cable now
    // lands on Left alone, and Right borrows Left at render time for as long as Right stays
    // unpatched -- audible normalling, never an edge. Not broadcasting is what lets the
    // reconnect-the-chain heal land on the SAME single leg a manual mono cable would.
    if (dest == nullptr && destVisibleJack == 0) {
        for (const auto& s : sourceTargets) {
            if (s.role == PortRole::Audio && s.voiceSpan == 2) {
                return PolyLink{s.rawHeadChannel, 0, 2, 1};
            }
        }
    }

    return link;
}

namespace {
juce::Component::SafePointer<juce::Component>& focusBeforeCableDrag();

// Whether a release at `screenPos` lands on a module card before any macro card: endConnectionDrag walks the cards'
// jacks and knobs first, then a plugin card's body. Hit tests only (no modulation slot claimed, no picker opened), so
// the drag tick can ask it every move.
bool moduleCardTakesCable(const juce::OwnedArray<ModuleComponent>& cards, const ModuleComponent* source,
                          juce::Point<int> screenPos, bool fromInput, bool isMidi) {
    for (auto* comp : cards) {
        if (comp == nullptr || !comp->isVisible() || comp == source)
            continue;
        const auto local = comp->getLocalPoint(nullptr, screenPos);
        auto port = comp->getPortForPoint(local);
        if (!port && !fromInput && !isMidi)
            port = comp->getModTargetPortForPoint(local);
        if (port && port->isInput != fromInput && port->isMidi == isMidi)
            return true;
    }
    if (fromInput || isMidi)
        return false;
    for (auto* comp : cards) // offerHostedModTarget: the first card under the point decides
        if (comp != nullptr && comp->isVisible() && comp != source &&
            comp->getLocalBounds().contains(comp->getLocalPoint(nullptr, screenPos))) {
            auto* hosted = dynamic_cast<synth::HostedPluginModule*>(comp->getModule());
            return hosted != nullptr && hosted->hasInstance();
        }
    return false;
}

// The hint on each folded card: only the card `drop` lands on shows one, and a refused drop shows none.
void showMacroCardDropHints(juce::OwnedArray<MacroCardComponent>& cards,
                            const std::optional<MacroGroupController::CardCableDrop>& drop) {
    using Kind = MacroGroupController::CardCableDrop::Kind;
    for (auto* card : cards) {
        if (card == nullptr)
            continue;
        std::optional<MacroCardComponent::CableDropHint> hint;
        if (drop.has_value() && drop->card == card && drop->kind != Kind::refused)
            hint = MacroCardComponent::CableDropHint{drop->kind == Kind::newPort, drop->portIsInput, drop->portUuid,
                                                     drop->kind == Kind::existingPort ? drop->visibleJack : -1};
        card->setCableDropHint(hint);
    }
}

// The ports `macroId` has now, to tell which one a drop made.
std::vector<juce::String> macroPortUuids(const synth::MacroSet& macros, const juce::String& macroId) {
    std::vector<juce::String> out;
    if (const auto* macro = macros.find(macroId))
        for (const auto& port : macro->ports)
            out.push_back(port.nodeUuid);
    return out;
}

// Grows in, on the macro's card, each port that is in `macroId` now but was not in `before`.
void growInNewMacroPorts(juce::OwnedArray<MacroCardComponent>& cards, const synth::MacroSet& macros,
                         const juce::String& macroId, const std::vector<juce::String>& before) {
    for (auto* card : cards)
        if (card != nullptr && card->getMacroId() == macroId)
            for (const auto& uuid : macroPortUuids(macros, macroId))
                if (std::find(before.begin(), before.end(), uuid) == before.end())
                    card->growInPort(uuid);
}
} // namespace

bool GraphEditor::isCableDragFrom(const ModuleComponent& card) const noexcept {
    return isDraggingConnection && dragSourceModule == &card;
}

void GraphEditor::beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                                      juce::Point<int> screenPos) {
    isDraggingConnection = true;
    dragSourceModule = sourceModule;
    dragSourceChannel = channelIndex;
    dragSourceIsInput = isInput;
    dragSourceIsMidi = isMidi;
    dragCurrentPos = screenPos;
    // Escape has to reach GraphEditor::keyPressed even when a card's control held the focus; the drag ends by giving it
    // back.
    focusBeforeCableDrag() = juce::Component::getCurrentlyFocusedComponent();
    grabKeyboardFocus();
    maybeShowModDropHint(sourceModule, channelIndex, isInput, isMidi);
    repaintCanvas();
}

// The first time a cable drag starts from a modulation source's OUTPUT, point out that it
// can be dropped straight on a knob -- never again once shown (persisted through propertiesFile_,
// same as the macro recolour favourites shelf and the piano roll's scale assist). An audio/MIDI
// output, or any INPUT drag (disconnect-and-redrag), never qualifies.
void GraphEditor::maybeShowModDropHint(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi) {
    if (isInput || isMidi || sourceModule == nullptr || propertiesFile_ == nullptr)
        return;
    if (propertiesFile_->getBoolValue(synth::kModDropHintShownSettingKey, false))
        return;

    auto* mb = dynamic_cast<ModuleBase*>(sourceModule->getModule());
    if (mb == nullptr)
        return;
    const auto targets = mb->getJackTargets(channelIndex, false);
    const bool isModSource =
        std::any_of(targets.begin(), targets.end(), [](const auto& t) { return t.role == PortRole::ModCV; });
    if (!isModSource)
        return;

    if (onStatusMessage)
        onStatusMessage("Drop it on any knob to modulate that parameter");
    propertiesFile_->setValue(synth::kModDropHintShownSettingKey, true);
    propertiesFile_->saveIfNeeded();
}

void GraphEditor::dragConnection(juce::Point<int> screenPos) {
    if (!isDraggingConnection)
        return;
    dragCurrentPos = screenPos;

    // Ring whichever knob the cable would land on, so a Serum-style mod drop is aimed rather
    // than guessed at. Only a cable dragged FROM an output can land on a knob. A folded More row
    // under the cable opens first, so a hidden parameter's knob can take it too.
    for (auto* comp : content.getModules()) {
        int target = -1;
        if (!dragSourceIsInput && !dragSourceIsMidi && comp != dragSourceModule) {
            const auto localPos = comp->getLocalPoint(nullptr, screenPos);
            comp->unfoldMoreRowForCableDrag(localPos);
            if (auto* body = comp->getCardBody())
                body->cableDragMoved(comp->getLocalBounds().contains(localPos)); // leaving folds a row the cable opened
            if (!comp->getPortForPoint(localPos)) {
                if (auto port = comp->getModTargetPortForPoint(localPos))
                    target = port->index;
                else if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(comp->getModule());
                         hosted != nullptr && hosted->hasInstance() && comp->getLocalBounds().contains(localPos))
                    target = ModuleComponent::kHostedCardBodyDropChannel;
            }
        }
        comp->setModDropTargetChannel(target);
    }

    // A folded macro's card under the cable shows where the drop would land, from the drop's own answer.
    std::optional<MacroGroupController::CardCableDrop> macroDrop;
    if (dragSourceModule != nullptr &&
        !moduleCardTakesCable(content.getModules(), dragSourceModule, screenPos, dragSourceIsInput, dragSourceIsMidi))
        macroDrop = macroController_.cardCableDropAt(screenPos, dragSourceModule->getNodeId(), dragSourceChannel,
                                                     dragSourceIsInput, dragSourceIsMidi);
    showMacroCardDropHints(content.getMacroCards(), macroDrop);

    repaintCanvas();
}

namespace {
// A drop on a plugin knob or a pad knob that is not modulated yet: claim a modulation slot for its parameter, so the
// cable has a channel to land on. Claimed before the cable's undo step opens, so undo leaves the slot claimed with no
// cable; AudioEngine::releaseUnconnectedHostedModSlots frees it on the next rebuild. Every slot taken: the drop is
// refused.
std::optional<ModuleComponent::Port> claimHostedModSlot(ModuleComponent& card, ModuleComponent::Port port,
                                                        const std::function<void(const juce::String&)>& status) {
    auto* host = dynamic_cast<synth::ModSlotHost*>(card.getModule());
    const int channel = host != nullptr ? host->bindModSlot(card.hostedParamForDropChannel(port.index)) : -1;
    if (channel < 0) {
        if (host != nullptr && status)
            status(card.cardTitle() + " already has " + juce::String(host->maxModSlots()) +
                   (port.index <= ModuleComponent::kUnboundPadKnobChannelBase ? " modulated pad knobs"
                                                                              : " modulated parameters"));
        return std::nullopt;
    }
    port.index = channel;
    return port;
}

// A cable from `source` released over a plugin card's body (the knobs were checked first): open the parameter search
// there. True when a picker opened, so no other drop rule runs; the cable lands only if the person picks.
bool offerHostedModTarget(GraphEditor& editor, ModuleComponent& source, int sourceJack, juce::Point<int> screenPos) {
    for (auto* card : editor.getModuleComponents()) {
        if (card == nullptr || !card->isVisible() || card == &source ||
            !card->getLocalBounds().contains(card->getLocalPoint(nullptr, screenPos)))
            continue;
        auto* hosted = dynamic_cast<synth::HostedPluginModule*>(card->getModule());
        if (hosted == nullptr || !hosted->hasInstance())
            return false;
        synth::ui::showHostedModTargetPicker(editor, *card, source.getNodeId(), sourceJack, screenPos);
        return true;
    }
    return false;
}
} // namespace

namespace {
// The control that held the keyboard when the cable was picked up (one cable at a time, message thread only).
juce::Component::SafePointer<juce::Component>& focusBeforeCableDrag() {
    static juce::Component::SafePointer<juce::Component> focused;
    return focused;
}

// Gives the keyboard back to what had it, unless the drag or the drop has already moved it somewhere on purpose.
void restoreFocusAfterCableDrag(juce::Component& editor) {
    auto previous = std::exchange(focusBeforeCableDrag(), nullptr);
    if (previous != nullptr && previous->isShowing() && editor.hasKeyboardFocus(false))
        previous->grabKeyboardFocus();
}

// A More row this cable drag opened, and whether the drop is on a control of that row (which keeps it open).
struct CableOpenedRow {
    juce::Component::SafePointer<ModuleComponent> card;
    bool dropOnRevealedControl = false;
};

std::vector<CableOpenedRow> cableOpenedRows(const juce::OwnedArray<ModuleComponent>& cards,
                                            juce::Point<int> screenPos) {
    std::vector<CableOpenedRow> rows;
    for (auto* card : cards)
        if (auto* body = card->getCardBody(); body != nullptr && body->isMoreOpenedByCableDrag())
            rows.push_back({card, body->moreRowWidgetAt(card->getLocalPoint(nullptr, screenPos))});
    return rows;
}

// The drag is over: every row it opened folds, except the one whose control took the cable (`landedOn`), which
// stays and is the person's from now on. A card the connection rebuilt is gone from `rows` (its pointer is null).
void settleCableOpenedRows(const std::vector<CableOpenedRow>& rows, const ModuleComponent* landedOn) {
    for (const auto& row : rows)
        if (auto* card = row.card.getComponent(); card != nullptr && card->getCardBody() != nullptr)
            card->getCardBody()->cableDragEnded(row.dropOnRevealedControl && card == landedOn);
}
} // namespace

// Drops the pending drop-target highlight on every card: a knob's ring and a folded macro's drop hint (which fades).
void GraphEditor::clearModDropTargets() {
    for (auto* comp : content.getModules())
        comp->setModDropTargetChannel(-1);
    for (auto* card : content.getMacroCards())
        if (card != nullptr)
            card->setCableDropHint(std::nullopt);
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

    // Inside the cable's own undo step, and before the link is resolved: a module with no cables yet joins the poly
    // state of the module it is first cabled to (PolyChainController::joinConnectedModules), which decides the fan.
    auto doConnect = [this, &graph, srcId, dstId, srcJack, dstJack, isMidi] {
        polyChain_->joinConnectedModules(srcId, dstId);
        auto* srcModuleBase = dynamic_cast<ModuleBase*>(graph.getNodeForId(srcId)->getProcessor());
        auto* dstModuleBase = dynamic_cast<ModuleBase*>(graph.getNodeForId(dstId)->getProcessor());

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

        if (isMidi) {
            graph.addConnection({{srcId, juce::AudioProcessorGraph::midiChannelIndex},
                                 {dstId, juce::AudioProcessorGraph::midiChannelIndex}});
        } else if (isCV) {
            audioEngine.addModRouting(srcId, link.sourceRawChannel, dstId, link.destRawChannel);
        } else {
            for (int v = 0; v < link.voiceCount; ++v)
                graph.addConnection({{srcId, link.sourceChannel(v)}, {dstId, link.destChannel(v)}});
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

    const auto openedRows = cableOpenedRows(content.getModules(), screenPos);
    const ModuleComponent* landedOn = nullptr; // the card whose jack or knob took the cable
    bool connectedToAModule = false;           // gates the macro-card fallback below — unchanged loop otherwise
    juce::String refusal; // why a jack under the cursor was passed over, said when nothing connects

    // A connection can rebuild the cards (a macro port minted by the drop, a Dual I/O change, a Track In's auto
    // channel all reach updateComponents), which frees and reallocates the very array a walk over
    // content.getModules() reads. So the candidates are copied out first as weak pointers, top-most card first
    // (the one the person sees under the cursor), and the walk stops at the first connection: a drop lands on one
    // jack, and nothing touches a card or the array after the call that may have rebuilt them.
    std::vector<juce::Component::SafePointer<ModuleComponent>> candidates;
    for (int i = content.getNumChildComponents(); --i >= 0;)
        if (auto* card = dynamic_cast<ModuleComponent*>(content.getChildComponent(i)))
            candidates.emplace_back(card);
    const juce::Component::SafePointer<ModuleComponent> sourceCard(dragSourceModule);

    for (const auto& candidate : candidates) {
        auto* comp = candidate.getComponent();
        // A hidden member of a collapsed macro is not "on the canvas" as a drop target (same
        // reasoning as collectModuleBoxes' marquee/group-drag exclusion) — it is kept alive only
        // so its position tracks the card, and its ORIGINAL bounds (group-bounds top-left, see
        // groupSelectionIntoMacro) sit directly underneath the card, including its own jacks at
        // the same left/right inset a card jack now uses. Without this guard a release on
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
            if (port && port->index <= ModuleComponent::kUnboundHostedKnobChannelBase)
                port = claimHostedModSlot(*comp, *port, onStatusMessage);
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
            if (port->isInput == dragSourceIsInput) {
                refusal = "Connect an output to an input";
                continue;
            }
            if (port->isMidi != dragSourceIsMidi) {
                refusal = "MIDI jacks connect only to MIDI jacks; audio and CV jacks to audio and CV jacks";
                continue;
            }

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

                // The drop rules (poly fan, MIDI, attenuverter for mono CV, macro auto ports, a Track In's auto
                // channel) live in PortConnector, shared with the port connections panel's Add destination so they
                // cannot drift.
                synth::ui::PortConnector::connectJacks(*this, realSrc->nodeID, srcJack, realDst->nodeID, dstJack,
                                                       dragSourceIsMidi, /*recordUndo=*/true,
                                                       content.getLocalPoint(nullptr, screenPos).toFloat());
                connectedToAModule = true;
                landedOn = comp;
                break;
            }
        }
    }
    if (sourceCard == nullptr)
        dragSourceModule = nullptr; // the connection rebuilt the card the drag started on

    // A modulation cable released on a plugin card's body, not on a knob: pick any of the plugin's parameters.
    if (!connectedToAModule && dragSourceModule != nullptr && !dragSourceIsInput && !dragSourceIsMidi)
        connectedToAModule = offerHostedModTarget(*this, *dragSourceModule, dragSourceChannel, screenPos);

    // Macro card drop (docs/macros/ports.md#a-cable-hovering-a-folded-card): nothing above matched (no module jack
    // under the cursor), so a release over a FOLDED macro's card lands where cardCableDropAt says, the same answer the
    // drag's hint showed. On an existing port's jack the cable wires into that port's own node and follows the SAME
    // rules as a drop on a module's own jack (PortConnector::connectJacks: a source inside another macro leaves
    // through its own ports, a Track In's auto channel, the poly fan), and a port with nothing behind it yet is joined
    // to the module it was made for (connectEmptyPortInterior), both legs for a two-jack Stereo port. The row dropped
    // on picks the port's jack (Left 0, Right 1); a one-row port is its jack 0. Anywhere else on the card a fresh port
    // is shaped from the dropped cable (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed); a
    // port that already carries this cable takes it again and nothing new is made. A refused drop does nothing.
    // The cable then slides from the release point to the port's jack instead of appearing there
    // (docs/macros/ports.md#a-cable-dropped-on-a-folded-card): armed once, after the edit and outside its undo record,
    // so a redo does not slide it again, and after the card glide's Scope so the slide ends on the card's final jacks.
    if (!connectedToAModule && dragSourceModule != nullptr) {
        using DropKind = MacroGroupController::CardCableDrop::Kind;
        const auto drop = macroController_.cardCableDropAt(screenPos, dragSourceModule->getNodeId(), dragSourceChannel,
                                                           dragSourceIsInput, dragSourceIsMidi);
        if (drop.has_value() && drop->kind != DropKind::refused) {
            drop->card->setCableDropHint(std::nullopt, /*instant=*/true);
            connectedToAModule = true; // the card took the cable: a jack passed over on the way is not a refusal
            const auto srcId = dragSourceModule->getNodeId();
            const auto dropPoint = content.getLocalPoint(nullptr, screenPos).toFloat();
            repaintCanvas(); // the slide diffs against the cables as drawn before the drop
            const auto cablesBefore = rebuildVisibleCables();
            // The Scope lives through both branches: a card that grows a row glides to its new height.
            if (CardGlideAnimator::Scope glide(cardGlide_); drop->kind == DropKind::existingPort) {
                const bool toInput = drop->portIsInput;
                const auto portNodeId = drop->portNodeId;
                const int portJack = std::max(drop->visibleJack, 0);
                const auto connSrcId = toInput ? srcId : portNodeId;
                const auto connDstId = toInput ? portNodeId : srcId;
                const int connSrcJack = toInput ? dragSourceChannel : portJack;
                const int connDstJack = toInput ? portJack : dragSourceChannel;
                const auto macroId = drop->macroId;
                auto doDrop = [this, macroId, portNodeId, connSrcId, connSrcJack, connDstId, connDstJack] {
                    synth::ui::PortConnector::connectJacks(*this, connSrcId, connSrcJack, connDstId, connDstJack,
                                                           dragSourceIsMidi, /*recordUndo=*/false);
                    macroController_.connectEmptyPortInterior(macroId, portNodeId);
                    updateComponents();
                };
                if (undoManager)
                    undoManager->recordGraphAndMacroChange(audioEngine.getGraph(), macros, doDrop);
                else
                    doDrop();
            } else if (drop->kind == DropKind::newPort) {
                const auto portsBefore = macroPortUuids(macros, drop->macroId);
                macroController_.createMacroPortFromDroppedCable(drop->macroId, drop->portIsInput, dragSourceIsMidi,
                                                                 srcId, dragSourceChannel);
                growInNewMacroPorts(macroCards(), macros, drop->macroId, portsBefore); // the new row grows in
            }
            armMacroPortSlide(cablesBefore, dropPoint);
        }
    }

    // A drop on a jack that cannot take this cable is not silent: a toast says why.
    if (!connectedToAModule && refusal.isNotEmpty())
        onDropRefused(refusal);

    settleCableOpenedRows(openedRows, landedOn);
    restoreFocusAfterCableDrag(*this);
    isDraggingConnection = false;
    dragSourceModule = nullptr;
    clearModDropTargets();
    repaintCanvas();
}
