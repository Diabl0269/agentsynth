#pragma once

#include "AppUndoManager.h"
#include "AudioEngine/ModulationRoutingTypes.h"
#include "MacroSet.h"
#include "Modules/MacroPortShape.h"
#include "PatchDocument.h"
#include "UI/Graph/CableColour.h"
#include "UI/Graph/CableRetractAnimator/CableRetractAnimator.h"
#include "UI/Graph/CanvasFrame/CanvasEdgeDrag.h"
#include "UI/Graph/CanvasFrame/CanvasFrame.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphCanvasHost.h"
#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Graph/MacroCrossingAnimator/MacroCrossingAnimator.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/MacroHullGlide/MacroHullGlide.h"
#include "UI/Graph/ModuleClipboard.h"
#include "UI/Graph/SelectionModel.h"
#include "UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ScrollTween.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace synth::ui {
class ModDotController;    // UI/Graph/ModDot/ModDotController.h
class PortPanelController; // UI/Graph/PortPanel/PortPanelController.h
class PortConnector;       // UI/Graph/PortPanel/PortConnector.h (a friend: the drop rules, shared with the port panel)
class ColourPickerPopup;   // a unique_ptr return type only; 89 files include this header
} // namespace synth::ui
class AudioEngine;
class CanvasCardKeyboard;
class LoadRevealAnimator;
class ModuleComponent;
class MacroCardComponent;
namespace synth {
struct ViewDoc;              // Forward declaration (Source/Project/ViewDoc.h)
class PluginCardLayoutStore; // hosted-plugin card layouts, see setPluginCardLayoutStore
class MidiRemoteProjectDoc;  // see setMidiRemoteProjectDocForUndo
} // namespace synth
namespace graph_editor_paint {
class CanvasMemo;
} // namespace graph_editor_paint
#include "UI/Graph/MinimapComponent.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Layout/KeyboardContextMenu.h"

class GraphEditor
    : public juce::Component
    , public juce::Timer
    , public juce::DragAndDropTarget
    , public juce::FileDragAndDropTarget
    , public juce::SettableTooltipClient
    , public synth::ui::KeyboardContextMenuProvider
    , private GraphCanvasHost {
public:
    GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr = nullptr);
    ~GraphEditor() override;

    AudioEngine& getAudioEngine() { return audioEngine; }
    ModMatrixComponent& getModMatrix() { return modMatrix; }
    juce::OwnedArray<ModuleComponent>& getModuleComponents() { return content.getModules(); }
    std::function<void()> onBeforeDetachAllModuleComponents; // fires here AND from deleteSelection()
    std::function<void(const juce::String&)> onModuleAdded;  // a user placed a module; its factory type name
    std::function<void(const std::vector<juce::AudioProcessorGraph::NodeID>&)> onBeforeDetachModuleComponentsFor;
    void detachAllModuleComponents();
    void detachModuleComponentsFor(const std::vector<juce::AudioProcessorGraph::NodeID>& doomed); // before they go
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void timerCallback() override;
    void updateComponents() override; // also GraphCanvasHost::updateComponents()
    void toggleModMatrixVisibility();
    bool isModMatrixVisible() const { return isMatrixVisible; }

    // ---- Minimap ----
    void setMinimapVisible(bool shouldBeVisible);
    void toggleMinimapVisibility();
    bool isMinimapVisible() const noexcept { return minimapVisible; }
    /** Mini map slide-and-fade (GraphEditorMinimapSlide.cpp): 0 = gone, 1 = fully shown. */
    float getMinimapSlideProgress() const noexcept { return minimapSlide_.getProgress(); }
    bool isMinimapSlideMovingForTest() const noexcept { return minimapSlide_.isMoving(); }
    /** Lets a toggle animate while the editor is off screen, so a test can reach the animated path. */
    void setMinimapAnimateOffScreenForTest(bool animate) noexcept { minimapAnimateOffScreen_ = animate; }
    /** Advances the slide to `t` (0..1) with no VBlank, as the real driver's onUpdate does. */
    void advanceMinimapSlideForTest(float t);
    /** Lands the slide, as the real driver's onComplete does. */
    void finishMinimapSlideForTest();
    synth::ui::MinimapComponent& getMinimap() noexcept { return minimap; }

    juce::Rectangle<float> getVisibleCanvasRect() const;
    void centreViewOn(juce::Point<float> canvasPoint);
    void fitViewToModules();
    void zoomAroundCentre(float wheelDelta);
    /** The canvas zoom and pan as they are now; `applyViewDoc` clamps to the wheel-zoom range. Message thread. */
    synth::ViewDoc getViewDoc() const;
    void applyViewDoc(const synth::ViewDoc& view);
    synth::ui::MinimapModel buildMinimapModel();

    // ---- Locate Master ---- See GraphEditorTypes.h for the LocateMasterResult enum.
    using LocateMasterResult = graph_editor_types::LocateMasterResult;
    bool hasLocatableMasterOrOutput() const;
    /** Selects Master, falling back to Audio Output when there is none yet, and pans into view. */
    LocateMasterResult locateMasterOrOutput();
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);
    void clearModDropTargets();
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);
    // ---- Mod-amount drag gesture (the one path both the cable knob and a card knob's ring use) ----
    void beginModAmountGesture();
    void adjustModAmount(juce::AudioProcessorGraph::NodeID attenuverterNodeID, float delta);
    void commitModAmountGesture();
    // See GraphEditorTypes.h for the PolyLink struct's full field-level doc.
    using PolyLink = graph_editor_types::PolyLink;
    /** Which raw channels a cable dropped between two visible jacks should wire. */
    static PolyLink resolvePolyLink(const ModuleBase* source, int sourceVisibleJack, const ModuleBase* dest,
                                    int destVisibleJack);
    /** Re-evaluates every connection touching `module` after its poly parameter changed. */
    void rewireForPolyChange(ModuleComponent* module, const std::vector<LogicalPort>& previousInputMap,
                             const std::vector<LogicalPort>& previousOutputMap);
    void deleteModule(ModuleComponent* module);
    /** `recordUndo=false`: the caller already opened a recordGraphAndMacroChange around it. */
    void requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId, bool recordUndo = true);
    void replaceModule(ModuleComponent* module, const juce::String& newModuleType,
                       const std::function<void(juce::AudioProcessor&)>& configure = {});
    void updateModulePosition(ModuleComponent* module);

    // Preset Management
    void savePreset(juce::File file);
    void loadPreset(juce::File file, bool append = false);
    bool loadFactoryPreset(int index);
    void newPatch();

    /** The per-loaded-file stash of top-level JSON keys this build doesn't understand (see
     *  `patchDocument` below). Exposed so the app's `.agsproj` save/load path can re-merge it —
     *  GraphEditor owns no file dialogs, and MainComponent owns no PatchDocument. */
    synth::PatchDocument& getPatchDocument() noexcept { return patchDocument; }
    /** GraphEditor's live set of Macros for the current patch (Source/MacroSet.h). Exposed for the
     *  same reason as getPatchDocument() above. */
    synth::MacroSet& getMacros() noexcept override { return macros; }

    // ---- Macros (docs/macros/macros.md) — owned by MacroGroupController; contracts live there ----
    using MacroPortOwner = MacroGroupController::MacroPortOwner;

    // Layout / anti-overlap
    juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h,
                                      juce::AudioProcessorGraph::NodeID selfId) override;
    juce::Point<int> findLeftEdgeSlotBelowModules(int w, int h);
    void handleModuleResized(ModuleComponent* moduleComp);
    void dropRoutingsOnHiddenJacks(juce::AudioProcessorGraph::NodeID nodeId);
    void refreshIoModulesAfterDeviceChange();

    /** Output-card identity treatment (docs/layout/module-card.md): installs the callback
     *  MainComponent uses to describe where the signal actually goes. Set once; GraphEditor/
     *  ModuleComponent stay ignorant of Standalone-vs-Hosted framing and just render the string. */
    void setOutputDeviceInfoProvider(std::function<juce::String()> provider);
    void refreshOutputDeviceInfo();
    void completeStereoPairConnections(ModuleComponent* moduleComp);
    void finalizeModuleDrag(ModuleComponent* module);
    void autoArrange(bool record = true); // record=false: inside the caller's own undo step

    // ---- Output dock: Master, Rec Tap and Audio Output, always the rightmost cards (GraphEditorOutputDock.cpp,
    // docs/layout/layout.md#output-dock). Their x is derived, never user-set; their shared y is Audio Output's own.
    /** Re-derives the dock's positions (node x/y and live bounds, synchronously). No undo step, no dirtiness. */
    void reflowOutputDock() override;
    bool isOutputDockNode(juce::AudioProcessorGraph::NodeID nodeId) const;
    void carryOutputDockWith(ModuleComponent* initiator); // live vertical drag: the other dock cards follow its y
    void finalizeOutputDockDrag(ModuleComponent* module); // drag release: snaps y for the dock, re-derives x
    /** Empty when `nodeId` may be deleted, else why not (Audio Output never; Master while channels exist). */
    juce::String outputDockDeleteRefusal(juce::AudioProcessorGraph::NodeID nodeId) const;
    void removeUndeletableOutputNodes(std::vector<juce::AudioProcessorGraph::NodeID>& ids); // erases refused ids
    /** Takes any dock card out of the macro it is a member of (a saved project can hold one), quietly: no undo step. */
    void evictOutputDockFromMacros();
    void frameOutputDock(); // frames the whole dock in view ("Go to Output")

    // ---- Multi-select (gesture contract: GraphEditorSelection.cpp) ----
    const synth::ui::SelectionModel& getSelection() const override { return selection; }
    void selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive);
    void setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) override;
    void clearSelection();
    void selectAllModules();
    /** Selects the module `direction` (+1 next, -1 previous) steps from the selection, left to
     *  right across the canvas (ModuleStepOrder.h), and pans it into view if it is off-screen.
     *  @return false when the canvas has no module to select. */
    bool selectAdjacentModule(int direction);
    bool isNodeSelected(juce::AudioProcessorGraph::NodeID nodeId) const { return selection.contains(nodeId); }
    int getSelectionCount() const { return selection.size(); }
    std::vector<juce::AudioProcessorGraph::NodeID> getSelectedNodes() const { return selection.getSelected(); }
    /** Removes every selected module as ONE undoable change. */
    void deleteSelection() override;
    /** deleteSelection()'s removal with no undo record, for a bigger gesture that records one (MainComponent::
     *  deleteTrack, inside a CardGlideAnimator::Scope): cards of `ids` shrink away; `healChain` splices the survivors.
     */
    void removeNodesNow(const std::vector<juce::AudioProcessorGraph::NodeID>& ids, bool healChain, bool narrowDetach);
    void pruneSelection();

    // ---- Marquee (rubber-band) selection; points are in CANVAS coordinates ----
    void beginMarquee(juce::Point<int> canvasAnchor, bool additive);
    void updateMarquee(juce::Point<int> canvasCurrent);
    void endMarquee();
    bool isMarqueeActive() const { return marqueeActive; }
    juce::Rectangle<int> getMarqueeRect() const { return marqueeRect; }

    // ---- Group drag: a module in a multi-selection reports its delta here, the rest follow ----
    /** `initiator` (the grabbed card, when known): a dock card drags alone, never as a group. */
    void beginSelectionDrag(juce::AudioProcessorGraph::NodeID initiator = {});
    void dragSelectionBy(juce::Point<int> delta, ModuleComponent* initiator);
    void finalizeSelectionDrag();
    /** Discards the recorded drag origins without re-resolving any position. */
    void cancelSelectionDrag();
    bool isSelectionDragActive() const override { return selectionDragActive; }
    /** A live drag delta with the dragged cards held at the canvas origin (GraphEditorCanvasFrame.cpp). */
    juce::Point<int> clampDragDeltaToCanvas(juce::Point<int> rawDelta) { return edgeDrag_.clampDelta(rawDelta); }

    bool isMacroChipDragActive() const { return macroChipDragId.isNotEmpty(); } // test accessor
    void cancelLiveDragGestures();

    // ---- Cmd-drag macro reparent (docs/macros/menu-and-membership.md) ----
    /** The macro a live reparent drag would leave if released now, empty for none. */
    juce::String getMacroDragLeaveId() const noexcept { return macroDragLeaveId_; }
    /** The macro a live reparent drag (or a library drag, see setMacroDropCandidate) would join. */
    juce::String getMacroDragJoinId() const noexcept { return macroDragJoinId_; }
    /** Whether a live reparent drag has a leave or join candidate, or has already moved its module into or
     *  out of a macro as it crossed. */
    bool hasMacroDragCandidate() const;
    /** The macro a live drag has moved its module into as it crossed, empty when it is back where it started. */
    juce::String getMacroDragLiveOwnerId() const;
    /** The module a reparent drag is currently moving, invalid between gestures. */
    juce::AudioProcessorGraph::NodeID getMacroDragDraggedNodeId() const noexcept { return macroDragDraggedNodeId_; }
    void updateMacroDragCandidate(juce::AudioProcessorGraph::NodeID draggedNodeId, juce::Point<int> canvasCentre);
    void clearMacroDragCandidate(bool keepFrozenBorders = false);
    /** Freezes the borders of the module's macro and ancestors for a reparent drag (cleared on candidate clear). */
    void beginMacroDragFreeze(juce::AudioProcessorGraph::NodeID draggedNodeId);
    /** Paint-only hull bounds (hit-testing keeps using macroHullBounds). */
    juce::Rectangle<int> paintedMacroHullBounds(const juce::String& macroId) const;
    /** Single-undo-step finalize (position + leave + join). `module` must not be touched afterwards. */
    void finalizeMacroMembershipDrag(ModuleComponent* module, const juce::String& leaveId, const juce::String& joinId);
    /** Cables as drawn now / retract those no longer drawn since; `growAdded` (undo, redo) also grows the new ones. */
    std::vector<graph_editor_types::VisibleCable> snapshotCablesForRetract() override;
    void retractCablesGoneSince(const std::vector<graph_editor_types::VisibleCable>& before,
                                bool growAdded = false) override;
    /** The expanded macro borders as drawn now; hand it to glideHullsFrom() after a change that may move them. */
    MacroHullGlide::Hulls snapshotPaintedHulls() const;
    /** Glides every border that moved since `before` (a snapshotPaintedHulls()) to its live bounds. */
    void glideHullsFrom(const MacroHullGlide::Hulls& before);
    /** True once the drag in progress has moved its module into or out of a macro (applied as it crossed). */
    bool hasLiveMacroMembershipChange() const noexcept { return liveMembershipChanged_; }

    /** Preference "macroDragWithoutCmd": reparent by drag without Cmd (single-module drags only). */
    void setMacroDragWithoutCmdEnabled(bool enabled) { macroDragWithoutCmdEnabled = enabled; }
    bool getMacroDragWithoutCmdEnabled() const noexcept { return macroDragWithoutCmdEnabled; }
    /** Preference "moveMacroOnHullDrag" (off by default): dragging empty space inside an expanded macro's hull moves
     *  the macro (the name chip's drag) instead of panning. Shift still draws a marquee. */
    void setMoveMacroOnHullDragEnabled(bool enabled) { moveMacroOnHullDragEnabled = enabled; }
    bool getMoveMacroOnHullDragEnabled() const noexcept { return moveMacroOnHullDragEnabled; }
    juce::String macroJoinTargetAt(juce::Point<int> canvasCentre) const override;
    void setMacroDropCandidate(const juce::String& macroId) override;

    // ---- Macro auto-port preference (docs/macros/auto-ports.md) ----
    using MacroAutoPortPreference = graph_editor_types::MacroAutoPortPreference;

    void setMacroAutoPortPreference(MacroAutoPortPreference pref) noexcept { macroAutoPortPreference_ = pref; }
    MacroAutoPortPreference getMacroAutoPortPreference() const noexcept { return macroAutoPortPreference_; }

    /** Cmd+G / right-click "Create Macro" entry point; may show the auto-port modal. */
    void requestGroupSelectionIntoMacro() override;

    /** Test seam: replaces the real modal when set. Null in production. */
    std::function<void(std::function<void(bool createPorts, bool remember)> respond)> macroAutoPortModalForTest;

    // The collaborators, for the app to install its hooks on; the const overloads serve const call sites.
    /** The mod dot's controller: drag, tooltip and last-chosen source (UI/Graph/ModDot). */
    synth::ui::ModDotController& getModDot() noexcept { return *modDot_; }
    /** The jack panel's controller: jack clicks, the open panel, the cable highlight (UI/Graph/PortPanel). */
    synth::ui::PortPanelController& getPortPanel() noexcept { return *portPanel_; }
    MacroGroupController& getMacroController() noexcept { return macroController_; }
    const MacroGroupController& getMacroController() const noexcept { return macroController_; }
    /** Opacity factor (0..1) of the smart-connection preview cables; 1 once they have faded in. */
    float getSmartPreviewReveal() const noexcept { return smartPreviewReveal_; }
    SmartConnectionEngine& getSmartConnections() noexcept { return smartConnections_; }
    const SmartConnectionEngine& getSmartConnections() const noexcept { return smartConnections_; }
    GraphDragDropController& getDragDropController() noexcept { return dragDropController_; }
    const GraphDragDropController& getDragDropController() const noexcept { return dragDropController_; }
    /** The canvas's card keys (arrows, Alt+arrows, Return); install the ShortcutManager here. */
    CanvasCardKeyboard& getCardKeyboard() noexcept { return *cardKeyboard_; }

    /** Async rename prompt for a macro with no card (e.g. the expanded hull menu). */
    void promptRenameMacro(const juce::String& macroId);

    /** Test seam: replaces the real modal when set. Null in production. */
    std::function<void(const juce::String& macroId)> promptRenameMacroForTest;
    void promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea);

    /** Where the recolour picker's favourites persist. Null (default) keeps them in memory only. */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    // ---- Macro bypass/mute: fan-out commands over the members (docs/macros/ports.md#bypass-and-mute) ----
    using MacroToggleState = MacroGroupController::MacroToggleState;
    std::unique_ptr<synth::ui::ColourPickerPopup> createMacroColourPickerForTest(const juce::String& macroId);

    /** The shared macro actions menu for a collapsed card or an expanded hull. */
    juce::PopupMenu
    buildMacroMenu(const juce::String& macroId, std::function<void()> renameAction = nullptr,
                   const std::vector<juce::AudioProcessorGraph::NodeID>* addCandidateSelection = nullptr);

    /** Live bounds + colour category for the currently-resolvable MODULE members of `macroId`
     *  (a port node is excluded). See MacroGroupController::macroMemberPreviews. */
    using MacroMemberPreview = MacroGroupController::MacroMemberPreview;
    juce::Colour categoryPreviewColour(synth::ui::ModuleCategory category) const;

    /** Status-bar surface for a refused macro action. Owner installs; a no-op by default. Mirrors
     *  onSaveSnippetRequested's ownership split — GraphEditor owns no status bar. */
    std::function<void(const juce::String&)> onStatusMessage;

    // ---- Macro card drag (MacroCardComponent calls these; carries the hidden members along) ----
    void beginMacroCardDrag(const juce::String& macroId);
    void dragMacroCardBy(const juce::String& macroId, juce::Point<int> delta);
    void finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft);
    void cancelMacroCardDrag(const juce::String& macroId);

    // ---- Macro I/O: the "Configure I/O" modal (docs/macros/configure-io.md); each edit is one undo step ----

    // Not a pure forwarder (also repaints both surfaces and disarms the preview), so it stays on
    // GraphEditor; callers must not call MacroGroupController::changeMacroPortColour directly.
    void changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                               std::optional<juce::Colour> newColour);

    // Per-port jack colour: neither paint surface repaints on its own, so these force BOTH.
    using MacroPortRecolourTargets = graph_editor_types::MacroPortRecolourTargets;
    // Repaint BOTH surfaces; returns the (possibly-null) pair.
    MacroPortRecolourTargets repaintMacroPortColourTargets(const juce::String& macroId, const juce::String& nodeUuid);
    // Arm the view-layer-only preview (no stored colour); idempotent.
    void previewMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid, juce::Colour colour);
    // Disarm the preview so the jack falls back to the stored colour; no-op when unarmed.
    void clearMacroPortColourPreview(const juce::String& macroId, const juce::String& nodeUuid);
    // Teardown backstop for a picker abandoned with no commit.
    void cancelArmedMacroPortColourPreview();
    // The shared lookup behind preview and commit.
    MacroPortRecolourTargets findMacroPortRecolourTargets(const juce::String& macroId, const juce::String& nodeUuid);
    void promptConfigureMacroIO(const juce::String& macroId);

    /** Quick "Rename Port" prompt -- the one-name alternative to Configure I/O. */
    void promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    // ---- Macro card jacks (docs/macros/ports.md#cable-rendering-across-the-boundary) ----
    using MacroCardPort = MacroGroupController::MacroCardPort;

    // ---- Snippets ----
    juce::var extractSelectionSnippet(const juce::String& name);
    bool insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) override;

    // Prompts for a name and persists the snippet (canvas context menu); GraphEditor owns no file dialogs.
    std::function<void()> onSaveSnippetRequested;
    // Resolves a snippet name (from a library drag payload) to its JSON.
    std::function<juce::var(const juce::String&)> snippetProvider;
    // The channel macro menu's Save-preset/Set-default pair; 2nd arg true = set default.
    std::function<void(const juce::String& macroId, bool setAsDefault)> onTrackPresetMenuAction;

    // right-click-any-knob -> "Automate '<Param>'". Set by MainComponent::automateParameter;
    // GraphEditor owns no TimelineDoc, mirroring onSaveSnippetRequested above.
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onAutomateParameterRequested;

    // The node uuids each timeline track starts from (its Track In / Track Audio), in track order. Set by
    // MainComponent; auto-arrange gives every track its own row in this order. Unset (tests, plugin): track
    // source nodes are ordered by node id.
    std::function<std::vector<juce::String>()> trackSourceOrder;

    // ---- MIDI Learn (docs/control/midi-remote-ui.md#the-learn-interaction) ----
    // Set by MainComponent::wireGraphEditorCallbacks(); GraphEditor owns no RemoteEngine/doc.
    std::function<std::map<juce::String, juce::String>(juce::AudioProcessorGraph::NodeID)>
        onQueryMidiMappingsForNode; // mapped paramID -> display label; absent means unmapped
    // The paramIDs of a node that have an automation lane on the timeline (the marker beside their knob); absent means
    // none. Set by MainComponent; polled once per card on the card's gated tick.
    std::function<std::set<juce::String>(juce::AudioProcessorGraph::NodeID)> onQueryAutomatedParamsForNode;
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onMidiLearnRequested;
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onMidiForgetRequested;
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onEditMidiAssignmentRequested;
    // Pushes/clears the armed breathing outline onto the target ModuleComponent, if on screen.
    void setMidiLearnArmed(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    void clearMidiLearnArmed();
    // Repaints every card so its MIDI-mapped badges follow the Preferences switch (a card paints from a cached image).
    void repaintMidiLearnBadges();

    // Hosted-plugin card hooks ("Open Editor", the add-by-moving mode), wired to HostedPluginWindowManager.
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpenPluginEditorRequested;
    std::function<void(juce::AudioProcessorGraph::NodeID, bool)> onPluginAddingControlsChanged;
    void endPluginAddingControls(juce::AudioProcessorGraph::NodeID nodeId);

    // The per-plugin card-layout store hosted cards resolve against. Not owned, may be null, must outlive this editor.
    void setPluginCardLayoutStore(synth::PluginCardLayoutStore* store) noexcept { pluginCardLayoutStore_ = store; }
    synth::PluginCardLayoutStore* getPluginCardLayoutStore() const noexcept { return pluginCardLayoutStore_; }

    // ---- "Replace with..." keeps a module's MIDI Remote mappings (see replaceModule()'s definition) ----

    /** Fires from inside replaceModule(), after the new node exists and the old node (and its
     *  uuid) are gone, with the OLD node's uuid and the NEW node's id. May be null. */
    std::function<void(const juce::String& oldNodeUuid, juce::AudioProcessorGraph::NodeID newNodeId)> onModuleReplaced;

    /** Fires as the postRestore of the combined undo/redo this doc participates in (see
     *  setMidiRemoteProjectDocForUndo) -- never for the initial replace itself. May be null. */
    std::function<void()> onMidiRemoteDocRestored;
    std::function<std::vector<synth::PluginIdentity>()> installedPluginsProvider; // "Replace with..." plugin rows

    /** Non-owning; null (the default, and every headless test) means replaceModule() falls back to
     *  its plain graph-only undo step and onModuleReplaced/onMidiRemoteDocRestored never fire. Must
     *  outlive this editor. */
    void setMidiRemoteProjectDocForUndo(synth::MidiRemoteProjectDoc* doc) noexcept { midiRemoteDocForUndo_ = doc; }

    // ---- Copy / paste / duplicate (snippet pipeline, docs/layout/snippets-clipboard.md) ----
    bool copySelection();

    bool canPaste() const { return !clipboard.isEmpty(); }
    int getClipboardModuleCount() const { return clipboard.getModuleCount(); }

    bool pasteClipboard();
    bool pasteClipboardAt(juce::Point<int> canvasPos);
    bool duplicateSelection();

    void setAlignmentGuidesEnabled(bool enabled) { alignmentGuidesEnabled = enabled; }
    bool getAlignmentGuidesEnabled() const { return alignmentGuidesEnabled; }

    // Double-click a connected jack to disconnect. On by default.
    void setDoubleClickPortDisconnectEnabled(bool enabled) { doubleClickPortDisconnectEnabled = enabled; }
    bool getDoubleClickPortDisconnectEnabled() const noexcept { return doubleClickPortDisconnectEnabled; }

    // Auto-create a macro port when a dragged cable crosses a macro boundary (docs/macros/auto-ports.md).
    void setAutoCreateMacroPortsOnDragEnabled(bool enabled) { autoCreateMacroPortsOnDragEnabled = enabled; }
    bool getAutoCreateMacroPortsOnDragEnabled() const noexcept { return autoCreateMacroPortsOnDragEnabled; }

    // Auto-create a mixer channel on a qualifying MIDI connect (docs/mixer/mixer.md).
    void setAutoCreateChannelOnConnectEnabled(bool enabled) { autoCreateChannelOnConnectEnabled = enabled; }
    bool getAutoCreateChannelOnConnectEnabled() const noexcept { return autoCreateChannelOnConnectEnabled; }

    void createChannelsForUnchanneledTracks(const std::vector<juce::AudioProcessorGraph::NodeID>& trackSourceNodeIds);

    // ---- "Make channel" / "Duplicate into this channel" (docs/mixer/mixer.md#make-channel-and-shared-modules) ----
    /** "Make channel" for the chain starting at `source`; boxes it into a new collapsed macro. */
    bool makeChannelFromNode(juce::AudioProcessorGraph::NodeID source, const juce::String& channelName);
    bool nodeNeedsChannel(juce::AudioProcessorGraph::NodeID source) const;
    /** Moves a channel strip's Master-bound edges behind its track macro's output port, split into two jacks when the
     *  "Split Left/Right jacks" preference is on. No undo of its own: call inside the caller's undo transaction,
     *  before updateComponents(). */
    void routeChannelOutputThroughMacroPort(const juce::String& stripUuid);
    bool isChannelMacroForTrack(const juce::String& memberUuid) const;
    juce::AudioProcessorGraph::NodeID channelSourceForSelection() const;
    void requestMakeChannel(juce::AudioProcessorGraph::NodeID source);
    std::function<void(juce::AudioProcessorGraph::NodeID)> onMakeChannelRequested;
    void addMakeChannelMenuItem(juce::PopupMenu& menu);
    std::vector<juce::String> duplicateIntoChannelTargets(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** "Duplicate into this channel": copies `nodeId` into `macroId`, re-creating its cables. */
    bool duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);
    void requestDuplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onDuplicateIntoChannelRequested;
    void addDuplicateIntoChannelMenuItems(juce::PopupMenu& menu, juce::AudioProcessorGraph::NodeID nodeId);

    /** Test seam: a right-click on empty canvas hands its menu here instead of showing it. */
    void setShowCanvasContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook);

    // Auto-delete a macro port once its last cable is removed (docs/macros/auto-ports.md).
    void setAutoDeleteMacroPortsOnLastCableEnabled(bool enabled) { autoDeleteMacroPortsOnLastCableEnabled = enabled; }
    bool getAutoDeleteMacroPortsOnLastCableEnabled() const noexcept override {
        return autoDeleteMacroPortsOnLastCableEnabled;
    }

    // A manual macro-port delete drops the cable by default; when on, it splices it back together
    // instead (docs/macros/auto-ports.md#a-port-node-is-directly-deletable).
    void setSpliceCableOnMacroPortDeleteEnabled(bool enabled) { spliceCableOnMacroPortDeleteEnabled = enabled; }
    bool getSpliceCableOnMacroPortDeleteEnabled() const noexcept override {
        return spliceCableOnMacroPortDeleteEnabled;
    }
    void setReconnectChainOnDeleteEnabled(bool enabled) { reconnectChainOnDeleteEnabled = enabled; }
    bool getReconnectChainOnDeleteEnabled() const noexcept { return reconnectChainOnDeleteEnabled; }

    // Default jack layout for new Dual I/O modules: false = one collapsed "Audio" jack, true = split L/R.
    void setDefaultDualIOForNewModules(bool enabled) { defaultDualIOForNewModules = enabled; }
    void applyDualIODefaultTo(juce::AudioProcessor& processor, const juce::String& moduleType) const {
        applyDefaultDualIOForNewModule(processor, moduleType);
    }

    /** Re-lays every stereo-capable module already on the canvas to `dual`. */
    void applyDualIOToExistingModules(bool dual);

    /** Unhooks a collapsed split-block module's hidden right leg, re-pointing its cables. */
    void dropHiddenRightLegConnections(juce::AudioProcessorGraph::NodeID nodeId);
    static int rightAudioLegOf(juce::AudioProcessor* proc, bool asInput);
    static bool audioChannelReachableFromJack(const ModuleBase& mb, int rawChannel, bool isInput);
    bool getDefaultDualIOForNewModules() const noexcept { return defaultDualIOForNewModules; }

    /** Per-module-type overrides of the default above (keyed by type name); NEW modules only. */
    void setDualIOPerModuleOverrides(std::map<juce::String, bool> overrides);
    const std::map<juce::String, bool>& getDualIOPerModuleOverrides() const noexcept {
        return dualIOPerModuleOverrides;
    }

    // ---- Custom module titles (node property "displayName", see GraphEditorModuleTitles.cpp) ----
    /** The user's custom title for a node, or an empty string when it has none. */
    juce::String getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const;
    void setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name);

    juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                juce::AudioProcessor* processor) const override;

    /** Commits and closes any open inline title editor, on any card. */
    void commitAnyOpenTitleRename();
    bool isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const;

    // ---- Smart connections (owned by SmartConnectionEngine) ----
    using SmartConnectionMode = SmartConnectionEngine::SmartConnectionMode;
    using SmartSuggestion = SmartConnectionEngine::SmartSuggestion;

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack, juce::AudioProcessorGraph::NodeID dstId,
                      int dstJack, bool isMidi, bool recordUndo = true) override;
    bool nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const;

    // ---- Modulators added from a timeline lane (GraphEditorModulators.cpp) ----
    /** The raw CV channel that drives `paramId` on `nodeId`'s module; -1 when it has none. */
    int modulationChannelFor(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) const;
    /** An LFO beside `targetId`, cabled into `paramId`'s CV jack, as ONE undo step; invalid id when it can't. */
    juce::AudioProcessorGraph::NodeID addLfoModulator(juce::AudioProcessorGraph::NodeID targetId,
                                                      const juce::String& paramId);
    /** Cables the EXISTING LFO `lfoId` into `paramId`'s CV jack on `targetId`, as ONE undo step: no new card, a
     *  routing at the default depth, and macro ports minted or reused when the two sit in different macros.
     *  False (nothing changed) when either node, the jack or the LFO is missing, or the LFO already drives it. */
    bool connectExistingLfoModulator(juce::AudioProcessorGraph::NodeID lfoId,
                                     juce::AudioProcessorGraph::NodeID targetId, const juce::String& paramId);
    /** A new module of factory type `typeName` beside `targetId`, cabled from its output `sourceChannel` into raw CV
     *  channel `destChannel` at `depth`, as ONE undo step (the card, its place, the cable, its depth and any macro
     *  join). Returns the new routing's hidden attenuverter; invalid (nothing changed) when it can't. */
    juce::AudioProcessorGraph::NodeID addModulationSourceModule(const juce::String& typeName, int sourceChannel,
                                                                juce::AudioProcessorGraph::NodeID targetId,
                                                                int destChannel, float depth);
    /** Cables any modulation source (`sourceChannel` is the raw output channel a ModSourceItem names) into raw CV
     *  channel `destChannel` of `targetId` at `depth`, through the same macro-port seam, as ONE undo step. Returns
     *  the new routing's hidden attenuverter; invalid (nothing changed) when it can't or the pair is already wired.
     *  `recordUndo` false leaves the undo step to the caller (the timeline's Change source...). */
    juce::AudioProcessorGraph::NodeID connectModulationSource(juce::AudioProcessorGraph::NodeID sourceId,
                                                              int sourceChannel,
                                                              juce::AudioProcessorGraph::NodeID targetId,
                                                              int destChannel, float depth, bool recordUndo = true);
    /** Removes `routing` (and its source when `removeLonelySource` and no cable is left on it) as ONE undo step,
     *  or, with `recordUndo` false, as a plain edit for a caller whose own undo step already surrounds it. */
    void removeModulator(const ModulationRouting& routing, bool removeLonelySource, bool recordUndo = true);
    /** Removes the whole chain through hidden attenuverter `attenId` (ports it crossed included) as ONE undo step. */
    void removeModulationChain(juce::AudioProcessorGraph::NodeID attenId);
    /** The macro ports the chain through `attenId` crosses, read before a cut; empty when it crosses none. */
    std::vector<juce::AudioProcessorGraph::NodeID> modulationChainPorts(juce::AudioProcessorGraph::NodeID attenId);
    /** After a cable cut inside the caller's undo step: drops `touched` ports left cableless or one-sided (with the
     *  modulation hanging off one), then sweeps `chainPorts` (read before the cut). */
    void pruneMacroPortsAfterCut(const std::vector<juce::AudioProcessorGraph::NodeID>& touched,
                                 std::vector<juce::AudioProcessorGraph::NodeID> chainPorts);
    /** The resolved colour of a modulation wire from `sourceId`. */
    juce::Colour modulationWireColour(juce::AudioProcessorGraph::NodeID sourceId) const;
    /** Test seam: runs just the drag tick's modifier re-sample (no real 30 Hz timer needed). */
    void pumpDragModifierTickForTests();

    /** Test seam: exposes the private GraphCanvasHost base. Production code never calls this. */
    GraphCanvasHost& getCanvasHostForTest();

    static juce::Point<int> estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                               bool isInput, bool isMidi);

    // ---- Onboarding helpers (headless-testable) ----
    /** True when the canvas has no modules. nodeCount is the number of non-Attenuverter nodes
     *  rendered as ModuleComponents. */
    static bool isCanvasEmpty(int nodeCount) noexcept { return nodeCount <= 0; }

    /** The final snapped + anti-overlapped position for a newly dropped module. */
    static juce::Point<int> computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                     const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                     synth::LayoutUtil::NodeID selfId);

    /** Fires at the end of every updateComponents() (the module set may have changed). Owners use
     *  it to refresh UI that depends on patch contents. */
    std::function<void()> onGraphStructureChanged;
    static bool isSingletonIOModule(const juce::String& typeName);
    static bool graphHasModuleNamed(juce::AudioProcessorGraph& graph, const juce::String& typeName);
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

    /** Creates `name` at a canvas position, snapped and anti-overlapped, with undo recorded. A
     *  non-empty `joinMacroId` also adds it to that macro inside the same undo step. */
    void addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                   const std::function<void(juce::AudioProcessor&)>& configure,
                                   const juce::String& joinMacroId = {}) override;

    /** Creates a Hosted Plugin node already pointed at `identity`. */
    void addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos) override;
    juce::Point<int> getViewportCentreInCanvasSpace() const;

    // Mouse Overrides
    /** Wheel / two-finger swipe PANS the canvas (a mouse notch eases in); Cmd/Ctrl+wheel zooms at the cursor. */
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    /** Trackpad pinch zooms around the pinch point. */
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    /** With exactly one module card selected, opens that card's right-click menu at the card;
     *  otherwise the canvas (background) menu at the middle of the view. False while keyboard focus
     *  is inside the Mod Matrix, which has no menu of its own. */
    bool showContextMenuForKeyboardFocus() override;
    juce::AudioProcessorGraph::NodeID getAttenuverterNodeAt(juce::Point<float> localPos);

    // ---- Cables: one wire as the USER sees it, not a graph edge (see GraphEditorTypes.h) ----
    using CableId = graph_editor_types::CableId;
    using VisibleCable = graph_editor_types::VisibleCable;

    /** Enumerates the memoized visible-cable list feeding both paint() and hit-testing. */
    const std::vector<VisibleCable>& buildVisibleCables();
    static juce::Path buildCablePath(juce::Point<float> p1, juce::Point<float> p2);
    static float distanceToCable(const VisibleCable& cable, juce::Point<float> canvasPos);
    std::optional<VisibleCable> getCableAt(juce::Point<float> canvasPos, float tolerance = kCableHitTolerance);

    /** Click tolerance in canvas px. Wider than the wire itself so thin cables stay grabbable. */
    static constexpr float kCableHitTolerance = 7.0f;
    void disconnectCable(const VisibleCable& cable);

    /** Cable colouring config, owned/persisted by MainComponent / AppearanceSettingsTab. */
    void setCableColourMode(synth::ui::CableColourMode mode);
    synth::ui::CableColourMode getCableColourMode() const noexcept { return cableColourMode; }
    void setCableColourOverrides(const synth::ui::CableColourOverrides& overrides);
    const synth::ui::CableColourOverrides& getCableColourOverrides() const noexcept { return cableColourOverrides; }

    juce::Colour colourForCable(const VisibleCable& cable) const;
    void rememberWavetableFolder(const juce::File& folder);
    juce::File getLastWavetableFolder() const noexcept { return lastWavetableFolder; }
    std::function<void(const juce::File&)> onWavetableFolderChanged;

    /** A card's content changed in a way that can move a cable landing (e.g. a Wavetable page
     *  switch); ModuleComponent's public seam into the private repaintCanvas(). */
    void notifyModuleContentChanged() { repaintCanvas(); }

    // The modulation target (destination node + RAW channel) correlated with a hover in either
    // direction, cable -> knob or knob -> cable (docs/modules/modulation.md#modulation-rings-on-knobs).
    using HoveredModTarget = graph_editor_types::HoveredModTarget;
    const std::optional<HoveredModTarget>& getHoveredModTarget() const noexcept { return hoveredModTarget_; }
    /** Set/cleared by a card knob's hover; repaints only the affected card(s), and only on change. */
    void setHoveredModTarget(std::optional<HoveredModTarget> target);
    /** True when the cable itself is under the mouse, or it lands on a knob whose ring is hovered on
     *  the card (the other direction of the cable <-> ring correlation). */
    bool isCableHovered(const VisibleCable& cable) const;

    // Test accessors.
    int getVisibleCableCount() { return (int)buildVisibleCables().size(); }
    bool hasHoveredCable() const noexcept { return hoveredCableId.has_value(); }
    int getCableRebuildCountForTest() const noexcept { return cableRebuildCount; }
    juce::Rectangle<int> getLastTickRepaintAreaForTest() const; // canvas coordinates

    // ---- Zoom gesture (raster freeze) test seams ----
    bool isZoomGestureActive() const noexcept { return zoomGestureActive; }
    // Ends the zoom gesture now, as the settle timer would.
    void settleZoomNowForTest();

    // ---- Macro-crossing animation test seams (MacroCrossingAnimator.h) -----------
    /** True while the finalizeMacroMembershipDrag cable-slide/flash tween is in flight. */
    bool isMacroCrossingAnimLiveForTest() const noexcept { return macroCrossingAnim_.isLive(); }
    /** Advances the tween to `t` (0..1) with no VBlank, as the real driver's onUpdate does. */
    void advanceMacroCrossingAnimForTest(float t);
    /** Lands the tween at its final state, same as the real driver's onComplete. */
    void finishMacroCrossingAnimForTest();
    /** Lands every macro border glide (MacroHullGlide.h), as the real driver's onComplete does. */
    void finishHullGlideForTest();
    bool isHullGlideLiveForTest() const noexcept { return hullGlide_.isLive(); }
    // ---- Cable-retract test seams (CableRetractAnimator.h) ----
    const CableRetractAnimator& getCableRetractForTest() const noexcept { return cableRetract_; }
    void advanceCableRetractForTest(float t);
    void finishCableRetractForTest();

    /** The visible canvas frame rect (canvas coordinates, animated) and its retarget hook (the canvas frame). */
    juce::Rectangle<float> getCanvasFrameRect() const { return canvasFrame_.current(); }
    void refreshCanvasFrame(CanvasFrame::Mode mode);
    CanvasFrame& getCanvasFrameForTest() noexcept { return canvasFrame_; }

    AppUndoManager* getUndoManager() const noexcept { return undoManager; } // null for a bare editor
    CardGlideAnimator& getCardGlide() noexcept { return cardGlide_; } // AppUndoManager opens a Scope on it per undo
    LoadRevealAnimator& getLoadReveal(); // the project-open reveal and edit block (GraphEditorLoadReveal.cpp)

    // ---- Card-glide test seams (CardGlideAnimator.h) ----
    CardGlideAnimator& getCardGlideForTest() noexcept { return cardGlide_; }
    /** Advances the glide to `t` (0..1) with no VBlank. */
    void advanceCardGlideForTest(float t);
    /** Lands the glide, restoring every hidden card. */
    void finishCardGlideForTest();
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    class GraphContentComponent : public juce::Component {
    public:
        GraphContentComponent(GraphEditor& editor);
        void paint(juce::Graphics& g) override;
        void paintOverChildren(juce::Graphics& g) override;
        void resized() override;
        void childBoundsChanged(juce::Component* child) override; // a card moved or resized

        juce::OwnedArray<ModuleComponent>& getModules() { return moduleComponents; }
        juce::OwnedArray<MacroCardComponent>& getMacroCards() { return macroCardComponents; }

        float connectionAnimPhase = 0.0f;

    private:
        GraphEditor& editor;
        juce::OwnedArray<ModuleComponent> moduleComponents;
        juce::OwnedArray<MacroCardComponent> macroCardComponents;
    };
    AudioEngine& audioEngine;
    // See setOutputDeviceInfoProvider/refreshOutputDeviceInfo above.
    std::function<juce::String()> outputDeviceInfoProvider;
    GraphContentComponent content;
    ModMatrixComponent modMatrix;
    bool isMatrixVisible = false;

    // ---- Minimap ----
    synth::ui::MinimapComponent minimap;
    // User preference, independent of resized()'s auto-hide-when-tiny effective visibility.
    bool minimapVisible = true;
    // Its show/hide ease (docs/layout/animation.md): the visible fraction and the one driver moving it.
    synth::ui::PanelSlide minimapSlide_;
    synth::ui::AnimationDriver minimapAnim_;
    int minimapSlideDistance_ = 0; // px it travels from its corner; 0 under Reduce Motion
    bool minimapAnimateOffScreen_ = false;
    void layoutMinimap();
    void finishMinimapSlide();

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
    void maybeShowModDropHint(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi);
    // A new modulator's card: estimated, created, then re-resolved at its real size (GraphEditorModulators.cpp).
    void placeNewModulator(juce::AudioProcessorGraph::Node& node, juce::AudioProcessorGraph::NodeID targetId,
                           const juce::String& typeName);
    void fireBeforeDetachAllModuleComponents();
    void refreshSmartSuggestions() override;
    void clearSmartSuggestions() override;
    void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor, const juce::String& moduleType) const override;

    // ---- GraphCanvasHost (private: only code holding a GraphCanvasHost& can call these) ----
    juce::AudioProcessorGraph& graph() override;
    AudioEngine& engine() override { return audioEngine; }
    ModuleComponent* moduleComponentFor(juce::AudioProcessorGraph::NodeID nodeId) override;
    juce::OwnedArray<ModuleComponent>& modules() override { return content.getModules(); }
    AppUndoManager* undo() override { return undoManager; }
    // Only the GraphCanvasHost pure virtuals no public method already satisfies are declared here.
    juce::OwnedArray<MacroCardComponent>& macroCards() override { return content.getMacroCards(); }
    CardGlideAnimator& cardGlide() override { return cardGlide_; }
    void reportStatusMessage(const juce::String& message) override;
    void clearModMatrixRows() override { modMatrix.clearRows(); }
    void requestRepaint() override { repaint(); }
    juce::LookAndFeel& lookAndFeel() override { return getLookAndFeel(); }
    void seedInsertModifierSample() override;
    /** Runs a suggestion refresh and, when it newly shows preview cables, fades them in. */
    void refreshSmartSuggestionsWithReveal(const std::function<void()>& refresh);
    /** The 30 Hz drag tick's re-sample of the Ctrl modifier (no mouse move needed). */
    void refreshSmartSuggestionsForModifierChange();
    juce::Point<int> canvasPositionOfLocalPoint(juce::Point<int> pointOnHost) const override;
    juce::Point<int> estimateModuleSizeForType(const juce::String& typeName) const override;
    juce::var resolveSnippetPayload(const juce::String& name) const override;
    SmartConnectionEngine smartConnections_{*this};
    MacroGroupController macroController_{*this};
    GraphDragDropController dragDropController_{*this};
    std::unique_ptr<synth::ui::ModDotController> modDot_;       // the mod dot's drag, tooltip and last-chosen source
    std::unique_ptr<synth::ui::PortPanelController> portPanel_; // the jack panel and its cable highlight
    // The open picker's armed preview: node + WEAK handles, never raw -- see previewMacroPortColour.
    juce::String previewSessionNode_;
    juce::Component::SafePointer<MacroCardComponent> previewSessionCard_;
    juce::Component::SafePointer<ModuleComponent> previewSessionWidget_;
    void endMacroPortPreviewSession(); // forget an armed session; out of line (both types fwd-declared)

    juce::AudioProcessorGraph::NodeID draggingAttenuverterNodeId;
    float attenDragStartValue = 0.0f;

    // ---- Cable hover / colouring state ----
    // Only the ID is kept between frames: the geometry is rebuilt each paint anyway, and holding
    // a stale VisibleCable across a graph edit would dangle conceptually (ports move, nodes go).
    std::optional<CableId> hoveredCableId;
    synth::ui::CableColourMode cableColourMode = synth::ui::CableColourMode::BySignalType;
    synth::ui::CableColourOverrides cableColourOverrides;
    juce::File lastWavetableFolder;

    // ---- Selection state ----
    synth::ui::SelectionModel selection;

    // Copy/paste payload. In-app and in-memory only: it is never written to disk and never touches
    // the system clipboard, so Cmd+C on the canvas cannot silently destroy the user's copied text.
    synth::ui::ModuleClipboard clipboard;

    /** Inserts a clipboard-dialect payload at a canvas position, carrying module state through. */
    bool insertClipboardPayload(const juce::var& payload, juce::Point<int> canvasPos);
    /** `screenAnchor` places the real menu there instead of at the mouse. */
    void showCanvasContextMenu(juce::Point<int> canvasPos, std::optional<juce::Rectangle<int>> screenAnchor = {});
    // See setShowCanvasContextMenuHookForTest. Null = show the real async menu.
    std::function<void(juce::PopupMenu&)> showCanvasContextMenuHook_;

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

    // The macros a live Cmd-drag would LEAVE and JOIN if released now, empty for none.
    juce::String macroDragLeaveId_;
    juce::String macroDragJoinId_;
    // Set once a drag has applied a membership change as it crossed; the macros as they were at press.
    bool liveMembershipChanged_ = false;
    juce::var macrosBeforeLiveDrag_;
    juce::String liveMembershipPressOwner_; // the macro the module sat directly in at press, empty for none
    bool liveMembershipAway_ = false;       // the module now sits somewhere other than at press
    // The module that drag is moving; set/cleared together with the two ids above.
    juce::AudioProcessorGraph::NodeID macroDragDraggedNodeId_;

    // True while a click on empty canvas has not yet turned into a pan or marquee drag; a mouseUp
    // in that state is a plain click and clears the selection.
    bool pendingEmptyCanvasClick = false;

    // ---- Expanded-macro chip drag: macroChipDragId is non-empty for the gesture; the start point is CANVAS-space ----
    juce::String macroChipDragId;
    juce::Point<int> macroChipDragStartCanvasPos;
    /** True while the chip's DraggingHandCursor is set, so it resets on the way out. */
    bool hoveringMacroChip = false;
    std::vector<synth::LayoutUtil::Box> collectModuleBoxes(bool selectedOnly, bool excludeSelected) const;
    juce::Point<int> estimateSnippetSize(const juce::String& payload) const override;
    void applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection) override;

    // replaceModule()'s undo-recording step (graph-only, or combined with the MIDI Remote doc).
    void recordReplaceModuleUndo(juce::AudioProcessorGraph& graph, const std::function<void()>& doReplace);
    AppUndoManager* undoManager = nullptr;
    std::unique_ptr<CanvasCardKeyboard> cardKeyboard_; // never null
    synth::PluginCardLayoutStore* pluginCardLayoutStore_ = nullptr;
    synth::MidiRemoteProjectDoc* midiRemoteDocForUndo_ = nullptr; // see setMidiRemoteProjectDocForUndo

    // See setPropertiesFile.
    juce::PropertiesFile* propertiesFile_ = nullptr;

    // Unknown top-level JSON keys stashed on load and re-merged on save; only savePreset/loadPreset
    // touch it (newPatch() clears it).
    synth::PatchDocument patchDocument;

    // Live macro grouping state for the patch; newPatch() clears it, same lifecycle as patchDocument.
    synth::MacroSet macros;
    void syncMacroCards() override;
    std::unique_ptr<synth::ui::ColourPickerPopup> buildMacroColourPicker(const juce::String& macroId);

    // ---- Auto-create-channel-on-connect (docs/mixer/mixer.md#channels-follow-audio-not-tracks) ----
    bool nodeIsTimelineMidiSource(juce::AudioProcessorGraph::NodeID nodeId) const;
    void maybeAutoCreateChannelAfterConnect(juce::AudioProcessorGraph::NodeID searchFrom);
    void showMacroAutoPortModal(std::function<void(bool createPorts, bool remember)> respond);
    MacroAutoPortPreference macroAutoPortPreference_ = MacroAutoPortPreference::Unset;
    std::vector<ModulationDisplayInfo> cachedModDisplayInfo;
    std::vector<ModulationRouting> cachedModRoutings;

    // ---- Animation members ----
    // Drop-landing tween; both must be members so they outlive the VBlank frame callbacks.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    CanvasFrame canvasFrame_{vblankUpdater}; // growing patch frame (CanvasFrame.h)
    CanvasEdgeDrag edgeDrag_;                // a drag held at the canvas origin (CanvasEdgeDrag.h)
    synth::ui::AnimationDriver dropLandingAnim;
    // Smart-connection preview cables fade in (0..1) when they first appear instead of popping.
    float smartPreviewReveal_ = 1.0f;
    synth::ui::AnimationDriver smartPreviewRevealAnim_;

    // Mod-matrix panel ease; modMatrixTargetBounds is the final position set on complete.
    synth::ui::AnimationDriver modMatrixAnim;
    juce::Rectangle<int> modMatrixTargetBounds;

    // Macro-crossing cable slide + module flash: tween state (MacroCrossingAnimator.h) plus its driver.
    MacroCrossingAnimator macroCrossingAnim_;
    synth::ui::AnimationDriver macroCrossingDriverAnim_;

    // Paint-only slide of cards a make-room / return / auto-arrange moved (CardGlideAnimator.h).
    CardGlideAnimator cardGlide_;
    std::unique_ptr<LoadRevealAnimator> loadReveal_; // built on first use

    // Removed cables retracting into their source jack (CableRetractAnimator.h) plus its driver.
    CableRetractAnimator cableRetract_;
    synth::ui::AnimationDriver cableRetractDriverAnim_;

    // Expanded macro borders gliding to new bounds (MacroHullGlide.h) plus its driver.
    MacroHullGlide hullGlide_;
    synth::ui::AnimationDriver hullGlideDriverAnim_;

    // ---- Alignment guides (drag-preview feedback) ----
    using AlignmentGuide = graph_editor_types::AlignmentGuide;
    std::vector<AlignmentGuide> alignmentGuides;
    bool alignmentGuidesEnabled = true;
    bool doubleClickPortDisconnectEnabled = true;
    bool autoCreateMacroPortsOnDragEnabled = true;
    bool autoDeleteMacroPortsOnLastCableEnabled = true;
    bool spliceCableOnMacroPortDeleteEnabled = false; // off by default — a manual delete drops the cable
    bool macroDragWithoutCmdEnabled = true;
    bool moveMacroOnHullDragEnabled = false; // off by default — hull drag pans
    bool autoCreateChannelOnConnectEnabled = true;
    bool reconnectChainOnDeleteEnabled = true; // see the getter/setter's doc comment
    bool defaultDualIOForNewModules = false;
    std::map<juce::String, bool> dualIOPerModuleOverrides;
    void updateTransform();
    void applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor);
    // Wheel-pan tween (a mouse notch eases ~120 ms; trackpad events apply at once); axis 0 = x, 1 = y of panOffset.
    synth::ui::ScrollTweenRunner wheelPanTween_;
    void panByWheel(int axis, float amountPx, bool eased);
    void configureCardGlide();
    void noteCardExits(const std::vector<juce::AudioProcessorGraph::NodeID>& ids);
    void configureCanvasFrame();
    void applyContentBounds();
    void beginCanvasEdgeDrag();
    void slidePatchForEdgeDrop();
    void animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos);

    // Arms macroCrossingAnim_ from a pre-splice cable snapshot the caller takes (see its definition).
    void armMacroCrossingAnimation(const std::vector<VisibleCable>& cablesBeforeSplice, uint32_t crossingNodeUid,
                                   juce::Rectangle<int> flashBounds);
    // Slides the port-side end of every cable a cable drop created from `dropPoint` (canvas coordinates) to its anchor.
    void armMacroPortSlide(const std::vector<VisibleCable>& cablesBeforeDrop, juce::Point<float> dropPoint);
    void startMacroCrossingDriver();
    juce::Rectangle<int> macroHullTargetBounds(const juce::String& macroId) const;
    bool canApplyMembershipLive(juce::AudioProcessorGraph::NodeID draggedNodeId, const juce::String& leaveId) const;
    void applyMembershipLive(juce::AudioProcessorGraph::NodeID draggedNodeId, const juce::String& leaveId,
                             const juce::String& joinId);
    void recordUnfinishedLiveMembershipChange();
    juce::String currentOwnerOfDraggedModule() const;
    void revertLiveMembership();

    // ---- Cable memo (perf) ----
    std::vector<VisibleCable> rebuildVisibleCables();
    std::vector<VisibleCable> cablesCache;
    bool cablesCacheValid = false;
    bool updatingComponents = false; // guards updateComponents() against re-entry
    int cableRebuildCount = 0;       // test seam, see docs/layout/animation.md#the-paint-count-pattern
    void repaintCanvas() override;
    std::unique_ptr<graph_editor_paint::CanvasMemo> canvasMemo_; // never null
    friend class graph_editor_paint::CanvasMemo;
    friend class synth::ui::PortConnector;

    // ---- Knob-anchored cables + hover correlation (GraphEditorModHover.cpp) ----
    // Post-passes of rebuildVisibleCables(); the knob re-anchor MUST run before the collapsed-macro pass.
    void reanchorCablesToKnobTargets(std::vector<VisibleCable>& cables);
    void reanchorCablesAroundCollapsedMacros(std::vector<VisibleCable>& cables);
    ModuleComponent* moduleComponentForNode(juce::AudioProcessorGraph::NodeID id);
    std::optional<HoveredModTarget> hoveredModTarget_;

    // ---- Zoom gesture (raster freeze; docs/layout/animation.md#the-time-bounded-animation-rule) ----
    // Cards' raster scale is pinned until kZoomSettleMs after the last zoom event, then thawed once.
    bool zoomGestureActive = false;
    synth::ui::AnimationDriver zoomSettleAnim;
    static constexpr double kZoomSettleMs = 140.0;
    void beginOrRefreshZoomGesture();
    void endZoomGesture();
    void setModuleRasterFrozen(bool frozen);
    struct HealSplice; // delete heal; defined in GraphEditorInternal.h
    std::vector<HealSplice> captureHealSplices(const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const;
    void healDeletedChain(const std::vector<HealSplice>& splices);

public:
    const std::vector<ModulationDisplayInfo>& getCachedModDisplayInfo() const { return cachedModDisplayInfo; }
    const std::vector<ModulationRouting>& getCachedModRoutings() const { return cachedModRoutings; }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditor)
};
