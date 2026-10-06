// GraphEditorDragDrop.cpp
//
// Estimated module footprint (estimateModuleSize, used before a component exists), placement
// resolution (resolvePlacement and friends), hosted-plugin/module drop creation, and drop-landing
// animation — the drag-and-drop helpers that stay on GraphEditor (not
// drag-exclusive, or SafePointer<GraphEditor>-based and therefore tied to this Component's own
// identity). The drag-preview API's plain one-line forwarders onto
// GraphDragDropController (beginDragPreview/updateDragPreview/endDragPreview/
// isDragPreviewActive/getDragPreviewGhost/getAlignmentGuides/getDragPreviewSelfId/
// buildDragPreviewState) are gone — every call site now reaches it directly, either through
// GraphEditor::getDragDropController() (external callers) or the dragDropController_ member
// itself (GraphEditor's own other .cpp files). The DragAndDropTarget/FileDragAndDropTarget
// overrides below stay as forwarders — JUCE resolves a drop target by Component identity, so the
// actual override must stay a GraphEditor member even though everything it does is one call into
// the controller. GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp files in
// this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include <utility>

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CanvasAccessibilityClip.h"
#include "GraphEditorInternal.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroControlModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

// Sizes of the cards whose body is NOT drawn from layout data (the bespoke cards, the I/O nodes, a
// hosted plugin and the macro-port widgets), for estimateModuleSize below. Every other card is
// measured from its card-body plan. Each entry is pinned to the real card by
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents or the test its comment names.
static juce::Point<int> bespokeCardSizeTable(const juce::String& typeName) {
    if (typeName.containsIgnoreCase("Sequencer"))
        // Sequencer and Poly Sequencer: the step grid, plus one toggle row for Sync to Transport.
        return {synth::LayoutUtil::kDoubleWidth, 406};
    if (typeName.containsIgnoreCase("MidiKeyboard") || typeName.containsIgnoreCase("Midi Keyboard") ||
        typeName.containsIgnoreCase("MIDI Keyboard"))
        // The keys under a 24 px Octave stepper row; pinned to the real card (ModuleComponentMidiKeyboardCard.cpp).
        return {synth::LayoutUtil::kDoubleWidth, 160};
    if (typeName == "PolyMidi")
        return {280, 185}; // an alias with no factory entry, so not measurable; "Poly MIDI" is measured
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
    if (typeName == "Macros")
        // Height tracks the bank's "Knobs" count at runtime; the drop estimate uses the default.
        return {synth::LayoutUtil::kSingleWidth,
                synth::LayoutUtil::macroBankHeight(MacroControlModule::kDefaultMacros)};
    if (typeName == "Parametric EQ")
        // Double-width card: a 150px response curve set between the port-label gutters, then a
        // 4-column band grid (on/off + Freq/Gain/Q). Mirrors parametricEQHeight().
        return {synth::LayoutUtil::kDoubleWidth, 592};
    if (typeName == "External MIDI")
        return {280, 146};
    if (typeName == "Hosted Plugin")
        // Bypass and mute live in the header; a bare card's body is the Open Editor / Edit Layout...
        // button row, one jack a side while empty. The card grows with the loaded plugin's real port count,
        // like the Macro bank and Audio Input; the estimate is the resting size, and
        // finalizeNewDrop re-resolves against the real component anyway. Library-less until the
        // scan list and load UX ship. Measured against the real card by
        // HostedPluginTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        return {280, 135};
    if (typeName == "Macro In" || typeName == "Macro Out" || typeName == "Macro MIDI In" ||
        typeName == "Macro MIDI Out")
        // A docked port widget is one 16px sidebar row (two for Stereo, which grows through the ordinary component
        // re-layout) and takes the strip's width when docked, so this is a NOMINAL size: the same first-layout default
        // ModuleComponent::layoutMacroPortWidget uses, before the dock sets the real width. Library-less (the
        // "Configure I/O" modal places it).
        return {detail::kMacroCardStripWidth, detail::kMacroPortRowHeight};
    return {280, 360};
}

// A card whose body is drawn from layout data is measured from its card-body plan (the same plan and
// layout walk the real card uses, including which parameters become rotary knobs and so which CV
// jacks are knob-bound and draw no gutter row); only the bespoke cards read the table above.
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents builds a REAL ModuleComponent per
// library type and holds both paths to it.
juce::Point<int> GraphEditor::estimateModuleSize(const juce::String& typeName) {
    if (const auto measured = synth::measureDataDrivenCardSize(typeName))
        return *measured;
    return bespokeCardSizeTable(typeName);
}

// ---- DragAndDropTarget / FileDragAndDropTarget overrides ----------------------------------
// Bodies live on GraphDragDropController: JUCE resolves a drop target by Component
// identity, so the actual override must stay a GraphEditor member, but everything it does is one
// call into the controller.
bool GraphEditor::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    return dragDropController_.isInterestedInDragSource(dragSourceDetails);
}

void GraphEditor::itemDragEnter(const SourceDetails& dragSourceDetails) {
    dragDropController_.itemDragEnter(dragSourceDetails);
}

void GraphEditor::itemDragMove(const SourceDetails& dragSourceDetails) {
    dragDropController_.itemDragMove(dragSourceDetails);
}

void GraphEditor::itemDragExit(const SourceDetails& dragSourceDetails) {
    dragDropController_.itemDragExit(dragSourceDetails);
}

void GraphEditor::itemDropped(const SourceDetails& dragSourceDetails) {
    dragDropController_.itemDropped(dragSourceDetails);
}

// True for the module-library entries that must exist at most once per patch (Audio Input /
// Audio Output). A second one would sum into the same device buffer rather than address a
// different physical output, and the app's node lookups all take the first match.
bool GraphEditor::isSingletonIOModule(const juce::String& typeName) {
    return typeName == "Audio Input" || typeName == "Audio Output";
}

// True when the graph already contains a node whose processor reports this name.
bool GraphEditor::graphHasModuleNamed(juce::AudioProcessorGraph& graph, const juce::String& typeName) {
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == typeName)
            return true;
    return false;
}

// =============================================================================
// Audio-file drag and drop — dropping a sample on empty canvas builds a Sampler for it.
// A drop that lands on an existing Sampler is handled by ModuleComponent instead (JUCE hands the
// drop to the deepest interested target), which replaces that module's sample.
// =============================================================================

bool GraphEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    return dragDropController_.isInterestedInFileDrag(files);
}

void GraphEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    dragDropController_.filesDropped(files, x, y);
}

// Creates a Hosted Plugin node already pointed at `identity`. The actual load is asynchronous
// and resolves through the default backend's scan service, so a canvas with no service
// installed adds a placeholder rather than failing the add. See the note below for
// why this is a thin wrapper over addModuleAtCanvasPosition rather than a second add path.
//
// Deliberately a thin wrapper over addModuleAtCanvasPosition rather than a second add path: the
// identity is set through the same `configure` hook the Sampler's dropped file uses, so it is in
// place before the node joins the graph and is therefore inside the undo snapshot — undo/redo of
// a plugin add behaves exactly like undo/redo of any other module add, including remembering
// WHICH plugin on redo.
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

// Canvas coordinates of the middle of the current view — where a clicked (rather than dragged)
// library row lands.
juce::Point<int> GraphEditor::getViewportCentreInCanvasSpace() const {
    return getVisibleCanvasRect().getCentre().roundToInt();
}

// Creates `name` at a canvas position, snapped and anti-overlapped, with undo recorded.
// `configure` runs on the processor BEFORE it joins the graph, so any non-parameter state it
// sets is captured by the undo snapshot.
//
// A non-empty `joinMacroId` (a library drop with Cmd held over an expanded hull) also makes
// the new node a member of that macro. Node creation and membership must be ONE undo step, and a
// graph-only recordStructuralChange snapshot cannot carry a membership change, so that case is
// recorded through recordGraphAndMacroChange instead (same mutation lambda). The node gets its uuid
// right after addNode (membership is keyed by uuid, and a fresh node has none), and the join runs
// LAST inside the mutation, after finalizeNewDrop has placed the node and applied any smart
// connections: addSelectionToMacro computes its crossing-port plan from the then-current graph, so
// cables the drop just made are seen as boundary crossings and get their ports.
void GraphEditor::addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                            const std::function<void(juce::AudioProcessor&)>& configure,
                                            const juce::String& joinMacroId) {
    // Audio Input/Output are singletons. JUCE ties every audioOutputNode's channel count to the whole
    // graph and each one sums into the same device buffer, so a second instance would double the
    // signal rather than address another output — and every node lookup in the app (auto-connect,
    // PatchEval, auto-arrange) takes the first match and stops. Adding a duplicate is a no-op.
    if (isSingletonIOModule(name) && graphHasModuleNamed(audioEngine.getGraph(), name))
        return;

    auto newProcessor = synth::AIStateMapper::createModule(name);

    // The new card is about to join joinMacroId, so that hull is not an obstacle to placing it (resolvePlacement).
    juce::ScopedValueSetter<juce::String> joinScope(macroDragJoinId_,
                                                    joinMacroId.isNotEmpty() ? joinMacroId : macroDragJoinId_);

    if (newProcessor) {
        applyDefaultDualIOForNewModule(*newProcessor, name);
        if (configure)
            configure(*newProcessor);

        auto& graph = audioEngine.getGraph();
        // Use estimate for an initial snapped position; finalizeModuleDrag will re-resolve
        // using the real component size after updateComponents() creates the component.
        auto estSize = estimateModuleSize(name);
        // A card joining an open macro lands where it was dropped; the macro then makes room (below).
        const bool joining = joinMacroId.isNotEmpty();
        auto initialPlaced = joining
                                 ? synth::LayoutUtil::snap({juce::jmax(0, dropPos.x), juce::jmax(0, dropPos.y)})
                                 : resolvePlacement(dropPos, estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});

        // finalizeNewDrop: locate the newly created ModuleComponent, compute its
        // real final position (snapped + anti-overlapped using actual dimensions),
        // then animate it from the raw drop point to the settled position.
        auto finalizeNewDrop = [this, initialPlaced, joining](juce::AudioProcessorGraph::NodeID newNodeId) {
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
            auto toPos = joining ? newComp->getPosition()
                                 : resolvePlacement(newComp->getPosition(), newComp->getWidth(), newComp->getHeight(),
                                                    newComp->getNodeId());

            // Animate from the estimated initial-placed position to the real final position.
            // If they are identical, animateDropLanding is a no-op (just settles in place).
            animateDropLanding(newComp, initialPlaced, toPos);

            // The nearest free spot can be well away from the pointer (a crowded macro hull has none beside it), and
            // a card that landed below the window would look like the drop did nothing: bring it into view.
            const auto landed = juce::Rectangle<int>(toPos.x, toPos.y, newComp->getWidth(), newComp->getHeight());
            if (!getVisibleCanvasRect().contains(landed.getCentre().toFloat()))
                centreViewOn(landed.getCentre().toFloat());

            // Persist the final position immediately so it survives reload even if the
            // animation is still in-flight when the user saves.
            updateModulePosition(newComp);

            // Auto-wire any smart-connection previews the user saw while dragging. Already inside
            // a structural undo transaction when one is open, so do not nest another.
            smartConnections_.applySmartSuggestions(newNodeId, /*recordUndo=*/false);
        };

        // Shared by the undo and no-undo paths; the node is handed over through a shared_ptr so the
        // lambda stays copyable (std::function requires it).
        auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
        auto placeNode = [this, proc, initialPlaced, finalizeNewDrop, joinMacroId] {
            if (!*proc)
                return;
            auto node = audioEngine.getGraph().addNode(std::move(*proc));
            if (!node)
                return;
            node->properties.set("x", initialPlaced.x);
            node->properties.set("y", initialPlaced.y);
            const auto newNodeId = node->nodeID;
            const juce::String uuid =
                joinMacroId.isNotEmpty() ? synth::AIStateMapper::ensureNodeUuid(node.get()) : juce::String();
            updateComponents();
            finalizeNewDrop(newNodeId);
            if (uuid.isNotEmpty()) {
                macroController_.addSelectionToMacro(joinMacroId, {uuid}, /*recordUndo=*/false);
                // The card sits where it was dropped: members it overlaps are pushed aside (and the hull grows),
                // inside this same undo record, gliding like any other push.
                macroController_.makeRoomFor("n:" + juce::String(static_cast<juce::int64>(newNodeId.uid)));
            }
            reflowOutputDock(); // the drop may sit right of the dock: the dock moves, inside this undo step
        };

        if (undoManager && joinMacroId.isNotEmpty())
            undoManager->recordGraphAndMacroChange(graph, macros, placeNode);
        else if (undoManager)
            undoManager->recordStructuralChange(graph, placeNode);
        else
            placeNode();
    }
}

// Removes every connection leaving an output jack this node no longer shows. The other half of
// the max-channel/visible-port pattern (docs/modules/modules.md#audio-input): the module silences its hidden
// channels, and the owner unplugs them — a jack you cannot see is a jack you cannot unplug.
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

juce::Point<int> GraphEditor::resolvePlacement(juce::Point<int> desired, int w, int h,
                                               juce::AudioProcessorGraph::NodeID selfId) {
    // Layout units, not raw components (hidden members of collapsed macros are not obstacles; collapsed cards and
    // open hulls are). macroDragJoinId_ is the macro this placement is about to join: landing inside it is the point.
    const auto boxes = macroController_.placementBlockers(
        selfId.uid != 0 ? std::vector{selfId} : std::vector<juce::AudioProcessorGraph::NodeID>{}, macroDragJoinId_);
    auto snapped = synth::LayoutUtil::snap(desired);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, boxes, selfId);
}

// A free canvas slot at the LEFT edge, below every module currently on the canvas — where the
// timeline's add-track flow drops the "Track In" node it creates. Falls back to the canvas
// origin on an empty canvas. Anti-overlapped through resolvePlacement like any drop.
juce::Point<int> GraphEditor::findLeftEdgeSlotBelowModules(int w, int h) {
    // Left edge, below everything: a Track In node is the head of a chain the user reads
    // left-to-right, and stacking new ones downwards keeps successive tracks in track order rather
    // than scattered wherever a free slot happened to be.
    int left = synth::LayoutUtil::kArrangeOriginX;
    int bottom = synth::LayoutUtil::kArrangeOriginY;
    bool any = false;

    for (const auto& box : macroController_.placementBlockers({})) {
        const auto& bounds = box.rect;
        left = any ? std::min(left, bounds.getX()) : bounds.getX();
        bottom = any ? std::max(bottom, bounds.getBottom()) : bounds.getBottom();
        any = true;
    }

    // The head ends up boxed in a channel macro, whose open hull reaches kMacroHullSideOutset left of it: floor x so
    // that hull opens at x >= 0 rather than being nudged (or clipped) at the canvas edge.
    left = std::max(left, synth::LayoutUtil::kMacroHullSideOutset);

    const juce::Point<int> desired(left, any ? bottom + synth::LayoutUtil::kArrangeOriginY
                                             : synth::LayoutUtil::kArrangeOriginY);
    return resolvePlacement(desired, w, h, juce::AudioProcessorGraph::NodeID{});
}

// Compute the final snapped + anti-overlapped position for a newly dropped module.
// Equivalent to snap(dropPoint) + findFreeSlot.  Pure helper — does not touch GUI state.
// @param dropPoint   Desired top-left in canvas coordinates (will be snapped internally).
// @param w, h        Module footprint in pixels.
// @param existingBoxes  All already-placed module bounding boxes (selfId excluded from collision).
// @param selfId      NodeID of the module being placed (excluded from self-collision).
//
// static
juce::Point<int> GraphEditor::computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                       const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                       synth::LayoutUtil::NodeID selfId) {
    auto snapped = synth::LayoutUtil::snap(dropPoint);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, existingBoxes, selfId);
}

void GraphEditor::finalizeModuleDrag(ModuleComponent* module) {
    // Every exit path: a drop can both grow and shrink the canvas frame.
    const juce::ScopeGuard frameRefresh{[this] { refreshCanvasFrame(CanvasFrame::Mode::Animate); }};
    if (module == nullptr)
        return;
    if (isOutputDockNode(module->getNodeId())) {
        finalizeOutputDockDrag(module); // vertical only; x stays derived
        return;
    }
    slidePatchForEdgeDrop(); // a card held at the canvas origin: the rest of the patch makes room
    auto clear = resolvePlacement(module->getPosition(), module->getWidth(), module->getHeight(), module->getNodeId());
    module->setTopLeftPosition(clear);
    // Persist the snapped/cleared position to graph node properties so it survives reload.
    updateModulePosition(module);

    // A single-module drag inside an expanded macro (not a whole-macro group drag — that already
    // stays consistent via a uniform selection-drag delta, see dockMacroPortWidgets' own comment)
    // can move the hull this module contributes to without going through updateComponents(), so
    // its macro's port widgets would otherwise lag one drag behind. Re-derive now, the same as
    // every other layout pass.
    macroController_.dockMacroPortWidgets();

    // Apply proximity suggestions before the drag-preview teardown clears them. Group drags never
    // reach here with multi-select (finalizeSelectionDrag handles those). Connections join the
    // surrounding module-drag undo snapshot (captureBeforeState / pushSnapshotFromCapture).
    // shouldOfferSmartConnections/smartSuggestions moved onto SmartConnectionEngine —
    // same two conditions, read through the engine instead of GraphEditor's own former fields.
    if (smartConnections_.shouldOfferSmartConnections(dragDropController_.buildDragPreviewState()) &&
        smartConnections_.getSmartSuggestionCount() > 0)
        smartConnections_.applySmartSuggestions(module->getNodeId(), /*recordUndo=*/false);

    // A single-module drag can land the module fully outside the visible rect (drag it
    // under the bottom dock, or past the edge while zoomed in) without any pan/zoom of its own.
    detail::applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());

    reflowOutputDock(); // the released module may now be the rightmost thing on the canvas
    repaintCanvas();
}

// The membership half of a reparent drop, across nesting levels. The module leaves its current macro one
// level at a time (each step builds its own port plan against the graph as it then stands, see the
// TRANSFER ORDERING note below) until it sits directly in `joinId`, or in no macro when `joinId` is empty:
// so pulling a child's member out of both the child and its parent is one gesture. Stepping down into a
// macro nested below the module's owner needs no leave at all (the owner's boundary is unchanged): the
// module just moves to that descendant. `leaveId` is only a sanity gate: the walk starts from the
// module's real owner.
namespace {
void applyMacroMembershipChange(synth::MacroSet& macros, MacroGroupController& controller, const juce::String& uuid,
                                const juce::String& leaveId, const juce::String& joinId) {
    if (leaveId.isNotEmpty()) {
        for (const auto* owner = macros.findByMember(uuid); owner != nullptr && owner->id != joinId;
             owner = macros.findByMember(uuid)) {
            const auto chain = joinId.isNotEmpty() ? macros.ancestorChain(joinId) : std::vector<juce::String>();
            if (std::find(chain.begin(), chain.end(), owner->id) != chain.end()) {
                auto* live = macros.find(owner->id);
                live->members.erase(std::remove(live->members.begin(), live->members.end(), uuid), live->members.end());
                break;
            }
            controller.removeSelectionFromMacro(owner->id, {uuid}, /*recordUndo=*/false);
        }
    }
    if (joinId.isNotEmpty())
        controller.addSelectionToMacro(joinId, {uuid}, /*recordUndo=*/false);
}
} // namespace

// ---- Cmd-drag macro reparent (docs/macros/menu-and-membership.md) -----------------------------
//
// A Cmd-armed drag (or a plain single-module drag with the drag-without-Cmd preference) joins,
// leaves or transfers between expanded macros by crossing their hull borders.
// ModuleComponentInteraction.cpp's mouseDrag calls updateMacroDragCandidate on every tick while the
// drag is reparent-armed; mouseUp reads the last leave/join pair updateMacroDragCandidate left
// rather than re-querying geometry of its own, so what gets finalized is exactly what was
// highlighted. GraphEditorCables.cpp's paint() reads getMacroDragLeaveId()/getMacroDragJoinId()
// (declared inline in GraphEditor.h) to draw the emphasis.

void GraphEditor::updateMacroDragCandidate(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                           juce::Point<int> canvasCentre) {
    // Set unconditionally, BEFORE the candidate early-return below, so paintedMacroHullBounds
    // holds the dragged module's macro borders from the very first tick of the drag, not only once
    // the drag crosses into LEAVE-candidate territory.
    const bool nodeChanged = draggedNodeId != macroDragDraggedNodeId_;
    macroDragDraggedNodeId_ = draggedNodeId;
    // A drag with no press-time snapshot (the module was never pressed through ModuleComponent) freezes now.
    if (!macroController_.hasFrozenDragHulls())
        macroController_.freezeHullsForDrag(draggedNodeId);

    const auto targets = macroController_.macroDragJoinOrLeaveTarget(draggedNodeId, canvasCentre);
    if (targets.leave == macroDragLeaveId_ && targets.join == macroDragJoinId_ && !nodeChanged)
        return;
    if ((targets.leave.isNotEmpty() || targets.join.isNotEmpty()) &&
        canApplyMembershipLive(draggedNodeId, targets.leave)) {
        applyMembershipLive(draggedNodeId, targets.leave, targets.join);
        return;
    }
    const auto hullsBefore = snapshotPaintedHulls();
    macroDragLeaveId_ = targets.leave;
    macroDragJoinId_ = targets.join;
    glideHullsFrom(hullsBefore);
    repaintCanvas();
}

// A crossing is applied the moment it happens, so the cables are already re-routed through (or away from) the
// macro's ports when the mouse is released and nothing moves on the drop. Not for a group drag, and not when
// leaving would empty the macro: that dissolves it, and the module could not drag back in, so that one still
// waits for the drop.
bool GraphEditor::canApplyMembershipLive(juce::AudioProcessorGraph::NodeID draggedNodeId,
                                         const juce::String& leaveId) const {
    if (getSelectionCount() > 1)
        return false;
    if (leaveId.isEmpty())
        return true;
    const auto uuid = macroController_.nodeUuidFor(draggedNodeId);
    const auto* owner = macros.findByMember(uuid);
    if (owner == nullptr)
        return true;
    for (const auto& member : owner->members)
        if (member != uuid && !owner->memberIsPort(member))
            return true;
    return !macros.childrenOf(owner->id).empty();
}

// The macros as they stood at press are kept the first time, so the drop records the whole gesture as one undo
// step however many times it crossed. The borders are re-frozen for the new membership: the macro the module
// just joined now holds it in until its centre leaves that border, so a module on the edge does not flicker in
// and out.
void GraphEditor::applyMembershipLive(juce::AudioProcessorGraph::NodeID draggedNodeId, const juce::String& leaveId,
                                      const juce::String& joinId) {
    if (!liveMembershipChanged_) {
        macrosBeforeLiveDrag_ = macros.toVar();
        liveMembershipPressOwner_ = currentOwnerOfDraggedModule();
        liveMembershipChanged_ = true;
    }
    const auto hullsBefore = snapshotPaintedHulls();
    const auto cablesBefore = rebuildVisibleCables();
    macroController_.setMakeRoomDeferred(true);
    applyMacroMembershipChange(macros, macroController_, macroController_.nodeUuidFor(draggedNodeId), leaveId, joinId);
    macroController_.setMakeRoomDeferred(false);
    liveMembershipAway_ = currentOwnerOfDraggedModule() != liveMembershipPressOwner_;
    macroController_.clearFrozenDragHulls();
    macroController_.freezeHullsForDrag(draggedNodeId);
    macroDragLeaveId_.clear();
    macroDragJoinId_.clear();
    macroDragDraggedNodeId_ = draggedNodeId;
    armMacroCrossingAnimation(cablesBefore, draggedNodeId.uid, {});
    glideHullsFrom(hullsBefore);
    repaintCanvas();
}

bool GraphEditor::hasMacroDragCandidate() const {
    return macroDragLeaveId_.isNotEmpty() || macroDragJoinId_.isNotEmpty() || liveMembershipAway_;
}

juce::String GraphEditor::getMacroDragLiveOwnerId() const {
    return liveMembershipAway_ ? currentOwnerOfDraggedModule() : juce::String();
}

juce::String GraphEditor::currentOwnerOfDraggedModule() const {
    const auto* owner = macroController_.macroForNode(macroDragDraggedNodeId_);
    return owner != nullptr ? owner->id : juce::String();
}

// A drag that stops being a reparent drag (Cmd released with the drag-without-Cmd preference off) is a plain
// move again, so a crossing it already applied is put back. The undo record still happens on the drop: the
// round trip can mint fresh port nodes.
void GraphEditor::revertLiveMembership() {
    const auto draggedNodeId = macroDragDraggedNodeId_;
    const auto current = currentOwnerOfDraggedModule();
    if (!liveMembershipChanged_ || current == liveMembershipPressOwner_)
        return;
    const auto hullsBefore = snapshotPaintedHulls();
    const auto cablesBefore = rebuildVisibleCables();
    macroController_.setMakeRoomDeferred(true);
    applyMacroMembershipChange(macros, macroController_, macroController_.nodeUuidFor(draggedNodeId), current,
                               liveMembershipPressOwner_);
    macroController_.setMakeRoomDeferred(false);
    liveMembershipAway_ = false;
    macroController_.clearFrozenDragHulls();
    macroController_.freezeHullsForDrag(draggedNodeId);
    armMacroCrossingAnimation(cablesBefore, draggedNodeId.uid, {});
    glideHullsFrom(hullsBefore);
}

// A drag that changed membership live but ended without the drop finalize (cancelled mid-gesture) still records
// its change as one undo step.
void GraphEditor::recordUnfinishedLiveMembershipChange() {
    if (!liveMembershipChanged_)
        return;
    liveMembershipChanged_ = false;
    liveMembershipAway_ = false;
    const auto macrosBefore = std::exchange(macrosBeforeLiveDrag_, juce::var());
    if (undoManager != nullptr)
        undoManager->recordGraphAndMacroChange(
            audioEngine.getGraph(), macros,
            [this] {
                for (const auto& grower : macroController_.takeDeferredMakeRoom())
                    macroController_.makeRoomFor(grower);
            },
            undoManager->takeCapturedGraphBeforeState(), macrosBefore);
}

void GraphEditor::beginMacroDragFreeze(juce::AudioProcessorGraph::NodeID draggedNodeId) {
    macroController_.freezeHullsForDrag(draggedNodeId);
}

void GraphEditor::clearMacroDragCandidate(bool keepFrozenBorders) {
    if (keepFrozenBorders)
        revertLiveMembership();
    else
        recordUnfinishedLiveMembershipChange();
    const bool nodeWasSet = macroDragDraggedNodeId_ != juce::AudioProcessorGraph::NodeID{};
    const bool froze = !keepFrozenBorders && macroController_.hasFrozenDragHulls();
    const auto hullsBefore = snapshotPaintedHulls(); // a border held still at press glides to its live bounds
    if (!keepFrozenBorders)
        macroController_.clearFrozenDragHulls();
    if (macroDragLeaveId_.isEmpty() && macroDragJoinId_.isEmpty() && !nodeWasSet && !froze)
        return;
    macroDragLeaveId_.clear();
    macroDragJoinId_.clear();
    macroDragDraggedNodeId_ = {};
    glideHullsFrom(hullsBefore);
    repaintCanvas();
}

// The border drawn for macroId (and read by its chip, buttons and strips) once any glide has settled
// (paintedMacroHullBounds adds the glide). Normally the live
// hull. While a reparent drag moves one of macroId's OWN members (macroId is that member's macro
// or one of its ancestors) the live union would chase the dragged card, so instead: the border
// frozen at press while the drag is still staying inside (macroDragLeaveId_ empty), and the hull
// without that member once a LEAVE is armed, so the macro visibly lets go of it. A macro the drag
// might JOIN holds none of the dragged module, so it keeps its live bounds. Without a frozen
// snapshot (a drag that never went through a press) the excluding hull is painted from the first tick.
juce::Rectangle<int> GraphEditor::macroHullTargetBounds(const juce::String& macroId) const {
    if (macroDragDraggedNodeId_ != juce::AudioProcessorGraph::NodeID{}) {
        const auto* ownMacro = macroController_.macroForNode(macroDragDraggedNodeId_);
        const auto ancestors = ownMacro != nullptr ? macros.ancestorChain(ownMacro->id) : std::vector<juce::String>();
        if (ownMacro != nullptr &&
            (ownMacro->id == macroId || std::find(ancestors.begin(), ancestors.end(), macroId) != ancestors.end())) {
            if (macroDragLeaveId_.isEmpty())
                if (const auto frozen = macroController_.frozenDragHull(macroId); !frozen.isEmpty())
                    return frozen;
            const juce::String uuid = macroController_.nodeUuidFor(macroDragDraggedNodeId_);
            return macroController_.macroHullBoundsExcluding(macroId, uuid);
        }
    }
    return macroController_.macroHullBounds(macroId);
}

// The macro a library drop joins: the innermost expanded hull under the pointer (macroHullAt), with no modifier. A
// library drop has nothing to disambiguate (unlike moving an existing card out of or between macros), so neither Cmd
// nor the drag-without-Cmd preference is read.
juce::String GraphEditor::macroJoinTargetAt(juce::Point<int> canvasCentre) const {
    return macroController_.macroHullAt(canvasCentre);
}

// The live drop-target highlight of a LIBRARY drag reuses the join id a canvas reparent drag
// uses, so GraphEditorCables.cpp draws both the same way. The two gestures never overlap, and the
// leave id is left alone (a library drag has nothing to leave).
void GraphEditor::setMacroDropCandidate(const juce::String& macroId) {
    if (macroId == macroDragJoinId_)
        return;
    macroDragJoinId_ = macroId;
    repaintCanvas();
}

// The single-undo-step finalize (docs/macros/menu-and-membership.md): modeled on
// finalizeMacroCardDrag (GraphEditorSelection.cpp) — ONE lambda runs the ordinary position finalize
// AND the membership mutations, handed to ONE recordGraphAndMacroChange call, so Cmd+Z undoes the
// whole gesture (position + leave + join + any macro-port splicing addSelectionToMacro/
// removeSelectionFromMacro do along the way) together. A transfer (`leaveId` and `joinId` both
// set) is the leave followed by the join inside that same lambda.
//
// The graph "before" state is NOT recordGraphAndMacroChange's own default fresh capture — it is
// whatever ModuleComponent::mouseDown's captureBeforeState() stashed, taken back via
// takeCapturedGraphBeforeState(). This is load-bearing, not a style choice: ModuleComponent::
// moved() (fired on EVERY position change, including every live mouseDrag tick, not only at
// finalize) already wrote the dragged module's mid-drag position into its graph node's properties
// well before this method ever runs, so a fresh graphToJSON() taken now would only see that
// already-contaminated position as "before" and the combined undo would land the module back at
// wherever the live drag last released it, never at its true PRE-drag position. Consuming the
// mousedown-time capture instead is what makes "one undo restores both position and membership"
// actually mean the position from BEFORE the whole gesture started, matching the plain
// captureBeforeState()/pushSnapshotFromCapture() pair every ordinary body-drag already relies on
// for exactly the same reason (see ModuleComponent::mouseUp's plain-finalize branch).
//
// Ordering inside the lambda still matters, independent of the point above: `finalizeModuleDrag`
// runs BEFORE the membership mutations because addSelectionToMacro/removeSelectionFromMacro's
// updateComponents() call can add or remove macro PORT nodes for cables that just started/stopped
// crossing the boundary — never the dragged node itself (its own graph node persists through a
// membership-only + port-splicing change) — but finalizeModuleDrag still wants to resolve `module`
// while the canvas is in the most predictable state. This is also why ModuleComponent::mouseUp
// calls this method LAST and touches nothing on `this` afterwards: defensive practice for the day
// a future macroController_ change does end up tearing down the dragged component, even though
// today's addSelectionToMacro/removeSelectionFromMacro never do.
//
// TRANSFER ORDERING: the leave runs first, then the join, and neither precomputes a
// port-crossing plan up front. Each call builds its own plan at call time from the graph as it is
// THEN: macro A's plan is computed off the pre-remove graph (cables from the node to A's members
// become A's boundary ports), and macro B's plan off the post-remove graph, in which the splice A's
// leave just did has already rewired those cables. Planning both against the pre-gesture graph
// instead would hand B a plan describing cables that A's splice has since replaced with port nodes,
// yielding orphan or duplicate ports. Ids stay valid across the steps because macro ids are stable;
// removeSelectionFromMacro can dissolve A outright (the node was its last ordinary member) and the
// join still works because it only ever looks B up by id, lazily, inside its own call.
void GraphEditor::finalizeMacroMembershipDrag(ModuleComponent* module, const juce::String& leaveId,
                                              const juce::String& joinId) {
    // Every exit path: a drop can both grow and shrink the canvas frame.
    const juce::ScopeGuard frameRefresh{[this] { refreshCanvasFrame(CanvasFrame::Mode::Animate); }};
    if (module == nullptr)
        return;

    const juce::String uuid = macroController_.nodeUuidFor(module->getNodeId());
    auto& graph = audioEngine.getGraph();
    const juce::var graphBeforeOverride = undoManager ? undoManager->takeCapturedGraphBeforeState() : juce::var();
    // A drag that already changed membership as it crossed records against the macros as they were at press.
    const juce::var macrosBeforeOverride = liveMembershipChanged_ ? macrosBeforeLiveDrag_ : juce::var();
    const bool movedLive = liveMembershipAway_;
    liveMembershipChanged_ = false;
    liveMembershipAway_ = false;
    macrosBeforeLiveDrag_ = juce::var();

    // Snapshot the pre-mutation cable geometry and the dragged module's own bounds now,
    // while `module` is still known-good — doFinalize's macroController_ calls are free to add or
    // remove macro-port components (see this method's own header comment above), so both are
    // captured up front rather than read off `module` once the splice has already run. A plain
    // drag (no leave/join candidate) skips the snapshot entirely — nothing to diff, nothing to
    // animate, and MacroCrossingAnimator::arm() is never even called. A crossing applied mid-drag already slid
    // its cables; the drop only flashes the module where it lands.
    const bool crossedHull = leaveId.isNotEmpty() || joinId.isNotEmpty() || movedLive;
    const auto crossingNodeUid = module->getNodeId().uid;
    const auto crossingFlashBounds = crossedHull ? module->getBounds() : juce::Rectangle<int>();
    const auto cablesBeforeSplice = crossedHull ? rebuildVisibleCables() : std::vector<VisibleCable>();

    auto doFinalize = [this, module, leaveId, joinId, uuid, movedLive] {
        finalizeModuleDrag(module);
        applyMacroMembershipChange(macros, macroController_, uuid, leaveId, joinId);
        // The joined border grew around the card where it was dragged; it settles here, so its neighbours make
        // room for where it really is.
        if (movedLive) {
            for (const auto& grower : macroController_.takeDeferredMakeRoom())
                macroController_.makeRoomFor(grower);
            if (const auto* owner = macros.findByMember(uuid))
                macroController_.makeRoomFor("m:" + owner->id);
        }
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doFinalize, graphBeforeOverride, macrosBeforeOverride);
    else
        doFinalize();

    // A slide a mid-drag crossing started is left to finish; arming again would cut it short.
    if (crossedHull && !(macroCrossingAnim_.isLive() && leaveId.isEmpty() && joinId.isEmpty()))
        armMacroCrossingAnimation(cablesBeforeSplice, crossingNodeUid, crossingFlashBounds);

    clearMacroDragCandidate();
    // Torn down HERE, not by the mouseUp call site, because doFinalize (still running above) calls
    // finalizeModuleDrag, which reads buildDragPreviewState() for smart-connection suggestions — the
    // preview has to stay live for that and only end once this method is otherwise done with it.
    dragDropController_.endDragPreview();
    repaintCanvas();
}

// Internal: start the drop-landing animation for a newly placed module component.
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

// The cable-slide + module-flash counterpart to animateDropLanding above, fired once per
// finalizeMacroMembershipDrag call that actually crossed a hull. `cablesBeforeSplice` is the
// caller's pre-mutation snapshot; the post-mutation geometry is read fresh here, after the splice
// has already landed. MacroCrossingAnimator::arm() does the actual before/after diff (see its own
// header for the matching rule) — this method only owns handing the result to
// macroCrossingDriverAnim_, exactly like every other AnimationDriver use in this file.
void GraphEditor::armMacroCrossingAnimation(const std::vector<VisibleCable>& cablesBeforeSplice,
                                            uint32_t crossingNodeUid, juce::Rectangle<int> flashBounds) {
    if (macroCrossingAnim_.arm(cablesBeforeSplice, rebuildVisibleCables(), crossingNodeUid, flashBounds))
        startMacroCrossingDriver();
}

// A cable dropped across a macro boundary has no earlier self to slide from, so each cable it created that
// ends on a minted port emerges from the drop point instead (MacroCrossingAnimator::armSlideFrom).
void GraphEditor::armMacroPortSlide(const std::vector<VisibleCable>& cablesBeforeDrop, juce::Point<float> dropPoint) {
    const auto isPort = [this](uint32_t uid) {
        return macroController_.nodeIsMacroPort(juce::AudioProcessorGraph::NodeID{uid});
    };
    if (macroCrossingAnim_.armSlideFrom(cablesBeforeDrop, rebuildVisibleCables(), dropPoint, isPort)) {
        cablesCacheValid = false; // the memo was built before the slide existed
        startMacroCrossingDriver();
    }
}

// The one place macroCrossingDriverAnim_ is started, whichever gesture armed macroCrossingAnim_.
void GraphEditor::startMacroCrossingDriver() {
    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    macroCrossingDriverAnim_.start(
        vblankUpdater,
        220.0, // ms — short slide/flash, same order of magnitude as animateDropLanding's 180ms
        synth::ui::easeOutCubic,
        [safeEditor](float t) {
            if (safeEditor == nullptr)
                return;
            safeEditor->macroCrossingAnim_.applyTweenAt(t);
            safeEditor->repaintCanvas();
        },
        [safeEditor]() {
            if (safeEditor == nullptr)
                return;
            safeEditor->macroCrossingAnim_.finish();
            safeEditor->repaintCanvas();
        });
}
