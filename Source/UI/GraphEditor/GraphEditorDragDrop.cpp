// GraphEditorDragDrop.cpp
//
// Estimated module footprint (estimateModuleSize, used before a component exists), the
// drag-preview API, audio-file drag/drop (Sampler), hosted-plugin drop, module placement
// resolution (resolvePlacement and friends) and drop-landing animation. GraphEditor is declared
// in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "GraphEditor.h"

#include "../../AI/AIStateMapper.h"
#include "../../Modules/AttenuverterModule.h"
#include "../../Modules/AudioInputModule.h"
#include "../../Modules/MacroControlModule.h"
#include "../../Modules/SamplerModule.h"
#include "../../Plugin/Hosting/HostedPluginModule.h"
#include "../../SnippetManager.h"
#include "../ModuleComponent.h"
#include "../Theme/AppLookAndFeel.h"

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
    if (typeName == "Gate")
        // 6 float sliders (Threshold/Attack/Hold/Release/Range/Level): same row count as
        // Compressor's 5 (3+3 wraps to the same number of rows as 6 in a 3-per-row grid).
        return {280, 237};
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
        // gain + pan sliders below up to 2 input jacks a side (Stereo shape, what
        // MainComponent::addAudioTrack always builds — width doesn't move with shape, only the
        // Mono/Stereo jack-row count would, and this card's height already covers both). Internal-
        // only like Track Audio/Rec Tap: library-less, no replace-menu entry. Measured against the
        // real card by ChannelFlowTest.ChannelStripAndMasterHaveAPinnedSizeEstimate.
        return {280, 181};
    if (typeName == "Master")
        // gain slider only, 4 input jacks (Mix L/R, Direct L/R) a side setting the port gutter.
        // Internal-only, singleton, library-less. Measured against the real card by
        // ChannelFlowTest.ChannelStripAndMasterHaveAPinnedSizeEstimate.
        return {280, 221};
    if (typeName == "Hosted Plugin")
        // Bypass and mute live in the header; the only body content is the "Open Editor" button,
        // one jack a side while empty. The card grows with the loaded plugin's real port count,
        // like the Macro bank and Audio Input; the estimate is the resting size, and
        // finalizeNewDrop re-resolves against the real component anyway. Library-less until the
        // scan list and load UX ship. Measured against the real card by
        // HostedPluginTest.AbsentFromTheLibraryWithAPinnedSizeEstimate.
        // +8: header-to-first-port gap grew 1px -> 9px (base offset 30->38).
        return {280, 131};
    if (typeName == "Macro In" || typeName == "Macro Out")
        // Founder-review fix F2 (docs/macros.md §5.3/§7 item 3): no longer a full module card — a
        // small docked widget (ModuleComponent::layoutMacroPortWidget), constructed Mono by
        // default (one jack row) — the port-creation flow grows it to two rows for Stereo via the
        // ordinary component re-layout, same as any other jack-count change. Library-less (the
        // "Configure I/O" modal places it). Measured against the real card by
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

    // A single-module drag inside an expanded macro (not a whole-macro group drag — that already
    // stays consistent via a uniform selection-drag delta, see dockMacroPortWidgets' own comment)
    // can move the hull this module contributes to without going through updateComponents(), so
    // its macro's port widgets would otherwise lag one drag behind. Re-derive now, the same as
    // every other layout pass (P8-15 fix F2).
    dockMacroPortWidgets();

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
