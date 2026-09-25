// GraphDragDropController.cpp
//
// The drag-preview API (begin/update/endDragPreview) and the DragAndDropTarget/
// FileDragAndDropTarget bodies GraphEditor forwards into it (FRO77 PR3). GraphDragDropController
// is declared in GraphDragDropController.h.

#include "GraphDragDropController.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/SamplerModule.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "SnippetManager.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/SelectionModel.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <limits>

void GraphDragDropController::beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId) {
    dragPreviewActive_ = true;
    dragPreviewW_ = w;
    dragPreviewH_ = h;
    dragPreviewSelfId_ = selfId;
    dragPreviewGhost_ = {};
    alignmentGuides_.clear();
    host_.clearSmartSuggestions();
    // Seed the tick's comparison from the state at press time, so a drag started WITH the modifier
    // already held is not reported as a change on its very first tick.
    host_.seedInsertModifierSample();
    // Body-drag of an existing module: clear any leftover library-drop probe.
    if (selfId.uid != 0) {
        dragPreviewIsSnippet_ = false;
        dragPreviewProbe_.reset();
    }
    host_.repaintCanvas();
}

void GraphDragDropController::updateDragPreview(juce::Point<int> desiredTopLeftCanvas) {
    if (!dragPreviewActive_)
        return;
    // Where the user is POINTING, before anti-overlap moves the card. A ghost aimed at the gap
    // between two wired cards necessarily overlaps them — it is wider than the gap — so
    // resolvePlacement throws it clear, and judging a suggestion only by that landing spot means
    // aiming at the gap can never earn one. Candidacy is judged from the aim; the landing spot is
    // still what gets drawn and where the card ends up (see refreshSmartSuggestions).
    dragPreviewAim_ =
        juce::Rectangle<int>(desiredTopLeftCanvas.x, desiredTopLeftCanvas.y, dragPreviewW_, dragPreviewH_);

    auto resolved = host_.resolvePlacement(desiredTopLeftCanvas, dragPreviewW_, dragPreviewH_, dragPreviewSelfId_);
    dragPreviewGhost_ = juce::Rectangle<int>(resolved.x, resolved.y, dragPreviewW_, dragPreviewH_);

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // Scan existing modules and compute alignment guides for closest edges
    alignmentGuides_.clear();
    if (dragPreviewGhost_.isEmpty()) {
        host_.refreshSmartSuggestions();
        return;
    }

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&host_.lookAndFeel());
    const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
    const float snapThreshold = static_cast<float>(m.gridSize); // kGridSize

    // Build list of existing module boxes (excluding self)
    std::vector<synth::LayoutUtil::Box> existingBoxes;
    for (auto* comp : host_.modules()) {
        if (comp->getNodeId() == dragPreviewSelfId_)
            continue; // Skip self
        juce::Rectangle<int> rect = comp->getBounds();
        existingBoxes.push_back({comp->getNodeId(), rect});
    }

    if (existingBoxes.empty()) {
        host_.refreshSmartSuggestions();
        host_.repaintCanvas(); // Still need to clear any old guides
        return;
    }

    auto ghostRect = dragPreviewGhost_.toFloat();
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
    alignmentGuides_.clear();
    for (const auto& c : bestCandidates) {
        alignmentGuides_.push_back({c.start, c.end, c.type});
    }

    host_.refreshSmartSuggestions();
    host_.repaintCanvas();
}

void GraphDragDropController::endDragPreview() {
    dragPreviewActive_ = false;
    dragPreviewGhost_ = {};
    alignmentGuides_.clear();
    host_.clearSmartSuggestions();
    dragPreviewIsSnippet_ = false;
    dragPreviewProbe_.reset();
    host_.repaintCanvas();
}

SmartConnectionEngine::DragPreviewState GraphDragDropController::buildDragPreviewState() const {
    SmartConnectionEngine::DragPreviewState state;
    state.active = dragPreviewActive_;
    state.ghost = dragPreviewGhost_;
    state.aim = dragPreviewAim_;
    state.selfId = dragPreviewSelfId_;
    state.isSnippet = dragPreviewIsSnippet_;
    state.probe = dragPreviewProbe_.get();
    // shouldOfferSmartConnections' selection-drag guard: selection itself is not on
    // GraphCanvasHost for the selection MODEL directly, but getSelection()/isSelectionDragActive()
    // are — GraphEditor's own former precompute (selectionDragActive && selection.size() > 1)
    // moves here unchanged, just read through the host instead of GraphEditor's private fields.
    state.selectionDragBlocksSuggestions = host_.isSelectionDragActive() && host_.getSelection().size() > 1;
    return state;
}

bool GraphDragDropController::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) { return true; }

void GraphDragDropController::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    juce::String name = dragSourceDetails.description.toString();

    // A snippet covers a whole group, so size the ghost from the snippet's own bounding box
    // instead of the single-module estimate table.
    juce::Point<int> estSize;
    dragPreviewIsSnippet_ = synth::SnippetManager::isSnippetPayload(name);
    dragPreviewIsPlainModule_ = !dragPreviewIsSnippet_ && !synth::PluginIdentity::isDragPayload(name);
    dragPreviewProbe_.reset();
    if (dragPreviewIsSnippet_) {
        estSize = host_.estimateSnippetSize(name);
    } else if (synth::PluginIdentity::isDragPayload(name)) {
        // A hosted-plugin payload is not a factory module type: no smart-connection probe (the
        // plugin isn't loaded yet, so its jacks are unknowable); sized from the Hosted Plugin card.
        estSize = host_.estimateModuleSizeForType("Hosted Plugin");
    } else {
        dragPreviewProbe_ = synth::AIStateMapper::createModule(name);
        // The probe IS the ghost for smart-connect purposes: its jack layout decides the preview
        // AND the plan that gets applied on drop. It must therefore go through exactly the same
        // Dual I/O default the real module will get in itemDropped — otherwise a plan computed for
        // a collapsed ghost is applied to a module that spawned dual, and only the left legs get
        // wired (the ghost's fan resolves to one raw channel per jack instead of two).
        if (dragPreviewProbe_ != nullptr)
            host_.applyDefaultDualIOForNewModule(*dragPreviewProbe_, name);
        estSize = host_.estimateModuleSizeForType(name);
    }

    beginDragPreview(estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});
    // Centred on the cursor — see ghostTopLeftForCursor. beginDragPreview above has already set the
    // ghost size this depends on.
    auto canvasPos = host_.canvasPositionOfLocalPoint(dragSourceDetails.localPosition);
    updateDragPreview(ghostTopLeftForCursor(canvasPos));
}

// The hull is tested at the centre of the ghost BEFORE anti-overlap moves it, which is the cursor
// (ghostTopLeftForCursor centres the ghost on it), not at where the card finally lands. A macro's
// hull is the union of its members plus a margin, so it is mostly member cards: the free slot
// findFreeSlot picks for a new card is usually outside the hull, and testing the landing spot would
// make "drop it on the macro" fail exactly when the macro is tight. The card still lands at the
// relocated slot and, as a member, pulls the live hull out to include it.
juce::String
GraphDragDropController::macroJoinTargetForDrop(const juce::DragAndDropTarget::SourceDetails& details) const {
    return host_.macroJoinTargetAt(host_.canvasPositionOfLocalPoint(details.localPosition));
}

void GraphDragDropController::itemDragMove(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    auto canvasPos = host_.canvasPositionOfLocalPoint(dragSourceDetails.localPosition);
    updateDragPreview(ghostTopLeftForCursor(canvasPos));
    // Highlight the hull the drop would join, through the same emphasis a module reparent drag uses.
    host_.setMacroDropCandidate(dragPreviewIsPlainModule_ ? macroJoinTargetForDrop(dragSourceDetails) : juce::String());
}

void GraphDragDropController::itemDragExit(const juce::DragAndDropTarget::SourceDetails&) {
    host_.setMacroDropCandidate({});
    endDragPreview();
}

void GraphDragDropController::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    const juce::String name = dragSourceDetails.description.toString();
    // The drop lands exactly where the preview showed: the ghost rect is already snapped and
    // de-overlapped, so taking its position (rather than re-deriving one from the cursor) makes it
    // impossible for the two to disagree. Falls back to the centred cursor if there is no live ghost
    // (a drop with no preceding drag-move, which only happens in tests).
    auto dropPos = (dragPreviewActive_ && !dragPreviewGhost_.isEmpty())
                       ? dragPreviewGhost_.getPosition()
                       : ghostTopLeftForCursor(host_.canvasPositionOfLocalPoint(dragSourceDetails.localPosition));
    host_.setMacroDropCandidate({});

    // Snippet drop: resolve the payload to its JSON via the owner and insert the whole group.
    // Checked before the single-module path because both arrive on the same DragAndDrop channel,
    // distinguished only by the payload prefix.
    if (synth::SnippetManager::isSnippetPayload(name)) {
        endDragPreview();
        // host_.resolveSnippetPayload returns a default (non-object) juce::var when the owner set
        // no snippetProvider, so the isObject() check below covers both "no provider" and "provider
        // returned something unusable" in one branch — see the class doc for the access-rewrite.
        auto snippet = host_.resolveSnippetPayload(synth::SnippetManager::nameFromPayload(name));
        if (!snippet.isObject())
            return;
        host_.insertSnippetAt(snippet, dropPos);
        return;
    }

    // A scanned plugin — same channel again, told apart by its "plugin:" prefix.
    if (synth::PluginIdentity::isDragPayload(name)) {
        host_.addHostedPluginAtCanvasPosition(synth::PluginIdentity::fromDragPayload(name), dropPos);
        endDragPreview();
        return;
    }

    // Modules only: a snippet or plugin payload never reaches here. Cmd (or the drag-without-Cmd
    // preference) over an expanded hull makes the new module a member, in the same undo step as its
    // creation.
    const juce::String joinMacroId = macroJoinTargetForDrop(dragSourceDetails);
    host_.addModuleAtCanvasPosition(name, dropPos, {}, joinMacroId);
    endDragPreview();
}

// =============================================================================
// Audio-file drag and drop — dropping a sample on empty canvas builds a Sampler for it.
// A drop that lands on an existing Sampler is handled by ModuleComponent instead (JUCE hands the
// drop to the deepest interested target), which replaces that module's sample.
// =============================================================================

bool GraphDragDropController::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void GraphDragDropController::filesDropped(const juce::StringArray& files, int x, int y) {
    auto canvasPos = host_.canvasPositionOfLocalPoint(juce::Point<int>(x, y));

    for (const auto& path : files) {
        const juce::File file(path);
        if (!SamplerModule::isSupportedAudioFile(file))
            continue;

        // Load into the processor BEFORE it joins the graph: recordStructuralChange snapshots the
        // graph afterwards, and that snapshot is what undo/redo replays — so the file path has to be
        // in place by then or the sample is lost on the first Cmd+Z.
        host_.addModuleAtCanvasPosition("Sampler", canvasPos,
                                        [file](juce::AudioProcessor& processor) {
                                            if (auto* sampler = dynamic_cast<SamplerModule*>(&processor))
                                                sampler->loadSampleFile(file);
                                        },
                                        {});

        // Cascade multiple files so they do not all land on the same spot.
        canvasPos += juce::Point<int>(32, 32);
    }

    endDragPreview();
}
