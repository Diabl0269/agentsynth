// GraphDragDropController.h
//
// Owns the drag-preview state (grid + landing ghost + alignment guides shown while a module is
// being placed) and the JUCE DragAndDropTarget/FileDragAndDropTarget bodies that drive it.
// Reaches its owning canvas only through GraphCanvasHost — see that header for the narrow seam
// this depends on. GraphEditor holds one instance (`dragDropController_`) and forwards its own
// (unchanged) public drag-and-drop API to it, including the actual juce::DragAndDropTarget /
// juce::FileDragAndDropTarget overrides themselves, which stay one-line forwarders on GraphEditor
// because JUCE resolves drop targets by Component identity, not by an arbitrary owned object.
//
// Static helpers used widely outside a live drag (estimateModuleSize, isSingletonIOModule,
// graphHasModuleNamed, resolvePlacement, findLeftEdgeSlotBelowModules, dropRoutingsOnHiddenJacks)
// are not drag-exclusive and STAY on GraphEditor (FRO77 PR3 design) — reached here, when needed,
// through GraphCanvasHost rather than duplicated.
//
// Self-contained header: never includes GraphEditor.h (GraphEditor.h includes THIS header to
// declare its `dragDropController_` member, so the reverse would cycle).
#pragma once

#include "UI/Graph/GraphCanvasHost.h"
#include "UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class GraphDragDropController {
public:
    explicit GraphDragDropController(GraphCanvasHost& host)
        : host_(host) {}

    // ---- Drag-preview (grid + landing ghost shown during a module drag) -------------------
    void beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId);
    void updateDragPreview(juce::Point<int> desiredTopLeftCanvas);
    void endDragPreview();

    bool isDragPreviewActive() const noexcept { return dragPreviewActive_; }
    juce::Rectangle<int> getDragPreviewGhost() const noexcept { return dragPreviewGhost_; }
    juce::AudioProcessorGraph::NodeID getDragPreviewSelfId() const noexcept { return dragPreviewSelfId_; }

    /** Ghost top-left for a library drag's cursor position: the ghost is CENTRED on the cursor.
     *
     *  That is what every other drag-and-drop surface does, and it is what makes "aim at the gap"
     *  mean what it looks like. Anchoring the ghost by its top-left put the card a full width to the
     *  RIGHT of the cursor, so a suggestion could only be earned by aiming roughly one card-width
     *  LEFT of the destination — nobody does that, and it read as "this module doesn't support
     *  insert". A canvas MOVE deliberately keeps its grab-point anchoring: that card is already
     *  under the user's finger and re-anchoring it mid-drag would make it jump.
     *
     *  Every library path (enter, move, drop) resolves through this, or the drop lands somewhere the
     *  preview never showed. */
    juce::Point<int> ghostTopLeftForCursor(juce::Point<int> cursorCanvasPos) const {
        return cursorCanvasPos - juce::Point<int>(dragPreviewW_ / 2, dragPreviewH_ / 2);
    }

    /** The current drag-preview fields, packaged for SmartConnectionEngine (see
     *  SmartConnectionEngine::DragPreviewState). FRO254: GraphEditor's own former
     *  buildDragPreviewState() forwarder is gone — GraphEditorSmartConnections.cpp and
     *  GraphEditorDragDrop.cpp now call this directly via the dragDropController_ member. */
    SmartConnectionEngine::DragPreviewState buildDragPreviewState() const;

    // ---- Alignment guides (UI Phase 7 - Item 4) -------------------------------------------
    // During drag previews, store guide positions for visual feedback. The enable/disable
    // preference flag stays on GraphEditor (paint-only read, never consulted by updateDragPreview
    // itself) — only the computed guide list moves here with the drag-preview state it derives
    // from.
    struct AlignmentGuide {
        juce::Point<float> start; // line start point (canvas coords)
        juce::Point<float> end;   // line end point (canvas coords)
        int type;                 // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
    };
    const std::vector<AlignmentGuide>& getAlignmentGuides() const noexcept { return alignmentGuides_; }

    // ---- DragAndDropTarget / FileDragAndDropTarget bodies (GraphEditor forwards the actual
    // overrides here; see this header's top comment for why the overrides themselves stay put) ----
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);

    bool isInterestedInFileDrag(const juce::StringArray& files);
    void filesDropped(const juce::StringArray& files, int x, int y);

private:
    GraphCanvasHost& host_;

    // Drag-preview state (grid + landing ghost)
    bool dragPreviewActive_ = false;
    int dragPreviewW_ = 0, dragPreviewH_ = 0;
    juce::AudioProcessorGraph::NodeID dragPreviewSelfId_{};
    juce::Rectangle<int> dragPreviewGhost_;
    // The un-de-overlapped rect under the cursor. Suggestion candidacy is judged from this, so
    // aiming at a gap narrower than the card still counts; the card still LANDS at dragPreviewGhost_.
    juce::Rectangle<int> dragPreviewAim_;
    // Library-drag probe: jack metadata for a module that does not exist on the canvas yet.
    bool dragPreviewIsSnippet_ = false;
    std::unique_ptr<juce::AudioProcessor> dragPreviewProbe_;

    std::vector<AlignmentGuide> alignmentGuides_;
};
