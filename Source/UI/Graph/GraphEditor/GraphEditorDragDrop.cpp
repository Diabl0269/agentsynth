// GraphEditorDragDrop.cpp
//
// Estimated module footprint (estimateModuleSize, used before a component exists), placement
// resolution (resolvePlacement and friends), hosted-plugin/module drop creation, and drop-landing
// animation — the drag-and-drop helpers that stay on GraphEditor (FRO77 PR3 design: not
// drag-exclusive, or SafePointer<GraphEditor>-based and therefore tied to this Component's own
// identity). FRO254: the drag-preview API's plain one-line forwarders onto
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

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CanvasAccessibilityClip.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroControlModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent.h"

// Returns an estimated (w, h) footprint for a module type name.
// Used when the component does not yet exist (e.g. on drag-drop before layout).
// Heights match the real component sizes so the library-drag ghost preview is accurate.
// The final drop placement uses the real component size via finalizeModuleDrag() — the
// estimate is only used for the live ghost preview.
// Heights are measured from the real components, not guessed — see
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents, which constructs every type and
// fails if this table drifts from what layoutDefaultContent() actually produces.
// Estimated (w, h) footprint for a module type name, used for the library drag ghost before a
// real component exists. Public so a test can hold it to the real component sizes — see
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents.
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
        // FRO281: +40 for the Rate/Level/Glide CV jacks (3 input jacks, one row shared per pair
        // with the mono CV output already there — see ModuleComponentTest.
        // EstimatedModuleSizesMatchTheRealComponents, which pins this to the real component).
        return {280, 401}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "VCA")
        return {280, 253}; // +20 in #219: the Audio L/R input pair adds a jack row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "ADSR" || typeName == "Amp Env" || typeName == "Filter Env")
        // FRO112: five rotary knobs (attack/hold/decay/sustain/release) now flow through the
        // generic 3-per-row knob grid (3+2, same shape as every other module), the three curve
        // params moved onto the collapsed-by-default envelope graph's bend handles, and a
        // collapsed graph adds one row (its disclosure toggle + BPM|MS). Below 2 jacks + Poly
        // toggle + threshold control + knob grid (2 rows) + disclosure row, collapsed.
        // FRO281: +100 for the five Attack/Hold/Decay/Sustain/Release CV jacks appended after
        // Threshold (see ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents).
        return {280, 489};
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
        return {280, 297}; // Dual I/O off: one Audio jack (not L/R) + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
                           // +60: Time/Feedback/Mix CV jacks finally exist (4 port rows)
    if (typeName == "Reverb")
        return {280, 337}; // Dual I/O off: one Audio jack (not L/R) + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
                           // +100: Size/Damping/Wet/Dry/Width CV jacks finally exist (6 port rows)
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
        // +60: every continuous parameter has a CV jack now (Audio + 5 CV = 6 port rows), and
        // the port gutter, not the 2-row knob grid, sets the height.
        return {280, 337};
    if (typeName == "Bitcrusher")
        return {280, 323}; // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
    if (typeName == "Pitch Shifter")
        return {280, 507}; // Dual I/O off: one Audio jack + Level knob row
                           // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38)
                           // +40: Fine and Window CV jacks (Audio + 6 CV = 7 port rows)
    if (typeName == "Parametric EQ")
        // Double-width card: a 150px response curve set between the port-label gutters, then a
        // 4-column band grid (on/off + Freq/Gain/Q). Mirrors parametricEQHeight().
        return {synth::LayoutUtil::kDoubleWidth, 592};
    if (typeName == "Compressor")
        return {280, 337}; // +100: a CV jack per parameter (Audio + 5 CV = 6 port rows) sets it
    if (typeName == "Limiter")
        return {280, 221}; // +60: a CV jack per parameter (Audio + 3 CV = 4 port rows) sets it
    if (typeName == "Gate")
        // 6 float sliders (Threshold/Attack/Hold/Release/Range/Level): same row count as
        // Compressor's 5 (3+3 wraps to the same number of rows as 6 in a 3-per-row grid).
        // +100: a CV jack per parameter (Audio + 5 CV = 6 port rows) sets it, as for Compressor.
        return {280, 337};
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
    if (typeName == "Channel Strip")
        // gain + pan + the four send levels (FRO15: sendNLevel exists unconditionally, so the card
        // always shows all four — +76 over the pre-send 181) below up to 2 input jacks a side
        // (Stereo shape, what MainComponent::addAudioTrack always builds — width doesn't move with
        // shape, only the Mono/Stereo jack-row count would, and this card's height already covers
        // both). The send OUTPUT jacks appear only for active slots and sit in the right-hand
        // gutter, which the control rows already outgrow. Internal-only like Track Audio/Rec Tap:
        // library-less, no replace-menu entry. Measured against the real card by
        // ChannelFlowTest.ChannelStripAndMasterHaveAPinnedSizeEstimate.
        return {280, 257};
    if (typeName == "Master")
        // gain slider only, 4 input jacks (Mix L/R, Direct L/R) a side setting the port gutter.
        // Internal-only, singleton, library-less. Measured against the real card by
        // ChannelFlowTest.ChannelStripAndMasterHaveAPinnedSizeEstimate.
        return {280, 221};
    if (typeName == "Hosted Plugin")
        // Bypass and mute live in the header; a bare card's body is the Open Editor / Choose knobs
        // button row, one jack a side while empty. The card grows with the loaded plugin's real port count,
        // like the Macro bank and Audio Input; the estimate is the resting size, and
        // finalizeNewDrop re-resolves against the real component anyway. Library-less until the
        // scan list and load UX ship. Measured against the real card by
        // HostedPluginTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        // +4: FRO128 gave the button row a 6px gap below it (was 2).
        return {280, 135};
    if (typeName == "Macro In" || typeName == "Macro Out")
        // Founder-review fix F2 (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed /
        // docs/macros/configure-io.md#adding-a-port): no longer a full module card — a small docked widget
        // (ModuleComponent::layoutMacroPortWidget), constructed Mono by default (one jack row) — the port-creation flow
        // grows it to two rows for Stereo via the ordinary component re-layout, same as any other jack-count change.
        // Library-less (the "Configure I/O" modal places it). Measured against the real card by
        // MacroPortFlow.AllFourTypesAreAbsentFromTheLibraryWithAPinnedSizeEstimate.
        return {ModuleComponent::kMacroPortWidgetWidth,
                ModuleComponent::kMacroPortWidgetHeaderY + ModuleComponent::kMacroPortWidgetBottomPad};
    if (typeName == "Macro MIDI In" || typeName == "Macro MIDI Out")
        // One MIDI jack row, no audio jacks and no body controls — same compact widget, always
        // one row (MIDI has no Mono/Stereo/Poly-N shape to grow). Measured against the real card
        // by MacroPortFlow.AllFourTypesAreAbsentFromTheLibraryWithAPinnedSizeEstimate.
        return {ModuleComponent::kMacroPortWidgetWidth,
                ModuleComponent::kMacroPortWidgetHeaderY + ModuleComponent::kMacroPortWidgetBottomPad};
    return {280, 360};
}

// ---- DragAndDropTarget / FileDragAndDropTarget overrides ----------------------------------
// Bodies live on GraphDragDropController (FRO77 PR3): JUCE resolves a drop target by Component
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
// A non-empty `joinMacroId` (a library drop with Cmd held over an expanded hull, FRO168) also makes
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
            if (uuid.isNotEmpty())
                macroController_.addSelectionToMacro(joinMacroId, {uuid}, /*recordUndo=*/false);
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
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        boxes.push_back({comp->getNodeId(), comp->getBounds()});
    }
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
    if (module == nullptr)
        return;
    auto clear = resolvePlacement(module->getPosition(), module->getWidth(), module->getHeight(), module->getNodeId());
    module->setTopLeftPosition(clear);
    // Persist the snapped/cleared position to graph node properties so it survives reload.
    updateModulePosition(module);

    // A single-module drag inside an expanded macro (not a whole-macro group drag — that already
    // stays consistent via a uniform selection-drag delta, see dockMacroPortWidgets' own comment)
    // can move the hull this module contributes to without going through updateComponents(), so
    // its macro's port widgets would otherwise lag one drag behind. Re-derive now, the same as
    // every other layout pass (P8-15 fix F2).
    macroController_.dockMacroPortWidgets();

    // Apply proximity suggestions before the drag-preview teardown clears them. Group drags never
    // reach here with multi-select (finalizeSelectionDrag handles those). Connections join the
    // surrounding module-drag undo snapshot (captureBeforeState / pushSnapshotFromCapture).
    // FRO77 PR1: shouldOfferSmartConnections/smartSuggestions moved onto SmartConnectionEngine —
    // same two conditions, read through the engine instead of GraphEditor's own former fields.
    if (smartConnections_.shouldOfferSmartConnections(dragDropController_.buildDragPreviewState()) &&
        smartConnections_.getSmartSuggestionCount() > 0)
        smartConnections_.applySmartSuggestions(module->getNodeId(), /*recordUndo=*/false);

    // FRO300: a single-module drag can land the module fully outside the visible rect (drag it
    // under the bottom dock, or past the edge while zoomed in) without any pan/zoom of its own.
    detail::applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());

    repaintCanvas();
}

// ---- Cmd-drag macro reparent (FRO40, FRO168, docs/macros/menu-and-membership.md) --------------
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
    // already excludes the dragged module from its own macro's hull from the very first tick of the
    // drag — not only once the drag actually crosses into LEAVE-candidate territory. That first
    // stretch (still inside the excluding hull, no candidate yet) is exactly the phase where a live
    // union would otherwise keep inflating around the module being pulled out.
    const bool nodeChanged = draggedNodeId != macroDragDraggedNodeId_;
    macroDragDraggedNodeId_ = draggedNodeId;

    const auto targets = macroController_.macroDragJoinOrLeaveTarget(draggedNodeId, canvasCentre);
    if (targets.leave == macroDragLeaveId_ && targets.join == macroDragJoinId_ && !nodeChanged)
        return;
    macroDragLeaveId_ = targets.leave;
    macroDragJoinId_ = targets.join;
    repaintCanvas();
}

void GraphEditor::clearMacroDragCandidate() {
    const bool nodeWasSet = macroDragDraggedNodeId_ != juce::AudioProcessorGraph::NodeID{};
    if (macroDragLeaveId_.isEmpty() && macroDragJoinId_.isEmpty() && !nodeWasSet)
        return;
    macroDragLeaveId_.clear();
    macroDragJoinId_.clear();
    macroDragDraggedNodeId_ = {};
    repaintCanvas();
}

// macroHullBounds(macroId), except while a reparent drag is pulling one of macroId's OWN members
// out: then it's macroHullBoundsExcluding that member, so the hull visibly shrinks away from the
// module instead of the live union chasing it. Paint-only. Only the macro the dragged module is
// CURRENTLY a member of (the one a LEAVE would remove it from) gets the excluding hull. A macro it
// might JOIN is by construction not its current macro (a transfer joins a DIFFERENT one, per
// macroDragJoinOrLeaveTarget), so the dragged module contributes nothing to that hull and it keeps
// its live bounds.
juce::Rectangle<int> GraphEditor::paintedMacroHullBounds(const juce::String& macroId) const {
    if (macroDragDraggedNodeId_ != juce::AudioProcessorGraph::NodeID{}) {
        const auto* ownMacro = macroController_.macroForNode(macroDragDraggedNodeId_);
        if (ownMacro != nullptr && ownMacro->id == macroId) {
            const juce::String uuid = macroController_.nodeUuidFor(macroDragDraggedNodeId_);
            return macroController_.macroHullBoundsExcluding(macroId, uuid);
        }
    }
    return macroController_.macroHullBounds(macroId);
}

// Whether a library drop should join a macro: Cmd held (the same modifier a canvas module drag
// uses), or the drag-without-Cmd preference on. THE one place the drop-side modifier is read, so a
// platform where JUCE's realtime modifier state is not live during a drag-and-drop has exactly one
// function to change; tests drive the Cmd half through setMacroJoinCommandOverrideForTests.
bool GraphEditor::isMacroJoinModifierDown() const {
    const bool cmdDown = macroJoinCommandOverride_.has_value()
                             ? *macroJoinCommandOverride_
                             : juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
    return cmdDown || macroDragWithoutCmdEnabled;
}

juce::String GraphEditor::macroJoinTargetAt(juce::Point<int> canvasCentre) const {
    if (!isMacroJoinModifierDown())
        return {};
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
// TRANSFER ORDERING (FRO168): the leave runs first, then the join, and neither precomputes a
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
    if (module == nullptr)
        return;

    const juce::String uuid = macroController_.nodeUuidFor(module->getNodeId());
    auto& graph = audioEngine.getGraph();
    const juce::var graphBeforeOverride = undoManager ? undoManager->takeCapturedGraphBeforeState() : juce::var();

    auto doFinalize = [this, module, leaveId, joinId, uuid] {
        finalizeModuleDrag(module);
        if (leaveId.isNotEmpty())
            macroController_.removeSelectionFromMacro(leaveId, {uuid}, /*recordUndo=*/false);
        if (joinId.isNotEmpty())
            macroController_.addSelectionToMacro(joinId, {uuid}, /*recordUndo=*/false);
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doFinalize, graphBeforeOverride);
    else
        doFinalize();

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
