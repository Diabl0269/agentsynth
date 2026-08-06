#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "LayoutUtil.h"
#include "SelectionModel.h"
#include "UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleComponent;
#include "ModMatrixComponent.h"

class GraphEditor
    : public juce::Component
    , public juce::Timer
    , public juce::DragAndDropTarget
    , public juce::SettableTooltipClient {
public:
    GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr = nullptr);
    ~GraphEditor() override;

    AudioEngine& getAudioEngine() { return audioEngine; }
    ModMatrixComponent& getModMatrix() { return modMatrix; }
    juce::OwnedArray<ModuleComponent>& getModuleComponents() { return content.getModules(); }
    void detachAllModuleComponents();

    void paint(juce::Graphics& g) override;
    /** Draws the empty-canvas onboarding hint centred in the visible viewport (untransformed
     *  GraphEditor local coordinates).  Called after all children have painted, so it draws
     *  on top of the inner canvas without being affected by the content transform. */
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void timerCallback() override;
    void updateComponents();
    void toggleModMatrixVisibility();
    bool isModMatrixVisible() const { return isMatrixVisible; }

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);
    void deleteModule(ModuleComponent* module);
    // Request deletion by NodeID (called from ModuleComponent's delete button).
    // Resolves the module component and delegates to the single removal path.
    void requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId);
    void replaceModule(ModuleComponent* module, const juce::String& newModuleType);
    void updateModulePosition(ModuleComponent* module);

    // Preset Management
    void savePreset(juce::File file);
    void loadPreset(juce::File file);
    // Loads a factory preset by index. Detaches existing module components (stopping their scope timers)
    // BEFORE the graph is cleared, so no ScopeComponent reads a freed VisualBuffer. Returns true if loaded.
    bool loadFactoryPreset(int index);

    // Clears the canvas to the empty state (New Patch). Detaches module components (stopping scope timers)
    // BEFORE clearing the graph — same safe ordering as loadFactoryPreset. The clear is recorded as an
    // undoable structural change when undoManager is present (Cmd+Z restores the prior patch).
    void newPatch();

    // Layout / anti-overlap
    juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h, juce::AudioProcessorGraph::NodeID selfId);
    void finalizeModuleDrag(ModuleComponent* module);
    void autoArrange();

    // ---- Multi-select (issue #156) ------------------------------------------------------
    //
    // Gesture contract, chosen so the existing drag-to-pan muscle memory is untouched:
    //   plain drag on empty canvas          -> pan (unchanged)
    //   Shift + drag on empty canvas        -> marquee select, replacing the selection
    //   Cmd/Ctrl + Shift + drag             -> marquee select, adding to the selection
    //   click a module                      -> select just it
    //   Shift/Cmd + click a module          -> toggle it in the selection
    //   drag any selected module            -> moves the whole selection together
    //   Escape / click empty canvas         -> clear
    //   Delete / Backspace                  -> delete the selection

    const synth::ui::SelectionModel& getSelection() const { return selection; }

    /** Selects a single module. When additive, toggles it instead and leaves the rest alone. */
    void selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive);
    void setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids);
    void clearSelection();
    void selectAllModules();
    bool isNodeSelected(juce::AudioProcessorGraph::NodeID nodeId) const { return selection.contains(nodeId); }
    int getSelectionCount() const { return selection.size(); }
    std::vector<juce::AudioProcessorGraph::NodeID> getSelectedNodes() const { return selection.getSelected(); }

    /** Removes every selected module as ONE undoable change, so Cmd+Z restores the whole group. */
    void deleteSelection();

    /** Drops selected ids whose nodes no longer exist. Called after any graph mutation that can
     *  remove nodes (delete, undo/redo, preset load) — a stale id would otherwise be handed to
     *  snippet extraction or a group drag. */
    void pruneSelection();

    // ---- Marquee (rubber-band) selection ----
    // Points are in CANVAS coordinates (post-transform), so the marquee tracks the content under
    // the cursor while zoomed or panned.
    void beginMarquee(juce::Point<int> canvasAnchor, bool additive);
    void updateMarquee(juce::Point<int> canvasCurrent);
    void endMarquee();
    bool isMarqueeActive() const { return marqueeActive; }
    juce::Rectangle<int> getMarqueeRect() const { return marqueeRect; }

    // ---- Group drag ----
    // ModuleComponent drives its own drag with a ComponentDragger; when the dragged module is part
    // of a multi-selection it reports its delta here and every other selected module follows.
    void beginSelectionDrag();
    void dragSelectionBy(juce::Point<int> delta, ModuleComponent* initiator);
    void finalizeSelectionDrag();
    /** Discards the recorded drag origins without re-resolving any position — for a press that
     *  never moved (positions loaded from a preset are not necessarily grid-aligned, so a
     *  finalize on a zero-delta drag would visibly nudge the group). */
    void cancelSelectionDrag();
    bool isSelectionDragActive() const { return selectionDragActive; }

    // ---- Snippets (issue #156) ----

    /** Snippet JSON for the current selection, ready to hand to SnippetManager::saveSnippet. */
    juce::var extractSelectionSnippet(const juce::String& name);

    /** Inserts a snippet at a canvas position as one undoable change, then selects what landed.
     *  @return true when at least one module was added. */
    bool insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos);

    /** Set by the owner (MainComponent) to prompt for a name and persist the snippet. Invoked
     *  from the canvas context menu; GraphEditor deliberately owns no file dialogs. */
    std::function<void()> onSaveSnippetRequested;

    /** Set by the owner to resolve a snippet name (from a library drag payload) to its JSON. */
    std::function<juce::var(const juce::String&)> snippetProvider;

    // Drag-preview (grid + landing ghost shown during a module drag)
    void beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId);
    void updateDragPreview(juce::Point<int> desiredTopLeftCanvas);
    void endDragPreview();

    // Test accessors for drag-preview state
    bool isDragPreviewActive() const { return dragPreviewActive; }
    juce::Rectangle<int> getDragPreviewGhost() const { return dragPreviewGhost; }

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled) { alignmentGuidesEnabled = enabled; }
    bool getAlignmentGuidesEnabled() const { return alignmentGuidesEnabled; }

    // ---- Onboarding / UI Phase 5 helpers (headless-testable) ----

    /** Returns true when the canvas has no modules (empty state). Pure predicate.
     *  nodeCount is the number of non-Attenuverter nodes rendered as ModuleComponents. */
    static bool isCanvasEmpty(int nodeCount) noexcept { return nodeCount <= 0; }

    /** Compute the final snapped + anti-overlapped position for a newly dropped module.
     *  Equivalent to snap(dropPoint) + findFreeSlot.  Pure helper — does not touch GUI state.
     *  @param dropPoint   Desired top-left in canvas coordinates (will be snapped internally).
     *  @param w, h        Module footprint in pixels.
     *  @param existingBoxes  All already-placed module bounding boxes (selfId excluded from collision).
     *  @param selfId      NodeID of the module being placed (excluded from self-collision).
     */
    static juce::Point<int> computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                     const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                     synth::LayoutUtil::NodeID selfId);

    // DragAndDropTarget overrides
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;

    // Mouse Overrides
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Canvas-scoped keys: Delete/Backspace removes the selection, Escape clears it. Deliberately
    // NOT routed through ShortcutManager — an unmodified Delete binding registered app-wide would
    // fire from any panel that doesn't consume the key first.
    bool keyPressed(const juce::KeyPress& key) override;

    juce::AudioProcessorGraph::NodeID getAttenuverterNodeAt(juce::Point<float> localPos);

private:
    class GraphContentComponent : public juce::Component {
    public:
        GraphContentComponent(GraphEditor& editor);
        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;

        juce::OwnedArray<ModuleComponent>& getModules() { return moduleComponents; }

        float connectionAnimPhase = 0.0f;

    private:
        GraphEditor& editor;
        juce::OwnedArray<ModuleComponent> moduleComponents;
    };

    AudioEngine& audioEngine;
    GraphContentComponent content;
    ModMatrixComponent modMatrix;
    bool isMatrixVisible = false;

    // Navigation State
    float zoomLevel = 1.0f;
    juce::Point<float> panOffset;
    juce::Point<int> lastMousePos;

    // Drag State
    bool isDraggingConnection = false;
    ModuleComponent* dragSourceModule = nullptr;
    int dragSourceChannel = 0;
    bool dragSourceIsInput = false;
    bool dragSourceIsMidi = false;
    juce::Point<int> dragCurrentPos;

    // Drag-preview state (grid + landing ghost)
    bool dragPreviewActive = false;
    int dragPreviewW = 0, dragPreviewH = 0;
    juce::AudioProcessorGraph::NodeID dragPreviewSelfId{};
    juce::Rectangle<int> dragPreviewGhost;

    juce::AudioProcessorGraph::NodeID draggingAttenuverterNodeId;
    float attenDragStartValue = 0.0f;

    // ---- Selection state (issue #156) ----
    synth::ui::SelectionModel selection;

    // Marquee drag, in canvas coordinates.
    bool marqueeActive = false;
    bool marqueeAdditive = false;
    juce::Point<int> marqueeAnchor;
    juce::Rectangle<int> marqueeRect;
    // Selection as it stood when the marquee began — the base an additive marquee unions onto.
    std::vector<juce::AudioProcessorGraph::NodeID> marqueeBaseSelection;

    // Group drag: each selected module's position when the drag started, so every member can be
    // placed from its own origin rather than accumulating per-frame deltas (which would drift).
    bool selectionDragActive = false;
    std::vector<std::pair<juce::AudioProcessorGraph::NodeID, juce::Point<int>>> selectionDragStartPositions;

    // True while a click on empty canvas has not yet turned into a pan or marquee drag; a mouseUp
    // in that state is a plain click and clears the selection.
    bool pendingEmptyCanvasClick = false;

    /** Bounding boxes of every rendered module, for marquee hit-testing and group collision. */
    std::vector<synth::LayoutUtil::Box> collectModuleBoxes(bool selectedOnly, bool excludeSelected) const;

    /** Footprint of the group a snippet drag payload would drop, for the landing ghost. Falls back
     *  to a single-module estimate when the payload can't be resolved. */
    juce::Point<int> estimateSnippetSize(const juce::String& payload) const;

    /** Repaints only the module components whose selected state actually changed. Selection
     *  changes must never trigger a full-canvas repaint storm during a marquee drag. */
    void applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection);

    AppUndoManager* undoManager = nullptr;
    std::vector<AudioEngine::ModulationDisplayInfo> cachedModDisplayInfo;
    std::vector<AudioEngine::ModulationRouting> cachedModRoutings;

    // ---- Animation members (UI Phase 5) ----
    // Drop-landing tween: animates the newly dropped module from drop point to snapped position.
    // Both must be class members so they outlive the VBlank frame callbacks.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver dropLandingAnim;

    // Mod-matrix panel ease: animates the panel bounds on show/hide.
    synth::ui::AnimationDriver modMatrixAnim;

    // Tracks the target bounds for mod-matrix animation so we can set final position on complete.
    juce::Rectangle<int> modMatrixTargetBounds;

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // During drag previews, store guide positions for visual feedback.
    struct AlignmentGuide {
        juce::Point<float> start; // line start point (canvas coords)
        juce::Point<float> end;   // line end point (canvas coords)
        int type;                 // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
    };
    std::vector<AlignmentGuide> alignmentGuides;

    // Alignment guides toggle (UI Phase 7 - Item 4)
    bool alignmentGuidesEnabled = true;

    void updateTransform();

    // Internal: start the drop-landing animation for a newly placed module component.
    void animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos);

public:
    const std::vector<AudioEngine::ModulationDisplayInfo>& getCachedModDisplayInfo() const {
        return cachedModDisplayInfo;
    }

    const std::vector<AudioEngine::ModulationRouting>& getCachedModRoutings() const { return cachedModRoutings; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditor)
};
