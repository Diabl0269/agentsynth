#include "GraphEditor.h"
#include "../AI/AIStateMapper.h"
#include "../Modules/ADSRModule.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/AudioInputModule.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/FX/ChorusModule.h"
#include "../Modules/FX/CompressorModule.h"
#include "../Modules/FX/DelayModule.h"
#include "../Modules/FX/DistortionModule.h"
#include "../Modules/FX/FlangerModule.h"
#include "../Modules/FX/LimiterModule.h"
#include "../Modules/FX/PhaserModule.h"
#include "../Modules/FX/ReverbModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/LFOModule.h"
#include "../Modules/MacroControlModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "../Modules/NoiseModule.h"
#include "../Modules/OscillatorModule.h"
#include "../Modules/PolyMidiModule.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SampleHoldModule.h"
#include "../Modules/SamplerModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/VCAModule.h"
#include "../Modules/VoiceMixerModule.h"
#include "../Modules/WavetableOscillatorModule.h"
#include "../Plugin/Hosting/HostedPluginModule.h"
#include "../PresetManager.h"
#include "../SnippetManager.h"
#include "LayoutUtil.h"
#include "MacroCardComponent.h"
#include "ModuleComponent.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

// Returns an estimated (w, h) footprint for a module type name.
// Used when the component does not yet exist (e.g. on drag-drop before layout).
// Heights match the real component sizes so the library-drag ghost preview is accurate.
// The final drop placement uses the real component size via finalizeModuleDrag() — the
// estimate is only used for the live ghost preview.
// Heights are measured from the real components, not guessed — see
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents, which constructs every type and
// fails if this table drifts from what layoutDefaultContent() actually produces.
juce::Point<int> GraphEditor::estimateModuleSize(const juce::String& typeName) {
    if (typeName == "Oscillator")
        return {280, 533}; // +96 in #219: an Audio R output jack row and the Pan knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Filter")
        // +1 knob row: the Level knob took it from 3 sliders to 4 (issue #122).
        // +20 in #219: the Audio L/R input pair adds a jack row to the port gutter.
        // −128: frequency-response chart is opt-in via "Show Response" (was always reserved).
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 443};
    if (typeName == "LFO")
        return {280, 361}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "VCA")
        return {280, 253}; // +20 in #219: the Audio L/R input pair adds a jack row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "ADSR" || typeName == "Amp Env" || typeName == "Filter Env")
        return {280, 339}; // sliders below 2 jacks + threshold control + Poly toggle
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName.containsIgnoreCase("Sequencer") && !typeName.containsIgnoreCase("Poly"))
        // +26 (one toggle row) for the Sync to Transport switch, appended below the step grid.
        return {synth::LayoutUtil::kDoubleWidth, 406};
    if (typeName.containsIgnoreCase("Poly") && typeName.containsIgnoreCase("Sequencer"))
        // +26 (one toggle row) for the Sync to Transport switch, appended below the step grid.
        return {synth::LayoutUtil::kDoubleWidth, 406};
    if (typeName.containsIgnoreCase("MidiKeyboard") || typeName.containsIgnoreCase("Midi Keyboard") ||
        typeName.containsIgnoreCase("MIDI Keyboard"))
        return {synth::LayoutUtil::kDoubleWidth, 150};
    if (typeName == "Poly MIDI" || typeName == "PolyMidi")
        // +48 (one combo row) from the issue #198 Voice Steal selector, then +26 (one toggle row)
        // for the Vel → Gate switch. +8: header-to-first-port gap grew 1px -> 9px (base 30->38).
        return {280, 185};
    if (typeName == "Distortion")
        return {280, 323}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Ring Modulator")
        return {280, 391}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Delay")
        return {280, 237}; // Dual I/O off: one Audio jack (not L/R) + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Reverb")
        return {280, 237}; // Dual I/O off: one Audio jack (not L/R) + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "AudioInput" || typeName == "Audio Input")
        // Height tracks the DEVICE's input channel count at runtime (one jack per channel, up to
        // AudioInputModule::kMaxChannels — eight jacks measure 217px, pinned by
        // AudioInputModuleTest.DeviceShrinkDropsHiddenRoutings), exactly like the Macro bank tracks
        // its knob count. The drop estimate uses the resting card, which the 100px floor in
        // updateLayout sets for anything up to two jacks; finalizeNewDrop re-resolves the placement
        // against the real component size anyway.
        return {280, 100};
    if (typeName == "AudioOutput" || typeName == "Audio Output")
        return {280, 100};
    if (typeName == "Attenuverter")
        return {synth::LayoutUtil::kNarrowWidth, synth::LayoutUtil::kNarrowWidth};
    if (typeName == "Noise")
        return {280, 281}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Envelope Follower")
        // Noise's control count (3 floats + a choice) plus a taller port gutter for 4 input jacks.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 295};
    if (typeName == "Math")
        return {280, 239}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Sample & Hold")
        return {280, 551}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Comparator")
        return {280, 185}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Macros")
        // Height tracks the bank's "Knobs" count at runtime; the drop estimate uses the default.
        return {synth::LayoutUtil::kSingleWidth,
                synth::LayoutUtil::macroBankHeight(MacroControlModule::kDefaultMacros)};
    if (typeName == "Sampler")
        return {280, 645}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Wavetable")
        // Double-width since issue #180. The 16 CV jacks run in two left-hand columns and the 23
        // controls are paged behind a tab strip (only Position and Warp stay pinned), so neither
        // the gutter nor the control count sets the height on its own.
        return {synth::LayoutUtil::kDoubleWidth, 554};
    if (typeName == "Chorus" || typeName == "Phaser" || typeName == "Flanger")
        return {280, 277}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Bitcrusher")
        return {280, 323}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Pitch Shifter")
        return {280, 467}; // Dual I/O off: one Audio jack + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Parametric EQ")
        // Double-width card: a 150px response curve set between the port-label gutters, then a
        // 4-column band grid (on/off + Freq/Gain/Q). Mirrors parametricEQHeight().
        return {synth::LayoutUtil::kDoubleWidth, 592};
    if (typeName == "Compressor")
        return {280, 237}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Limiter")
        return {280, 161}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Voice Mixer")
        return {280, 301}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "External MIDI")
        return {280, 146}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Track In")
        // Param-less card (only the inherited bypass, which lives in the header), no jacks: the
        // 100 px floor in updateLayout is what sets the height. Not in the library — the timeline's
        // add-track flow places it — so this only ever feeds a programmatic size query.
        return {280, 100};
    if (typeName == "Rec Tap")
        // Like Track In it has no body controls (only the inherited bypass, which lives in the
        // header), but it has two jacks a side, so the port gutter — not the 100 px floor — sets
        // the height. Also library-less: the record flow places it. Measured against the real card
        // by RecordTapTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 131};
    if (typeName == "Track Audio")
        // Same shape as Rec Tap — no body controls, jacks setting the height — but with outputs
        // only. Library-less like the other two internal nodes: the add-track flow places it.
        // Measured against the real card by
        // AudioClipPlaybackTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 131};
    if (typeName == "Hosted Plugin")
        // Bypass and mute live in the header; the only body content is the "Open Editor" button,
        // one jack a side while empty. The card grows with the loaded plugin's real port count,
        // like the Macro bank and Audio Input; the estimate is the resting size, and
        // finalizeNewDrop re-resolves against the real component anyway. Library-less until the
        // scan list and load UX ship. Measured against the real card by
        // HostedPluginTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 131};
    return {280, 360};
}

GraphEditor::GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr)
    : audioEngine(engine)
    , content(*this)
    , modMatrix(engine, undoMgr)
    , undoManager(undoMgr) {
    addAndMakeVisible(content);
    addAndMakeVisible(modMatrix);
    content.setInterceptsMouseClicks(false, true); // Fallback clicks to parent

    // Minimap (issue #159): visibility is driven by setMinimapVisible(), called by the owner once
    // it has restored the persisted preference — NOT addAndMakeVisible, which would show it before
    // that preference is known.
    addChildComponent(minimap);
    minimap.setVisible(minimapVisible);
    minimap.onNavigate = [this](juce::Point<float> p) { centreViewOn(p); };
    minimap.onZoom = [this](float d) { zoomAroundCentre(d); };

    // Tooltips on GraphEditor-owned affordances.
    // The canvas itself hints at pan/zoom. Double-click on an attenuverter knob removes it.
    setTooltip(synth::ui::formatShortcutHint("Patch canvas - drag modules here to build your patch",
                                             "Scroll to zoom | Drag to pan | Shift+drag to select | Double-click mod "
                                             "knob to remove"));

    // Needed for the canvas-scoped Delete/Escape keys (see keyPressed).
    setWantsKeyboardFocus(true);

    startTimerHz(30);
}

GraphEditor::~GraphEditor() { stopTimer(); }

GraphEditor::GraphContentComponent::GraphContentComponent(GraphEditor& ed)
    : editor(ed) {
    // The canvas fills its whole bounds opaquely (bg1 + grid), so tell JUCE not to repaint
    // whatever is behind it on every frame — a real win for zoom/pan and the 30Hz wire animation.
    setOpaque(true);
}

// ============================================================================
// Cables (issue #157)
//
// One enumeration feeds both painting and hit-testing. Keeping them separate was the obvious
// shortcut and the wrong one: the drawn curve and the clickable curve would drift apart the
// first time either was tweaked, and clicks would silently miss the wire.
// ============================================================================

juce::Path GraphEditor::buildCablePath(juce::Point<float> p1, juce::Point<float> p2) {
    juce::Path wp;
    const float dx = p2.x - p1.x;
    wp.startNewSubPath(p1);
    wp.cubicTo(p1.x + dx * 0.5f, p1.y, p2.x - dx * 0.5f, p2.y, p2.x, p2.y);
    return wp;
}

float GraphEditor::distanceToCable(const VisibleCable& cable, juce::Point<float> canvasPos) {
    auto path = buildCablePath(cable.p1, cable.p2);
    juce::Point<float> nearest;
    // getNearestPoint returns the distance ALONG the path; the perpendicular distance we want is
    // from the cursor to the point it writes out.
    path.getNearestPoint(canvasPos, nearest);
    return canvasPos.getDistanceFrom(nearest);
}

std::optional<GraphEditor::VisibleCable> GraphEditor::getCableAt(juce::Point<float> canvasPos, float tolerance) {
    std::optional<VisibleCable> best;
    float bestDist = tolerance;
    for (const auto& c : buildVisibleCables()) {
        const float d = distanceToCable(c, canvasPos);
        // '<=' so a later cable wins a tie, matching paint order (mod wires draw over audio).
        if (d <= bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

namespace {
// Category of the module a cable leaves from. Unknown/!ModuleBase nodes fall back to Utility so
// BySourceCategory mode always has a colour to use.
synth::ui::ModuleCategory categoryForNode(juce::AudioProcessorGraph::Node* node) {
    if (node != nullptr)
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            return synth::ui::categoryFor(mb->getModuleType());
    return synth::ui::ModuleCategory::Utility;
}
} // namespace

const std::vector<GraphEditor::VisibleCable>& GraphEditor::buildVisibleCables() {
    if (!cablesCacheValid) {
        cablesCache = rebuildVisibleCables();
        cablesCacheValid = true;
        ++cableRebuildCount;
    }
    return cablesCache;
}

void GraphEditor::repaintCanvas() {
    cablesCacheValid = false;
    content.repaint();
}

std::vector<GraphEditor::VisibleCable> GraphEditor::rebuildVisibleCables() {
    std::vector<VisibleCable> cables;
    auto& graph = audioEngine.getGraph();
    auto& moduleComponents = content.getModules();

    // nodeID -> component, so both passes resolve port positions in O(1).
    std::unordered_map<uint32_t, ModuleComponent*> nodeCompMap;
    for (auto* comp : moduleComponents) {
        if (comp == nullptr)
            continue;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor() == comp->getModule()) {
                nodeCompMap[node->nodeID.uid] = comp;
                break;
            }
        }
    }
    auto compFor = [&](juce::AudioProcessorGraph::NodeID id) -> ModuleComponent* {
        auto it = nodeCompMap.find(id.uid);
        return it == nodeCompMap.end() ? nullptr : it->second;
    };
    auto portPos = [](ModuleComponent* c, int port, bool isInput) {
        return (c->getBounds().getPosition() + c->getPortCenter(port, isInput)).toFloat();
    };
    auto channelExposedOnJack = [](ModuleBase* mb, int rawChannel, bool isInput) {
        const int visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
        for (int j = 0; j < visible; ++j)
            for (const auto& t : mb->getJackTargets(j, isInput))
                for (int v = 0; v < t.voiceSpan; ++v)
                    if (t.rawHeadChannel + v == rawChannel)
                        return true;
        return false;
    };

    // Edges the mod-routing pass will draw; pass 1 must not draw them again in a conflicting
    // style. AttenuverterChain edges are deliberately excluded — pass 1 owns those.
    using EdgeKey = std::tuple<uint32_t, int, uint32_t, int>;
    std::set<EdgeKey> pass2HandledEdges;
    for (const auto& routing : cachedModRoutings) {
        if (routing.kind == AudioEngine::RoutingKind::DirectCV) {
            pass2HandledEdges.emplace(routing.sourceNodeID.uid, routing.sourceChannelIndex, routing.destNodeID.uid,
                                      routing.destChannelIndex);
        } else if (routing.kind == AudioEngine::RoutingKind::PolyBus) {
            for (int i = 0; i < routing.voiceCount; ++i)
                pass2HandledEdges.emplace(routing.sourceNodeID.uid, routing.sourceChannelIndex + i,
                                          routing.destNodeID.uid, routing.destChannelIndex + i);
        }
    }

    // ---- Pass 1: raw graph edges (audio, MIDI, attenuverter chains) ----
    // Hoisted once: getConnections() returns std::vector<Connection> BY VALUE, so calling it again
    // inside the attenuverter scan below would copy the whole edge list per attenuverter-terminated
    // edge (O(E^2) with allocation). Reuse this copy for both passes.
    const auto connections = graph.getConnections();
    for (auto& connection : connections) {
        auto* node1 = graph.getNodeForId(connection.source.nodeID);
        auto* node2 = graph.getNodeForId(connection.destination.nodeID);
        if (!node1 || !node2)
            continue;

        const bool srcIsMidi = connection.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex;
        const bool dstIsMidi = connection.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex;

        // Map raw graph channels onto visible jacks. A collapsed Dual I/O "Audio" jack still
        // owns raw ch1 (voiceSpan 2); treating `raw >= visibleCount` as hidden left the Right
        // cable undrawn whenever only one end of the wire was split.
        // Follower-to-follower edges (poly voices 1–7, collapsed stereo's right leg when BOTH
        // ends are still a single Audio jack) stay suppressed — the head cable / PolyBus
        // already represents them.
        int srcJack = connection.source.channelIndex;
        int dstJack = connection.destination.channelIndex;
        bool srcHead = true;
        bool dstHead = true;
        if (!srcIsMidi) {
            if (auto* srcMb = dynamic_cast<ModuleBase*>(node1->getProcessor())) {
                if (!channelExposedOnJack(srcMb, connection.source.channelIndex, false))
                    continue;
                const auto p = srcMb->mapOutputChannel(connection.source.channelIndex);
                srcJack = p.visibleJackIndex;
                srcHead = p.isPolyGroupHead;
            }
        }
        if (!dstIsMidi) {
            if (auto* dstMb = dynamic_cast<ModuleBase*>(node2->getProcessor())) {
                if (!channelExposedOnJack(dstMb, connection.destination.channelIndex, true))
                    continue;
                const auto p = dstMb->mapInputChannel(connection.destination.channelIndex);
                dstJack = p.visibleJackIndex;
                dstHead = p.isPolyGroupHead;
            }
        }
        if (!srcIsMidi && !dstIsMidi && !srcHead && !dstHead)
            continue;

        if (!srcIsMidi && !dstIsMidi) {
            EdgeKey ek{connection.source.nodeID.uid, connection.source.channelIndex, connection.destination.nodeID.uid,
                       connection.destination.channelIndex};
            if (pass2HandledEdges.count(ek))
                continue;
        }

        // Attenuverter chain: source -> atten -> real destination, drawn as ONE wire.
        if (dynamic_cast<AttenuverterModule*>(node2->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::Node* realDstNode = nullptr;
            int realDstPort = 0;
            for (auto& c : connections) {
                if (c.source.nodeID == node2->nodeID) {
                    realDstNode = graph.getNodeForId(c.destination.nodeID);
                    realDstPort = c.destination.channelIndex;
                    break;
                }
            }
            if (realDstNode == nullptr)
                continue;

            auto* srcComp = compFor(node1->nodeID);
            auto* dstComp = compFor(realDstNode->nodeID);
            if (srcComp == nullptr || dstComp == nullptr)
                continue;

            VisibleCable cable;
            cable.kind = VisibleCable::Kind::AttenuverterChain;
            cable.id = {node1->nodeID.uid, connection.source.channelIndex, realDstNode->nodeID.uid, realDstPort,
                        node2->nodeID.uid};
            cable.p1 = portPos(srcComp, srcJack, false);
            int realDstJack = realDstPort;
            if (auto* realDstMb = dynamic_cast<ModuleBase*>(realDstNode->getProcessor()))
                realDstJack = realDstMb->mapInputChannel(realDstPort).visibleJackIndex;
            cable.p2 = portPos(dstComp, realDstJack, true);
            cable.signal = synth::ui::CableSignal::ModCV;
            cable.sourceCategory = categoryForNode(node1);

            for (auto& info : cachedModDisplayInfo) {
                if (info.attenuverterNodeID == node2->nodeID) {
                    cable.activity = info.modSignalPeak;
                    break;
                }
            }
            if (auto* p = findParameterByID(node2->getProcessor(), "amount"))
                cable.attenAmount = p->getValue() * 2.0f - 1.0f; // 0..1 -> -1..1

            cables.push_back(cable);
            continue;
        }
        if (dynamic_cast<AttenuverterModule*>(node1->getProcessor()) != nullptr)
            continue; // the chain's outgoing edge — already covered above

        auto* srcComp = compFor(node1->nodeID);
        auto* dstComp = compFor(node2->nodeID);
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        VisibleCable cable;
        cable.kind = VisibleCable::Kind::Direct;
        cable.id = {node1->nodeID.uid, connection.source.channelIndex, node2->nodeID.uid,
                    connection.destination.channelIndex, 0};
        cable.p1 = srcIsMidi ? portPos(srcComp, 0, false) : portPos(srcComp, srcJack, false);
        // MIDI inputs have no jack of their own; the wire lands on the card's top-left corner.
        cable.p2 = dstIsMidi ? (dstComp->getBounds().getPosition() + juce::Point<int>(10, 30)).toFloat()
                             : portPos(dstComp, dstJack, true);
        cable.signal = (srcIsMidi || dstIsMidi) ? synth::ui::CableSignal::Midi : synth::ui::CableSignal::Audio;
        cable.sourceCategory = categoryForNode(node1);
        cables.push_back(cable);
    }

    // ---- Pass 2: DirectCV / PolyBus mod routings ----
    for (const auto& routing : cachedModRoutings) {
        if (routing.kind == AudioEngine::RoutingKind::AttenuverterChain)
            continue; // rendered by pass 1

        auto* srcComp = compFor(routing.sourceNodeID);
        auto* dstComp = compFor(routing.destNodeID);
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        VisibleCable cable;
        cable.kind = VisibleCable::Kind::ModRouting;
        cable.id = {routing.sourceNodeID.uid, routing.sourceChannelIndex, routing.destNodeID.uid,
                    routing.destChannelIndex, 0};
        // getPortCenter clamps out-of-range indices to the last visible jack, so these always
        // land on a real rendered port.
        cable.p1 = portPos(srcComp, routing.sourceVisibleJack, false);
        cable.p2 = portPos(dstComp, routing.destVisibleJack, true);

        if (routing.role == PortRole::Pitch)
            cable.signal = synth::ui::CableSignal::Pitch;
        else if (routing.role == PortRole::Gate)
            cable.signal = synth::ui::CableSignal::Gate;
        else
            cable.signal = routing.kind == AudioEngine::RoutingKind::PolyBus ? synth::ui::CableSignal::PolyBus
                                                                             : synth::ui::CableSignal::ModCV;

        cable.sourceCategory = categoryForNode(graph.getNodeForId(routing.sourceNodeID));
        cable.isBypassed = routing.isBypassed;
        cable.activity = routing.modSignalPeak;
        cable.isPolyBus = routing.kind == AudioEngine::RoutingKind::PolyBus;
        cable.voiceCount = routing.voiceCount;
        cables.push_back(cable);
    }

    // ---- Collapsed-macro cable treatment (P8-12) ----
    //
    // A collapsed macro hides its member ModuleComponents (setVisible(false) in syncMacroCards),
    // but their graph edges — and the cables above computed from them — don't know that. A cable
    // wholly inside one collapsed macro is dropped outright (both endpoints are off-screen, and
    // there is nothing useful to draw); a cable crossing a collapsed macro's boundary is
    // re-anchored to the card's own centre point rather than left pointing at a hidden jack.
    if (!macros.empty()) {
        // nodeID.uid -> macro id, built once, collapsed macros only.
        std::unordered_map<uint32_t, const synth::Macro*> collapsedMacroForNode;
        for (const auto& macro : macros.getAll()) {
            if (!macro.collapsed)
                continue;
            for (const auto& uuid : macro.members) {
                auto nodeId = resolveMemberNodeId(uuid);
                if (nodeId.uid != 0)
                    collapsedMacroForNode[nodeId.uid] = &macro;
            }
        }

        if (!collapsedMacroForNode.empty()) {
            std::vector<VisibleCable> filtered;
            filtered.reserve(cables.size());
            for (auto& cable : cables) {
                auto srcIt = collapsedMacroForNode.find(cable.id.srcUid);
                auto dstIt = collapsedMacroForNode.find(cable.id.dstUid);
                const bool srcHidden = srcIt != collapsedMacroForNode.end();
                const bool dstHidden = dstIt != collapsedMacroForNode.end();

                if (srcHidden && dstHidden && srcIt->second == dstIt->second)
                    continue; // wholly inside one collapsed macro — nothing on screen to draw

                if (srcHidden)
                    cable.p1 = srcIt->second->bounds.getCentre().toFloat();
                if (dstHidden)
                    cable.p2 = dstIt->second->bounds.getCentre().toFloat();

                filtered.push_back(cable);
            }
            cables = std::move(filtered);
        }
    }

    return cables;
}

juce::Colour GraphEditor::colourForCable(const VisibleCable& cable) const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    // Headless tests install the stock JUCE LnF; fall back to the token defaults so colour
    // resolution stays exercised rather than short-circuited.
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    return synth::ui::resolveCableColour(cableColourMode, cable.signal, cable.sourceCategory, colors,
                                         cableColourOverrides, cable.isBypassed);
}

void GraphEditor::rememberWavetableFolder(const juce::File& folder) {
    if (folder == lastWavetableFolder)
        return;
    lastWavetableFolder = folder;
    if (onWavetableFolderChanged != nullptr)
        onWavetableFolderChanged(folder);
}

void GraphEditor::setCableColourMode(synth::ui::CableColourMode mode) {
    if (cableColourMode == mode)
        return;
    cableColourMode = mode;
    repaintCanvas();
}

void GraphEditor::setCableColourOverrides(const synth::ui::CableColourOverrides& overrides) {
    cableColourOverrides = overrides;
    repaintCanvas();
}

void GraphEditor::disconnectCable(const VisibleCable& cable) {
    auto& graph = audioEngine.getGraph();

    // An attenuverter chain is a hidden node plus its two edges — removing the routing takes all
    // of it, which is also what double-clicking the knob does.
    if (cable.kind == VisibleCable::Kind::AttenuverterChain) {
        const juce::AudioProcessorGraph::NodeID attenId{cable.id.attenUid};
        if (undoManager)
            undoManager->recordStructuralChange(graph, [this, attenId] { audioEngine.removeModRouting(attenId); });
        else
            audioEngine.removeModRouting(attenId);
        repaintCanvas();
        return;
    }

    const juce::AudioProcessorGraph::NodeID srcId{cable.id.srcUid};
    const juce::AudioProcessorGraph::NodeID dstId{cable.id.dstUid};

    // Expand audio/poly fans via resolvePolyLink so a collapsed stereo (or poly voice) cable that
    // only drew its head edge still removes every raw channel the user-visible wire owns.
    auto removeEdges = [this, &graph, srcId, dstId, cable] {
        auto* srcNode = graph.getNodeForId(srcId);
        auto* dstNode = graph.getNodeForId(dstId);
        auto* srcMb = srcNode ? dynamic_cast<ModuleBase*>(srcNode->getProcessor()) : nullptr;
        auto* dstMb = dstNode ? dynamic_cast<ModuleBase*>(dstNode->getProcessor()) : nullptr;

        if (cable.isPolyBus) {
            const int edgeCount = juce::jmax(1, cable.voiceCount);
            for (int i = 0; i < edgeCount; ++i) {
                juce::AudioProcessorGraph::Connection c{{srcId, cable.id.srcPort + i}, {dstId, cable.id.dstPort + i}};
                graph.removeConnection(c);
            }
            return;
        }

        if (cable.signal != synth::ui::CableSignal::Midi && (srcMb != nullptr || dstMb != nullptr)) {
            const int srcJack =
                srcMb != nullptr ? srcMb->mapOutputChannel(cable.id.srcPort).visibleJackIndex : cable.id.srcPort;
            const int dstJack =
                dstMb != nullptr ? dstMb->mapInputChannel(cable.id.dstPort).visibleJackIndex : cable.id.dstPort;
            const auto link = resolvePolyLink(srcMb, srcJack, dstMb, dstJack);
            for (int v = 0; v < link.voiceCount; ++v) {
                juce::AudioProcessorGraph::Connection c{{srcId, link.sourceRawChannel + v * link.sourceStride},
                                                        {dstId, link.destRawChannel + v}};
                graph.removeConnection(c);
            }
            return;
        }

        juce::AudioProcessorGraph::Connection c{{srcId, cable.id.srcPort}, {dstId, cable.id.dstPort}};
        graph.removeConnection(c);
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, removeEdges);
    else
        removeEdges();

    hoveredCableId.reset();
    repaintCanvas();
}

void GraphEditor::GraphContentComponent::paint(juce::Graphics& g) {
    // Resolve the themed LookAndFeel once. In headless tests the default JUCE LnF is
    // installed, so the cast returns null and we fall back to plain fills/lines.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    if (lf != nullptr)
        lf->fillThemedBackground(g, getLocalBounds().toFloat(), /*isCanvas*/ true);
    else
        g.fillAll(juce::Colours::darkgrey);

    // ---- Drag-preview grid dots (only while a module is being dragged) ----
    // Draw subtle dots at kGridSize*5 = 40px spacing over the VISIBLE canvas region only.
    // This stays cheap: we compute the visible clip in canvas coords and skip everything outside.
    if (editor.dragPreviewActive) {
        // The content component's transform maps canvas -> screen. The clip rect of g is
        // already in canvas coords (paint runs in local/canvas space), so getClipBounds()
        // gives us the visible region for free.
        auto clip = g.getClipBounds();

        // Dot colour: textPrimary at ~8% alpha for a gentle, non-distracting grid.
        const juce::Colour textPrimaryColourForGrid =
            lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
        g.setColour(textPrimaryColourForGrid.withAlpha(0.08f));

        constexpr int kMajorGrid = synth::LayoutUtil::kGridSize * 5; // 40px
        int startX = (clip.getX() / kMajorGrid) * kMajorGrid;
        int startY = (clip.getY() / kMajorGrid) * kMajorGrid;

        for (int gx = startX; gx <= clip.getRight(); gx += kMajorGrid) {
            for (int gy = startY; gy <= clip.getBottom(); gy += kMajorGrid) {
                g.fillEllipse((float)gx - 1.2f, (float)gy - 1.2f, 2.4f, 2.4f);
            }
        }
    }
    // ---- End drag-preview grid dots ----

    // Theme color tokens (fall back to legacy literals when unthemed). Wire colours are NOT
    // read here — they come from GraphEditor::colourForCable so that mode + user overrides are
    // applied in exactly one place.
    const juce::Colour surfaceColour = lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::darkgrey;
    const juce::Colour knobPointerColour = lf != nullptr ? lf->getTheme().colors.knobPointer : juce::Colours::white;
    const juce::Colour textPrimaryColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;

    // Stroke a wire via the LnF helper (themed) or a curved fallback, and RETURN the path so the
    // caller can place the animated dots along it. The curve comes from
    // GraphEditor::buildCablePath — the same one hit-testing measures against.
    auto strokeWire = [&](juce::Point<float> p1, juce::Point<float> p2, juce::Colour colour, bool isModulation,
                          float activity, float fallbackWidth, bool hovered) -> juce::Path {
        juce::Path wire = GraphEditor::buildCablePath(p1, p2);
        if (lf != nullptr) {
            lf->drawConnectionWire(g, p1, p2, wire, colour, isModulation, activity, hovered);
        } else {
            float brightness = juce::jlimit(0.5f, 1.0f, 0.5f + activity * 0.5f);
            g.setColour(colour.withMultipliedBrightness(brightness));
            g.strokePath(wire, juce::PathStrokeType(hovered ? fallbackWidth + 1.0f : fallbackWidth,
                                                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        return wire;
    };

    // Draw the 3 animated signal-flow dots evenly spaced along a wire path so they follow the curve.
    auto drawWireDots = [&](const juce::Path& wire, juce::Colour colour) {
        const float len = wire.getLength();
        for (int d = 0; d < 3; ++d) {
            float t = std::fmod(connectionAnimPhase + (float)d / 3.0f, 1.0f);
            auto pt = wire.getPointAlongPath(t * len);
            g.setColour(colour.withAlpha(0.7f));
            g.fillEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
        }
    };

    // ---- Draw cables ----
    // One list, built by GraphEditor::buildVisibleCables(), drives both what is painted and what
    // the mouse can hit. Colour comes from GraphEditor::colourForCable so the active mode and any
    // user overrides are applied in exactly one place.
    for (const auto& cable : editor.buildVisibleCables()) {
        const bool hovered = editor.hoveredCableId.has_value() && *editor.hoveredCableId == cable.id;
        const juce::Colour colour = editor.colourForCable(cable);
        const bool isModulation = cable.kind != GraphEditor::VisibleCable::Kind::Direct;

        float fallbackWidth = 2.0f;
        if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain)
            fallbackWidth = 2.0f + cable.activity * 2.0f;
        else if (cable.kind == GraphEditor::VisibleCable::Kind::ModRouting)
            fallbackWidth = 2.5f + cable.activity * 2.0f;

        auto wirePath = strokeWire(cable.p1, cable.p2, colour, isModulation, cable.activity, fallbackWidth, hovered);
        drawWireDots(wirePath, colour);

        // Attenuverter chains carry their amount knob at the wire midpoint.
        if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain) {
            auto mid = (cable.p1 + cable.p2) / 2.0f;
            juce::Rectangle<float> knobArea(mid.x - 10.0f, mid.y - 10.0f, 20.0f, 20.0f);
            g.setColour(surfaceColour);
            g.fillEllipse(knobArea);
            g.setColour(knobPointerColour);
            g.drawEllipse(knobArea, 1.0f);

            float angle = juce::jmap(cable.attenAmount, -1.0f, 1.0f, -juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 0.75f);
            float dx = std::sin(angle) * 8.0f;
            float dy = -std::cos(angle) * 8.0f;
            g.drawLine(mid.x, mid.y, mid.x + dx, mid.y + dy, 2.0f);
        }

        // Poly buses label the midpoint with "xN" so the bundle size is visible.
        if (cable.isPolyBus && cable.voiceCount > 1) {
            auto mid = (cable.p1 + cable.p2) / 2.0f;
            juce::String badge = "x" + juce::String(cable.voiceCount);
            g.setFont(11.0f);
            int textW = (int)g.getCurrentFont().getStringWidthFloat(badge) + 8;
            juce::Rectangle<float> pill((float)((int)mid.x + 5), mid.y - 8.0f, (float)textW, 16.0f);
            g.setColour(surfaceColour);
            const float smallRadius = lf != nullptr ? lf->getTheme().metrics.cornerRadiusSmall : 4.0f;
            g.fillRoundedRectangle(pill, smallRadius);
            g.setColour(textPrimaryColour);
            g.drawText(badge, pill, juce::Justification::centred, false);
        }
    }
    // ---- End cables ----

    // Draw Line being dragged
    if (editor.isDraggingConnection) {
        if (editor.dragSourceModule) {
            juce::Point<int> p;
            if (editor.dragSourceIsMidi) {
                if (editor.dragSourceIsInput)
                    p = juce::Point<int>(10, 30);
                else
                    p = editor.dragSourceModule->getPortCenter(0, false);
            } else {
                p = editor.dragSourceModule->getPortCenter(editor.dragSourceChannel, editor.dragSourceIsInput);
            }

            auto posInContent = editor.dragSourceModule->getBounds().getPosition() + p;
            auto mouseInContent = getLocalPoint(&(editor), editor.dragCurrentPos);

            // Since 'content' is transformed, we need to handle coordinates
            // carefully. But if this 'paint' is called on content, and we use
            // getLocalPoint(editor, ...) it should be transformed back. Actually,
            // easier: editor.dragCurrentPos is screen pos.
            auto mouseLocal = getLocalPoint(nullptr, editor.dragCurrentPos);

            // In-progress drag wire: resolved through the same colour path as a real cable, so
            // the preview already looks like the cable it is about to become (in By-source mode
            // that means the dragged-from module's category colour, not a generic white).
            GraphEditor::VisibleCable preview;
            preview.signal = editor.dragSourceIsMidi ? synth::ui::CableSignal::Midi : synth::ui::CableSignal::Audio;
            if (auto* mb = dynamic_cast<ModuleBase*>(editor.dragSourceModule->getModule()))
                preview.sourceCategory = synth::ui::categoryFor(mb->getModuleType());
            strokeWire(posInContent.toFloat(), mouseLocal.toFloat(), editor.colourForCable(preview),
                       /*isModulation*/ false,
                       /*activity*/ 0.0f, /*fallbackWidth*/ 3.0f, /*hovered*/ false);
        }
    }
}

void GraphEditor::GraphContentComponent::resized() {}

void GraphEditor::GraphContentComponent::paintOverChildren(juce::Graphics& g) {
    // ---- Drag-preview landing ghost (on top of module cards) ----
    // Draw a translucent rounded rect at the exact snapped+anti-overlapped landing position.
    if (editor.dragPreviewActive && !editor.dragPreviewGhost.isEmpty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
        const float cornerRadius = m.cornerRadius;

        auto ghostF = editor.dragPreviewGhost.toFloat();

        // Fill: accent colour at ~18% alpha
        g.setColour(accentColour.withAlpha(0.18f));
        g.fillRoundedRectangle(ghostF, cornerRadius);

        // Outline: accent colour at ~70% alpha, 1.5px
        g.setColour(accentColour.withAlpha(0.70f));
        const float guideLineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;
        g.drawRoundedRectangle(ghostF, cornerRadius, guideLineWidth);
    }

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // Draw aligned edges when hovering near other modules (Figma-style)
    if (editor.dragPreviewActive && !editor.alignmentGuides.empty() && editor.alignmentGuidesEnabled) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour guideColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;

        // Solid lines, ~70% opacity for visibility without distraction
        const float guideAlpha = lf != nullptr ? lf->getTheme().metrics.guideAlpha : 0.7f;
        g.setColour(guideColour.withAlpha(guideAlpha));
        for (const auto& guide : editor.alignmentGuides) {
            const float dx = guide.end.x - guide.start.x;
            const float dy = guide.end.y - guide.start.y;

            if (std::abs(dx) > std::abs(dy)) { // Horizontal line
                g.drawHorizontalLine((int)guide.start.y, guide.start.x, guide.end.x);
            } else { // Vertical line
                g.drawVerticalLine((int)guide.start.x, guide.start.y, guide.end.y);
            }
        }
    }

    // ---- Marquee selection band (issue #156) ----
    // Drawn here, in canvas space, so it stays locked to the modules it is selecting while the
    // view is zoomed. Paint-only — the selection itself is computed in updateMarquee().
    if (editor.marqueeActive && !editor.marqueeRect.isEmpty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        const float lineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;

        auto bandF = editor.marqueeRect.toFloat();
        g.setColour(accentColour.withAlpha(0.12f));
        g.fillRect(bandF);
        g.setColour(accentColour.withAlpha(0.80f));
        g.drawRect(bandF, lineWidth);
    }

    // ---- Smart-connection frosted preview cables ----
    if (editor.dragPreviewActive && !editor.smartSuggestions.empty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

        // Colours resolve only through colourForCable (→ synth::ui::resolveCableColour), so the
        // cable-colour mode and any user override keep applying to a preview too.
        auto previewColour = [this](synth::ui::CableSignal signal, synth::ui::ModuleCategory category, float alpha) {
            GraphEditor::VisibleCable preview;
            preview.signal = signal;
            preview.sourceCategory = category;
            return editor.colourForCable(preview).withAlpha(alpha);
        };

        // An insert REROUTES existing cabling rather than adding to it, which is a destructive-ish
        // edit the user should be able to tell apart from an ordinary suggestion at a glance. Its
        // new legs are tinted toward the theme's warning colour — INTERPOLATED, not replaced, so the
        // cable's own resolved identity (signal, category, user override) still reads through, and
        // taken from a theme token rather than a literal.
        static const synth::theme::Colors fallbackColors{};
        const auto& themeColors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
        auto insertTint = [&themeColors](juce::Colour base) {
            return base.interpolatedWith(themeColors.warning.withAlpha(base.getFloatAlpha()), 0.65f);
        };
        auto strokePreview = [&](juce::Point<float> p1, juce::Point<float> p2, juce::Colour colour,
                                 synth::ui::CableSignal signal) {
            auto path = GraphEditor::buildCablePath(p1, p2);
            if (lf != nullptr)
                lf->drawConnectionWire(g, p1, p2, path, colour,
                                       /*isModulation*/ signal == synth::ui::CableSignal::ModCV,
                                       /*activity*/ 0.0f, /*hovered*/ false);
            else {
                g.setColour(colour);
                g.strokePath(path,
                             juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        };

        // An insert REPLACES cabling, so EVERY doomed cable is struck out underneath the frosted
        // segments taking their place — otherwise the extra previews read as "and also", and the
        // user expects the old wires to still be there after the drop. All of them, not just this
        // leg's: a stereo upstream can have one doomed cable per leg.
        for (const auto& s : editor.smartSuggestions) {
            if (!s.isInsert)
                continue;
            for (const auto& doomed : s.doomedLinks) {
                const auto curve = GraphEditor::buildCablePath(doomed.p1, doomed.p2);
                const float dashes[] = {5.0f, 5.0f};
                juce::Path dashed;
                juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt)
                    .createDashedStroke(dashed, curve, dashes, juce::numElementsInArray(dashes));
                g.setColour(previewColour(s.signal, s.upstreamCategory, 0.18f));
                g.fillPath(dashed);
            }
        }

        // Draw the RESOLVED legs, not one segment per suggestion: a collapsed jack landing on the
        // terminal sink is one suggestion but two cables, and a preview that showed a single wire
        // while the drop fanned both raws was lying about what was about to happen.
        for (const auto& s : editor.smartSuggestions) {
            const auto legColour = [&](synth::ui::ModuleCategory category) {
                const auto base = previewColour(s.signal, category, 0.40f);
                return s.isInsert ? insertTint(base) : base;
            };

            for (const auto& leg : s.upstreamPreviewLegs)
                strokePreview(leg.p1, leg.p2, legColour(s.upstreamCategory), s.signal);

            if (s.mainPreviewLegs.empty()) {
                strokePreview(s.p1, s.p2, legColour(s.sourceCategory), s.signal);
                continue;
            }
            for (const auto& leg : s.mainPreviewLegs)
                strokePreview(leg.p1, leg.p2, legColour(s.sourceCategory), s.signal);
        }
    }
}

namespace {
/** Ranks a candidate source/destination fan pairing; higher wins. Matching roles are the strongest
 *  signal (this is what tells Poly MIDI's Pitch fan from its Gate fan when both share one jack); a
 *  ModCV or unclassified end is a wildcard, since mod inputs accept anything; equal fan widths break
 *  what is left. */
/** True when a source channel carries a structural, absolute-valued signal rather than normalised
 *  modulation. Poly MIDI's pitch fan is raw Hz and its gate fan is a 0/1 trigger; neither should ever
 *  be routed through an attenuverter, which would scale an absolute frequency and feed Hz-magnitude
 *  peaks into the UI's signal-activity metering. */
bool carriesStructuralSignal(const ModuleBase* source, int sourceRawChannel) {
    if (source == nullptr)
        return false;
    const PortRole role = source->mapOutputChannel(sourceRawChannel).role;
    return role == PortRole::Pitch || role == PortRole::Gate;
}

int scoreJackPair(const ModuleBase::JackTarget& src, const ModuleBase::JackTarget& dst) {
    int score = 0;
    if (src.role == dst.role)
        score += 4;
    else if (src.role == PortRole::ModCV || dst.role == PortRole::ModCV || src.role == PortRole::Other ||
             dst.role == PortRole::Other)
        score += 1;

    if (src.voiceSpan == dst.voiceSpan && src.voiceSpan > 1)
        score += 2;

    return score;
}
} // namespace

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

void GraphEditor::clearModDropTargets() {
    for (auto* comp : content.getModules())
        comp->setModDropTargetChannel(-1);
}

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

    for (auto* comp : content.getModules()) {
        auto localPos = comp->getLocalPoint(nullptr, screenPos);
        auto port = comp->getPortForPoint(localPos);

        // Serum-style modulation drop: a cable released on a KNOB connects to that parameter's
        // CV jack. Only as a fallback, so an actual jack under the cursor still wins, and only
        // for a cable coming from an output — a mod source drives a destination, not the reverse.
        if (!port && !dragSourceIsInput && !dragSourceIsMidi && comp != dragSourceModule)
            port = comp->getModTargetPortForPoint(localPos);

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
                connectPorts(realSrc->nodeID, srcJack, realDst->nodeID, dstJack, dragSourceIsMidi, true);
            }
        }
    }

    isDraggingConnection = false;
    dragSourceModule = nullptr;
    clearModDropTargets();
    repaintCanvas();
}

GraphEditor::SmartConnectionMode GraphEditor::smartConnectionModeFromString(const juce::String& s) {
    if (s == "Off")
        return SmartConnectionMode::Off;
    if (s == "NewOnly")
        return SmartConnectionMode::NewOnly;
    if (s == "AllMoves")
        return SmartConnectionMode::AllMoves;
    return SmartConnectionMode::NewAndUnwired;
}

juce::String GraphEditor::smartConnectionModeToString(SmartConnectionMode mode) {
    switch (mode) {
    case SmartConnectionMode::Off:
        return "Off";
    case SmartConnectionMode::NewOnly:
        return "NewOnly";
    case SmartConnectionMode::AllMoves:
        return "AllMoves";
    case SmartConnectionMode::NewAndUnwired:
    default:
        return "NewAndUnwired";
    }
}

juce::String GraphEditor::getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["displayName"].toString();
    return {};
}

void GraphEditor::setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    // Blank or whitespace-only reverts to the auto-numbered default rather than showing an empty
    // header. Capped at the same length the untrusted patch path caps at, so a title typed here and
    // a title loaded from a file can never disagree about what is storable.
    const auto trimmed = name.trim().substring(0, synth::kMaxModuleDisplayNameChars);
    if (trimmed == getModuleDisplayName(nodeId))
        return; // no-op rename: do not burn an undo step on it

    auto apply = [this, nodeId, trimmed] {
        if (auto* n = audioEngine.getGraph().getNodeForId(nodeId)) {
            if (trimmed.isEmpty())
                n->properties.remove("displayName");
            else
                n->properties.set("displayName", trimmed);
        }
        for (auto* comp : content.getModules())
            if (comp != nullptr && comp->getNodeId() == nodeId)
                comp->repaint();
    };

    if (undoManager)
        undoManager->recordStructuralChange(graph, apply);
    else
        apply();
}

juce::String GraphEditor::getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                         juce::AudioProcessor* processor) const {
    const auto custom = getModuleDisplayName(nodeId);
    if (custom.isNotEmpty())
        return custom;
    return processor != nullptr ? processor->getName() : juce::String();
}

void GraphEditor::commitAnyOpenTitleRename() {
    // Copy the card list first: committing mutates the graph (and pushes an undo snapshot), and
    // nothing may be iterating content.getModules() across that.
    std::vector<ModuleComponent*> renaming;
    for (auto* comp : content.getModules())
        if (comp != nullptr && comp->isRenamingTitle())
            renaming.push_back(comp);

    for (auto* comp : renaming) {
        juce::Component::SafePointer<ModuleComponent> safe(comp);
        if (safe != nullptr)
            safe->finishTitleRename(true);
    }
}

void GraphEditor::refreshSuggestionsIfInsertModifierChanged() {
    if (!dragPreviewActive)
        return;
    const bool insertNow = isInsertModifierDown();
    if (insertNow == lastSampledInsertModifier)
        return; // the common case: one bool compare per drag tick
    lastSampledInsertModifier = insertNow;
    refreshSmartSuggestions();
}

void GraphEditor::clearSmartSuggestions() { smartSuggestions.clear(); }

bool GraphEditor::nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const {
    auto& graph = audioEngine.getGraph();
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            return true;
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if ((r.hasSource && r.sourceNodeID == nodeId) || (r.hasDest && r.destNodeID == nodeId))
            return true;
    }
    return false;
}

bool GraphEditor::shouldOfferSmartConnections() const {
    if (smartConnectionMode == SmartConnectionMode::Off)
        return false;
    if (selectionDragActive && selection.size() > 1)
        return false;
    if (dragPreviewIsSnippet)
        return false;

    const bool isNewDrop = dragPreviewSelfId.uid == 0;
    if (isNewDrop)
        return true; // NewOnly / NewAndUnwired / AllMoves all allow library drops

    switch (smartConnectionMode) {
    case SmartConnectionMode::NewOnly:
        return false;
    case SmartConnectionMode::AllMoves:
    case SmartConnectionMode::NewAndUnwired:
        // NewAndUnwired still applies on every single-module move; "unwired" is the
        // per-jack free check in refreshSmartSuggestions (main I/O not already patched).
        return true;
    case SmartConnectionMode::Off:
        return false;
    }
    return false;
}

juce::Point<int> GraphEditor::estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                                 bool isInput, bool isMidi) {
    if (proc == nullptr)
        return bounds.getCentre();

    if (isMidi) {
        if (isInput)
            return {bounds.getX() + 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
        return {bounds.getRight() - 10, bounds.getY() + ModuleComponent::kPortGutterHeaderHeight};
    }

    const int yStep = 20;
    // MUST equal ModuleComponent::getPortCenter's headerHeight. It read 30 while the real card used
    // 38, so every ghost preview cable terminated 8px ABOVE the jack dot it claimed to land on —
    // visibly floating over the jack's label row. Pinned by
    // GraphEditorTest.GhostPortEstimateMatchesTheRealJackCentre.
    const int headerHeight = ModuleComponent::kPortGutterHeaderHeight;
    int portOffset = 0;
    if (proc->producesMidi())
        portOffset = 20;

    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(proc))
        visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    else
        visible = isInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();

    const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, jack) : 0;

    if (auto* macro = dynamic_cast<MacroControlModule*>(proc)) {
        if (!isInput) {
            return {bounds.getRight() - 10, bounds.getY() + synth::LayoutUtil::macroRowCentreY(clamped)};
        }
    }

    if (isInput) {
        int columns = 1;
        if (auto* mb = dynamic_cast<ModuleBase*>(proc))
            if (mb->getVisibleInputPortCount() > 10 && bounds.getWidth() >= synth::LayoutUtil::kDoubleWidth)
                columns = 2;
        if (columns > 1 && visible > 0) {
            const int rows = (visible + columns - 1) / columns;
            const int col = clamped / rows;
            const int row = clamped % rows;
            return {bounds.getX() + 10 + col * 100, bounds.getY() + headerHeight + portOffset + row * yStep + 20};
        }
        return {bounds.getX() + 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
    }
    return {bounds.getRight() - 10, bounds.getY() + headerHeight + portOffset + clamped * yStep + 20};
}

bool GraphEditor::isInputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
    if (isMidi) {
        const int channel = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.destination.nodeID == nodeId && c.destination.channelIndex == channel)
                return false;
        }
        return true;
    }

    // Visible jack → raw channel(s), same path as areJacksAlreadyConnected / connectPorts.
    auto* node = graph.getNodeForId(nodeId);
    auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    std::vector<int> rawChannels;
    if (mb != nullptr) {
        for (const auto& t : mb->getJackTargets(jack, true)) {
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
        }
    } else {
        rawChannels.push_back(jack); // Audio I/O identity mapping
    }

    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (c.destination.channelIndex == ch)
                return false;
        }
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasDest || r.destNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.destChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool GraphEditor::isOutputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
    if (isMidi) {
        const int channel = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == nodeId && c.source.channelIndex == channel)
                return false;
        }
        return true;
    }

    auto* node = graph.getNodeForId(nodeId);
    auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    std::vector<int> rawChannels;
    if (mb != nullptr) {
        for (const auto& t : mb->getJackTargets(jack, false)) {
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
        }
        if (rawChannels.empty())
            rawChannels.push_back(jack);
    } else {
        rawChannels.push_back(jack);
    }

    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (c.source.channelIndex == ch)
                return false;
        }
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (!r.hasSource || r.sourceNodeID != nodeId)
            continue;
        for (int ch : rawChannels) {
            if (r.sourceChannelIndex == ch)
                return false;
        }
    }
    return true;
}

bool GraphEditor::areJacksAlreadyConnected(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                           juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi) const {
    auto& graph = audioEngine.getGraph();
    if (isMidi) {
        const int ch = juce::AudioProcessorGraph::midiChannelIndex;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == srcId && c.destination.nodeID == dstId && c.source.channelIndex == ch &&
                c.destination.channelIndex == ch)
                return true;
        }
        return false;
    }

    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    auto* srcMb = srcNode ? dynamic_cast<ModuleBase*>(srcNode->getProcessor()) : nullptr;
    auto* dstMb = dstNode ? dynamic_cast<ModuleBase*>(dstNode->getProcessor()) : nullptr;
    const auto link = resolvePolyLink(srcMb, srcJack, dstMb, dstJack);

    for (int v = 0; v < link.voiceCount; ++v) {
        const int sCh = link.sourceRawChannel + v * link.sourceStride;
        const int dCh = link.destRawChannel + v;
        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == srcId && c.destination.nodeID == dstId && c.source.channelIndex == sCh &&
                c.destination.channelIndex == dCh)
                return true;
        }
    }
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasSource && r.hasDest && r.sourceNodeID == srcId && r.destNodeID == dstId &&
            r.sourceChannelIndex == link.sourceRawChannel && r.destChannelIndex == link.destRawChannel)
            return true;
    }
    return false;
}

std::optional<GraphEditor::UpstreamLink>
GraphEditor::findSingleUpstreamAudioLink(juce::AudioProcessorGraph::NodeID dstId, int dstJack) const {
    auto& graph = audioEngine.getGraph();
    auto* dstNode = graph.getNodeForId(dstId);
    if (dstNode == nullptr)
        return std::nullopt;

    // Visible jack → raw channel(s), the same expansion isInputJackFree uses.
    auto* dstMb = dynamic_cast<ModuleBase*>(dstNode->getProcessor());
    std::vector<int> rawChannels;
    if (dstMb != nullptr) {
        for (const auto& t : dstMb->getJackTargets(dstJack, true))
            for (int v = 0; v < t.voiceSpan; ++v)
                rawChannels.push_back(t.rawHeadChannel + v);
    } else {
        rawChannels.push_back(dstJack); // Audio I/O identity mapping
    }
    const auto coversRaw = [&rawChannels](int ch) {
        return std::find(rawChannels.begin(), rawChannels.end(), ch) != rawChannels.end();
    };

    // A mod routing is a cable with a hidden attenuverter node in it; splicing an FX into that is
    // not what the user asked for, so the jack is treated as un-reroutable.
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasDest && r.destNodeID == dstId && coversRaw(r.destChannelIndex))
            return std::nullopt;
    }

    std::optional<UpstreamLink> found;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != dstId || c.destination.isMIDI() || !coversRaw(c.destination.channelIndex))
            continue;

        auto* srcNode = graph.getNodeForId(c.source.nodeID);
        if (srcNode == nullptr || dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr)
            return std::nullopt;

        auto* srcMb = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
        const int srcJack =
            srcMb != nullptr ? srcMb->mapOutputChannel(c.source.channelIndex).visibleJackIndex : c.source.channelIndex;

        if (!found.has_value())
            found = UpstreamLink{c.source.nodeID, {}};
        else if (found->nodeId != c.source.nodeID)
            return std::nullopt; // a genuine hand-built mix: rerouting it would change what sums

        // Several legs of the SAME node is our own dual-to-mono wiring, not a mix — collect them all
        // and let the planner doom every one of them.
        if (std::find(found->jacks.begin(), found->jacks.end(), srcJack) == found->jacks.end())
            found->jacks.push_back(srcJack);
    }
    if (found.has_value())
        std::sort(found->jacks.begin(), found->jacks.end()); // Left before Right, deterministically
    return found;
}

void GraphEditor::disconnectAudioLink(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                      juce::AudioProcessorGraph::NodeID dstId, int dstJack) {
    auto& graph = audioEngine.getGraph();
    auto* srcNode = graph.getNodeForId(srcId);
    auto* dstNode = graph.getNodeForId(dstId);
    if (srcNode == nullptr || dstNode == nullptr)
        return;

    // Same fan expansion connectPorts and disconnectCable use, so a collapsed stereo wire takes
    // both raw legs with it rather than leaving a half-connected pair behind.
    const auto link = resolvePolyLink(dynamic_cast<ModuleBase*>(srcNode->getProcessor()), srcJack,
                                      dynamic_cast<ModuleBase*>(dstNode->getProcessor()), dstJack);
    for (int v = 0; v < link.voiceCount; ++v)
        graph.removeConnection(
            {{srcId, link.sourceRawChannel + v * link.sourceStride}, {dstId, link.destRawChannel + v}});
}

namespace {
float edgeToEdgeDistance(juce::Rectangle<float> a, juce::Rectangle<float> b) {
    if (a.intersects(b))
        return 0.0f;
    float dx = 0.0f;
    if (a.getRight() < b.getX())
        dx = b.getX() - a.getRight();
    else if (b.getRight() < a.getX())
        dx = a.getX() - b.getRight();
    float dy = 0.0f;
    if (a.getBottom() < b.getY())
        dy = b.getY() - a.getBottom();
    else if (b.getBottom() < a.getY())
        dy = a.getY() - b.getBottom();
    return std::sqrt(dx * dx + dy * dy);
}

PortRole primaryRoleForJack(const ModuleBase* mb, int visibleJack, bool isInput) {
    if (mb == nullptr)
        return PortRole::Other;
    const auto targets = mb->getJackTargets(visibleJack, isInput);
    if (targets.empty())
        return PortRole::Other;
    return targets.front().role;
}

synth::ui::CableSignal signalForRoles(bool isMidi, PortRole srcRole) {
    if (isMidi)
        return synth::ui::CableSignal::Midi;
    switch (srcRole) {
    case PortRole::Pitch:
        return synth::ui::CableSignal::Pitch;
    case PortRole::Gate:
        return synth::ui::CableSignal::Gate;
    case PortRole::ModCV:
        return synth::ui::CableSignal::ModCV;
    default:
        return synth::ui::CableSignal::Audio;
    }
}

int scoreSmartPair(const ModuleBase* srcMb, int srcJack, const ModuleBase* dstMb, int dstJack) {
    if (srcMb == nullptr || dstMb == nullptr) {
        // Audio I/O nodes without ModuleBase: treat as plain mono audio jacks.
        return 2;
    }
    int best = -1;
    for (const auto& s : srcMb->getJackTargets(srcJack, false)) {
        for (const auto& d : dstMb->getJackTargets(dstJack, true)) {
            best = std::max(best, scoreJackPair(s, d));
        }
    }
    return best;
}

/** ModuleBase defaults producesMidi/acceptsMidi to true, so almost every card reports MIDI
 *  jacks. Smart-connect only suggests MIDI for modules that actually source or sink MIDI in
 *  practice (mirrors the AI merge auto-connect allow-lists). */
bool isKnownMidiSourceName(const juce::String& name) {
    return name == "Sequencer" || name == "Poly Sequencer" || name == "Poly MIDI" || name == "MIDI Keyboard" ||
           name == "External MIDI";
}

bool isKnownMidiDestName(const juce::String& name) {
    return name == "Oscillator" || name == "Sampler" || name == "Wavetable" || name == "ADSR" || name == "Sequencer" ||
           name == "Poly Sequencer" || name == "Poly MIDI";
}

bool isStereoLegLabel(const juce::String& label) {
    const auto l = label.trim().toLowerCase();
    return l == "left" || l == "right" || l == "audio l" || l == "audio r" || l == "l" || l == "r";
}

bool isLeftLegLabel(const juce::String& label) {
    const auto l = label.trim().toLowerCase();
    return l == "left" || l == "audio l" || l == "l";
}

bool audioJackIsModCvDest(const ModuleBase* dest, int visibleJack) {
    if (dest == nullptr)
        return false;
    const auto targets = dest->getJackTargets(visibleJack, true);
    for (const auto& t : targets) {
        for (const auto& mt : dest->getModulationTargets()) {
            if (mt.channelIndex == t.rawHeadChannel)
                return true;
        }
    }
    return false;
}

/** True for the graph's terminal audio sink. Audio Output is a bare juce::AudioGraphIOProcessor —
 *  never a ModuleBase — because the graph's output channel count is tied to that node. Detected by
 *  type rather than by the "Audio Output" name isSingletonIOModule matches on, so a ModuleBase that
 *  happened to be called that could not impersonate the sink. */
bool isTerminalAudioSink(const juce::AudioProcessor* proc) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    if (auto* io = dynamic_cast<const IOProcessor*>(proc))
        return io->getType() == IOProcessor::audioOutputNode;
    return false;
}

/** Visible audio jacks used for smart-connect. A stereo pair is returned only when jacks are
 *  explicitly labeled Left/Right (or Audio L/R) — arity alone is not enough (Math A/B would
 *  otherwise look like stereo). Unlabeled audio-ish jacks contribute at most one mono leg so
 *  Voice Mixer banks are not fan-wired. Audio I/O nodes without ModuleBase use channel count. */
std::vector<int> collectSmartAudioLegs(juce::AudioProcessor* proc, bool isInput) {
    std::vector<int> legs;
    if (proc == nullptr)
        return legs;

    auto* mb = dynamic_cast<ModuleBase*>(proc);
    if (mb == nullptr) {
        const int n = isInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
        if (n >= 2)
            return {0, 1};
        if (n == 1)
            return {0};
        return legs;
    }

    const int vis = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    std::vector<int> labeled;
    std::vector<int> unlabeled;

    for (int j = 0; j < vis; ++j) {
        if (isInput && audioJackIsModCvDest(mb, j))
            continue;
        const PortRole role = primaryRoleForJack(mb, j, isInput);
        if (role == PortRole::Pitch || role == PortRole::Gate || role == PortRole::Midi || role == PortRole::ModCV)
            continue;

        const juce::String label = isInput ? mb->getInputPortLabel(j) : mb->getOutputPortLabel(j);
        if (isStereoLegLabel(label))
            labeled.push_back(j);
        else if (role == PortRole::Audio || role == PortRole::Other)
            unlabeled.push_back(j);
    }

    if (labeled.size() >= 2) {
        std::sort(labeled.begin(), labeled.end(), [&](int a, int b) {
            const auto la = isInput ? mb->getInputPortLabel(a) : mb->getOutputPortLabel(a);
            const auto lb = isInput ? mb->getInputPortLabel(b) : mb->getOutputPortLabel(b);
            const bool aLeft = isLeftLegLabel(la);
            const bool bLeft = isLeftLegLabel(lb);
            if (aLeft != bLeft)
                return aLeft;
            return a < b;
        });
        return {labeled[0], labeled[1]};
    }
    if (labeled.size() == 1)
        return labeled;

    // Unlabeled: mono only — never treat Math A/B (or any two Others) as L/R.
    if (!unlabeled.empty())
        return {unlabeled.front()};
    return legs;
}

/** Jack pairs for a smart audio link: L→L/R→R, or fan mono↔stereo both ways. */
std::vector<std::pair<int, int>> expandAudioJackPairs(const std::vector<int>& srcLegs,
                                                      const std::vector<int>& dstLegs) {
    std::vector<std::pair<int, int>> pairs;
    if (srcLegs.empty() || dstLegs.empty())
        return pairs;

    if (srcLegs.size() >= 2 && dstLegs.size() >= 2) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[1], dstLegs[1]);
    } else if (srcLegs.size() == 1 && dstLegs.size() >= 2) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[0], dstLegs[1]);
    } else if (srcLegs.size() >= 2 && dstLegs.size() == 1) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[1], dstLegs[0]);
    } else {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
    }
    return pairs;
}
} // namespace

void GraphEditor::refreshSmartSuggestions() {
    const auto previous = smartSuggestions;
    smartSuggestions.clear();

    if (!dragPreviewActive || dragPreviewGhost.isEmpty() || !shouldOfferSmartConnections()) {
        if (previous != smartSuggestions)
            repaintCanvas();
        return;
    }

    juce::AudioProcessor* ghostProc = nullptr;
    if (dragPreviewSelfId.uid != 0) {
        for (auto* c : content.getModules()) {
            if (c != nullptr && c->getNodeId() == dragPreviewSelfId) {
                ghostProc = c->getModule();
                break;
            }
        }
    } else {
        ghostProc = dragPreviewProbe.get();
    }
    if (ghostProc == nullptr) {
        if (previous != smartSuggestions)
            repaintCanvas();
        return;
    }

    const auto ghostBounds = dragPreviewGhost;
    // Where the cursor is pointing, before anti-overlap relocated the card. Empty on paths that
    // never set it (older tests drive updateDragPreview directly), in which case candidacy falls
    // back to the landing rect exactly as before.
    const auto aimBounds = dragPreviewAim;

    struct Candidate {
        SmartSuggestion suggestion;
        int score = 0;
        float distance = 0.0f;
        bool isMidi = false;
    };
    std::vector<Candidate> audioCandidates;
    std::vector<Candidate> midiCandidates;

    /** The reroute shared by every surviving jack pair of one insert group. Group-wide rather than
     *  per-leg: the cable sets must survive the fan dedupe that drops redundant pairs. */
    struct InsertPlan {
        juce::AudioProcessorGraph::NodeID upstreamId{};
        std::vector<SmartSuggestion::InsertLink> doomedLinks;
        std::vector<SmartSuggestion::InsertLink> upstreamCables;
        std::vector<SmartSuggestion::InsertLink> upstreamPreviewLegs;
        synth::ui::ModuleCategory upstreamCategory = synth::ui::ModuleCategory::Utility;
    };

    auto componentForNode = [this](juce::AudioProcessorGraph::NodeID id) -> ModuleComponent* {
        for (auto* c : content.getModules())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    };

    const bool ghostAcceptsMidi = ghostProc->acceptsMidi();
    const bool ghostProducesMidi = ghostProc->producesMidi();

    for (auto* neighbor : content.getModules()) {
        if (neighbor == nullptr || neighbor->getModule() == nullptr)
            continue;
        if (neighbor->getNodeId() == dragPreviewSelfId)
            continue;
        // Hidden attenuverter nodes are never smart-wired.
        if (dynamic_cast<AttenuverterModule*>(neighbor->getModule()) != nullptr)
            continue;

        const auto neighborBounds = neighbor->getBounds();
        // Cheap cull: facing jacks cannot be closer than the modules themselves. Measured against
        // BOTH where the card will land and where the cursor is aiming, whichever is closer — a
        // ghost aimed into a gap gets pushed clear by anti-overlap, and only the aim reflects what
        // the user meant. The jack-level test below still gates ordinary suggestions, so admitting
        // an aim-based candidate here does not by itself create one.
        const float landingDist = edgeToEdgeDistance(ghostBounds.toFloat(), neighborBounds.toFloat());
        const float aimDist =
            aimBounds.isEmpty() ? landingDist : edgeToEdgeDistance(aimBounds.toFloat(), neighborBounds.toFloat());
        if (std::min(landingDist, aimDist) > kSmartConnectionProximityPx)
            continue;

        auto* neighborProc = neighbor->getModule();
        const bool requireSourceFree = smartConnectionMode == SmartConnectionMode::NewAndUnwired;

        auto jackPoint = [&](bool fromGhost, int jack, bool isInput, bool isMidi) -> juce::Point<float> {
            if (fromGhost)
                return estimatePortCenter(ghostProc, ghostBounds, jack, isInput, isMidi).toFloat();
            if (isMidi) {
                const int x = isInput ? 10 : neighbor->getWidth() - 10;
                return (neighbor->getBounds().getPosition() + juce::Point<int>(x, 30)).toFloat();
            }
            return (neighbor->getBounds().getPosition() + neighbor->getPortCenter(jack, isInput)).toFloat();
        };

        /** The cables one connectPorts call actually draws, so a preview can never claim less than
         *  the drop will wire. Walks the SAME PolyLink connectPorts walks, maps each raw pair back
         *  to its visible jacks, and dedupes: several raw edges through one jack pair are one cable,
         *  but a collapsed jack fanning onto a destination that fronts those raws separately is two.
         *  Endpoints come from caller-supplied providers because either end can be the ghost, the
         *  neighbour, or (for an insert's upstream leg) a third card entirely. */
        using JackPointFn = std::function<juce::Point<float>(int jack, bool isInput)>;
        auto resolveDrawnLegs = [](juce::AudioProcessor* sProc, int sJack, juce::AudioProcessor* dProc, int dJack,
                                   const JackPointFn& srcPointFor, const JackPointFn& dstPointFor) {
            std::vector<SmartSuggestion::InsertLink> legs;
            auto* sMb = dynamic_cast<ModuleBase*>(sProc);
            auto* dMb = dynamic_cast<ModuleBase*>(dProc);
            const auto link = resolvePolyLink(sMb, sJack, dMb, dJack);
            for (int v = 0; v < link.voiceCount; ++v) {
                const int rawSrc = link.sourceRawChannel + v * link.sourceStride;
                const int rawDst = link.destRawChannel + v;
                SmartSuggestion::InsertLink leg;
                leg.fromJack = sMb != nullptr ? sMb->mapOutputChannel(rawSrc).visibleJackIndex : rawSrc;
                leg.toJack = dMb != nullptr ? dMb->mapInputChannel(rawDst).visibleJackIndex : rawDst;
                if (std::find(legs.begin(), legs.end(), leg) != legs.end())
                    continue; // same drawn cable, just another raw edge inside it
                leg.p1 = srcPointFor(leg.fromJack, false);
                leg.p2 = dstPointFor(leg.toJack, true);
                legs.push_back(leg);
            }
            return legs;
        };

        auto pushAudioGroup = [&](bool ghostIsSource, juce::AudioProcessor* srcProc, juce::AudioProcessor* dstProc,
                                  juce::AudioProcessorGraph::NodeID srcNodeIdForFreeCheck,
                                  juce::AudioProcessorGraph::NodeID dstNodeIdForFreeCheck, bool checkDstFree) {
            const auto srcLegs = collectSmartAudioLegs(srcProc, false);
            const auto dstLegs = collectSmartAudioLegs(dstProc, true);
            auto pairs = expandAudioJackPairs(srcLegs, dstLegs);
            if (pairs.empty())
                return;

            auto* srcMb = dynamic_cast<ModuleBase*>(srcProc);
            auto* dstMb = dynamic_cast<ModuleBase*>(dstProc);

            // Drop pairs that target mod-CV or are already connected.
            pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                                       [&](const std::pair<int, int>& pr) {
                                           if (audioJackIsModCvDest(dstMb, pr.second))
                                               return true;
                                           if (dragPreviewSelfId.uid == 0)
                                               return false;
                                           const auto srcId = ghostIsSource ? dragPreviewSelfId : neighbor->getNodeId();
                                           const auto dstId = ghostIsSource ? neighbor->getNodeId() : dragPreviewSelfId;
                                           return areJacksAlreadyConnected(srcId, pr.first, dstId, pr.second, false);
                                       }),
                        pairs.end());
            if (pairs.empty())
                return;

            // An INSERT is aimed differently from a new cable, so the jack-level test below does not
            // apply to it. Both of its halves assume the ghost sits clear to the LEFT of the card it
            // is being wired into — true for a new cable, false for an insert, where the natural aim
            // is the gap between two wired cards or the doomed cable itself. There the ghost
            // OVERLAPS its destination: its output jack is inside (or past) the destination's left
            // edge, so the flow rule rejects every pair and the jack distance blows past the cap.
            //
            // For an insert we lean on the module-level proximity cull above (which an overlapping
            // ghost passes at distance 0) plus one guard: the ghost's CENTRE must not be past the
            // destination's right edge. Dragged clean past a card is not "insert into it" — and that
            // guard is also what stops an insert being offered into a card the ghost has already
            // moved beyond, e.g. the upstream it is being spliced in after. Final geometry is
            // findFreeSlot's business either way, so a transiently overlapping ghost is harmless.
            const auto& insertAim = aimBounds.isEmpty() ? ghostBounds : aimBounds;
            const bool relaxFlowForInsert =
                ghostIsSource && isInsertModifierDown() && insertAim.getCentreX() <= neighborBounds.getRight();

            const size_t beforeProximity = pairs.size();
            if (!relaxFlowForInsert) {
                // Jack-to-jack proximity + left-to-right flow: a module on the right must not wrap
                // its outputs around to the dragged module's left inputs.
                pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                                           [&](const std::pair<int, int>& pr) {
                                               const auto srcPt = jackPoint(ghostIsSource, pr.first, false, false);
                                               const auto dstPt = jackPoint(!ghostIsSource, pr.second, true, false);
                                               if (srcPt.x > dstPt.x + 8.0f)
                                                   return true;
                                               return srcPt.getDistanceFrom(dstPt) > kSmartConnectionProximityPx;
                                           }),
                            pairs.end());
                if (pairs.empty())
                    return;
                // Stereo / fan groups: both-or-neither on proximity, same as occupancy.
                if (beforeProximity >= 2 && pairs.size() != beforeProximity)
                    return;
            }

            // What an already-occupied destination jack means depends on the modifier and the node:
            //
            //   * Cmd held  → INSERT IN SERIES, at ANY module. The upstream cabling is rerouted
            //                 through the ghost. This is the only way to insert; nothing inserts
            //                 without the modifier.
            //   * No Cmd, terminal audio sink → plain ADDITIVE parallel connection. The sink is
            //                 wired in essentially every real patch, so a hard stop there means a
            //                 module parked next to it can never be offered anything; and summing
            //                 into the mix bus is exactly what dragging a cable there by hand does.
            //                 Existing cables are left alone.
            //   * No Cmd, any other module → hard stop, unchanged. Silently summing into a jack the
            //                 user wired mid-patch is never something to suggest.
            //
            // insertPlan is set for the whole group, or left empty for an ordinary add.
            std::optional<InsertPlan> insertPlan;
            if (checkDstFree && dstNodeIdForFreeCheck.uid != 0) {
                std::set<int> uniqueDsts;
                for (const auto& pr : pairs)
                    uniqueDsts.insert(pr.second);
                bool anyOccupied = false;
                for (int d : uniqueDsts) {
                    if (!isInputJackFree(dstNodeIdForFreeCheck, d, false))
                        anyOccupied = true;
                }

                // Proximity to the destination's INPUT side is already what gates us here: the
                // jack-to-jack filter above measures the ghost's output jack against
                // jackPoint(dstJack, isInput=true), and rejects a source sitting to the right of it.
                if (anyOccupied && !isInsertModifierDown()) {
                    // Parallel add is offered at the terminal sink only.
                    if (!(ghostIsSource && isTerminalAudioSink(dstProc)))
                        return;
                } else if (anyOccupied) {
                    // A ghost with no audio input cannot go in series — there would be nothing for
                    // the rerouted upstream to feed.
                    const auto ghostInLegs = collectSmartAudioLegs(ghostProc, true);
                    if (!ghostIsSource || ghostInLegs.empty())
                        return;

                    // Both-or-neither, mirroring the stereo group rule above: every leg of the group
                    // must be fed by one and the same upstream node, or nothing is offered. A mix of
                    // free and occupied legs, or two different feeds, would change the summing.
                    std::optional<juce::AudioProcessorGraph::NodeID> upstreamNode;
                    std::unordered_map<int, std::vector<int>> upstreamJacksForDst;
                    for (int d : uniqueDsts) {
                        const auto up = findSingleUpstreamAudioLink(dstNodeIdForFreeCheck, d);
                        if (!up.has_value() || up->jacks.empty() || up->nodeId == dragPreviewSelfId)
                            return;
                        if (upstreamNode.has_value() && *upstreamNode != up->nodeId)
                            return;
                        upstreamNode = up->nodeId;
                        upstreamJacksForDst[d] = up->jacks;
                    }

                    auto* upstreamComp = upstreamNode.has_value() ? componentForNode(*upstreamNode) : nullptr;
                    if (upstreamComp == nullptr || upstreamComp->getModule() == ghostProc)
                        return;
                    auto upstreamCategory = synth::ui::ModuleCategory::Utility;
                    if (auto* umb = dynamic_cast<ModuleBase*>(upstreamComp->getModule()))
                        upstreamCategory = synth::ui::categoryFor(umb->getModuleType());

                    auto* upstreamMb = dynamic_cast<ModuleBase*>(upstreamComp->getModule());
                    auto* ghostMb = dynamic_cast<ModuleBase*>(ghostProc); // == srcMb here (ghostIsSource)
                    auto upstreamJackPoint = [&](int jack) {
                        return (upstreamComp->getBounds().getPosition() +
                                upstreamComp->getPortCenter(jack, /*isInput=*/false))
                            .toFloat();
                    };

                    InsertPlan plan;
                    plan.upstreamId = *upstreamNode;
                    plan.upstreamCategory = upstreamCategory;

                    // EVERY occupied sink jack has a doomed cable, collected here — before and
                    // independently of the fan dedupe below. Hanging these off the surviving pairs
                    // instead would lose the link of any pair the dedupe drops, and the cable it
                    // stood for would survive and sum into the sink beside the ghost's output.
                    // One doomed cable per (upstream leg -> destination jack): a dual upstream summed
                    // into a collapsed mono input contributes TWO, and both have to go or the
                    // survivor keeps summing in beside the ghost.
                    for (int d : uniqueDsts) {
                        for (int fromJack : upstreamJacksForDst[d]) {
                            SmartSuggestion::InsertLink doomed;
                            doomed.fromJack = fromJack;
                            doomed.toJack = d;
                            doomed.p1 = upstreamJackPoint(fromJack);
                            doomed.p2 = jackPoint(/*fromGhost=*/false, d, true, false);
                            plan.doomedLinks.push_back(doomed);
                        }
                    }

                    // Upstream -> ghost. Redundant only when a cable adds NOTHING on EITHER side —
                    // no new raw ghost-input channel AND no new raw upstream-output channel. The
                    // same both-sides rule the destination-side dedupe uses, and for the same
                    // reason: a collapsed jack's fan already claims both raws on both ends, so its
                    // redundant partner contributes neither, while two DISTINCT upstream legs
                    // summing into one collapsed ghost input each contribute a real source channel
                    // and must both survive. Keying on the ghost input alone dropped the Right leg.
                    //
                    // The index mapping does the rest: a dual ghost's legs are taken in order (L->L,
                    // R->R) and a collapsed ghost clamps to its single jack, which sums.
                    std::set<int> claimedRawGhostIns, claimedRawUpstreamOuts;
                    for (size_t i = 0; i < plan.doomedLinks.size(); ++i) {
                        const int ghostInJack = ghostInLegs[std::min(i, ghostInLegs.size() - 1)];
                        const auto fan =
                            resolvePolyLink(upstreamMb, plan.doomedLinks[i].fromJack, ghostMb, ghostInJack);
                        bool addsGhostIn = false, addsUpstreamOut = false;
                        for (int v = 0; v < fan.voiceCount; ++v) {
                            if (claimedRawGhostIns.insert(fan.destRawChannel + v).second)
                                addsGhostIn = true;
                            if (claimedRawUpstreamOuts.insert(fan.sourceRawChannel + v * fan.sourceStride).second)
                                addsUpstreamOut = true;
                        }
                        if (!addsGhostIn && !addsUpstreamOut)
                            continue;

                        SmartSuggestion::InsertLink cable;
                        cable.fromJack = plan.doomedLinks[i].fromJack;
                        cable.toJack = ghostInJack;
                        cable.p1 = plan.doomedLinks[i].p1;
                        cable.p2 = jackPoint(/*fromGhost=*/true, ghostInJack, true, false);
                        plan.upstreamCables.push_back(cable);

                        // ONE connectPorts call here can still draw two cables (a collapsed upstream
                        // jack landing on a Dual I/O ghost covers both its legs), so the preview is
                        // resolved from the fan rather than from the cable list.
                        for (auto& leg : resolveDrawnLegs(
                                 upstreamComp->getModule(), cable.fromJack, ghostProc, ghostInJack,
                                 [&](int jack, bool) { return upstreamJackPoint(jack); },
                                 [&](int jack, bool isInput) { return jackPoint(true, jack, isInput, false); }))
                            plan.upstreamPreviewLegs.push_back(leg);
                    }
                    if (plan.upstreamCables.empty())
                        return;

                    insertPlan = std::move(plan);
                }
            }

            // One surviving pair per distinct set of raw destination channels. A collapsed jack
            // already fans across the whole raw pair, so when the destination fronts two legs (the
            // terminal sink is the only node that does — it has no ModuleBase to group them) a
            // second pair for its right leg would wire the source's LEFT leg there too, summing.
            // A no-op wherever the pairs already claim distinct raws. Insert plans are built above
            // and deliberately unaffected: a doomed link must not vanish with the pair that named it.
            //
            // A pair is redundant only when it adds NOTHING on EITHER side — no new destination raw
            // channel AND no new source raw channel. Keying on the destination alone was wrong for a
            // dedicated mono input: a dual upstream feeding a Ring Modulator's Carrier produces
            // (Left -> Carrier) and (Right -> Carrier), which share a destination raw but carry
            // DIFFERENT source legs, and summing both into that one jack is exactly the intent (it
            // is what hand-wiring and the Dual I/O toggle rewire both do). Dropping the second one
            // silently threw away a channel. The source test is what keeps the original case fixed:
            // a collapsed jack's fan already claims both raws on both sides, so its redundant
            // partner still contributes neither.
            {
                std::vector<std::pair<int, int>> keptPairs;
                std::set<int> claimedRawDsts, claimedRawSrcs;
                for (const auto& pr : pairs) {
                    const auto fan = resolvePolyLink(srcMb, pr.first, dstMb, pr.second);
                    bool addsDst = false, addsSrc = false;
                    for (int v = 0; v < fan.voiceCount; ++v) {
                        if (claimedRawDsts.insert(fan.destRawChannel + v).second)
                            addsDst = true;
                        if (claimedRawSrcs.insert(fan.sourceRawChannel + v * fan.sourceStride).second)
                            addsSrc = true;
                    }
                    if (addsDst || addsSrc)
                        keptPairs.push_back(pr);
                }
                if (keptPairs.empty())
                    return;
                pairs = std::move(keptPairs);
            }

            if (requireSourceFree && srcNodeIdForFreeCheck.uid != 0) {
                std::set<int> uniqueSrcs;
                for (const auto& pr : pairs)
                    uniqueSrcs.insert(pr.first);
                for (int s : uniqueSrcs) {
                    if (!isOutputJackFree(srcNodeIdForFreeCheck, s, false))
                        return;
                }
            }

            for (const auto& [srcJack, dstJack] : pairs) {
                const int pairScore = scoreSmartPair(srcMb, srcJack, dstMb, dstJack);
                if (pairScore < 0)
                    continue;

                SmartSuggestion s;
                s.ghostIsSource = ghostIsSource;
                s.neighborId = neighbor->getNodeId();
                s.ghostJack = ghostIsSource ? srcJack : dstJack;
                s.neighborJack = ghostIsSource ? dstJack : srcJack;
                s.isMidi = false;
                const auto srcPt = jackPoint(ghostIsSource, srcJack, false, false);
                const auto dstPt = jackPoint(!ghostIsSource, dstJack, true, false);
                s.p1 = srcPt;
                s.p2 = dstPt;
                s.signal = signalForRoles(false, primaryRoleForJack(srcMb, srcJack, false));
                if (auto* smb = dynamic_cast<ModuleBase*>(srcProc))
                    s.sourceCategory = synth::ui::categoryFor(smb->getModuleType());

                // What the drop will really wire — one preview segment per DRAWN cable, which for a
                // collapsed jack landing on the terminal sink is two, not one.
                const JackPointFn ghostPointFn = [&](int jack, bool isInput) {
                    return jackPoint(/*fromGhost=*/true, jack, isInput, false);
                };
                const JackPointFn neighborPointFn = [&](int jack, bool isInput) {
                    return jackPoint(/*fromGhost=*/false, jack, isInput, false);
                };
                s.mainPreviewLegs =
                    resolveDrawnLegs(srcProc, srcJack, dstProc, dstJack, ghostIsSource ? ghostPointFn : neighborPointFn,
                                     ghostIsSource ? neighborPointFn : ghostPointFn);

                int score = pairScore;
                if (srcLegs.size() >= 2 && dstLegs.size() >= 2 && srcJack == srcLegs[0] && dstJack == dstLegs[0])
                    score += 2;

                // An insert scores like the plain cable it replaces, so it competes with (and can
                // lose to) a neighbour offering a free jack instead of always winning by novelty.
                if (insertPlan.has_value()) {
                    s.isInsert = true;
                    s.upstreamId = insertPlan->upstreamId;
                    s.doomedLinks = insertPlan->doomedLinks;
                    s.upstreamCables = insertPlan->upstreamCables;
                    s.upstreamPreviewLegs = insertPlan->upstreamPreviewLegs;
                    s.upstreamCategory = insertPlan->upstreamCategory;
                }

                audioCandidates.push_back({s, score, srcPt.getDistanceFrom(dstPt), false});
            }
        };

        // Ghost outputs → neighbor inputs, then neighbor outputs → ghost inputs.
        pushAudioGroup(true, ghostProc, neighborProc, dragPreviewSelfId, neighbor->getNodeId(), true);
        {
            const auto ghostDstId =
                dragPreviewSelfId.uid != 0 ? dragPreviewSelfId : juce::AudioProcessorGraph::NodeID{};
            pushAudioGroup(false, neighborProc, ghostProc, neighbor->getNodeId(), ghostDstId,
                           dragPreviewSelfId.uid != 0);
        }

        // MIDI
        auto considerMidi = [&](bool ghostIsSource) {
            const juce::String ghostName = ghostProc->getName();
            const juce::String neighborName = neighborProc->getName();
            if (ghostIsSource) {
                if (!ghostProducesMidi || !neighborProc->acceptsMidi())
                    return;
                if (!isKnownMidiSourceName(ghostName) || !isKnownMidiDestName(neighborName))
                    return;
                if (!isInputJackFree(neighbor->getNodeId(), 0, true))
                    return;
                if (dragPreviewSelfId.uid != 0 &&
                    areJacksAlreadyConnected(dragPreviewSelfId, 0, neighbor->getNodeId(), 0, true))
                    return;
            } else {
                if (!neighborProc->producesMidi() || !ghostAcceptsMidi)
                    return;
                if (!isKnownMidiSourceName(neighborName) || !isKnownMidiDestName(ghostName))
                    return;
                if (dragPreviewSelfId.uid != 0 && !isInputJackFree(dragPreviewSelfId, 0, true))
                    return;
                if (dragPreviewSelfId.uid != 0 &&
                    areJacksAlreadyConnected(neighbor->getNodeId(), 0, dragPreviewSelfId, 0, true))
                    return;
            }

            const auto srcPt = jackPoint(ghostIsSource, 0, false, true);
            const auto dstPt = jackPoint(!ghostIsSource, 0, true, true);
            if (srcPt.x > dstPt.x + 8.0f)
                return;
            const float jackDist = srcPt.getDistanceFrom(dstPt);
            if (jackDist > kSmartConnectionProximityPx)
                return;

            if (requireSourceFree) {
                const auto srcId = ghostIsSource ? dragPreviewSelfId : neighbor->getNodeId();
                if (srcId.uid != 0 && !isOutputJackFree(srcId, 0, true))
                    return;
            }

            int score = 4;

            SmartSuggestion s;
            s.ghostIsSource = ghostIsSource;
            s.neighborId = neighbor->getNodeId();
            s.ghostJack = 0;
            s.neighborJack = 0;
            s.isMidi = true;
            s.p1 = srcPt;
            s.p2 = dstPt;
            s.signal = synth::ui::CableSignal::Midi;
            if (auto* smb = dynamic_cast<ModuleBase*>(ghostIsSource ? ghostProc : neighborProc))
                s.sourceCategory = synth::ui::categoryFor(smb->getModuleType());

            midiCandidates.push_back({s, score, jackDist, true});
        };
        considerMidi(true);
        considerMidi(false);
    }

    auto pickBest = [](std::vector<Candidate>& list) -> std::optional<SmartSuggestion> {
        if (list.empty())
            return std::nullopt;
        std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.distance < b.distance;
        });
        return list.front().suggestion;
    };

    // Audio: keep every suggestion that shares the winning neighbor + direction (stereo L/R
    // pairs and mono↔stereo fans are multiple candidates with the same neighborId/ghostIsSource).
    if (!audioCandidates.empty()) {
        // Only ONE neighbour's group survives this sort, so the ordering decides which offer the user
        // gets. Two competing pressures, and getting either wrong is a bug we have already shipped:
        //
        //  * A ghost being spliced into a cable sits between two cards that are BOTH valid
        //    neighbours — the upstream it is being inserted AFTER also offers a perfectly good plain
        //    "feed the new module" cable. That plain offer was winning on proximity and discarding
        //    the insert, which is what made Ctrl+drag near the Audio Output look flaky (whether the
        //    sink or the upstream won flipped with small cursor moves, so nudging down "fixed" it).
        //    It surfaced at the sink specifically because a bare AudioGraphIOProcessor scores a flat
        //    2 in scoreSmartPair, so it loses on SCORE to any real module before distance matters.
        //
        //  * But making an insert beat EVERY plain candidate outright was an overcorrection: with
        //    Ctrl held, an insert into some occupied module across the canvas then stole the drop
        //    from the free module the user was actually aiming at, so Ctrl+drag stopped connecting
        //    anything ordinary.
        //
        // So the demotion is targeted rather than global: a plain candidate loses only when its
        // neighbour is the very upstream an insert wants to reroute. Every other plain candidate
        // still competes with the insert on score and distance, so aim wins.
        std::set<juce::uint32> insertUpstreamUids;
        for (const auto& c : audioCandidates)
            if (c.suggestion.isInsert)
                insertUpstreamUids.insert(c.suggestion.upstreamId.uid);

        const auto rankOf = [&insertUpstreamUids](const Candidate& c) {
            if (c.suggestion.isInsert)
                return 1;
            // A plain offer FROM the cable's upstream is the one thing that must never mask the
            // insert: it is the same gesture read two ways, and the insert is the explicit one.
            return insertUpstreamUids.count(c.suggestion.neighborId.uid) > 0 ? 0 : 1;
        };

        std::sort(audioCandidates.begin(), audioCandidates.end(), [&rankOf](const Candidate& a, const Candidate& b) {
            const int ra = rankOf(a), rb = rankOf(b);
            if (ra != rb)
                return ra > rb;
            if (a.score != b.score)
                return a.score > b.score;
            return a.distance < b.distance;
        });
        const auto& best = audioCandidates.front();
        for (const auto& c : audioCandidates) {
            if (c.suggestion.neighborId != best.suggestion.neighborId)
                continue;
            if (c.suggestion.ghostIsSource != best.suggestion.ghostIsSource)
                continue;
            if (c.suggestion.isInsert != best.suggestion.isInsert)
                continue; // never mix a reroute and a plain add in one applied group
            smartSuggestions.push_back(c.suggestion);
        }
    }
    if (auto best = pickBest(midiCandidates))
        smartSuggestions.push_back(*best);

    if (smartSuggestions != previous)
        repaintCanvas();
}

void GraphEditor::applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo) {
    if (smartSuggestions.empty() || ghostNodeId.uid == 0)
        return;

    auto applyAll = [this, ghostNodeId] {
        for (const auto& s : smartSuggestions) {
            if (s.isInsert) {
                // Reroute, never double: drop EVERY doomed cable first so the sink's jacks are free
                // for the ghost's output, then wire upstream → ghost → sink. Dropping only this
                // leg's cable would leave the other one summing into the sink beside the ghost.
                // Both sets are group-wide and deduped, so a second insert suggestion re-running
                // them is a no-op. All of it shares the caller's transaction — one undo, one step.
                for (const auto& doomed : s.doomedLinks)
                    disconnectAudioLink(s.upstreamId, doomed.fromJack, s.neighborId, doomed.toJack);
                for (const auto& cable : s.upstreamCables)
                    connectPorts(s.upstreamId, cable.fromJack, ghostNodeId, cable.toJack, false, false);
                connectPorts(ghostNodeId, s.ghostJack, s.neighborId, s.neighborJack, false, false);
                continue;
            }

            const auto srcId = s.ghostIsSource ? ghostNodeId : s.neighborId;
            const auto dstId = s.ghostIsSource ? s.neighborId : ghostNodeId;
            const int srcJack = s.ghostIsSource ? s.ghostJack : s.neighborJack;
            const int dstJack = s.ghostIsSource ? s.neighborJack : s.ghostJack;
            connectPorts(srcId, srcJack, dstId, dstJack, s.isMidi, false);
        }
    };

    if (recordUndo && undoManager)
        undoManager->recordStructuralChange(audioEngine.getGraph(), applyAll);
    else
        applyAll();

    clearSmartSuggestions();
}

void GraphEditor::detachAllModuleComponents() {
    // A teardown can't be allowed to leave the settle animator holding a SafePointer to a card
    // set that no longer applies. Harmless either way (SafePointer guards it), but keeps the
    // zoomGestureActive state machine honest.
    endZoomGesture();
    for (auto* comp : content.getModules())
        comp->detachFromProcessor();
    content.getModules().clear(); // Remove after detach so ~ModuleComponent doesn't double-detach freed params
    modMatrix.detachAllRows();
    modMatrix.clearRows();
}

void GraphEditor::updateComponents() {
    auto& graph = audioEngine.getGraph();
    auto& modules = content.getModules();

    // Nodes appear/disappear here, so the cable memo can go stale from this call alone (a repaint
    // is not guaranteed to follow immediately).
    cablesCacheValid = false;

    // Any reconcile can follow a node removal (delete, undo/redo, preset load). Drop selected ids
    // whose nodes are gone BEFORE anything reads the selection again.
    pruneSelection();

    // 1. Remove components for nodes that no longer exist
    for (int i = modules.size(); --i >= 0;) {
        auto* comp = modules.getUnchecked(i);
        bool stillExists = false;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor() == comp->getModule()) {
                stillExists = true;
                break;
            }
        }
        if (!stillExists) {
            content.removeChildComponent(comp);
            modules.remove(i);
        }
    }

    // 2. Add components for new nodes
    int moduleIndex = 0;
    for (auto* node : graph.getNodes()) {
        auto* processor = node->getProcessor();
        if (!processor)
            continue;

        if (dynamic_cast<AttenuverterModule*>(processor) != nullptr)
            continue;

        // Check if we already have a component for this module
        ModuleComponent* existingComp = nullptr;
        for (auto* comp : modules) {
            if (comp->getModule() == processor) {
                existingComp = comp;
                break;
            }
        }

        if (existingComp == nullptr) {
            auto* newComp = modules.add(new ModuleComponent(processor, node->nodeID, *this, undoManager));
            content.addAndMakeVisible(newComp);
            existingComp = newComp;
        }

        // Always sync position from properties OR deterministic fallback
        auto x = node->properties.getWithDefault("x", -1);
        auto y = node->properties.getWithDefault("y", -1);

        if (static_cast<int>(x) != -1 && static_cast<int>(y) != -1) {
            existingComp->setTopLeftPosition(static_cast<int>(x), static_cast<int>(y));
        } else {
            // Fallback: no stored position — resolve a non-overlapping slot.
            // Desired origin strides by 300px to reduce clustering; resolvePlacement
            // then snaps and spirals clear of any previously placed components.
            auto desired = juce::Point<int>(synth::LayoutUtil::kArrangeOriginX + moduleIndex * 300, 600);
            auto clear = resolvePlacement(desired, existingComp->getWidth(), existingComp->getHeight(), node->nodeID);
            existingComp->setTopLeftPosition(clear);
            // Persist the resolved position so subsequent loads don't need to re-resolve
            node->properties.set("x", clear.x);
            node->properties.set("y", clear.y);
        }

        moduleIndex++;
    }

    // Reconcile macro membership against whatever nodes actually survived — the MacroSet
    // analogue of pruneSelection() above, and the ONE seam that keeps `macros` from ever
    // naming a dead node, regardless of which delete/undo/preset-load path got here.
    {
        std::vector<juce::String> aliveUuids;
        for (auto* node : graph.getNodes()) {
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                aliveUuids.push_back(uuid);
        }
        macros.retainOnly(aliveUuids);
    }
    syncMacroCards();

    // Refresh mod matrix to pick up any new/removed attenuverter routings
    // Use callAsync to avoid re-entrancy during graph modification
    // SafePointer guards against the GraphEditor being destroyed before the callback fires
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->modMatrix.updateRowsFromGraph();
    });

    // Let owners refresh anything that depends on which modules the patch now contains. Event-driven
    // on purpose: no timer and no per-tick repaint.
    if (onGraphStructureChanged)
        onGraphStructureChanged();

    // A card created mid-gesture (e.g. paste/duplicate while zooming) must join the freeze, or it
    // rasterizes once at the pre-gesture scale and then again at thaw instead of just once.
    if (zoomGestureActive)
        setModuleRasterFrozen(true);

    repaint();
}

void GraphEditor::paint(juce::Graphics& g) {
    // GraphEditor itself can draw a background or overlay if needed
    // But content handles it now.
}

void GraphEditor::paintOverChildren(juce::Graphics& g) {
    // ---- Empty-canvas first-run hint ----
    // Drawn here (OUTER, untransformed GraphEditor local coordinates) so it is ALWAYS
    // centred in the visible viewport regardless of pan/zoom on the inner canvas.
    // The inner GraphContentComponent runs in a transformed (pan+zoom) space over a
    // ~10000x10000 virtual canvas — any rect drawn there would land off-screen once the
    // user pans or zooms. Drawing here, in getLocalBounds(), guarantees centre alignment.
    //
    // Gate: only when canvas is empty. Show/hide is driven by the existing updateComponents()
    // repaint path — no extra timer or per-tick repaint is added.
    if (!GraphEditor::isCanvasEmpty(static_cast<int>(content.getModules().size())))
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // textMuted token at ~60% alpha — tasteful, non-distracting.
    const juce::Colour textMutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;
    g.setColour(textMutedColour.withAlpha(0.6f));

    // Use the theme h1 font (~18pt) for comfortable legibility; fall back to 16pt headless.
    juce::Font hintFont;
    if (lf != nullptr)
        hintFont = juce::Font(juce::FontOptions(lf->getTheme().type.h1)).withStyle(juce::Font::plain);
    else
        hintFont = juce::Font(16.0f);
    g.setFont(hintFont);

    // Centre in the GraphEditor's visible local bounds (untransformed viewport coordinates).
    g.drawFittedText("Drag modules here to build your patch", getLocalBounds(), juce::Justification::centred, 1);
    // ---- End empty-canvas hint ----
}

void GraphEditor::resized() {
    // Mitigates the case where a zoom gesture's settle animator never completes (no VBlank, e.g.
    // the window is hidden mid-gesture): a resize is a good proxy for "something else is about to
    // repaint everything anyway", so thaw now rather than leave every card soft indefinitely.
    endZoomGesture();

    // Only set the mod-matrix bounds when we are not in the middle of an animated show/hide.
    // If an animation is running, it owns the bounds until it completes; we update the target
    // but don't interrupt the tween.
    if (!modMatrixAnim.isRunning()) {
        if (isMatrixVisible) {
            modMatrix.setBounds(getWidth() - 600, 0, 600, getHeight());
        }
    } else {
        // Update the stored target so the onComplete callback uses the updated size.
        modMatrixTargetBounds = isMatrixVisible ? juce::Rectangle<int>(getWidth() - 600, 0, 600, getHeight())
                                                : juce::Rectangle<int>(getWidth(), 0, 600, getHeight());
    }

    // ---- Minimap (issue #159) ----
    // Bottom-LEFT with a 12px margin — the mod-matrix panel occupies a 600px panel on the right.
    // Auto-hide when the editor is too small to show it without swallowing the view, but never
    // clobber the user's preference: `minimapVisible` still reflects what they asked for, and
    // resized() just recomputes whether that preference currently fits.
    {
        constexpr int kMargin = 12;
        // Absolute floors, not a fraction of the editor: a fraction-of-self test is always
        // satisfied (w/4 * 2 <= w for any w), so it would never actually hide anything.
        constexpr int kMinEditorW = 480, kMinEditorH = 360;
        const int mmW = juce::jmin(220, getWidth() / 4);
        const int mmH = juce::jmin(150, getHeight() / 4);
        const bool fits = getWidth() >= kMinEditorW && getHeight() >= kMinEditorH;
        minimap.setBounds(kMargin, getHeight() - mmH - kMargin, mmW, mmH);
        minimap.setVisible(minimapVisible && fits);
    }

    updateTransform();
}

void GraphEditor::lookAndFeelChanged() {
    // Same rationale as resized(): a theme swap re-skins/re-rasters everything anyway, so a zoom
    // gesture straddling a theme change must not leave cards frozen at the old scale afterwards.
    endZoomGesture();
}

void GraphEditor::toggleModMatrixVisibility() {
    isMatrixVisible = !isMatrixVisible;

    // Always make it visible before the animation so it paints during the tween.
    // On hide we keep it visible until the animation completes, then hide it.
    modMatrix.setVisible(true);

    const int panelW = 600;
    const int editorW = getWidth();
    const int editorH = getHeight();

    // From-bounds: current position (either fully shown or fully hidden off-screen right).
    juce::Rectangle<int> fromBounds = modMatrix.getBounds();
    // If the component has never been laid out, give it a sensible off-screen start.
    if (fromBounds.isEmpty())
        fromBounds = {editorW, 0, panelW, editorH};

    // To-bounds: target position.
    juce::Rectangle<int> toBounds = isMatrixVisible ? juce::Rectangle<int>(editorW - panelW, 0, panelW, editorH)
                                                    : juce::Rectangle<int>(editorW, 0, panelW, editorH);

    modMatrixTargetBounds = toBounds;

    // Start a 220 ms easeOutCubic tween on modMatrix bounds.
    const bool hidingAfterAnim = !isMatrixVisible;
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    modMatrixAnim.start(
        vblankUpdater, 220.0, synth::ui::easeOutCubic,
        [safeThis, fromBounds, toBounds](float t) {
            if (auto* self = safeThis.getComponent())
                self->modMatrix.setBounds(synth::ui::AnimationDriver::lerpBounds(fromBounds, toBounds, t));
        },
        [safeThis, hidingAfterAnim, toBounds]() {
            if (auto* self = safeThis.getComponent()) {
                self->modMatrix.setBounds(toBounds);
                if (hidingAfterAnim)
                    self->modMatrix.setVisible(false);
            }
        });
}

void GraphEditor::updateTransform() {
    juce::AffineTransform t;
    t = t.scaled(zoomLevel, zoomLevel);
    t = t.translated(panOffset);

    content.setBounds(0, 0, 10000, 10000);
    content.setTransform(t);
    repaint();

    // Keep the minimap tracking pan/zoom immediately rather than waiting up to 33ms for the next
    // timer tick. Only the viewport rect is pushed here: pan/zoom move what you're LOOKING at, not
    // where the modules and cables are, so rebuilding the full model on every drag frame would
    // re-walk every graph edge for nothing. The 30 Hz tick owns node/cable changes.
    if (minimap.isVisible())
        minimap.setViewport(getVisibleCanvasRect());
}

void GraphEditor::applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor) {
    float oldZoom = zoomLevel;
    zoomLevel += wheelDelta * 0.1f * zoomLevel;
    zoomLevel = juce::jlimit(0.1f, 2.0f, zoomLevel);

    if (oldZoom != zoomLevel) {
        // Transform the anchor position to get the graph point before scaling
        auto invT =
            juce::AffineTransform::translation(-panOffset.x, -panOffset.y).scaled(1.0f / oldZoom, 1.0f / oldZoom);
        float gx = screenAnchor.x;
        float gy = screenAnchor.y;
        invT.transformPoint(gx, gy);

        // We want to keep the graph point under the anchor constant:
        // anchor = (graphPointBefore * zoomLevel) + newPanOffset
        // newPanOffset = anchor - (graphPointBefore * zoomLevel)
        panOffset.x = screenAnchor.x - (gx * zoomLevel);
        panOffset.y = screenAnchor.y - (gy * zoomLevel);
    }

    updateTransform();

    // Only a real scale change costs a re-raster; a clamped wheel tick at the 0.1/2.0 limits
    // must not keep the cards soft forever.
    if (oldZoom != zoomLevel)
        beginOrRefreshZoomGesture();
}

void GraphEditor::setModuleRasterFrozen(bool frozen) {
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            comp->setRasterFrozen(frozen);
}

void GraphEditor::beginOrRefreshZoomGesture() {
    if (!zoomGestureActive) {
        zoomGestureActive = true;
        setModuleRasterFrozen(true);
    }
    // start() stops+replaces any running animator, so every zoom event restarts the settle
    // window: this IS the debounce. onUpdate is a no-op — the driver adds no repaint source.
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    zoomSettleAnim.start(
        vblankUpdater, kZoomSettleMs, [](float t) { return t; }, [](float) {},
        [safeThis] {
            if (auto* self = safeThis.getComponent())
                self->endZoomGesture();
        });
}

void GraphEditor::endZoomGesture() {
    if (!zoomGestureActive)
        return;
    zoomGestureActive = false;
    // Thawing each card drops its image and repaints it, so the crisp pass costs exactly one
    // rasterization per visible card for the whole gesture.
    setModuleRasterFrozen(false);
}

void GraphEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    applyZoomAt(wheel.deltaY, e.position);
}

juce::Rectangle<float> GraphEditor::getVisibleCanvasRect() const {
    // Rebuilt from zoomLevel/panOffset rather than reading content.getTransform(), so this stays
    // independent of child state and can be const.
    const juce::AffineTransform t = juce::AffineTransform().scaled(zoomLevel, zoomLevel).translated(panOffset);
    return getLocalBounds().toFloat().transformedBy(t.inverted());
}

void GraphEditor::centreViewOn(juce::Point<float> canvasPoint) {
    panOffset = getLocalBounds().getCentre().toFloat() - canvasPoint * zoomLevel;
    updateTransform();
}

void GraphEditor::zoomAroundCentre(float wheelDelta) {
    applyZoomAt(wheelDelta, getLocalBounds().getCentre().toFloat());
}

void GraphEditor::setMinimapVisible(bool shouldBeVisible) {
    minimapVisible = shouldBeVisible;
    // resized() recomputes the effective (preference && fits) visibility.
    resized();
    // Seed the full model on the way in: updateTransform() only pushes the viewport, so without
    // this the map would show an empty canvas until the next 30 Hz tick.
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());
}

void GraphEditor::toggleMinimapVisibility() { setMinimapVisible(!minimapVisible); }

synth::ui::MinimapModel GraphEditor::buildMinimapModel() {
    synth::ui::MinimapModel model;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;

    for (auto* comp : content.getModules()) {
        if (comp == nullptr || comp->getModule() == nullptr)
            continue;

        // Per-category theme colour, the same source buildVisibleCables()/colourForCable() use for
        // "By source module" cable colouring — there is no cheap per-instance colour on
        // ModuleComponent itself, but category IS available cheaply via ModuleBase::getModuleType().
        auto category = synth::ui::ModuleCategory::Utility;
        if (auto* mb = dynamic_cast<ModuleBase*>(comp->getModule()))
            category = synth::ui::categoryFor(mb->getModuleType());

        synth::ui::MinimapModel::Node node;
        node.bounds = comp->getBounds().toFloat();
        node.colour = synth::ui::themeColourForCategory(colors, category);
        node.selected = isNodeSelected(comp->getNodeId());
        model.nodes.push_back(node);
    }

    for (const auto& cable : buildVisibleCables()) {
        synth::ui::MinimapModel::Cable mc;
        mc.p1 = cable.p1;
        mc.p2 = cable.p2;
        mc.colour = colourForCable(cable);
        model.cables.push_back(mc);
    }

    model.viewport = getVisibleCanvasRect();
    return model;
}

void GraphEditor::mouseMove(const juce::MouseEvent& e) {
    auto localPos = content.getLocalPoint(this, e.getPosition());

    std::optional<CableId> newId;
    if (auto cable = getCableAt(localPos.toFloat()))
        newId = cable->id;

    // Repaint only when the hovered cable actually CHANGES, not on every mouse move. (The canvas
    // already repaints at 30Hz for the wire animation, so this just marks the next frame dirty
    // rather than introducing a new repaint source — see the no-continuous-repaint invariant.)
    const bool changed =
        newId.has_value() != hoveredCableId.has_value() || (newId.has_value() && *newId != *hoveredCableId);
    if (!changed)
        return;

    hoveredCableId = newId;
    setMouseCursor(newId.has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaintCanvas();
}

void GraphEditor::mouseExit(const juce::MouseEvent&) {
    if (!hoveredCableId.has_value())
        return;
    hoveredCableId.reset();
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaintCanvas();
}

void GraphEditor::mouseDown(const juce::MouseEvent& e) {
    // Clicking away from an inline title editor commits it. Done explicitly and FIRST rather than
    // leaning on the grabKeyboardFocus() below to fire onFocusLost: that ordering happens to work
    // for a canvas press but is invisible coupling, and it does nothing for a press on a card
    // (ModuleComponent never grabs focus). Idempotent — whichever path runs second finds no editor.
    commitAnyOpenTitleRename();

    // Any press on the canvas takes focus, so the canvas-scoped Delete/Escape keys land here
    // rather than on whichever panel happened to be focused last. Must come BEFORE the cable
    // menu below, which returns early — otherwise a right-click on a wire would skip it.
    grabKeyboardFocus();

    // Right-click on a cable: act on the wire itself. Until this existed the only way to remove
    // a connection was to right-click one of its PORTS, which is not where users aim.
    if (e.mods.isPopupMenu()) {
        auto canvasPos = content.getLocalPoint(this, e.getPosition()).toFloat();
        if (auto cable = getCableAt(canvasPos)) {
            const auto captured = *cable;
            juce::Component::SafePointer<GraphEditor> safeThis(this);

            juce::PopupMenu m;
            m.addSectionHeader(juce::String(synth::ui::cableSignalLabel(captured.signal)) + " cable");
            m.addItem("Disconnect Cable", [safeThis, captured] {
                if (safeThis != nullptr)
                    safeThis->disconnectCable(captured);
            });
            m.showMenuAsync(juce::PopupMenu::Options());
            return;
        }

        // Nothing under the cursor: the canvas menu, which is how paste is reachable without the
        // keyboard. Right-clicking empty canvas leaves the selection alone (see mouseUp), so a
        // paste from here still knows what was selected.
        showCanvasContextMenu(canvasPos.roundToInt());
        return;
    }

    if (e.mods.isLeftButtonDown()) {
        lastMousePos = e.getPosition();
        pendingEmptyCanvasClick = false;

        auto localPos = content.getLocalPoint(this, e.getPosition());
        auto attenId = getAttenuverterNodeAt(localPos.toFloat());
        if (attenId.uid != 0) {
            draggingAttenuverterNodeId = attenId;
            if (undoManager)
                undoManager->captureBeforeState(audioEngine.getGraph());
            return;
        }

        draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

        // Shift starts a marquee; anything else keeps the historical drag-to-pan behaviour.
        // Cmd/Ctrl alongside Shift makes the marquee additive.
        if (e.mods.isShiftDown()) {
            beginMarquee(localPos.roundToInt(), e.mods.isCommandDown() || e.mods.isCtrlDown());
        } else {
            // Deferred: only a press that never becomes a drag counts as "click empty canvas to
            // deselect", so panning does not wipe the selection.
            pendingEmptyCanvasClick = true;
        }
    }
}

void GraphEditor::mouseDrag(const juce::MouseEvent& e) {
    if (marqueeActive) {
        updateMarquee(content.getLocalPoint(this, e.getPosition()).roundToInt());
        return;
    }

    if (e.mods.isLeftButtonDown() && !isDraggingConnection) {
        pendingEmptyCanvasClick = false;
        if (draggingAttenuverterNodeId.uid != 0) {
            auto& graph = audioEngine.getGraph();
            auto* node = graph.getNodeForId(draggingAttenuverterNodeId);
            if (node) {
                if (auto* p =
                        dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))) {
                    float delta = (e.getPosition().y - lastMousePos.y) * -0.01f;
                    float currentVal = p->get(); // -1 to 1
                    currentVal = juce::jlimit(-1.0f, 1.0f, currentVal + delta);
                    p->setValueNotifyingHost(p->convertTo0to1(currentVal));
                    repaintCanvas();
                }
            }
            lastMousePos = e.getPosition();
            return;
        }

        auto delta = e.getPosition() - lastMousePos;
        panOffset += delta.toFloat();
        lastMousePos = e.getPosition();
        updateTransform();
    }
}

void GraphEditor::mouseUp(const juce::MouseEvent& e) {
    if (marqueeActive) {
        endMarquee();
        return;
    }

    if (draggingAttenuverterNodeId.uid != 0 && undoManager) {
        undoManager->pushSnapshotFromCapture(audioEngine.getGraph());
    }
    draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

    // A press on empty canvas that never turned into a pan is a plain click: deselect.
    if (pendingEmptyCanvasClick) {
        pendingEmptyCanvasClick = false;
        if (e.mods.isPopupMenu())
            return; // right-click keeps the selection so the context menu can act on it
        clearSelection();
    }
}

// ---------------------------------------------------------------------------------------
// Selection (issue #156)
// ---------------------------------------------------------------------------------------

std::vector<synth::LayoutUtil::Box> GraphEditor::collectModuleBoxes(bool selectedOnly, bool excludeSelected) const {
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : const_cast<GraphContentComponent&>(content).getModules()) {
        // A hidden member of a collapsed macro is not "on the canvas" as far as marquee
        // hit-testing or group-drag collision are concerned — the visible collapsed card stands
        // in for it (see syncMacroCards).
        if (comp == nullptr || comp->getModule() == nullptr || !comp->isVisible())
            continue;
        const bool isSel = selection.contains(comp->getNodeId());
        if (selectedOnly && !isSel)
            continue;
        if (excludeSelected && isSel)
            continue;
        boxes.push_back({comp->getNodeId(), comp->getBounds()});
    }
    return boxes;
}

void GraphEditor::applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection) {
    std::set<juce::AudioProcessorGraph::NodeID> before;
    for (auto id : selection.getSelected())
        before.insert(id);

    selection.setSelection(newSelection);

    std::set<juce::AudioProcessorGraph::NodeID> after;
    for (auto id : selection.getSelected())
        after.insert(id);

    if (before == after)
        return;

    // Repaint ONLY the cards that changed state. ModuleComponent is setBufferedToImage(true), so a
    // blanket repaint of every card would re-rasterize the whole canvas on each marquee frame.
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const auto id = comp->getNodeId();
        if ((before.count(id) > 0) != (after.count(id) > 0))
            comp->repaint();
    }
}

void GraphEditor::selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive) {
    if (nodeId.uid == 0)
        return;

    auto next = selection.getSelected();
    if (additive) {
        if (selection.contains(nodeId))
            next.erase(std::remove(next.begin(), next.end(), nodeId), next.end());
        else
            next.push_back(nodeId);
    } else {
        next = {nodeId};
    }
    applySelectionChange(next);
}

void GraphEditor::setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) {
    applySelectionChange(ids);
}

void GraphEditor::clearSelection() { applySelectionChange({}); }

void GraphEditor::selectAllModules() {
    std::vector<juce::AudioProcessorGraph::NodeID> all;
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && comp->getModule() != nullptr)
            all.push_back(comp->getNodeId());
    }
    applySelectionChange(all);
}

void GraphEditor::pruneSelection() {
    if (selection.isEmpty())
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> alive;
    for (auto* node : audioEngine.getGraph().getNodes())
        alive.push_back(node->nodeID);

    if (selection.retainOnly(alive))
        repaintCanvas();
}

void GraphEditor::deleteSelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return;

    auto& graph = audioEngine.getGraph();

    // One transaction for the whole group: Cmd+Z must bring back every module at once, not peel
    // them back one at a time. Deleting a collapsed macro card's members (see
    // deleteMacroAndMembers) flows through here too, so the macro half of the change has to be
    // captured in the SAME undo step as the graph half — recordGraphAndMacroChange, not
    // recordStructuralChange. updateComponents() prunes `macros` against whatever survives.
    auto doDelete = [this, ids, &graph] {
        modMatrix.clearRows();
        for (auto id : ids)
            graph.removeNode(id);
        selection.clear();
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doDelete);
    else
        doDelete();

    repaint();
}

// ---- Marquee ----

void GraphEditor::beginMarquee(juce::Point<int> canvasAnchor, bool additive) {
    marqueeActive = true;
    marqueeAdditive = additive;
    marqueeAnchor = canvasAnchor;
    marqueeRect = juce::Rectangle<int>(canvasAnchor, canvasAnchor);
    marqueeBaseSelection = additive ? selection.getSelected() : std::vector<juce::AudioProcessorGraph::NodeID>{};

    // A non-additive marquee starts from nothing, so dragging over no module deselects.
    if (!additive)
        applySelectionChange({});
}

void GraphEditor::updateMarquee(juce::Point<int> canvasCurrent) {
    if (!marqueeActive)
        return;

    marqueeRect = synth::ui::marqueeRectFrom(marqueeAnchor, canvasCurrent);
    auto hits = synth::ui::hitTestMarquee(marqueeRect, collectModuleBoxes(false, false));
    applySelectionChange(marqueeAdditive ? synth::ui::unionSelection(marqueeBaseSelection, hits) : hits);

    repaintCanvas();
}

void GraphEditor::endMarquee() {
    if (!marqueeActive)
        return;
    marqueeActive = false;
    marqueeAdditive = false;
    marqueeRect = {};
    marqueeBaseSelection.clear();
    repaintCanvas();
}

// ---- Group drag ----

void GraphEditor::beginSelectionDrag() {
    selectionDragStartPositions.clear();
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && selection.contains(comp->getNodeId()))
            selectionDragStartPositions.emplace_back(comp->getNodeId(), comp->getPosition());
    }
    selectionDragActive = selectionDragStartPositions.size() > 1;
}

void GraphEditor::dragSelectionBy(juce::Point<int> delta, ModuleComponent* initiator) {
    if (!selectionDragActive)
        return;

    // Place each follower from ITS OWN recorded origin. Applying per-frame deltas incrementally
    // would accumulate rounding error at non-1.0 zoom and shear the group apart.
    for (auto& [nodeId, startPos] : selectionDragStartPositions) {
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp == initiator || comp->getNodeId() != nodeId)
                continue;
            comp->setTopLeftPosition(startPos + delta);
            break;
        }
    }
}

void GraphEditor::finalizeSelectionDrag() {
    if (!selectionDragActive) {
        selectionDragStartPositions.clear();
        return;
    }

    // Resolve the group as a single rigid body: snap and de-overlap its bounding box, then apply
    // that one offset to every member. Running finalizeModuleDrag() per module would let members
    // spiral away from each other and destroy the layout the user arranged.
    std::vector<ModuleComponent*> members;
    juce::Rectangle<int> groupBounds;
    for (auto& [nodeId, startPos] : selectionDragStartPositions) {
        juce::ignoreUnused(startPos);
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp->getNodeId() != nodeId)
                continue;
            members.push_back(comp);
            groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
            break;
        }
    }

    if (!members.empty() && !groupBounds.isEmpty()) {
        auto snapped = synth::LayoutUtil::snap(groupBounds.getTopLeft());
        // Collide against unselected modules only — members are moving together and must not be
        // treated as obstacles by one another.
        auto obstacles = collectModuleBoxes(/*selectedOnly=*/false, /*excludeSelected=*/true);
        auto clear = synth::LayoutUtil::findFreeSlot(snapped, groupBounds.getWidth(), groupBounds.getHeight(),
                                                     obstacles, juce::AudioProcessorGraph::NodeID{});
        auto offset = clear - groupBounds.getTopLeft();

        for (auto* comp : members) {
            comp->setTopLeftPosition(comp->getPosition() + offset);
            updateModulePosition(comp);
        }
    }

    selectionDragActive = false;
    selectionDragStartPositions.clear();
    repaintCanvas();
}

void GraphEditor::cancelSelectionDrag() {
    selectionDragActive = false;
    selectionDragStartPositions.clear();
}

// ---- Macros (P8-12) ----------------------------------------------------------------------

namespace {
// The collapsed card's fixed footprint — deliberately independent of however large or scattered
// the group it stands in for is; that is the whole point of collapsing. Matches a standard
// module card's width so it sits comfortably on the same grid.
constexpr int kMacroCardHeight = 90;
} // namespace

juce::String GraphEditor::nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const {
    if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
        return node->properties["uuid"].toString();
    return {};
}

juce::AudioProcessorGraph::NodeID GraphEditor::resolveMemberNodeId(const juce::String& memberUuid) const {
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node->properties["uuid"].toString() == memberUuid)
            return node->nodeID;
    }
    return {};
}

void GraphEditor::syncMacroCards() {
    auto& cards = content.getMacroCards();

    // 1. Remove cards for macros that no longer exist.
    for (int i = cards.size(); --i >= 0;) {
        auto* card = cards.getUnchecked(i);
        if (macros.find(card->getMacroId()) == nullptr) {
            content.removeChildComponent(card);
            cards.remove(i);
        }
    }

    // 2. Add cards for new macros; every card's bounds/visibility follow its macro's collapsed
    //    state (bounds are meaningless while expanded — see synth::Macro's comment).
    for (const auto& macro : macros.getAll()) {
        MacroCardComponent* card = nullptr;
        for (auto* c : cards) {
            if (c->getMacroId() == macro.id) {
                card = c;
                break;
            }
        }
        if (card == nullptr) {
            card = cards.add(new MacroCardComponent(*this, macro.id));
            content.addAndMakeVisible(card);
        }
        card->setBounds(macro.bounds);
        card->setVisible(macro.collapsed);
    }

    // 3. A member's own ModuleComponent is hidden exactly while its macro is collapsed — kept
    //    alive (not removed), so its position keeps tracking a card drag underneath.
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const juce::String uuid = nodeUuidFor(comp->getNodeId());
        const auto* macro = uuid.isEmpty() ? nullptr : macros.findByMember(uuid);
        comp->setVisible(macro == nullptr || !macro->collapsed);
    }
}

juce::String GraphEditor::groupSelectionIntoMacro() {
    auto ids = selection.getSelected();
    if (ids.size() < 2) {
        if (onStatusMessage)
            onStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    std::vector<juce::String> memberUuids;
    juce::Rectangle<int> groupBounds;
    for (auto id : ids) {
        // A module freshly dropped onto the canvas has no "uuid" property yet — it's only ever
        // lazily assigned on first save (synth::AIStateMapper::graphToJSON). Assign it here too,
        // the same way, so grouping newly-placed modules doesn't drop them from the selection.
        auto* node = audioEngine.getGraph().getNodeForId(id);
        const juce::String uuid = node != nullptr ? synth::AIStateMapper::ensureNodeUuid(node) : juce::String();
        if (uuid.isEmpty())
            continue; // no persistent identity to group by — shouldn't happen for a real module

        if (macros.findByMember(uuid) != nullptr) {
            // Flat model, deliberately refused rather than silently merging/re-parenting — see
            // synth::Macro's class comment. A status message, not a silent no-op: Cmd+G doing
            // nothing with no explanation reads as broken, and a test can only assert "nothing
            // happened" against a no-op, which is indistinguishable from an actual bug.
            if (onStatusMessage)
                onStatusMessage("Can't group: a selected module is already in a macro. Ungroup it first.");
            return {};
        }
        memberUuids.push_back(uuid);

        for (auto* comp : content.getModules()) {
            if (comp != nullptr && comp->getNodeId() == id) {
                groupBounds = groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                break;
            }
        }
    }

    if (memberUuids.size() < 2) {
        if (onStatusMessage)
            onStatusMessage("Select at least two modules to group into a macro.");
        return {};
    }

    synth::Macro macro;
    macro.name = "Macro";
    macro.members = memberUuids;
    macro.collapsed = true;
    const auto origin = groupBounds.isEmpty() ? juce::Point<int>() : groupBounds.getTopLeft();
    macro.bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);

    auto& graph = audioEngine.getGraph();
    juce::String newId;
    auto doGroup = [this, macro, &newId] {
        newId = macros.add(macro).id;
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doGroup);
    else
        doGroup();

    if (!newId.isEmpty())
        selectMacro(newId, false);

    repaint();
    return newId;
}

void GraphEditor::ungroupSelection() {
    auto ids = selection.getSelected();
    std::set<juce::String> macroIdsToRemove;
    for (auto id : ids) {
        const juce::String uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (auto* m = macros.findByMember(uuid))
            macroIdsToRemove.insert(m->id);
    }

    if (macroIdsToRemove.empty()) {
        if (onStatusMessage)
            onStatusMessage("Select a macro's modules to ungroup it.");
        return;
    }

    auto& graph = audioEngine.getGraph();
    auto doUngroup = [this, macroIdsToRemove] {
        std::vector<juce::AudioProcessorGraph::NodeID> newSelection;
        for (const auto& macroId : macroIdsToRemove) {
            if (auto* m = macros.find(macroId)) {
                for (const auto& uuid : m->members) {
                    auto nodeId = resolveMemberNodeId(uuid);
                    if (nodeId.uid != 0)
                        newSelection.push_back(nodeId);
                }
            }
            macros.remove(macroId);
        }
        updateComponents();
        setSelectedNodes(newSelection);
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doUngroup);
    else
        doUngroup();

    repaint();
}

void GraphEditor::selectMacro(const juce::String& macroId, bool additive) {
    auto* m = macros.find(macroId);
    if (m == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    applySelectionChange(additive ? synth::ui::unionSelection(selection.getSelected(), memberIds) : memberIds);
}

bool GraphEditor::isMacroSelected(const juce::String& macroId) const {
    const auto* m = macros.find(macroId);
    if (m == nullptr || m->members.empty() || selection.size() != (int)m->members.size())
        return false;

    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0 || !selection.contains(nodeId))
            return false;
    }
    return true;
}

void GraphEditor::setMacroCollapsed(const juce::String& macroId, bool collapsed) {
    auto& graph = audioEngine.getGraph();
    auto doToggle = [this, macroId, collapsed] {
        auto* m = macros.find(macroId);
        if (m == nullptr || m->collapsed == collapsed)
            return;

        if (collapsed) {
            // Collapsing FROM expanded: seed the card at the current member bounding box's
            // top-left, sized to the standard card footprint rather than the (possibly huge)
            // group — that is the whole point of collapsing.
            juce::Rectangle<int> groupBounds;
            for (const auto& uuid : m->members) {
                auto nodeId = resolveMemberNodeId(uuid);
                for (auto* comp : content.getModules()) {
                    if (comp != nullptr && comp->getNodeId() == nodeId) {
                        groupBounds =
                            groupBounds.isEmpty() ? comp->getBounds() : groupBounds.getUnion(comp->getBounds());
                        break;
                    }
                }
            }
            const auto origin = groupBounds.isEmpty() ? m->bounds.getTopLeft() : groupBounds.getTopLeft();
            m->bounds = juce::Rectangle<int>(origin.x, origin.y, synth::LayoutUtil::kSingleWidth, kMacroCardHeight);
        }
        m->collapsed = collapsed;
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doToggle);
    else
        doToggle();

    repaint();
}

void GraphEditor::renameMacro(const juce::String& macroId, const juce::String& newName) {
    auto& graph = audioEngine.getGraph();
    auto doRename = [this, macroId, newName] {
        if (auto* m = macros.find(macroId))
            m->name = newName;
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRename);
    else
        doRename();

    syncMacroCards();
    repaint();
}

void GraphEditor::setMacroColour(const juce::String& macroId, juce::Colour colour) {
    auto& graph = audioEngine.getGraph();
    auto doRecolour = [this, macroId, colour] {
        if (auto* m = macros.find(macroId))
            m->colour = colour;
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRecolour);
    else
        doRecolour();

    syncMacroCards();
    repaint();
}

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

void GraphEditor::beginMacroCardDrag(const juce::String& macroId) {
    selectMacro(macroId, false);
    beginSelectionDrag();
}

void GraphEditor::dragMacroCardBy(const juce::String&, juce::Point<int> delta) { dragSelectionBy(delta, nullptr); }

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

// ---- Snippets ----

juce::Point<int> GraphEditor::estimateSnippetSize(const juce::String& payload) const {
    // Fallback footprint when the snippet can't be resolved or carries no placeable nodes.
    const juce::Point<int> fallback{synth::LayoutUtil::kSingleWidth, 200};

    if (!snippetProvider)
        return fallback;

    auto snippet = snippetProvider(synth::SnippetManager::nameFromPayload(payload));
    auto* obj = snippet.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("nodes"))
        return fallback;
    auto* nodes = obj->getProperty("nodes").getArray();
    if (nodes == nullptr || nodes->isEmpty())
        return fallback;

    // Snippet positions are already origin-relative, so the union of every node's estimated box
    // from (0,0) is the group footprint.
    juce::Rectangle<int> bounds;
    for (const auto& nVar : *nodes) {
        auto* nObj = nVar.getDynamicObject();
        if (nObj == nullptr)
            continue;

        juce::Point<int> pos;
        if (auto* posObj = nObj->getProperty("position").getDynamicObject()) {
            pos = {(int)posObj->getProperty("x"), (int)posObj->getProperty("y")};
        }
        auto size = estimateModuleSize(nObj->getProperty("type").toString());
        juce::Rectangle<int> box(pos.x, pos.y, size.x, size.y);
        bounds = bounds.isEmpty() ? box : bounds.getUnion(box);
    }

    return bounds.isEmpty() ? fallback : juce::Point<int>(bounds.getWidth(), bounds.getHeight());
}

juce::var GraphEditor::extractSelectionSnippet(const juce::String& name) {
    return synth::SnippetManager::extractSnippet(audioEngine.getGraph(), selection.getSelected(), name,
                                                 /*includeExtraState=*/false, macros);
}

bool GraphEditor::insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) {
    auto& graph = audioEngine.getGraph();

    // Snap the drop point so an inserted group lands on the same grid as everything else. The
    // snippet's own internal offsets are preserved relative to it.
    auto dropPos = synth::LayoutUtil::snap(canvasPos);

    std::vector<juce::AudioProcessorGraph::NodeID> added;
    std::vector<synth::Macro> addedMacros;
    auto doInsert = [this, &graph, &snippet, dropPos, &added, &addedMacros] {
        added =
            synth::SnippetManager::insertSnippet(snippet, graph, dropPos, /*includeExtraState=*/false, &addedMacros);
        for (auto& macro : addedMacros)
            macros.add(macro);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doInsert);
    else
        doInsert();

    if (added.empty())
        return false;

    // Leave the freshly inserted group selected: it is what the user will want to move next.
    applySelectionChange(added);
    repaint();
    return true;
}

// ---- Copy / paste / duplicate ----

bool GraphEditor::insertClipboardPayload(const juce::var& payload, juce::Point<int> canvasPos) {
    auto& graph = audioEngine.getGraph();
    auto dropPos = synth::LayoutUtil::snap(canvasPos);

    std::vector<juce::AudioProcessorGraph::NodeID> added;
    std::vector<synth::Macro> addedMacros;
    auto doInsert = [this, &graph, &payload, dropPos, &added, &addedMacros] {
        // includeExtraState: the payload came from the live graph in this session, so carrying a
        // Sampler's loaded file or a Wavetable's custom table through is both safe and expected —
        // a duplicated Sampler that lost its sample would not be a duplicate.
        added = synth::SnippetManager::insertSnippet(payload, graph, dropPos, /*includeExtraState=*/true, &addedMacros);
        for (auto& macro : addedMacros)
            macros.add(macro);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doInsert);
    else
        doInsert();

    if (added.empty())
        return false;

    // Leave the copies selected, not the originals: the group the user just made is what they will
    // want to drag, delete or duplicate again.
    applySelectionChange(added);
    repaint();
    return true;
}

bool GraphEditor::copySelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return false;

    auto payload = synth::SnippetManager::extractSnippet(audioEngine.getGraph(), ids, "Clipboard",
                                                         /*includeExtraState=*/true, macros);
    if (synth::SnippetManager::getModuleCount(payload) <= 0)
        return false; // nothing eligible (e.g. only Audio Output was selected) — keep what we had

    clipboard.set(payload, synth::SnippetManager::selectionOrigin(audioEngine.getGraph(), ids));
    return true;
}

bool GraphEditor::pasteClipboard() {
    if (clipboard.isEmpty())
        return false;

    // Take the payload by value first: the cascade advances even if the insert is rejected, which
    // is the right behaviour — a failed paste must not leave the next one aimed at the same spot.
    const auto payload = clipboard.getPayload();
    return insertClipboardPayload(payload, clipboard.nextPastePosition());
}

bool GraphEditor::pasteClipboardAt(juce::Point<int> canvasPos) {
    if (clipboard.isEmpty())
        return false;

    const auto payload = clipboard.getPayload();
    clipboard.anchorAt(canvasPos);
    return insertClipboardPayload(payload, canvasPos);
}

bool GraphEditor::duplicateSelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return false;

    auto& graph = audioEngine.getGraph();
    auto payload = synth::SnippetManager::extractSnippet(graph, ids, "Duplicate", /*includeExtraState=*/true, macros);
    if (synth::SnippetManager::getModuleCount(payload) <= 0)
        return false;

    const auto origin = synth::SnippetManager::selectionOrigin(graph, ids);
    const int step = synth::ui::ModuleClipboard::kOffsetStep;
    return insertClipboardPayload(payload, origin + juce::Point<int>(step, step));
}

void GraphEditor::showCanvasContextMenu(juce::Point<int> canvasPos) {
    juce::Component::SafePointer<GraphEditor> safeThis(this);

    juce::PopupMenu m;
    const int clipboardCount = clipboard.getModuleCount();
    juce::PopupMenu::Item paste(clipboardCount > 1 ? "Paste " + juce::String(clipboardCount) + " Modules Here"
                                                   : "Paste Here");
    paste.setEnabled(clipboardCount > 0);
    paste.action = [safeThis, canvasPos] {
        if (safeThis != nullptr)
            safeThis->pasteClipboardAt(canvasPos);
    };
    m.addItem(paste);

    m.addSeparator();
    m.addItem("Select All Modules", [safeThis] {
        if (safeThis != nullptr)
            safeThis->selectAllModules();
    });

    const int selectionCount = getSelectionCount();
    if (selectionCount > 1) {
        m.addItem("Create Macro from " + juce::String(selectionCount) + " Modules", [safeThis] {
            if (safeThis != nullptr)
                safeThis->groupSelectionIntoMacro();
        });
    }

    m.showMenuAsync(juce::PopupMenu::Options());
}

bool GraphEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (selection.isEmpty())
            return false;
        clearSelection();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (selection.isEmpty())
            return false;
        deleteSelection();
        return true;
    }

    return false;
}

void GraphEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    auto localPos = content.getLocalPoint(this, e.getPosition());
    auto attenId = getAttenuverterNodeAt(localPos.toFloat());
    if (attenId.uid != 0) {
        if (undoManager) {
            undoManager->recordStructuralChange(audioEngine.getGraph(),
                                                [this, attenId] { audioEngine.removeModRouting(attenId); });
        } else {
            audioEngine.removeModRouting(attenId);
        }
        repaintCanvas();
    }
}

juce::AudioProcessorGraph::NodeID GraphEditor::getAttenuverterNodeAt(juce::Point<float> localPos) {
    auto& graph = audioEngine.getGraph();
    for (auto& connection : graph.getConnections()) {
        auto* node1 = graph.getNodeForId(connection.source.nodeID);
        auto* node2 = graph.getNodeForId(connection.destination.nodeID);

        if (!node1 || !node2)
            continue;

        // An attenuverter always has a source connection into its port 0
        if (dynamic_cast<AttenuverterModule*>(node2->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::Node* realDstNode = nullptr;
            int realDstPort = 0;
            // Find the output connection FROM this attenuverter
            for (auto& c : graph.getConnections()) {
                if (c.source.nodeID == node2->nodeID) {
                    realDstNode = graph.getNodeForId(c.destination.nodeID);
                    realDstPort = c.destination.channelIndex;
                    break;
                }
            }

            if (realDstNode) {
                juce::Point<int> p1, p2;
                bool found1 = false, found2 = false;
                for (auto* comp : content.getModules()) {
                    if (comp->getModule() == node1->getProcessor()) {
                        auto localP = comp->getPortCenter(connection.source.channelIndex, false);
                        p1 = comp->getBounds().getPosition() + localP;
                        found1 = true;
                    }
                    if (comp->getModule() == realDstNode->getProcessor()) {
                        auto localP = comp->getPortCenter(realDstPort, true);
                        p2 = comp->getBounds().getPosition() + localP;
                        found2 = true;
                    }
                }

                if (found1 && found2) {
                    auto mid = (p1 + p2) / 2;
                    if (juce::Point<float>(mid.toFloat().x, mid.toFloat().y).getDistanceFrom(localPos) <= 15.0f) {
                        return node2->nodeID;
                    }
                }
            }
        }
    }
    return {};
}

void GraphEditor::updateModulePosition(ModuleComponent* module) {
    if (!module)
        return;
    for (auto* n : audioEngine.getGraph().getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            n->properties.set("x", module->getX());
            n->properties.set("y", module->getY());
            break;
        }
    }
}

void GraphEditor::deleteModule(ModuleComponent* module) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    // Resolve NodeID then delegate to the single removal path.
    requestDeleteModule(nodeId);
}

void GraphEditor::requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId) {
    if (nodeId.uid == 0)
        return;

    auto& graph = audioEngine.getGraph();

    // updateComponents() below prunes `macros` against whatever nodes survive, so a module that
    // was a macro member either shrinks or dissolves its macro as part of the SAME undo step —
    // recordGraphAndMacroChange captures both "before"/"after" snapshots, not just the graph's.
    auto doDelete = [this, nodeId, &graph] {
        modMatrix.clearRows();
        graph.removeNode(nodeId);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doDelete);
    else
        doDelete();
    repaint();
}

void GraphEditor::replaceModule(ModuleComponent* moduleComp, const juce::String& newModuleType) {
    auto& graph = audioEngine.getGraph();

    // Find the old node by processor pointer (same pattern as deleteModule)
    juce::AudioProcessorGraph::NodeID oldNodeId;
    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == moduleComp->getModule()) {
            oldNodeId = n->nodeID;
            break;
        }
    }
    if (oldNodeId.uid == 0)
        return;

    // Use shared_ptr to make the lambda copyable (std::function requires it)
    auto newModuleTypeCopy = newModuleType;

    auto doReplace = [this, &graph, oldNodeId, newModuleTypeCopy] {
        auto* oldNode = graph.getNodeForId(oldNodeId);
        if (!oldNode)
            return;

        // 1. Create the new module
        auto newProcessor = synth::AIStateMapper::createModule(newModuleTypeCopy);
        if (!newProcessor)
            return;

        // 2. Snapshot old module's properties
        int posX = oldNode->properties.getWithDefault("x", 0);
        int posY = oldNode->properties.getWithDefault("y", 0);
        auto* oldProc = oldNode->getProcessor();
        int oldNumInputs = oldProc->getTotalNumInputChannels();
        int oldNumOutputs = oldProc->getTotalNumOutputChannels();
        bool oldAcceptsMidi = oldProc->acceptsMidi();
        bool oldProducesMidi = oldProc->producesMidi();

        // 3. Get new module capabilities before addNode moves it
        int newNumInputs = newProcessor->getTotalNumInputChannels();
        int newNumOutputs = newProcessor->getTotalNumOutputChannels();
        bool newAcceptsMidi = newProcessor->acceptsMidi();
        bool newProducesMidi = newProcessor->producesMidi();

        // 4. Collect all connections involving the old node
        struct ConnectionInfo {
            juce::AudioProcessorGraph::NodeID otherNodeId;
            int otherChannelIndex;
            int oldChannelIndex;
            bool isIncoming; // true = other->old, false = old->other
            bool isMidi;
        };
        std::vector<ConnectionInfo> directConnections;

        struct ModRoutingRewire {
            juce::AudioProcessorGraph::NodeID attenuverterId;
            int channelOnOldModule;
            bool oldModuleIsSource;
        };
        std::vector<ModRoutingRewire> modRoutings;

        for (auto& conn : graph.getConnections()) {
            bool srcIsOld = (conn.source.nodeID == oldNodeId);
            bool dstIsOld = (conn.destination.nodeID == oldNodeId);
            if (!srcIsOld && !dstIsOld)
                continue;

            // Check if this involves an attenuverter
            if (srcIsOld) {
                auto* dstNode = graph.getNodeForId(conn.destination.nodeID);
                if (dstNode && dynamic_cast<AttenuverterModule*>(dstNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.destination.nodeID;
                    rw.channelOnOldModule = conn.source.channelIndex;
                    rw.oldModuleIsSource = true;
                    modRoutings.push_back(rw);
                    continue;
                }
            }
            if (dstIsOld) {
                auto* srcNode = graph.getNodeForId(conn.source.nodeID);
                if (srcNode && dynamic_cast<AttenuverterModule*>(srcNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.source.nodeID;
                    rw.channelOnOldModule = conn.destination.channelIndex;
                    rw.oldModuleIsSource = false;
                    modRoutings.push_back(rw);
                    continue;
                }
            }

            // Direct connection (not attenuverter-mediated)
            ConnectionInfo ci;
            bool isMidiConn =
                (srcIsOld && conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) ||
                (dstIsOld && conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex);
            ci.isMidi = isMidiConn;
            if (srcIsOld) {
                ci.otherNodeId = conn.destination.nodeID;
                ci.otherChannelIndex = conn.destination.channelIndex;
                ci.oldChannelIndex = conn.source.channelIndex;
                ci.isIncoming = false;
            } else {
                ci.otherNodeId = conn.source.nodeID;
                ci.otherChannelIndex = conn.source.channelIndex;
                ci.oldChannelIndex = conn.destination.channelIndex;
                ci.isIncoming = true;
            }
            directConnections.push_back(ci);
        }

        // 5. Add the new node to the graph
        auto newNode = graph.addNode(std::move(newProcessor));
        if (!newNode)
            return;
        auto newNodeId = newNode->nodeID;
        newNode->properties.set("x", posX);
        newNode->properties.set("y", posY);

        // 6. Remove the old node (this removes all its connections)
        modMatrix.clearRows();
        graph.removeNode(oldNodeId);

        // 7. Re-create compatible direct connections
        for (auto& ci : directConnections) {
            if (ci.isMidi) {
                if (ci.isIncoming && newAcceptsMidi) {
                    graph.addConnection({{ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {newNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                } else if (!ci.isIncoming && newProducesMidi) {
                    graph.addConnection({{newNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                }
            } else {
                if (ci.isIncoming && ci.oldChannelIndex < newNumInputs) {
                    graph.addConnection({{ci.otherNodeId, ci.otherChannelIndex}, {newNodeId, ci.oldChannelIndex}});
                } else if (!ci.isIncoming && ci.oldChannelIndex < newNumOutputs) {
                    graph.addConnection({{newNodeId, ci.oldChannelIndex}, {ci.otherNodeId, ci.otherChannelIndex}});
                }
            }
        }

        // 8. Re-create compatible modulation routings
        // removeNode(oldNodeId) only removes connections TO/FROM oldNodeId.
        // For mod routings: source->attenuverter->dest
        // If old was source: old->atten connection is removed, atten->dest survives
        // If old was dest: atten->old connection is removed, source->atten survives
        // We only need to re-add the destroyed leg.
        for (auto& rw : modRoutings) {
            if (rw.oldModuleIsSource) {
                if (rw.channelOnOldModule < newNumOutputs) {
                    graph.addConnection({{newNodeId, rw.channelOnOldModule}, {rw.attenuverterId, 0}});
                }
            } else {
                if (rw.channelOnOldModule < newNumInputs) {
                    graph.addConnection({{rw.attenuverterId, 0}, {newNodeId, rw.channelOnOldModule}});
                }
            }
        }

        // 9. Refresh UI
        updateComponents();
        audioEngine.updateModuleNames();
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doReplace);
    } else {
        doReplace();
    }
    repaint();
}

void GraphEditor::disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    if (nodeId.uid == 0)
        return;

    // A visible jack can front an N-voice fan (and Poly MIDI's single jack fronts two), so gather every
    // raw channel it owns — otherwise "Disconnect" would leave 7 of 8 voices still wired.
    std::vector<int> targetChannels;
    if (isMidi) {
        targetChannels.push_back(juce::AudioProcessorGraph::midiChannelIndex);
    } else if (auto* modBase = dynamic_cast<ModuleBase*>(module->getModule())) {
        for (const auto& t : modBase->getJackTargets(portIndex, isInput))
            for (int v = 0; v < t.voiceSpan; ++v)
                targetChannels.push_back(t.rawHeadChannel + v);
    } else {
        targetChannels.push_back(portIndex);
    }

    auto doDisconnect = [this, &graph, nodeId, targetChannels, isInput] {
        std::vector<juce::AudioProcessorGraph::Connection> toRemove;
        auto isTargetChannel = [&targetChannels](int channel) {
            return std::find(targetChannels.begin(), targetChannels.end(), channel) != targetChannels.end();
        };

        for (auto& c : graph.getConnections()) {
            if (isInput) {
                if (c.destination.nodeID == nodeId && isTargetChannel(c.destination.channelIndex)) {
                    if (auto* srcNode = graph.getNodeForId(c.source.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(srcNode->nodeID);
                        else
                            toRemove.push_back(c);
                    }
                }
            } else {
                if (c.source.nodeID == nodeId && isTargetChannel(c.source.channelIndex)) {
                    if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(dstNode->nodeID);
                        else
                            toRemove.push_back(c);
                    }
                }
            }
        }
        for (auto& c : toRemove)
            graph.removeConnection(c);
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doDisconnect);
    } else {
        doDisconnect();
    }
    repaint();
}

bool GraphEditor::isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const {
    if (module == nullptr)
        return false;

    juce::AudioProcessorGraph::NodeID nodeId;
    for (auto* n : audioEngine.getGraph().getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }
    if (nodeId.uid == 0)
        return false;

    return isInput ? !isInputJackFree(nodeId, portIndex, isMidi) : !isOutputJackFree(nodeId, portIndex, isMidi);
}

void GraphEditor::rewireForPolyChange(ModuleComponent* module, const std::vector<LogicalPort>& previousInputMap,
                                      const std::vector<LogicalPort>& previousOutputMap) {
    if (module == nullptr)
        return;

    auto* toggled = dynamic_cast<ModuleBase*>(module->getModule());
    if (toggled == nullptr)
        return;

    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;
    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == toggled) {
            nodeId = n->nodeID;
            break;
        }
    }
    if (nodeId.uid == 0)
        return;

    // Which visible jack a raw channel belonged to *before* the toggle. The live mapping already
    // reflects the new poly state, so only the captured maps can answer this.
    auto previousJack = [](const std::vector<LogicalPort>& map, int rawChannel) {
        return (rawChannel >= 0 && rawChannel < static_cast<int>(map.size()))
                   ? map[static_cast<size_t>(rawChannel)].visibleJackIndex
                   : rawChannel;
    };

    // One user-visible cable touching the toggled module, described in jack terms so it survives the
    // raw-channel reshuffle. An N-voice fan collapses into a single Wire.
    struct Wire {
        juce::AudioProcessorGraph::NodeID farNodeId;
        int farVisibleJack = 0;
        int ownVisibleJack = 0;
        bool ownIsSource = false;
        juce::AudioProcessorGraph::NodeID attenuverterNodeId; // invalid when the cable is direct
        float attenuverterAmount = 1.0f;
        std::vector<juce::AudioProcessorGraph::Connection> rawEdges;
    };
    std::vector<Wire> wires;

    const auto connections = graph.getConnections();

    for (const auto& c : connections) {
        const bool ownIsSource = (c.source.nodeID == nodeId);
        const bool ownIsDest = (c.destination.nodeID == nodeId);
        if (ownIsSource == ownIsDest)
            continue; // not ours, or a self-loop we leave alone

        const int ownRaw = ownIsSource ? c.source.channelIndex : c.destination.channelIndex;
        const int farRaw = ownIsSource ? c.destination.channelIndex : c.source.channelIndex;
        if (ownRaw == juce::AudioProcessorGraph::midiChannelIndex ||
            farRaw == juce::AudioProcessorGraph::midiChannelIndex)
            continue; // MIDI wires do not move when a module changes its voice layout

        auto farNodeId = ownIsSource ? c.destination.nodeID : c.source.nodeID;
        int farChannel = farRaw;
        juce::AudioProcessorGraph::NodeID attenuverterNodeId;
        float attenuverterAmount = 1.0f;

        // An attenuverter is an implementation detail of one mod cable — step over it to the module
        // the user actually patched, and remember its amount so a rebuild can restore it.
        if (auto* attenNode = graph.getNodeForId(farNodeId)) {
            if (dynamic_cast<AttenuverterModule*>(attenNode->getProcessor()) != nullptr) {
                attenuverterNodeId = farNodeId;
                const auto& attenParams = attenNode->getProcessor()->getParameters();
                if (attenParams.size() > 1)
                    if (auto* amount = dynamic_cast<juce::AudioParameterFloat*>(attenParams[1]))
                        attenuverterAmount = amount->get();

                bool resolved = false;
                for (const auto& leg : connections) {
                    if (ownIsSource && leg.source.nodeID == attenuverterNodeId) {
                        farNodeId = leg.destination.nodeID;
                        farChannel = leg.destination.channelIndex;
                        resolved = true;
                        break;
                    }
                    if (!ownIsSource && leg.destination.nodeID == attenuverterNodeId &&
                        leg.destination.channelIndex == 0) {
                        farNodeId = leg.source.nodeID;
                        farChannel = leg.source.channelIndex;
                        resolved = true;
                        break;
                    }
                }
                if (!resolved)
                    continue; // half-patched attenuverter — leave it to the mod matrix
            }
        }

        auto* farNode = graph.getNodeForId(farNodeId);
        if (farNode == nullptr)
            continue;
        auto* farModule = dynamic_cast<ModuleBase*>(farNode->getProcessor());

        const int ownVisibleJack = previousJack(ownIsSource ? previousOutputMap : previousInputMap, ownRaw);
        int farVisibleJack = farChannel;
        if (farModule != nullptr)
            farVisibleJack = ownIsSource ? farModule->mapInputChannel(farChannel).visibleJackIndex
                                         : farModule->mapOutputChannel(farChannel).visibleJackIndex;

        auto existing = std::find_if(wires.begin(), wires.end(), [&](const Wire& w) {
            return w.farNodeId == farNodeId && w.farVisibleJack == farVisibleJack &&
                   w.ownVisibleJack == ownVisibleJack && w.ownIsSource == ownIsSource &&
                   w.attenuverterNodeId == attenuverterNodeId;
        });

        if (existing == wires.end()) {
            Wire w;
            w.farNodeId = farNodeId;
            w.farVisibleJack = farVisibleJack;
            w.ownVisibleJack = ownVisibleJack;
            w.ownIsSource = ownIsSource;
            w.attenuverterNodeId = attenuverterNodeId;
            w.attenuverterAmount = attenuverterAmount;
            w.rawEdges.push_back(c);
            wires.push_back(std::move(w));
        } else {
            existing->rawEdges.push_back(c); // another voice of a fan we have already seen
        }
    }

    if (wires.empty())
        return;

    // Tear every affected cable down first, so a fan that moves onto channels another cable used to
    // occupy cannot collide with the version of itself we are about to rebuild.
    for (const auto& w : wires) {
        if (w.attenuverterNodeId.uid != 0)
            audioEngine.removeModRouting(w.attenuverterNodeId);
        else
            for (const auto& c : w.rawEdges)
                graph.removeConnection(c);
    }

    for (const auto& w : wires) {
        auto* farNode = graph.getNodeForId(w.farNodeId);
        if (farNode == nullptr)
            continue;
        auto* farModule = dynamic_cast<ModuleBase*>(farNode->getProcessor());

        const ModuleBase* sourceModule = w.ownIsSource ? toggled : farModule;
        const ModuleBase* destModule = w.ownIsSource ? farModule : toggled;
        const int sourceJack = w.ownIsSource ? w.ownVisibleJack : w.farVisibleJack;
        const int destJack = w.ownIsSource ? w.farVisibleJack : w.ownVisibleJack;
        const auto sourceId = w.ownIsSource ? nodeId : w.farNodeId;
        const auto destId = w.ownIsSource ? w.farNodeId : nodeId;

        const auto link = resolvePolyLink(sourceModule, sourceJack, destModule, destJack);

        // Only a single mono cable may sit behind an attenuverter; a fan is always direct, and a
        // structural pitch/gate source is never wrapped (see carriesStructuralSignal).
        const bool useAttenuverter = link.voiceCount == 1 && destModule != nullptr &&
                                     !carriesStructuralSignal(sourceModule, link.sourceRawChannel) &&
                                     destModule->isAutoPromotableModTarget(link.destRawChannel);

        if (useAttenuverter) {
            const auto newAttenId =
                audioEngine.addModRouting(sourceId, link.sourceRawChannel, destId, link.destRawChannel);
            if (newAttenId.uid != 0 && w.attenuverterNodeId.uid != 0) {
                // Carry the old amount across, so toggling poly does not silently reset the knob.
                if (auto* newAttenNode = graph.getNodeForId(newAttenId)) {
                    const auto& attenParams = newAttenNode->getProcessor()->getParameters();
                    if (attenParams.size() > 1)
                        if (auto* amount = dynamic_cast<juce::AudioParameterFloat*>(attenParams[1]))
                            amount->setValueNotifyingHost(amount->convertTo0to1(w.attenuverterAmount));
                }
            }
        } else {
            for (int v = 0; v < link.voiceCount; ++v)
                graph.addConnection(
                    {{sourceId, link.sourceRawChannel + v * link.sourceStride}, {destId, link.destRawChannel + v}});
        }
    }

    repaint();
}

void GraphEditor::timerCallback() {
    // Compute the routing traversal once; derive display info from the same snapshot.
    cachedModRoutings = audioEngine.getModulationRoutings();
    cachedModDisplayInfo = audioEngine.getModulationDisplayInfo(cachedModRoutings);
    content.connectionAnimPhase += 0.02f;
    if (content.connectionAnimPhase >= 1.0f)
        content.connectionAnimPhase -= 1.0f;
    repaintCanvas();

    // Pressing or RELEASING Ctrl is not a mouse move, and suggestions were only recomputed from
    // updateDragPreview — so a drag that stopped moving kept showing a stale insert preview after
    // the modifier was let go (and never picked one up if Ctrl went down while the mouse was
    // still). Re-evaluate on this existing 30 Hz tick rather than a new timer, and only when the
    // sampled state actually flipped: a drag that holds its modifier costs one bool compare, and
    // refreshSmartSuggestions repaints only when the suggestion set really changed.
    refreshSuggestionsIfInsertModifierChanged();

    // Minimap (issue #159): only build the model while visible, and only when it's needed —
    // setModel() itself only repaints when the model actually changed (no repaint storm on a
    // static patch).
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());

    // Drain the audio thread's UI reflection ring on the same 30 Hz cadence as everything else in
    // this callback — no separate free-running timer. A drain against an empty ring is just
    // one prepareToRead() call, so this is effectively free on every tick that has nothing queued.
    // Reflection never calls anything that repaints on its own: setValue(..., dontSendNotification)
    // marks the slider dirty and it rides the existing buffered-image repaint, same as any other
    // control change.
    audioEngine.getAutomationUiFeed().drain([this](const synth::AutomationUiEvent& event) {
        for (auto* comp : content.getModules()) {
            if (comp->getNodeId().uid != event.nodeId)
                continue;
            comp->reflectParameterValue(event.param, event.newNormalized);
            return;
        }
        // No live component for this NodeID (module hidden mid-teardown or already deleted) —
        // the event is simply discarded.
    });
}

// ============================================================================
// Drag-preview API
// ============================================================================

void GraphEditor::beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId) {
    dragPreviewActive = true;
    dragPreviewW = w;
    dragPreviewH = h;
    dragPreviewSelfId = selfId;
    dragPreviewGhost = {};
    alignmentGuides.clear();
    clearSmartSuggestions();
    // Seed the tick's comparison from the state at press time, so a drag started WITH the modifier
    // already held is not reported as a change on its very first tick.
    lastSampledInsertModifier = isInsertModifierDown();
    // Body-drag of an existing module: clear any leftover library-drop probe.
    if (selfId.uid != 0) {
        dragPreviewIsSnippet = false;
        dragPreviewProbe.reset();
    }
    repaintCanvas();
}

void GraphEditor::updateDragPreview(juce::Point<int> desiredTopLeftCanvas) {
    if (!dragPreviewActive)
        return;
    // Where the user is POINTING, before anti-overlap moves the card. A ghost aimed at the gap
    // between two wired cards necessarily overlaps them — it is wider than the gap — so
    // resolvePlacement throws it clear, and judging a suggestion only by that landing spot means
    // aiming at the gap can never earn one. Candidacy is judged from the aim; the landing spot is
    // still what gets drawn and where the card ends up (see refreshSmartSuggestions).
    dragPreviewAim = juce::Rectangle<int>(desiredTopLeftCanvas.x, desiredTopLeftCanvas.y, dragPreviewW, dragPreviewH);

    auto resolved = resolvePlacement(desiredTopLeftCanvas, dragPreviewW, dragPreviewH, dragPreviewSelfId);
    dragPreviewGhost = juce::Rectangle<int>(resolved.x, resolved.y, dragPreviewW, dragPreviewH);

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // Scan existing modules and compute alignment guides for closest edges
    alignmentGuides.clear();
    if (dragPreviewGhost.isEmpty()) {
        refreshSmartSuggestions();
        return;
    }

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
    const float snapThreshold = static_cast<float>(m.gridSize); // kGridSize

    // Build list of existing module boxes (excluding self)
    std::vector<synth::LayoutUtil::Box> existingBoxes;
    for (auto* comp : content.getModules()) {
        if (comp->getNodeId() == dragPreviewSelfId)
            continue; // Skip self
        juce::Rectangle<int> rect = comp->getBounds();
        existingBoxes.push_back({comp->getNodeId(), rect});
    }

    if (existingBoxes.empty()) {
        refreshSmartSuggestions();
        repaintCanvas(); // Still need to clear any old guides
        return;
    }

    auto ghostRect = dragPreviewGhost.toFloat();
    float left = ghostRect.getX();
    float right = ghostRect.getRight();
    float top = ghostRect.getY();
    float bottom = ghostRect.getBottom();
    float centerX = ghostRect.getX() + ghostRect.getWidth() * 0.5f;
    float centerY = ghostRect.getY() + ghostRect.getHeight() * 0.5f;

    // Track best alignment candidates (edge-to-edge snap within threshold)
    struct Candidate {
        int type;   // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
        float dist; // absolute distance to snap target
        juce::Point<float> start;
        juce::Point<float> end;
    };
    std::vector<Candidate> candidates;

    // Check each existing module for alignments
    for (const auto& box : existingBoxes) {
        float l = box.rect.getX();
        float r = box.rect.getRight();
        float t = box.rect.getY();
        float b = box.rect.getBottom();
        float cx = box.rect.getX() + box.rect.getWidth() * 0.5f;
        float cy = box.rect.getY() + box.rect.getHeight() * 0.5f;

        // Edge-to-edge alignments (left/right/top/bottom)
        struct Edge {
            float alignPos;  // position we're aligning (ghost edge)
            float targetPos; // target edge position
            int type;        // guide type enum
            bool horizontal; // true if horizontal line, false if vertical
        };

        Edge edges[] = {
            {left, l, 0, false},  // left-to-left
            {right, r, 1, false}, // right-to-right
            {top, t, 2, true},    // top-to-top
            {bottom, b, 3, true}  // bottom-to-bottom
        };

        for (const auto& edge : edges) {
            float dist = std::abs(edge.alignPos - edge.targetPos);
            if (dist <= snapThreshold) {
                Candidate c;
                c.type = edge.type;
                c.dist = dist;
                // Line spans the overlapping range
                if (edge.horizontal) {
                    float startX = std::min(left, l);
                    float endX = std::max(right, r);
                    c.start = {startX, edge.targetPos};
                    c.end = {endX, edge.targetPos};
                } else {
                    float startY = std::min(top, t);
                    float endY = std::max(bottom, b);
                    c.start = {edge.targetPos, startY};
                    c.end = {edge.targetPos, endY};
                }
                candidates.push_back(c);
            }
        }

        // Center alignment (X and Y)
        float cxDist = std::abs(centerX - cx);
        if (cxDist <= snapThreshold) {
            Candidate c;
            c.type = 4; // centerX
            c.dist = cxDist;
            c.start = {cx, std::min(top, t)};
            c.end = {cx, std::max(bottom, b)};
            candidates.push_back(c);
        }

        float cyDist = std::abs(centerY - cy);
        if (cyDist <= snapThreshold) {
            Candidate c;
            c.type = 5; // centerY
            c.dist = cyDist;
            c.start = {std::min(left, l), cy};
            c.end = {std::max(right, r), cy};
            candidates.push_back(c);
        }
    }

    // Deduplicate: keep only the closest guide for each type (left/right/top/bottom/centerX/centerY)
    std::vector<Candidate> bestCandidates;
    float minDist[] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    int bestIdx[] = {-1, -1, -1, -1, -1, -1};

    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (c.dist <= snapThreshold) {
            if (c.dist < minDist[c.type]) {
                minDist[c.type] = c.dist;
                bestIdx[c.type] = (int)i;
            }
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (bestIdx[i] != -1)
            bestCandidates.push_back(candidates[bestIdx[i]]);
    }

    // Convert to AlignmentGuide and store
    alignmentGuides.clear();
    for (const auto& c : bestCandidates) {
        alignmentGuides.push_back({c.start, c.end, c.type});
    }

    refreshSmartSuggestions();
    repaintCanvas();
}

void GraphEditor::endDragPreview() {
    dragPreviewActive = false;
    dragPreviewGhost = {};
    alignmentGuides.clear();
    clearSmartSuggestions();
    dragPreviewIsSnippet = false;
    dragPreviewProbe.reset();
    repaintCanvas();
}

bool GraphEditor::isInterestedInDragSource(const SourceDetails& dragSourceDetails) { return true; }

void GraphEditor::itemDragEnter(const SourceDetails& dragSourceDetails) {
    juce::String name = dragSourceDetails.description.toString();

    // A snippet covers a whole group, so size the ghost from the snippet's own bounding box
    // instead of the single-module estimate table.
    juce::Point<int> estSize;
    dragPreviewIsSnippet = synth::SnippetManager::isSnippetPayload(name);
    dragPreviewProbe.reset();
    if (dragPreviewIsSnippet) {
        estSize = estimateSnippetSize(name);
    } else if (synth::PluginIdentity::isDragPayload(name)) {
        // A hosted-plugin payload is not a factory module type: no smart-connection probe (the
        // plugin isn't loaded yet, so its jacks are unknowable); sized from the Hosted Plugin card.
        estSize = estimateModuleSize("Hosted Plugin");
    } else {
        dragPreviewProbe = synth::AIStateMapper::createModule(name);
        // The probe IS the ghost for smart-connect purposes: its jack layout decides the preview
        // AND the plan that gets applied on drop. It must therefore go through exactly the same
        // Dual I/O default the real module will get in itemDropped — otherwise a plan computed for
        // a collapsed ghost is applied to a module that spawned dual, and only the left legs get
        // wired (the ghost's fan resolves to one raw channel per jack instead of two).
        if (dragPreviewProbe != nullptr)
            applyDefaultDualIOForNewModule(*dragPreviewProbe, name);
        estSize = estimateModuleSize(name);
    }

    beginDragPreview(estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});
    // Centred on the cursor — see ghostTopLeftForCursor. beginDragPreview above has already set the
    // ghost size this depends on.
    auto canvasPos = content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt();
    updateDragPreview(ghostTopLeftForCursor(canvasPos));
}

void GraphEditor::itemDragMove(const SourceDetails& dragSourceDetails) {
    auto canvasPos = content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt();
    updateDragPreview(ghostTopLeftForCursor(canvasPos));
}

void GraphEditor::itemDragExit(const SourceDetails& dragSourceDetails) {
    juce::ignoreUnused(dragSourceDetails);
    endDragPreview();
}

bool GraphEditor::isSingletonIOModule(const juce::String& typeName) {
    return typeName == "Audio Input" || typeName == "Audio Output";
}

bool GraphEditor::graphHasModuleNamed(juce::AudioProcessorGraph& graph, const juce::String& typeName) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == typeName)
            return true;
    return false;
}

void GraphEditor::itemDropped(const SourceDetails& dragSourceDetails) {
    const juce::String name = dragSourceDetails.description.toString();
    // The drop lands exactly where the preview showed: the ghost rect is already snapped and
    // de-overlapped, so taking its position (rather than re-deriving one from the cursor) makes it
    // impossible for the two to disagree. Falls back to the centred cursor if there is no live ghost
    // (a drop with no preceding drag-move, which only happens in tests).
    auto dropPos =
        (dragPreviewActive && !dragPreviewGhost.isEmpty())
            ? dragPreviewGhost.getPosition()
            : ghostTopLeftForCursor(content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt());

    // Snippet drop: resolve the payload to its JSON via the owner and insert the whole group.
    // Checked before the single-module path because both arrive on the same DragAndDrop channel,
    // distinguished only by the payload prefix.
    if (synth::SnippetManager::isSnippetPayload(name)) {
        endDragPreview();
        if (!snippetProvider)
            return;
        auto snippet = snippetProvider(synth::SnippetManager::nameFromPayload(name));
        if (!snippet.isObject())
            return;
        insertSnippetAt(snippet, dropPos);
        return;
    }

    // A scanned plugin — same channel again, told apart by its "plugin:" prefix.
    if (synth::PluginIdentity::isDragPayload(name)) {
        addHostedPluginAtCanvasPosition(synth::PluginIdentity::fromDragPayload(name), dropPos);
        endDragPreview();
        return;
    }

    addModuleAtCanvasPosition(name, dropPos, {});
    endDragPreview();
}

// =============================================================================
// Audio-file drag and drop — dropping a sample on empty canvas builds a Sampler for it.
// A drop that lands on an existing Sampler is handled by ModuleComponent instead (JUCE hands the
// drop to the deepest interested target), which replaces that module's sample.
// =============================================================================

bool GraphEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void GraphEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    auto canvasPos = content.getLocalPoint(this, juce::Point<int>(x, y)).roundToInt();

    for (const auto& path : files) {
        const juce::File file(path);
        if (!SamplerModule::isSupportedAudioFile(file))
            continue;

        // Load into the processor BEFORE it joins the graph: recordStructuralChange snapshots the
        // graph afterwards, and that snapshot is what undo/redo replays — so the file path has to be
        // in place by then or the sample is lost on the first Cmd+Z.
        addModuleAtCanvasPosition("Sampler", canvasPos, [file](juce::AudioProcessor& processor) {
            if (auto* sampler = dynamic_cast<SamplerModule*>(&processor))
                sampler->loadSampleFile(file);
        });

        // Cascade multiple files so they do not all land on the same spot.
        canvasPos += juce::Point<int>(32, 32);
    }

    endDragPreview();
}

void GraphEditor::addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos) {
    if (!identity.isValid())
        return;

    // Same configure hook the dropped-sample path uses, and for the same reason: the identity has to
    // be on the processor BEFORE recordStructuralChange snapshots the graph, or Cmd+Z / redo would
    // bring back a bare Hosted Plugin that has forgotten which plugin it was.
    addModuleAtCanvasPosition("Hosted Plugin", dropPos, [identity](juce::AudioProcessor& processor) {
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(&processor))
            hosted->loadPlugin(identity);
    });
}

juce::Point<int> GraphEditor::getViewportCentreInCanvasSpace() const {
    return getVisibleCanvasRect().getCentre().roundToInt();
}

void GraphEditor::addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                            const std::function<void(juce::AudioProcessor&)>& configure) {
    // Audio Input/Output are singletons. JUCE ties every audioOutputNode's channel count to the whole
    // graph and each one sums into the same device buffer, so a second instance would double the
    // signal rather than address another output — and every node lookup in the app (auto-connect,
    // PatchEval, auto-arrange) takes the first match and stops. Adding a duplicate is a no-op.
    if (isSingletonIOModule(name) && graphHasModuleNamed(audioEngine.getGraph(), name))
        return;

    auto newProcessor = synth::AIStateMapper::createModule(name);

    if (newProcessor) {
        applyDefaultDualIOForNewModule(*newProcessor, name);
        if (configure)
            configure(*newProcessor);

        auto& graph = audioEngine.getGraph();
        // Use estimate for an initial snapped position; finalizeModuleDrag will re-resolve
        // using the real component size after updateComponents() creates the component.
        auto estSize = estimateModuleSize(name);
        auto initialPlaced = resolvePlacement(dropPos, estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});

        // finalizeNewDrop: locate the newly created ModuleComponent, compute its
        // real final position (snapped + anti-overlapped using actual dimensions),
        // then animate it from the raw drop point to the settled position.
        auto finalizeNewDrop = [this, initialPlaced](juce::AudioProcessorGraph::NodeID newNodeId) {
            ModuleComponent* newComp = nullptr;
            for (auto* comp : content.getModules()) {
                if (comp != nullptr && comp->getNodeId() == newNodeId) {
                    newComp = comp;
                    break;
                }
            }
            if (newComp == nullptr)
                return;

            // Compute final position using the real component size.
            auto toPos = resolvePlacement(newComp->getPosition(), newComp->getWidth(), newComp->getHeight(),
                                          newComp->getNodeId());

            // Animate from the estimated initial-placed position to the real final position.
            // If they are identical, animateDropLanding is a no-op (just settles in place).
            animateDropLanding(newComp, initialPlaced, toPos);

            // Persist the final position immediately so it survives reload even if the
            // animation is still in-flight when the user saves.
            updateModulePosition(newComp);

            // Auto-wire any smart-connection previews the user saw while dragging. Already inside
            // a structural undo transaction when one is open, so do not nest another.
            applySmartSuggestions(newNodeId, /*recordUndo=*/false);
        };

        if (undoManager) {
            // Use shared_ptr to make the lambda copyable (std::function requires it)
            auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
            undoManager->recordStructuralChange(graph, [this, proc, initialPlaced, finalizeNewDrop] {
                if (*proc) {
                    auto node = audioEngine.getGraph().addNode(std::move(*proc));
                    if (node) {
                        node->properties.set("x", initialPlaced.x);
                        node->properties.set("y", initialPlaced.y);
                        auto newNodeId = node->nodeID;
                        updateComponents();
                        finalizeNewDrop(newNodeId);
                    }
                }
            });
        } else {
            auto node = graph.addNode(std::move(newProcessor));
            if (node) {
                node->properties.set("x", initialPlaced.x);
                node->properties.set("y", initialPlaced.y);
                auto newNodeId = node->nodeID;
                updateComponents();
                finalizeNewDrop(newNodeId);
            }
        }
    }
}

void GraphEditor::dropRoutingsOnHiddenJacks(juce::AudioProcessorGraph::NodeID nodeId) {
    // Jacks that just disappeared take their cables with them. Leaving them connected would mean a
    // routing that still shows in the mod matrix, still costs a node, and no longer carries
    // anything (the module silences hidden channels) — with no jack to unplug it from.
    //
    // No undo transaction is opened here: the gesture that changed the count (a parameter move, or
    // a device change, which is not undoable at all) owns the surrounding snapshot.
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
    if (mb == nullptr)
        return;

    const int visible = mb->getVisibleOutputPortCount();
    std::vector<juce::AudioProcessorGraph::Connection> toRemove;

    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != nodeId || c.source.isMIDI() || c.source.channelIndex < visible)
            continue;

        if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
            if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr) {
                audioEngine.removeModRouting(dstNode->nodeID); // drops both legs of the cable
                continue;
            }
        }
        toRemove.push_back(c);
    }

    for (const auto& c : toRemove)
        graph.removeConnection(c);
}

void GraphEditor::refreshIoModulesAfterDeviceChange() {
    // MESSAGE THREAD: the audio device changed under us, so every Audio Input card's jack count
    // may have changed with it. Pushing the engine's prepared channel count into the module here
    // (rather than waiting for the audio thread to publish it from the next block) is what makes
    // the resize immediate — and what makes it testable without a device.
    //
    // Minimal on purpose: this is only the part that must not wait, because a shrunk device leaves
    // cables on jacks that no longer exist.
    auto& graph = audioEngine.getGraph();
    const int deviceChannels = audioEngine.getDeviceInputChannelCount();
    bool sawInputModule = false;

    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* input = dynamic_cast<AudioInputModule*>(node->getProcessor());
        if (input == nullptr)
            continue;

        input->setDeviceChannelCount(deviceChannels);
        dropRoutingsOnHiddenJacks(node->nodeID);
        sawInputModule = true;

        for (auto* comp : content.getModules())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                comp->refreshPortLayout();
    }

    if (sawInputModule)
        repaintCanvas();
}

void GraphEditor::refreshOutputDeviceInfo() {
    // MESSAGE THREAD. The provider (installed by MainComponent) is the only thing that knows
    // Standalone-vs-Hosted framing; this just finds the card and pushes whatever it returns.
    // setOutputDeviceInfoText is itself a no-op on every module except Audio Output, so there is
    // no need to filter with isTerminalAudioSink twice — but doing it here too skips the
    // (identical, cheap) text comparison on every other card on the canvas.
    if (!outputDeviceInfoProvider)
        return;

    const juce::String text = outputDeviceInfoProvider();
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && isTerminalAudioSink(comp->getModule()))
            comp->setOutputDeviceInfoText(text);
    }
}

void GraphEditor::applyDualIOToExistingModules(bool dual) {
    auto& graph = audioEngine.getGraph();

    // Walks the GRAPH, not the cards. At startup the preference is restored before any
    // ModuleComponent exists — AudioEngine loads the default preset in its own constructor — so a
    // card-driven pass would silently do nothing and the patch would open with whatever layout each
    // module's constructor happened to default to.
    std::vector<juce::AudioProcessorGraph::NodeID> changed;
    for (auto* node : graph.getNodes()) {
        auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (mb == nullptr || !mb->hasDualIOParameter() || mb->isDualIO() == dual)
            continue;
        if (auto* param = findParameterByID(node->getProcessor(), "dualIO"))
            param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
        changed.push_back(node->nodeID);
    }

    if (changed.empty())
        return;

    for (auto nodeId : changed) {
        // Settle through the card when there is one: it also completes L/R pairs on expand. Driven
        // straight rather than left to the parameter listener, which is asynchronous — re-laying
        // every module at once would settle a frame late and each module's cable cleanup would race
        // the next one's layout change. With no card yet (startup) the graph-side cleanup is all
        // that is needed; the first updateComponents() then builds the cards at the right size.
        ModuleComponent* card = nullptr;
        for (auto* mc : content.getModules())
            if (mc != nullptr && mc->getNodeId() == nodeId)
                card = mc;

        if (card != nullptr)
            card->applyDualIOLayoutChange();
        else
            dropHiddenRightLegConnections(nodeId);
    }
}

void GraphEditor::applyDefaultDualIOForNewModule(juce::AudioProcessor& processor,
                                                 const juce::String& moduleType) const {
    auto* mb = dynamic_cast<ModuleBase*>(&processor);
    if (mb == nullptr || !mb->hasDualIOParameter())
        return;

    // Applied in BOTH directions. It used to only ever force Dual I/O *on*, which meant the
    // preference could not express "I want single jacks" for a module whose own default is dual —
    // and the voice modules default to dual since #219. The preference is the user's stated intent
    // for anything they create, so it wins over the module's constructor default either way.
    //
    // The per-module override (Preferences → "Per-module I/O defaults...") wins over the global
    // default when the two disagree — it exists specifically to say "everything follows the
    // toggle EXCEPT this one type". A type with no entry in the map is untouched by the override
    // and falls through to the global default, same as before that popup existed.
    bool dual = defaultDualIOForNewModules;
    if (auto it = dualIOPerModuleOverrides.find(moduleType); it != dualIOPerModuleOverrides.end())
        dual = it->second;

    if (auto* param = findParameterByID(&processor, "dualIO"))
        param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

void GraphEditor::completeStereoPairConnections(ModuleComponent* moduleComp) {
    if (moduleComp == nullptr || moduleComp->getModule() == nullptr)
        return;

    auto* mb = dynamic_cast<ModuleBase*>(moduleComp->getModule());
    if (mb == nullptr || !mb->hasDualIOParameter())
        return;

    auto& graph = audioEngine.getGraph();
    const auto nodeId = moduleComp->getNodeId();
    if (graph.getNodeForId(nodeId) == nullptr)
        return;

    auto hasEdge = [&](juce::AudioProcessorGraph::NodeID src, int srcCh, juce::AudioProcessorGraph::NodeID dst,
                       int dstCh) {
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
                c.destination.channelIndex == dstCh)
                return true;
        return false;
    };

    auto inputFeedCount = [&](int rawChannel) {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == rawChannel)
                ++n;
        return n;
    };
    auto inputChannelIsFed = [&](int rawChannel) { return inputFeedCount(rawChannel) > 0; };

    const int myOutputLeg = rightAudioLegOf(mb, /*asInput=*/false);
    const int myInputLeg = rightAudioLegOf(mb, /*asInput=*/true);

    // MIGRATE BEFORE WIRING. An audio input that was one jack a moment ago can already carry TWO
    // feeds from the same upstream node: that is the summed cable the upstream's own split put there
    // (its Audio R aimed at our mono jack), or two raws of one collapsed jack hand-wired onto it.
    // Splitting gives that second feed a jack of its own, so it MOVES — anything else leaves it in
    // place and hangs a third cable off the same pair, which is exactly what the reported
    // Osc-then-Filter sequence produced: L->L, the old R->L sum, and a new R->R on top.
    //
    // Only a pair from the SAME upstream node migrates. Two feeds from two different modules are a
    // mix the user built by hand, and moving half of it somewhere else would be rewriting their
    // patch; that case falls through untouched (and, having more than one feed, takes neither the
    // right-leg wire nor the broadcast below).
    if (myInputLeg > 0 && mb->isDualIO() && !inputChannelIsFed(myInputLeg)) {
        std::vector<juce::AudioProcessorGraph::Connection> leftFeeds;
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == 0)
                leftFeeds.push_back(c);

        if (leftFeeds.size() > 1) {
            const juce::AudioProcessorGraph::Connection* migrate = nullptr;
            for (const auto& candidate : leftFeeds) {
                int fromSameNode = 0;
                for (const auto& other : leftFeeds)
                    if (other.source.nodeID == candidate.source.nodeID)
                        ++fromSameNode;
                if (fromSameNode < 2)
                    continue;

                // Prefer the feed that comes off the upstream's RIGHT leg, so the pair lands
                // L->L / R->R rather than crossed. Falling back to any second feed from that node
                // covers the collapsed-jack case, where both raws belong to one visible jack.
                auto* srcNode = graph.getNodeForId(candidate.source.nodeID);
                const int srcLeg =
                    srcNode != nullptr ? rightAudioLegOf(srcNode->getProcessor(), /*asInput=*/false) : -1;
                if (candidate.source.channelIndex == srcLeg) {
                    migrate = &candidate;
                    break;
                }
                if (migrate == nullptr && candidate.source.channelIndex != 0)
                    migrate = &candidate;
            }

            if (migrate != nullptr) {
                const juce::AudioProcessorGraph::Connection moved{migrate->source, {nodeId, myInputLeg}};
                graph.removeConnection(*migrate);
                if (!hasEdge(moved.source.nodeID, moved.source.channelIndex, nodeId, myInputLeg))
                    graph.addConnection(moved);
            }
        }
    }

    const auto connections = graph.getConnections();
    for (const auto& c : connections) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;

        // Outgoing: our left leg is patched to dest's left leg, but the right pair is not —
        // complete L→L / R→R using each end's own right-leg channel.
        if (c.source.nodeID == nodeId && c.source.channelIndex == 0 && c.destination.channelIndex == 0 &&
            myOutputLeg >= 0) {
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            const int destLeg = destNode != nullptr ? rightAudioLegOf(destNode->getProcessor(), /*asInput=*/true) : -1;
            if (destLeg >= 0 && !hasEdge(nodeId, myOutputLeg, destNode->nodeID, destLeg))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, destLeg}});

            // Our right leg now feeds the dest's right leg, so the LEFT leg must stop feeding it as
            // well. While we were collapsed, resolvePolyLink duplicated our one Audio jack onto both
            // of the dest's raw legs (the mono→collapsed-pair fan); leaving that edge in place after
            // the split sums L+R into the dest's right leg — audibly right-heavy, and invisible,
            // since both edges draw as the same cable. Mirror of the re-point in
            // dropHiddenRightLegConnections, which is what puts the duplicate back on collapse.
            if (destLeg > 0 && destNode != nullptr && hasEdge(nodeId, myOutputLeg, destNode->nodeID, destLeg))
                graph.removeConnection({{nodeId, 0}, {destNode->nodeID, destLeg}});

            // destLeg < 0 means the destination has no SECOND audio input the user can see — a
            // collapsed split-block module (Filter/VCA/Wavetable), whose one "Audio" jack is its left
            // leg alone. USER RULING: wire our right leg into that same mono jack anyway, as an
            // explicit summed second cable. A module the user just split must not come up with a
            // visibly dangling Audio R, and stereo-into-mono summing is what hand-wiring both legs
            // onto that jack already produces — connectPorts has always allowed summed inputs.
            //
            // While both legs still carry the identical signal (the usual state right after a split)
            // the sum is +6 dB. That is transient: it lasts only until the legs differ, which is the
            // point of splitting. This was left dangling before precisely to avoid that jump; the
            // ruling traded it for both jacks being wired.
            //
            // What does NOT change: never wire the destination's hidden kRightBase. That block has no
            // jack, so the cable would be audible and impossible to unplug — the invariant
            // dropHiddenRightLegConnections exists to enforce. Here the target is
            // c.destination.channelIndex, the very channel our left leg is already wired to, so it is
            // visible by construction.
            //
            // Confined to split-block modules on purpose: their right leg is hidden again on
            // collapse, so dropHiddenRightLegConnections removes this extra cable for free and the
            // on/off round-trip is exact. An FX's raw1 stays part of its collapsed jack, so the same
            // cable would survive a collapse and be indistinguishable from a hand-drawn one.
            // (myOutputLeg >= 0 on a split-block module already implies it is currently dual: its
            // kRightBase is only reachable from a visible jack in that state.)
            if (destLeg < 0 && destNode != nullptr && mb->hasSplitBlockStereo() &&
                !hasEdge(nodeId, myOutputLeg, destNode->nodeID, c.destination.channelIndex))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, c.destination.channelIndex}});
        }

        // Outgoing into a DEDICATED mono audio input, i.e. a jack that is nobody's stereo pair: the
        // Ring Modulator's Carrier (ch0) and Modulator (ch1) are the case that reached a user, since
        // their roles differ and ch1 is never a right-hand input. The branch above only ever looked at
        // cables landing on the destination's ch0, so a module split while feeding Modulator wired
        // Audio L and stopped. Same ruling as the collapsed mono jack: sum the right leg into the very
        // same input.
        if (c.source.nodeID == nodeId && c.source.channelIndex == 0 && c.destination.channelIndex != 0 &&
            myOutputLeg >= 0 && mb->hasSplitBlockStereo()) {
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            const int destCh = c.destination.channelIndex;

            // Qualifies only when the target is a standalone mono AUDIO input the user can see:
            //   * a real ModuleBase jack (graph I/O has no roles to ask, and its ch1 is a stereo leg),
            //   * reachable from a visible jack, so never a hidden block,
            //   * not modulation CV - our left leg being patched there does not license dumping an
            //     audio-rate copy onto a Cutoff or Rate jack as well,
            //   * span 1 and not the right leg of a pair, which the ch0 branch above already covers.
            bool qualifies = false;
            if (destNode != nullptr) {
                if (auto* destMb = dynamic_cast<ModuleBase*>(destNode->getProcessor())) {
                    const LogicalPort port = destMb->mapInputChannel(destCh);
                    qualifies = audioChannelReachableFromJack(*destMb, destCh, /*isInput=*/true) &&
                                port.role != PortRole::ModCV && !destMb->isAutoPromotableModTarget(destCh) &&
                                port.polyVoiceSpan == 1 && rightAudioLegOf(destMb, /*asInput=*/true) != destCh;
                }
            }

            if (qualifies && !hasEdge(nodeId, myOutputLeg, destNode->nodeID, destCh))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, destCh}});
        }

        // Incoming: source's left leg is patched to ours, and the source is itself a stereo pair.
        //
        // Gated on the left jack having exactly ONE feed, which is what it has after the migration
        // above did its work. More than one means a mix of two different modules that migration
        // deliberately left alone: pairing or broadcasting one of them onto Audio R would pick a
        // winner by cable order and leave the legs carrying different mixes.
        if (c.destination.nodeID == nodeId && c.destination.channelIndex == 0 && c.source.channelIndex == 0 &&
            myInputLeg >= 0 && inputFeedCount(0) == 1) {
            auto* srcNode = graph.getNodeForId(c.source.nodeID);
            const int srcLeg = srcNode != nullptr ? rightAudioLegOf(srcNode->getProcessor(), /*asInput=*/false) : -1;
            if (srcLeg >= 0 && !hasEdge(srcNode->nodeID, srcLeg, nodeId, myInputLeg))
                graph.addConnection({{srcNode->nodeID, srcLeg}, {nodeId, myInputLeg}});

            // Same de-duplication from the other end: once the source's own right leg reaches our
            // right input, its left leg must not still be wired there too.
            if (srcLeg > 0 && srcNode != nullptr && hasEdge(srcNode->nodeID, srcLeg, nodeId, myInputLeg))
                graph.removeConnection({{srcNode->nodeID, 0}, {nodeId, myInputLeg}});

            // MONO-ONLY UPSTREAM: the source has no second audio output the user can see — either a
            // collapsed split-block module (its "Audio" jack is the left leg alone) or a genuinely
            // mono one. Broadcast the feed our LEFT leg already has onto the right leg, so a module
            // the user just split arrives with both legs live rather than half-wired.
            //
            // This is the one place the mono broadcast is applied to a non-adjacent right leg, and it
            // is deliberately confined to this handler: flipping the toggle is an explicit user
            // action on one module, whereas resolvePolyLink governs manual cable drags and
            // smart-connect, where the established behaviour for a split-block pair is left-leg-only
            // (see its comment). Nothing here widens that.
            //
            // Copying a feed cannot change the mix at all: the source simply drives one more
            // destination at the same level. (The output side above sums instead, which is why it
            // needed a ruling and this did not.) Guarded on the right leg being unfed so a second
            // toggle cannot stack feeds, and it takes the channel that already feeds our left audio
            // input rather than assuming ch0 is audio on the peer - the user routed that edge.
            if (srcLeg < 0 && srcNode != nullptr && !inputChannelIsFed(myInputLeg))
                graph.addConnection({{srcNode->nodeID, c.source.channelIndex}, {nodeId, myInputLeg}});
        }
    }

    dropHiddenRightLegConnections(nodeId);
}

int GraphEditor::rightAudioLegOf(juce::AudioProcessor* proc, bool asInput) {
    // Which raw channel is this end's right leg? FX put it on ch1; the voice modules put it on their
    // own kRightBase block, so asking the module beats assuming ch1 — assuming would have wired an
    // Oscillator's Waveform CV channel as if it were audio.
    if (proc == nullptr)
        return -1;
    auto* peerMb = dynamic_cast<ModuleBase*>(proc);
    if (peerMb == nullptr) {
        // Graph I/O (Audio Input/Output) is a plain contiguous pair.
        const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
        return channels >= 2 ? 1 : -1;
    }
    const int leg = peerMb->rightAudioLegChannel();
    if (leg < 0)
        return -1;
    const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
    if (leg >= channels)
        return -1;
    const LogicalPort port = asInput ? peerMb->mapInputChannel(leg) : peerMb->mapOutputChannel(leg);
    if (port.role != PortRole::Audio)
        return -1;

    // ...and it has to be a leg the user can actually SEE. A collapsed split-block module keeps
    // rendering its kRightBase block but exposes no jack for it, and FilterModule/VCAModule report
    // PortRole::Audio for those channels either way — so a role check alone would let the toggle
    // wire a cable onto a hidden jack, which is exactly the routing the invariant in CLAUDE.md
    // ("an invisible jack cannot be unplugged") forbids. A collapsed FX pair still passes: its
    // single Audio jack owns raw ch1 through voiceSpan 2.
    return audioChannelReachableFromJack(*peerMb, leg, asInput) ? leg : -1;
}

bool GraphEditor::audioChannelReachableFromJack(const ModuleBase& mb, int rawChannel, bool isInput) {
    const int visible = isInput ? mb.getVisibleInputPortCount() : mb.getVisibleOutputPortCount();
    for (int jack = 0; jack < visible; ++jack)
        for (const auto& t : mb.getJackTargets(jack, isInput))
            for (int v = 0; v < t.voiceSpan; ++v)
                if (t.rawHeadChannel + v == rawChannel)
                    return true;
    return false;
}

void GraphEditor::dropHiddenRightLegConnections(juce::AudioProcessorGraph::NodeID nodeId) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
    if (mb == nullptr)
        return;

    // Collapsing a SPLIT-BLOCK module hides its right leg entirely — unlike an FX pair, where the
    // collapsed jack still owns both raw legs. An invisible jack cannot be unplugged, so anything
    // left wired to that block has to be dropped here or it becomes a cable whose effect the user
    // can hear but never reach. (Only for split-block layouts: dropping ch1 on an FX module would
    // silently break the perfectly valid collapsed stereo pair.)
    if (mb->isDualIO() || !mb->hasSplitBlockStereo())
        return;

    const int hiddenBase = mb->rightAudioLegChannel();
    const int inputCount = mb->getTotalNumInputChannels();
    const int outputCount = mb->getTotalNumOutputChannels();

    auto hasEdge = [&graph](const juce::AudioProcessorGraph::Connection& c) {
        for (const auto& existing : graph.getConnections())
            if (existing == c)
                return true;
        return false;
    };

    // Does anything at all feed this raw input channel of ours? Used below to decide whether a
    // dropped right-leg INPUT cable has a left leg to fall back onto or would double-feed it.
    auto inputChannelIsFed = [&graph, nodeId](int rawChannel) {
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == rawChannel)
                return true;
        return false;
    };

    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;
        const bool fromHiddenOutput =
            c.source.nodeID == nodeId && c.source.channelIndex >= hiddenBase && c.source.channelIndex < outputCount;
        const bool intoHiddenInput = c.destination.nodeID == nodeId && c.destination.channelIndex >= hiddenBase &&
                                     c.destination.channelIndex < inputCount;
        if (!fromHiddenOutput && !intoHiddenInput)
            continue;

        graph.removeConnection(c);

        // RE-POINT, don't just delete. Collapsing hides the right block, but the cable hanging off
        // it was drawn by the user and its far end is still there — so it moves to the leg that
        // survives, which is the same raw offset in the LEFT block (voice v of the right block
        // pairs with voice v of the left one).
        //
        // This is the missing link behind "toggling Dual I/O off biases the mix left": in the
        // default patch the VCA's right leg feeds Distortion ch1, and dropping that cable left the
        // whole collapsed FX tail (Distortion → Delay → Reverb → Audio Output) rendering silence on
        // every right channel. Re-pointing sends the surviving mono leg there instead, at UNITY —
        // ModuleBase::panGains keeps a centred module's left leg at full level, so a collapsed
        // module is exactly as loud in both channels as it was in the left one before (no gain
        // compensation, and none wanted: an equal-power law would have made it 3 dB quiet).
        if (fromHiddenOutput) {
            const int leftCh = c.source.channelIndex - hiddenBase;
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            if (destNode == nullptr || !audioChannelReachableFromJack(*mb, leftCh, /*isInput=*/false))
                continue;
            // The far end must still expose the channel we are re-pointing onto — a collapsed peer's
            // own hidden right block is not a legal target, and neither end may gain a cable it
            // cannot show.
            if (auto* destMb = dynamic_cast<ModuleBase*>(destNode->getProcessor())) {
                if (!audioChannelReachableFromJack(*destMb, c.destination.channelIndex, /*isInput=*/true))
                    continue;
            } else if (c.destination.channelIndex >= destNode->getProcessor()->getTotalNumInputChannels()) {
                continue;
            }
            const juce::AudioProcessorGraph::Connection moved{{nodeId, leftCh}, c.destination};
            if (!hasEdge(moved))
                graph.addConnection(moved);
        } else {
            // Input side: our left leg is normally already fed by the same upstream, and adding a
            // second feed there would sum L+R into one mono jack (+6 dB). So re-point ONLY when the
            // left leg has nothing at all — otherwise the collapse legitimately drops the cable.
            const int leftCh = c.destination.channelIndex - hiddenBase;
            if (!audioChannelReachableFromJack(*mb, leftCh, /*isInput=*/true) || inputChannelIsFed(leftCh))
                continue;
            const juce::AudioProcessorGraph::Connection moved{c.source, {nodeId, leftCh}};
            if (!hasEdge(moved))
                graph.addConnection(moved);
        }
    }
}

void GraphEditor::handleModuleResized(ModuleComponent* moduleComp) {
    if (moduleComp == nullptr || moduleComp->getModule() == nullptr)
        return;

    auto& graph = audioEngine.getGraph();
    const auto nodeId = moduleComp->getNodeId();
    if (graph.getNodeForId(nodeId) == nullptr)
        return;

    // 1. Jacks that just disappeared take their cables with them. Leaving them connected would
    //    mean a routing that still shows in the mod matrix, still costs a node, and no longer
    //    carries anything (the module silences hidden channels) — with no jack to unplug it from.
    //    No undo transaction is opened here: the parameter gesture that changed the count already
    //    snapshots the whole graph before and after, so this is part of that single undo step.
    if (auto* mb = dynamic_cast<ModuleBase*>(moduleComp->getModule())) {
        auto channelStillExposed = [mb](int rawChannel, bool isInput) {
            const int visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
            for (int j = 0; j < visible; ++j) {
                for (const auto& t : mb->getJackTargets(j, isInput))
                    for (int v = 0; v < t.voiceSpan; ++v)
                        if (t.rawHeadChannel + v == rawChannel)
                            return true;
            }
            return false;
        };

        std::vector<juce::AudioProcessorGraph::Connection> toRemove;

        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == nodeId && !c.source.isMIDI() && !channelStillExposed(c.source.channelIndex, false)) {
                if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
                    if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr) {
                        audioEngine.removeModRouting(dstNode->nodeID);
                        continue;
                    }
                }
                toRemove.push_back(c);
            }
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() &&
                !channelStillExposed(c.destination.channelIndex, true)) {
                if (auto* srcNode = graph.getNodeForId(c.source.nodeID)) {
                    if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr) {
                        audioEngine.removeModRouting(srcNode->nodeID);
                        continue;
                    }
                }
                toRemove.push_back(c);
            }
        }

        for (const auto& c : toRemove)
            graph.removeConnection(c);
    }

    // 2. Nudge neighbours clear of the new footprint. The resized module stays put.
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            boxes.push_back({comp->getNodeId(), comp->getBounds()});

    for (const auto& moved : synth::LayoutUtil::resolveOverlapsAfterResize(nodeId, boxes)) {
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp->getNodeId() != moved.id)
                continue;
            comp->setTopLeftPosition(moved.pos);
            if (auto* node = graph.getNodeForId(moved.id)) {
                node->properties.set("x", moved.pos.x);
                node->properties.set("y", moved.pos.y);
            }
        }
    }

    repaintCanvas();
}

juce::Point<int> GraphEditor::resolvePlacement(juce::Point<int> desired, int w, int h,
                                               juce::AudioProcessorGraph::NodeID selfId) {
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        boxes.push_back({comp->getNodeId(), comp->getBounds()});
    }
    auto snapped = synth::LayoutUtil::snap(desired);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, boxes, selfId);
}

juce::Point<int> GraphEditor::findLeftEdgeSlotBelowModules(int w, int h) {
    // Left edge, below everything: a Track In node is the head of a chain the user reads
    // left-to-right, and stacking new ones downwards keeps successive tracks in track order rather
    // than scattered wherever a free slot happened to be.
    int left = synth::LayoutUtil::kArrangeOriginX;
    int bottom = synth::LayoutUtil::kArrangeOriginY;
    bool any = false;

    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        const auto bounds = comp->getBounds();
        left = any ? std::min(left, bounds.getX()) : bounds.getX();
        bottom = any ? std::max(bottom, bounds.getBottom()) : bounds.getBottom();
        any = true;
    }

    const juce::Point<int> desired(left, any ? bottom + synth::LayoutUtil::kArrangeOriginY
                                             : synth::LayoutUtil::kArrangeOriginY);
    return resolvePlacement(desired, w, h, juce::AudioProcessorGraph::NodeID{});
}

// static
juce::Point<int> GraphEditor::computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                       const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                       synth::LayoutUtil::NodeID selfId) {
    auto snapped = synth::LayoutUtil::snap(dropPoint);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, existingBoxes, selfId);
}

void GraphEditor::finalizeModuleDrag(ModuleComponent* module) {
    if (module == nullptr)
        return;
    auto clear = resolvePlacement(module->getPosition(), module->getWidth(), module->getHeight(), module->getNodeId());
    module->setTopLeftPosition(clear);
    // Persist the snapped/cleared position to graph node properties so it survives reload.
    updateModulePosition(module);

    // Apply proximity suggestions before the drag-preview teardown clears them. Group drags never
    // reach here with multi-select (finalizeSelectionDrag handles those). Connections join the
    // surrounding module-drag undo snapshot (captureBeforeState / pushSnapshotFromCapture).
    if (shouldOfferSmartConnections() && !smartSuggestions.empty())
        applySmartSuggestions(module->getNodeId(), /*recordUndo=*/false);

    repaintCanvas();
}

void GraphEditor::animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos) {
    if (module == nullptr)
        return;

    // If from == to nothing to animate — just ensure the module is at the final position.
    if (fromPos == toPos)
        return;

    // Place module at fromPos immediately so the first frame starts correctly.
    module->setTopLeftPosition(fromPos);

    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    juce::Component::SafePointer<ModuleComponent> safeModule(module);

    dropLandingAnim.start(
        vblankUpdater,
        180.0, // ms — within the 150-220 ms spec
        synth::ui::easeOutCubic,
        [safeEditor, safeModule, fromPos, toPos](float t) {
            if (safeEditor == nullptr || safeModule == nullptr)
                return;
            auto pos = synth::ui::AnimationDriver::lerpBounds(juce::Rectangle<int>(fromPos.x, fromPos.y, 0, 0),
                                                              juce::Rectangle<int>(toPos.x, toPos.y, 0, 0), t)
                           .getTopLeft();
            safeModule->setTopLeftPosition(pos);
            safeEditor->repaintCanvas();
        },
        [safeEditor, safeModule, toPos]() {
            // Settle at exact final position and persist to graph properties.
            if (safeEditor == nullptr || safeModule == nullptr)
                return;
            safeModule->setTopLeftPosition(toPos);
            safeEditor->updateModulePosition(safeModule);
            safeEditor->repaintCanvas();
        });
}

void GraphEditor::autoArrange() {
    auto& graph = audioEngine.getGraph();
    if (undoManager)
        undoManager->captureBeforeState(graph);

    auto sizeOf = [this](synth::LayoutUtil::NodeID id) -> juce::Point<int> {
        for (auto* c : content.getModules()) {
            if (c != nullptr && c->getNodeId() == id)
                return {c->getWidth(), c->getHeight()};
        }
        return {synth::LayoutUtil::kSingleWidth, 300};
    };

    std::vector<std::pair<synth::LayoutUtil::NodeID, synth::LayoutUtil::NodeID>> extra;
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasSource && r.hasDest)
            extra.push_back({r.sourceNodeID, r.destNodeID});
    }

    auto layout = synth::LayoutUtil::computeAutoArrange(graph, sizeOf, extra);
    for (const auto& a : layout) {
        if (auto* n = graph.getNodeForId(a.id)) {
            n->properties.set("x", a.pos.x);
            n->properties.set("y", a.pos.y);
        }
    }

    updateComponents();

    if (undoManager)
        undoManager->pushSnapshotFromCapture(graph);
}

void GraphEditor::savePreset(juce::File file) {
    auto json = synth::AIStateMapper::graphToJSON(audioEngine.getGraph());
    // Re-merge whatever unknown top-level keys were stashed on the last load (e.g. a future
    // build's "timeline") — graphToJSON only knows about the keys this build understands.
    json = patchDocument.toVar(json);
    file.replaceWithText(juce::JSON::toString(json));
}

bool GraphEditor::loadFactoryPreset(int index) {
    // Tear down existing module components (which stops their ScopeComponent timers) BEFORE
    // PresetManager clears the graph and frees the old VisualBuffers — otherwise a scope timer can
    // fire and read a freed buffer (use-after-free). Mirrors the safe order used by the test harness.
    detachAllModuleComponents();
    bool loaded = synth::PresetManager::loadPreset(index, audioEngine.getGraph());
    updateComponents();
    return loaded;
}

void GraphEditor::newPatch() {
    auto& graph = audioEngine.getGraph();

    auto doClear = [this, &graph] {
        // Detach BEFORE clearing — same ordering as loadFactoryPreset — so no ScopeComponent
        // timer fires against a freed VisualBuffer after graph.clear().
        detachAllModuleComponents();
        graph.clear();
        // A fresh patch has no file behind it — preserved keys are per-loaded-file and must
        // never be resurrected into it.
        patchDocument.clear();
        macros.clear();
        updateComponents(); // reconciles the now-empty view; the empty-canvas hint will show
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doClear);
    } else {
        doClear();
    }
    repaint();
}

void GraphEditor::loadPreset(juce::File file) {
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not parse preset file.");
        return;
    }
    // Stash any top-level keys this build doesn't understand (e.g. a future "timeline") before
    // applyJSONToGraph — it only ever looks at the known patch keys, so this is the one place
    // that sees the raw root object.
    patchDocument.loadFromVar(json);
    // Detach before applyJSONToGraph clears the graph (see loadFactoryPreset — avoids scope-timer UAF).
    detachAllModuleComponents();
    // The user picked this file from their own filesystem — trusted, unlike an AI-authored patch.
    if (synth::AIStateMapper::applyJSONToGraph(json, audioEngine.getGraph(), true, /*trusted=*/true)) {
        updateComponents();
    } else {
        updateComponents(); // reconcile view to whatever state the graph is in after a failed apply
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not apply preset to graph.");
    }
}
