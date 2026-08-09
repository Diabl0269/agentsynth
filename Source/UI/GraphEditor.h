#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "CableColour.h"
#include "LayoutUtil.h"
#include "SelectionModel.h"
#include "UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ModuleComponent;
#include "ModMatrixComponent.h"

class GraphEditor
    : public juce::Component
    , public juce::Timer
    , public juce::DragAndDropTarget
    , public juce::FileDragAndDropTarget
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

    /** Raw-channel wiring for a single cable: the channel each end starts at, and how many
     *  consecutive per-voice channels the cable covers (1 = an ordinary mono wire).
     *  Voice v connects sourceRawChannel + v * sourceStride -> destRawChannel + v. */
    struct PolyLink {
        int sourceRawChannel = 0;
        int destRawChannel = 0;
        int voiceCount = 1;
        // 1 for a voice-to-voice fan; 0 when a single mono source is broadcast to every voice of
        // the destination fan, so all N wires leave the same source channel.
        int sourceStride = 1;
    };

    /** Works out which raw-channel fan a cable dropped between two *visible* jacks should wire.
     *  Port hit-testing yields visible jack indices, which are not raw channel numbers once a
     *  module goes poly (a poly VCA's CV jack is jack 1 but raw channel 8).  When both ends front
     *  equally wide fans the cable covers all N voices; otherwise it degrades to one head-to-head
     *  wire.  The one exception is a mono source landing on a per-voice *mod-CV* fan: that is
     *  broadcast to every voice (one LFO shakes all eight), which is what sourceStride == 0 means.
     *  Where a jack fronts more than one fan (Poly MIDI's "Poly Out" carries both Pitch and
     *  Gate) the pairing whose roles agree wins.  A null end (the graph's audio I/O nodes, which have
     *  no logical ports) is treated as a plain mono jack whose index is its raw channel.
     *  Pure — no graph access, headless-testable. */
    static PolyLink resolvePolyLink(const ModuleBase* source, int sourceVisibleJack, const ModuleBase* dest,
                                    int destVisibleJack);

    /** Re-evaluates every connection touching `module` after its "poly" parameter changed, so the
     *  graph matches the module's new channel layout: mono wires fan out to N voices when both ends
     *  are poly, fans collapse back to one wire when poly is switched off, and wires move to the raw
     *  channels the new layout puts them on.  MIDI connections are left alone.
     *  `previousInputMap`/`previousOutputMap` are the module's raw->LogicalPort maps captured
     *  *before* the change — they are the only way to tell which visible jack each existing raw
     *  connection was anchored to, since the live mapping already reflects the new state.
     *  Does not record undo state; the caller owns the surrounding transaction. */
    void rewireForPolyChange(ModuleComponent* module, const std::vector<LogicalPort>& previousInputMap,
                             const std::vector<LogicalPort>& previousOutputMap);
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
    // A module changed footprint in place (the Macro bank, when its "Knobs" count changes).
    // Drops any routing left on an output jack that is no longer visible, then pushes overlapping
    // neighbours clear. The resized module itself never moves.
    void handleModuleResized(ModuleComponent* moduleComp);
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

    /** Called at the end of every updateComponents(), i.e. whenever the set of modules in the graph
     *  may have changed (add, delete, replace, preset load, undo). Owners use it to refresh UI that
     *  depends on what the patch contains — the module library greys out its singleton I/O rows. */
    std::function<void()> onGraphStructureChanged;

    /** True for the module-library entries that must exist at most once per patch (Audio Input /
     *  Audio Output). A second one would sum into the same device buffer rather than address a
     *  different physical output, and the app's node lookups all take the first match. */
    static bool isSingletonIOModule(const juce::String& typeName);

    /** True when the graph already contains a node whose processor reports this name. */
    static bool graphHasModuleNamed(juce::AudioProcessorGraph& graph, const juce::String& typeName);

    /** Estimated (w, h) footprint for a module type name, used for the library drag ghost before a
     *  real component exists. Public so a test can hold it to the real component sizes — see
     *  ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents. */
    static juce::Point<int> estimateModuleSize(const juce::String& typeName);

    // DragAndDropTarget overrides
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;

    // FileDragAndDropTarget overrides — an audio file dropped on empty canvas becomes a Sampler
    // preloaded with it. A drop over an existing Sampler is claimed by that ModuleComponent first.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /** Creates `name` at a canvas position, snapped and anti-overlapped, with undo recorded.
     *  `configure` runs on the processor BEFORE it joins the graph, so any non-parameter state it
     *  sets is captured by the undo snapshot. */
    void addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                   const std::function<void(juce::AudioProcessor&)>& configure);

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

    // ---- Cables (issue #157) ----------------------------------------------
    //
    // A "cable" is one wire as the USER sees it, which is not the same thing as a graph edge:
    // an attenuverter chain is two edges plus a hidden node, and a poly bus is N edges. Both
    // render as a single wire, so anything that identifies, hit-tests, or colours a cable has to
    // key on this logical view rather than on juce::AudioProcessorGraph::Connection.

    /** Stable identity for a user-visible cable. Survives repaints; used to tell whether the
     *  hovered cable actually changed (so hover does not repaint on every mouse move). */
    struct CableId {
        uint32_t srcUid = 0;
        int srcPort = 0;
        uint32_t dstUid = 0;
        int dstPort = 0;
        uint32_t attenUid = 0; // non-zero only for an attenuverter chain

        bool operator==(const CableId& o) const noexcept {
            return srcUid == o.srcUid && srcPort == o.srcPort && dstUid == o.dstUid && dstPort == o.dstPort &&
                   attenUid == o.attenUid;
        }
        bool operator!=(const CableId& o) const noexcept { return !(*this == o); }
    };

    /** One drawn wire, with everything paint() and hit-testing need. Produced by
     *  buildVisibleCables() so the canvas and the mouse agree on where cables are — if these
     *  were computed separately they would drift and clicks would miss the wire. */
    struct VisibleCable {
        enum class Kind {
            Direct,            // a plain audio or MIDI graph edge
            AttenuverterChain, // source -> attenuverter -> destination, drawn as one wire + knob
            ModRouting         // DirectCV / PolyBus mod routing
        };

        CableId id;
        Kind kind = Kind::Direct;
        juce::Point<float> p1, p2; // canvas coords
        synth::ui::CableSignal signal = synth::ui::CableSignal::Audio;
        synth::ui::ModuleCategory sourceCategory = synth::ui::ModuleCategory::Utility;
        bool isBypassed = false;
        float activity = 0.0f;    // drives brightness / width
        bool isPolyBus = false;   // RoutingKind::PolyBus — drives the "xN" bundle badge
        int voiceCount = 1;       // PolyBus bundle size (badge)
        float attenAmount = 0.0f; // AttenuverterChain knob value, -1..1
    };

    /** Enumerates every cable currently drawn on the canvas, in paint order. */
    std::vector<VisibleCable> buildVisibleCables();

    /** The cubic bezier a cable is drawn along. Must stay identical to
     *  AppLookAndFeel::drawConnectionWire's default curve or hit-testing drifts off the wire. */
    static juce::Path buildCablePath(juce::Point<float> p1, juce::Point<float> p2);

    /** Perpendicular distance from a canvas point to a cable's curve, in pixels. */
    static float distanceToCable(const VisibleCable& cable, juce::Point<float> canvasPos);

    /** Topmost cable within `tolerance` px of a canvas point, or nullopt.
     *  Later cables win, matching paint order (mod wires draw over audio wires). */
    std::optional<VisibleCable> getCableAt(juce::Point<float> canvasPos, float tolerance = kCableHitTolerance);

    /** Click tolerance in canvas px. Wider than the wire itself so thin cables stay grabbable. */
    static constexpr float kCableHitTolerance = 7.0f;

    /** Removes every graph edge behind a user-visible cable, as one undoable action. */
    void disconnectCable(const VisibleCable& cable);

    /** Cable colouring config. Owned by MainComponent / AppearanceSettingsTab (which persist it);
     *  GraphEditor just renders what it is handed, so it needs no ApplicationProperties. */
    void setCableColourMode(synth::ui::CableColourMode mode);
    synth::ui::CableColourMode getCableColourMode() const noexcept { return cableColourMode; }
    void setCableColourOverrides(const synth::ui::CableColourOverrides& overrides);
    const synth::ui::CableColourOverrides& getCableColourOverrides() const noexcept { return cableColourOverrides; }

    /** Resolved colour for a cable under the current mode + overrides + active theme. */
    juce::Colour colourForCable(const VisibleCable& cable) const;

    // Test accessors.
    int getVisibleCableCount() { return (int)buildVisibleCables().size(); }
    bool hasHoveredCable() const noexcept { return hoveredCableId.has_value(); }

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

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

    // ---- Cable hover / colouring state (issue #157) ----
    // Only the ID is kept between frames: the geometry is rebuilt each paint anyway, and holding
    // a stale VisibleCable across a graph edit would dangle conceptually (ports move, nodes go).
    std::optional<CableId> hoveredCableId;
    synth::ui::CableColourMode cableColourMode = synth::ui::CableColourMode::BySignalType;
    synth::ui::CableColourOverrides cableColourOverrides;

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
