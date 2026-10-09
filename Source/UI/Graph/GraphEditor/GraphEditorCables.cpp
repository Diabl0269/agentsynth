// GraphEditorCables.cpp
//
// GraphEditor's cable geometry, colour and paint: buildVisibleCables()/rebuildVisibleCables(),
// hit-testing (getCableAt/distanceToCable), colour resolution, and GraphContentComponent's
// paint/paintOverChildren/resized. The collapsed-macro cable post-pass lives in
// GraphEditorMacroCableAnchors.cpp. GraphEditor itself is declared in GraphEditor.h; sibling
// GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "GraphEditorPaintMemo.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/MacroGroupController/MacroNesting.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/Layout/CableCurve.h"

#include "Modules/AttenuverterModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

// ============================================================================
// Cables
//
// One enumeration feeds both painting and hit-testing. Keeping them separate was the obvious
// shortcut and the wrong one: the drawn curve and the clickable curve would drift apart the
// first time either was tweaked, and clicks would silently miss the wire.
// ============================================================================

// The cubic bezier a cable is drawn along, hit-tested against and previewed with: one shared
// function (UI/Layout/CableCurve.h), also used by AppLookAndFeel::drawConnectionWire's fallback.
// p1 is the source (output) end, p2 the destination (input) end.
juce::Path GraphEditor::buildCablePath(juce::Point<float> p1, juce::Point<float> p2) {
    return synth::ui::makeCablePath(p1, p2);
}

// Perpendicular distance from a canvas point to a cable's curve, in pixels.
float GraphEditor::distanceToCable(const VisibleCable& cable, juce::Point<float> canvasPos) {
    auto path = buildCablePath(cable.p1, cable.p2);
    juce::Point<float> nearest;
    // getNearestPoint returns the distance ALONG the path; the perpendicular distance we want is
    // from the cursor to the point it writes out.
    path.getNearestPoint(canvasPos, nearest);
    return canvasPos.getDistanceFrom(nearest);
}

// Topmost cable within `tolerance` px of a canvas point, or nullopt.
// Later cables win, matching paint order (mod wires draw over audio wires).
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
// ---- Expanded-macro grouping hull ----------------------
// A collapsed macro reads as a card; an expanded one left no on-canvas trace that its members
// were still grouped — Cmd+G on them again just refused with "already in a macro", with nothing
// visible to explain why. Draws a light dashed outline + name chip around the live union of
// member bounds so the grouping stays visible while expanded. Extracted out of
// GraphContentComponent::paint (rather than inlined there) to keep that function under the
// check-function-sizes.sh ratchet — this is its own named step, not a collaborator with state of
// its own, so a free function beside paint() rather than a new class earns its keep here.
// Whether a repaint of `clip` touches anything paintExpandedMacroHulls draws for the macro: everything sits inside its
// border except a name chip wider than the border, which only the chip row can reach. A glide frame repaints a small
// area, and dashing every border and measuring every chip's name for it cost as much as the rest of that paint.
bool intersectsHullOrChip(juce::Rectangle<int> clip, juce::Rectangle<int> hull, GraphEditor& editor,
                          const juce::String& macroId) {
    if (clip.intersects(hull.expanded(4)))
        return true;
    const bool inChipRow = clip.getBottom() >= hull.getY() && clip.getY() <= hull.getY() + 24;
    return inChipRow && clip.intersects(editor.getMacroController().macroChipBounds(macroId).expanded(2));
}

void paintExpandedMacroHulls(juce::Graphics& g, GraphEditor& editor) {
    if (editor.getMacros().empty())
        return;

    // Parents first, so a nested child's outline and chip draw over its parent's. A macro that is
    // collapsed, or hidden inside a collapsed ancestor, gets an empty hull and is skipped below.
    for (const auto* macroPtr : macro_nesting::macrosParentsFirst(editor.getMacros())) {
        const auto& macro = *macroPtr;

        // macroHullBounds is the ONE definition of this rectangle for hit-testing
        // (GraphEditor::macroHullAt, used by mouseDown/mouseUp for hull click-to-select and the
        // hull's right-click macro menu). What gets PAINTED goes through paintedMacroHullBounds
        // instead, which is macroHullBounds itself except while a reparent drag is dragging one of
        // THIS macro's own members — see its doc comment (GraphEditor.h) for why the two diverge
        // only in that one case (a member being pulled out must visibly shrink the hull away from
        // it, which the live union alone can never do).
        const auto hull = editor.paintedMacroHullBounds(macro.id);
        if (hull.isEmpty() || editor.getCardGlide().isBorderHeld(macro.id) ||
            !intersectsHullOrChip(g.getClipBounds(), hull, editor, macro.id))
            continue;
        const float revealAlpha = editor.getLoadReveal().hullAlpha(macro.id); // fades in as a project opens
        if (revealAlpha <= 0.0f)
            continue;
        const LoadRevealAnimator::ScopedFade revealFade(g, revealAlpha);
        ++graph_editor_paint::workCounters().hullsPainted;

        // A live reparent drag whose leave OR join candidate (GraphEditor::getMacroDragLeaveId
        // / getMacroDragJoinId) is THIS macro gets the SAME dashed hull, just emphasized — heavier,
        // fully opaque, and topped with a solid stroke — rather than a second visual language for
        // "about to change" (docs/macros/menu-and-membership.md). A transfer emphasises both, and a macro
        // the drag has already moved its module into stays emphasised until the drop.
        const bool isDragCandidate = macro.id == editor.getMacroDragLeaveId() ||
                                     macro.id == editor.getMacroDragJoinId() ||
                                     macro.id == editor.getMacroDragLiveOwnerId();

        MacroFoldAnimator::paintDashedBorder(g, hull.toFloat(), macro.colour, isDragCandidate);

        // A tab overlapping the hull's own top edge, not floating above it — a macro whose
        // members sit near the top of the canvas would otherwise clip the label off-canvas with
        // nothing to scroll up to. macroChipBounds is the ONE definition of this rect - hit-
        // testing (GraphEditor::macroChipAt, the chip's drag/rename affordance) must see exactly
        // what gets painted here, so paint uses the same font macroChipBounds measures with
        // rather than computing its own width.
        const juce::String label = macro.name.isNotEmpty() ? macro.name : juce::String("Macro");
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        const auto chipBounds = editor.getMacroController().macroChipBounds(macro.id);
        juce::Rectangle<float> chip = chipBounds.toFloat();
        g.setColour(macro.colour.withAlpha(0.85f));
        g.fillRoundedRectangle(chip, 6.0f);

        // Grip affordance: three short vertical lines at the chip's left edge, so it reads as a
        // drag handle rather than a plain label. Restrained and inside the 18px chip height.
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        const float gripX = chip.getX() + 6.0f;
        const float gripTop = chip.getY() + 5.0f;
        const float gripBottom = chip.getBottom() - 5.0f;
        for (int i = 0; i < 3; ++i) {
            const float x = gripX + (float)i * 3.0f;
            g.drawLine(x, gripTop, x, gripBottom, 1.0f);
        }

        g.setColour(juce::Colours::white);
        g.drawText(label, chip.withLeft(chip.getX() + 12.0f), juce::Justification::centred, false);

        // Collapse button: the chip's own drag/rename affordance never
        // looked like "collapse me" — the only routes back to a collapsed card were the right-
        // click menu and an undocumented double-click. A small button at the OTHER end of the
        // same row, pointing the opposite way from MacroCardComponent's expand chevron, reads as
        // the same control in its two states. macroCollapseButtonBounds is the ONE definition of
        // this rect — hit-testing (GraphEditor::macroCollapseButtonAt, used by mouseDown) must see
        // exactly what gets painted here.
        const auto collapseBounds = editor.getMacroController().macroCollapseButtonBounds(macro.id).toFloat();
        g.setColour(macro.colour.withAlpha(0.85f));
        g.fillRoundedRectangle(collapseBounds, 4.0f);

        // The hull is the open macro, so this is the shared fold arrow open (points down), the pair of
        // the folded card's closed one. White on the macro's own colour, not a theme token: the chip
        // fill is user-chosen, so only a fixed contrast colour reads on every macro colour.
        constexpr float kCollapseChevronSize = 8.0f;
        synth::theme::paintDisclosureChevron(
            g,
            juce::Rectangle<float>(kCollapseChevronSize, kCollapseChevronSize).withCentre(collapseBounds.getCentre()),
            1.0f, juce::Colours::white.withAlpha(0.85f));
    }
}
} // namespace

// Enumerates every cable currently drawn on the canvas, in paint order.
// Memoized: the list is rebuilt when the canvas is asked to repaint (repaintCanvas()) and on
// every 30 Hz tick, never per-paint. Cable geometry is CANVAS-space, so zoom and pan cannot
// move a cable — except an open macro's port interior jack, which slides with the zoom, so updateTransform()
// drops the memo whenever a macro exists. Do not store the returned reference
// across a repaintCanvas(), a timerCallback() or any graph edit.
const std::vector<GraphEditor::VisibleCable>& GraphEditor::buildVisibleCables() {
    if (!cablesCacheValid) {
        {
            const ModuleComponent::JackLayoutPass jackLayoutPass; // each card's jack column is worked out once
            cablesCache = rebuildVisibleCables();
        }
        macroCrossingAnim_.applyTo(cablesCache); // cable-slide overlay, see MacroCrossingAnimator.h
        cardGlide_.applyTo(cablesCache);         // endpoints follow gliding cards, see CardGlideAnimator.h
        cardGlide_.fold().applyTo(cablesCache);  // an unfolding macro draws its modules' cables itself
        cablesCacheValid = true;
        ++cableRebuildCount;
    }
    return cablesCache;
}

// The single "the canvas changed" seam: drops the cable memo and the macro borders measured since the last change
// (CanvasMemo::layoutChanged), then repaints. Every former `content.repaint()` in this file goes through here. Also
// GraphCanvasHost::repaintCanvas(). Null-checked: cards laid out while the editor is built and torn down land here.
void GraphEditor::repaintCanvas() {
    cablesCacheValid = false;
    if (canvasMemo_ != nullptr)
        canvasMemo_->layoutChanged();
    content.repaint();
}

// The actual enumeration; buildVisibleCables() is the memoized public entry point above.
std::vector<GraphEditor::VisibleCable> GraphEditor::rebuildVisibleCables() {
    const graph_editor_paint::MacroOwnerScope oneOwnerMap(*this); // every macro port's owner from one map
    std::vector<VisibleCable> cables;
    auto& graph = audioEngine.getGraph();
    auto& moduleComponents = content.getModules();

    // nodeID -> component, so both passes resolve port positions in O(1); built through a processor -> node map, not a
    // scan of every node per card.
    std::unordered_map<const juce::AudioProcessor*, uint32_t> nodeOfProcessor;
    for (auto* node : graph.getNodes())
        nodeOfProcessor.emplace(node->getProcessor(), node->nodeID.uid);
    std::unordered_map<uint32_t, ModuleComponent*> nodeCompMap;
    for (auto* comp : moduleComponents)
        if (comp != nullptr)
            if (const auto it = nodeOfProcessor.find(comp->getModule()); it != nodeOfProcessor.end())
                nodeCompMap[it->second] = comp;
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
            cable.destNodeId = realDstNode->nodeID.uid;
            cable.destChannel = realDstPort;

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
        // MIDI ports are fixed anchors (top-right/top-left), not part of the audio jack stack —
        // portPos(..., 0, ...) would land on audio jack 0 instead, which paint() offsets downward
        // to avoid colliding with the MIDI Out dot.
        cable.p1 = srcIsMidi ? (srcComp->getBounds().getPosition() + srcComp->getMidiPortCenter(true)).toFloat()
                             : portPos(srcComp, srcJack, false);
        cable.p2 = dstIsMidi ? (dstComp->getBounds().getPosition() + dstComp->getMidiPortCenter(false)).toFloat()
                             : portPos(dstComp, dstJack, true);
        cable.signal = (srcIsMidi || dstIsMidi) ? synth::ui::CableSignal::Midi : synth::ui::CableSignal::Audio;
        cable.sourceCategory = categoryForNode(node1);
        cable.destNodeId = node2->nodeID.uid;
        cable.destChannel = connection.destination.channelIndex;
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
        cable.destNodeId = routing.destNodeID.uid;
        cable.destChannel = routing.destChannelIndex;
        cables.push_back(cable);
    }

    // Re-anchor AttenuverterChain cables onto their bound, visible target knob's ring
    // start point (docs/layout/cables.md#knob-landing) BEFORE the collapsed-macro pass below, so
    // a macro-crossing cable still gets the macro's own boundary anchor on top.
    reanchorCablesToKnobTargets(cables);
    reanchorCablesAroundCollapsedMacros(cables);

    return cables;
}

// Resolved colour for a cable under the current mode + overrides + active theme.
juce::Colour GraphEditor::colourForCable(const VisibleCable& cable) const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    // Headless tests install the stock JUCE LnF; fall back to the token defaults so colour
    // resolution stays exercised rather than short-circuited.
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    return synth::ui::resolveCableColour(cableColourMode, cable.signal, cable.sourceCategory, colors,
                                         cableColourOverrides, cable.isBypassed);
}

// Last folder a Wavetable card browsed to. Held here so a newly dropped Wavetable seeds
// its browser from wherever the user was last working; MainComponent owns the round trip
// to ApplicationProperties via onWavetableFolderChanged, keeping GraphEditor
// settings-free (same split as the cable-colour config above).
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

// Removes every graph edge behind a user-visible cable, as one undoable action.
void GraphEditor::disconnectCable(const VisibleCable& cable) {
    auto& graph = audioEngine.getGraph();

    // Both cable kinds populate id.srcUid/dstUid with the REAL logical endpoints — for an
    // AttenuverterChain that's the true mod source/destination the chain proxies, never the hidden
    // attenuverter itself (buildVisibleCables() constructs it that way, and the attenuverter splice
    // logic already treats them as such). Decide BEFORE mutating whether removing this cable touches a
    // macro port, so the right undo transaction is chosen up front: pruneMacroPortsAfterCut may then drop a
    // port left cableless or one-sided (see docs/macros/auto-ports.md#auto-deleting-a-port-when-its-last-cable-goes).
    const juce::AudioProcessorGraph::NodeID srcId{cable.id.srcUid};
    const juce::AudioProcessorGraph::NodeID dstId{cable.id.dstUid};
    const bool touchesMacroPort = macroController_.nodeIsMacroPort(srcId) || macroController_.nodeIsMacroPort(dstId);

    // An attenuverter chain is a hidden node plus its two edges, and the macro ports it crossed go with it --
    // whatever the auto-delete preference says, since the removal is the request. Double-clicking the knob
    // takes the same path.
    if (cable.kind == VisibleCable::Kind::AttenuverterChain) {
        removeModulationChain(juce::AudioProcessorGraph::NodeID{cable.id.attenUid});
        return;
    }

    const auto cablesBefore = snapshotCablesForRetract();

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

    if (touchesMacroPort) {
        auto doMutation = [this, removeEdges, srcId, dstId] {
            removeEdges();
            pruneMacroPortsAfterCut({srcId, dstId}, {});
            updateComponents();
        };
        if (undoManager)
            undoManager->recordGraphAndMacroChange(graph, macros, doMutation);
        else
            doMutation();
    } else if (undoManager) {
        undoManager->recordStructuralChange(graph, removeEdges);
    } else {
        removeEdges();
    }

    hoveredCableId.reset();
    repaintCanvas();
    retractCablesGoneSince(cablesBefore);
}

// Subtle dots every 40 px over the VISIBLE canvas region, shown only while a module is being dragged. Cheap: the clip
// is already in canvas coordinates, so everything outside it is skipped.
static void paintDragPreviewGrid(juce::Graphics& g, synth::theme::AppLookAndFeel* lf) {
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

void GraphEditor::GraphContentComponent::paint(juce::Graphics& g) {
    const graph_editor_paint::HullMemoScope hullMemo(editor); // each macro border is computed once per paint
    // Resolve the themed LookAndFeel once. In headless tests the default JUCE LnF is
    // installed, so the cast returns null and we fall back to plain fills/lines.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // Outside colour over the whole content, then the canvas frame (bg1 + dot grid, rounded, hairline border).
    if (lf != nullptr)
        lf->fillThemedBackground(g, getLocalBounds().toFloat(), /*isCanvas*/ false);
    else
        g.fillAll(juce::Colours::darkgrey);

    {
        constexpr float kFrameCorner = 10.0f;
        const auto frame = editor.getCanvasFrameRect();
        juce::Path outline;
        outline.addRoundedRectangle(frame, kFrameCorner);
        g.saveState();
        g.reduceClipRegion(outline);
        if (lf != nullptr) // with no theme installed (headless tests) the frame is the plain fill, no different
            lf->fillThemedBackground(g, frame, /*isCanvas*/ true);
        g.restoreState();
        if (lf != nullptr) {
            g.setColour(lf->getTheme().colors.border);
            g.strokePath(outline, juce::PathStrokeType(1.0f / juce::jmax(0.01f, editor.zoomLevel)));
        }
    }

    if (editor.getDragDropController().isDragPreviewActive())
        paintDragPreviewGrid(g, lf);

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
    // user overrides are applied in exactly one place. A cable wholly outside the area being repainted is skipped
    // before its path is built: a glide frame repaints a small area, and stroking every cable on a big patch only to
    // have it clipped away cost more than the rest of that paint.
    const auto clip = g.getClipBounds().toFloat();
    auto& reveal = editor.getLoadReveal();
    // The cable a port connections panel row points at is drawn as hovered and every other one dims.
    const auto& focusedCable = editor.getPortPanel().highlightedCable();
    for (const auto& cable : editor.buildVisibleCables()) {
        if (!clip.intersects(synth::ui::cablePaintBounds(cable.p1, cable.p2)))
            continue;
        const float drawn = reveal.cableProgress(cable.id.srcUid, cable.id.dstUid); // < 1 while a project opens
        if (drawn <= 0.0f)
            continue;
        const bool dimmed = focusedCable.has_value() && *focusedCable != cable.id;
        const juce::Colour colour =
            editor.colourForCable(cable).withMultipliedAlpha(dimmed ? synth::ui::kPortPanelDimAlpha : 1.0f);
        const bool isModulation = cable.kind != GraphEditor::VisibleCable::Kind::Direct;
        // A cable growing back (undo, redo) is real already: this pass skips it and its growing wire is drawn instead.
        if (editor.cableRetract_.isGrowing(cable.id)) {
            const auto growing = editor.cableRetract_.grown(cable);
            strokeWire(growing.p1, growing.p2, colour.withMultipliedAlpha(editor.cableRetract_.growOpacity()),
                       isModulation, 0.0f, 2.0f, false);
            continue;
        }
        ++graph_editor_paint::workCounters().cablesPainted;
        const bool hovered = editor.isCableHovered(cable) || (focusedCable.has_value() && *focusedCable == cable.id);

        float fallbackWidth = 2.0f;
        if (cable.kind == GraphEditor::VisibleCable::Kind::AttenuverterChain)
            fallbackWidth = 2.0f + cable.activity * 2.0f;
        else if (cable.kind == GraphEditor::VisibleCable::Kind::ModRouting)
            fallbackWidth = 2.5f + cable.activity * 2.0f;

        if (drawn < 1.0f) { // drawing out from its source jack (LoadRevealAnimator.h), no knob or badge yet
            strokeWire(cable.p1, cable.p1 + (cable.p2 - cable.p1) * drawn, colour, isModulation, 0.0f, fallbackWidth,
                       false);
            continue;
        }
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
    // Removed cables pulling back into their source jack and fading (CableRetractAnimator.h); the growing ones above.
    for (const auto& ghost : editor.cableRetract_.ghosts())
        strokeWire(ghost.p1, ghost.p2, editor.colourForCable(ghost).withMultipliedAlpha(editor.cableRetract_.opacity()),
                   ghost.kind != GraphEditor::VisibleCable::Kind::Direct, 0.0f, 2.0f, false);
    // ---- End cables ----

    // ---- Expanded-macro grouping hull (see paintExpandedMacroHulls
    // above, for what it draws and why it's a free function rather than inlined here) ----
    paintMacroPortStrips(g, editor, getTransform().getScaleFactor()); // under the widgets, under the outline
    paintExpandedMacroHulls(g, editor);

    // Draw Line being dragged
    if (editor.isDraggingConnection) {
        if (editor.dragSourceModule) {
            juce::Point<int> p;
            if (editor.dragSourceIsMidi) {
                p = editor.dragSourceModule->getMidiPortCenter(!editor.dragSourceIsInput);
            } else {
                p = editor.dragSourceModule->getPortCenter(editor.dragSourceChannel, editor.dragSourceIsInput);
            }

            auto posInContent = editor.dragSourceModule->getBounds().getPosition() + p;
            // dragCurrentPos is a screen position; the content component is transformed, so map it from the
            // screen rather than from the editor.
            auto mouseLocal = getLocalPoint(nullptr, editor.dragCurrentPos);

            // In-progress drag wire: resolved through the same colour path as a real cable, so
            // the preview already looks like the cable it is about to become (in By-source mode
            // that means the dragged-from module's category colour, not a generic white).
            GraphEditor::VisibleCable preview;
            preview.signal = editor.dragSourceIsMidi ? synth::ui::CableSignal::Midi : synth::ui::CableSignal::Audio;
            if (auto* mb = dynamic_cast<ModuleBase*>(editor.dragSourceModule->getModule()))
                preview.sourceCategory = synth::ui::categoryFor(mb->getModuleType());
            // Cables run source -> destination: dragging from an input, the jack is the destination.
            const auto jackPt = posInContent.toFloat();
            const auto mousePt = mouseLocal.toFloat();
            strokeWire(editor.dragSourceIsInput ? mousePt : jackPt, editor.dragSourceIsInput ? jackPt : mousePt,
                       editor.colourForCable(preview),
                       /*isModulation*/ false,
                       /*activity*/ 0.0f, /*fallbackWidth*/ 3.0f, /*hovered*/ false);
        }
    }
}

void GraphEditor::GraphContentComponent::resized() {}

void GraphEditor::GraphContentComponent::paintOverChildren(juce::Graphics& g) {
    // Gliding card snapshots first, so the dots and the drag ghost stay on top of them.
    editor.cardGlide_.paint(g);
    editor.getLoadReveal().paintOutlines(g); // cards still waiting for their assets as a project opens

    // ---- Knob-landing dots -------------
    // Cables are drawn in paint(), which runs BEFORE children -- an AttenuverterChain cable
    // re-anchored onto a knob (reanchorCablesToKnobTargets) therefore has its final stretch drawn
    // UNDER the opaque module card. Painting a small dot here, on top of every child, is what
    // actually shows the cable landing on the ring: docs/layout/cables.md#knob-landing.
    for (const auto& cable : editor.buildVisibleCables()) {
        if (!cable.landsOnKnob || editor.getLoadReveal().cableProgress(cable.id.srcUid, cable.id.dstUid) < 1.0f)
            continue;
        g.setColour(editor.colourForCable(cable));
        // Shared with ModuleComponent::getModTargetKnobAnchor's own push-out math, so the
        // anchor point and the dot drawn on it can never disagree on the dot's own size.
        g.fillEllipse(cable.p2.x - ModuleComponent::kKnobLandingDotDiameter * 0.5f,
                      cable.p2.y - ModuleComponent::kKnobLandingDotDiameter * 0.5f,
                      ModuleComponent::kKnobLandingDotDiameter, ModuleComponent::kKnobLandingDotDiameter);
    }

    // ---- Mod-dot tooltip: above the cards, so no card's clip cuts it ----
    editor.getModDot().paintTooltip(g);

    // ---- Drag-preview landing ghost (on top of module cards) ----
    // Draw a translucent rounded rect at the exact snapped+anti-overlapped landing position.
    if (editor.getDragDropController().isDragPreviewActive() &&
        !editor.getDragDropController().getDragPreviewGhost().isEmpty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour = synth::theme::themeOf(*this).colors.accent;
        const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
        const float cornerRadius = m.cornerRadius;

        auto ghostF = editor.getDragDropController().getDragPreviewGhost().toFloat();

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
    if (editor.getDragDropController().isDragPreviewActive() &&
        !editor.getDragDropController().getAlignmentGuides().empty() && editor.alignmentGuidesEnabled) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour guideColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;

        // Solid lines, ~70% opacity for visibility without distraction
        const float guideAlpha = lf != nullptr ? lf->getTheme().metrics.guideAlpha : 0.7f;
        g.setColour(guideColour.withAlpha(guideAlpha));
        for (const auto& guide : editor.getDragDropController().getAlignmentGuides()) {
            const float dx = guide.end.x - guide.start.x;
            const float dy = guide.end.y - guide.start.y;

            if (std::abs(dx) > std::abs(dy)) { // Horizontal line
                g.drawHorizontalLine((int)guide.start.y, guide.start.x, guide.end.x);
            } else { // Vertical line
                g.drawVerticalLine((int)guide.start.x, guide.start.y, guide.end.y);
            }
        }
    }

    // ---- Marquee selection band -----------------
    // Drawn here, in canvas space, so it stays locked to the modules it is selecting while the
    // view is zoomed. Paint-only — the selection itself is computed in updateMarquee().
    if (editor.marqueeActive && !editor.marqueeRect.isEmpty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour = synth::theme::themeOf(*this).colors.accent;
        const float lineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;

        auto bandF = editor.marqueeRect.toFloat();
        g.setColour(accentColour.withAlpha(0.12f));
        g.fillRect(bandF);
        g.setColour(accentColour.withAlpha(0.80f));
        g.drawRect(bandF, lineWidth);
    }

    // ---- Smart-connection frosted preview cables ----
    if (editor.getDragDropController().isDragPreviewActive() &&
        !editor.getSmartConnections().getSmartSuggestions().empty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

        // Colours resolve only through colourForCable (→ synth::ui::resolveCableColour), so the
        // cable-colour mode and any user override keep applying to a preview too.
        // Every preview alpha is scaled by the fade-in that runs when the suggestion first appears.
        const float reveal = editor.getSmartPreviewReveal();
        auto previewColour = [this, reveal](synth::ui::CableSignal signal, synth::ui::ModuleCategory category,
                                            float alpha) {
            GraphEditor::VisibleCable preview;
            preview.signal = signal;
            preview.sourceCategory = category;
            return editor.colourForCable(preview).withAlpha(alpha * reveal);
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
        for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
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
        for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
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

    editor.getPortPanel().paintPreview(g); // the cable a row of the port panel's Add connection page would make

    // ---- Macro-crossing module flash ------------
    // A fading ring over the module that just joined/left an expanded macro's hull, on top of its
    // (buffered-to-image) card. editor.macroCrossingAnim_ is private state on GraphEditor —
    // GraphContentComponent is a nested class, so this direct access is the same "one-line
    // forwarder" relationship every other block above already has with `editor`.
    if (auto flash = editor.macroCrossingAnim_.flashState()) {
        const juce::Colour accentColour = synth::theme::themeOf(*this).colors.accent;
        const auto [bounds, t] = *flash;
        // Settles from a slightly oversized ring down onto the card, fading out as it lands —
        // "flash-or-settle" per the ticket, not a static outline held for the tween's whole
        // duration.
        auto ring = bounds.toFloat().expanded(6.0f * (1.0f - t));
        g.setColour(accentColour.withAlpha(0.55f * (1.0f - t)));
        g.drawRoundedRectangle(ring, 10.0f, 2.0f);
    }
}
