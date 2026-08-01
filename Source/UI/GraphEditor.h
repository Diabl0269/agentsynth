#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "LayoutUtil.h"
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
