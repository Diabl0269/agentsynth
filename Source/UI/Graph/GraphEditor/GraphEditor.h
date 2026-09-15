#pragma once

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Modules/MacroPortShape.h"
#include "PatchDocument.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/CableColour.h"
#include "UI/Graph/GraphCanvasHost.h"
#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleClipboard.h"
#include "UI/Graph/SelectionModel.h"
#include "UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class ModuleComponent;
class MacroCardComponent;
#include "UI/Graph/MinimapComponent.h"
#include "UI/Graph/ModMatrixComponent.h"

class GraphEditor
    : public juce::Component
    , public juce::Timer
    , public juce::DragAndDropTarget
    , public juce::FileDragAndDropTarget
    , public juce::SettableTooltipClient
    , private GraphCanvasHost {
public:
    GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr = nullptr);
    ~GraphEditor() override;

    AudioEngine& getAudioEngine() { return audioEngine; }
    ModMatrixComponent& getModMatrix() { return modMatrix; }
    juce::OwnedArray<ModuleComponent>& getModuleComponents() { return content.getModules(); }
    std::function<void()> onBeforeDetachAllModuleComponents; // fires at the top of detachAllModuleComponents()
    void detachAllModuleComponents();

    void paint(juce::Graphics& g) override;
    /** Draws the empty-canvas onboarding hint centred in the visible, untransformed viewport,
     *  after children paint -- so it draws over the canvas unaffected by the content transform. */
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void timerCallback() override;
    void updateComponents() override; // also GraphCanvasHost::updateComponents()
    void toggleModMatrixVisibility();
    bool isModMatrixVisible() const { return isMatrixVisible; }

    // ---- Minimap (issue #159) ----
    void setMinimapVisible(bool shouldBeVisible);
    void toggleMinimapVisibility();
    bool isMinimapVisible() const noexcept { return minimapVisible; }
    synth::ui::MinimapComponent& getMinimap() noexcept { return minimap; }

    /** The canvas rect currently visible in the editor — the inverse of the content transform
     *  applied to getLocalBounds(). */
    juce::Rectangle<float> getVisibleCanvasRect() const;

    /** Pans so `canvasPoint` sits at the centre of the visible area. Zoom is unchanged. */
    void centreViewOn(juce::Point<float> canvasPoint);

    /** P8-31: scroll + zoom the viewport so every module component is on-screen, clamped to the
     *  same [0.1, 2.0] range as wheel zoom. Called after a patch is loaded so the just-loaded
     *  modules are not left off-screen at their saved coordinates; a no-op when there are no
     *  modules or the editor has no area yet. */
    void fitViewToModules();

    /** Multiplies zoom around the centre of the visible area, clamped to the same [0.1, 2.0]
     *  range as wheel zoom, so the point under the centre stays put. */
    void zoomAroundCentre(float wheelDelta);

    // Test accessor. Non-const because it calls buildVisibleCables(), which is non-const.
    synth::ui::MinimapModel buildMinimapModel();

    // ---- Locate Master (FRO45) ---- See GraphEditorTypes.h for the LocateMasterResult enum and
    // the founder-feedback rationale behind this stopgap (rule 4 of the FRO77 PR3 header trim).
    using LocateMasterResult = graph_editor_types::LocateMasterResult;

    /** True when locateMasterOrOutput() has a node to find — drives the canvas context menu item's
     *  (and the equivalent command's) enabled state, so the two surfaces can never disagree. */
    bool hasLocatableMasterOrOutput() const;

    /** Selects Master, falling back to Audio Output when there is no Master yet, and pans it into
     *  the centre of the view. Graceful no-op (LocateMasterResult::NoTarget) when the patch has
     *  neither node. See GraphEditorCanvas.cpp for the select/pan/minimap-highlight reuse. */
    LocateMasterResult locateMasterOrOutput();

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);

    /** Drops the pending modulation drop-target highlight on every card. */
    void clearModDropTargets();
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);

    // See GraphEditorTypes.h for the PolyLink struct's full field-level doc (rule 4 of the FRO77
    // PR3 header trim).
    using PolyLink = graph_editor_types::PolyLink;

    /** Works out which raw-channel fan a cable dropped between two *visible* jacks should wire.
     *  Port hit-testing yields visible jack indices, which are not raw channel numbers once a
     *  module goes poly (a poly VCA's CV jack is jack 1 but raw channel 8). Pure — no graph access,
     *  headless-testable. See GraphEditorConnections.cpp for the fan-width/mod-CV-broadcast rules. */
    static PolyLink resolvePolyLink(const ModuleBase* source, int sourceVisibleJack, const ModuleBase* dest,
                                    int destVisibleJack);

    /** Re-evaluates every connection touching `module` after its "poly" parameter changed, so the
     *  graph matches the module's new channel layout: mono wires fan out to N voices when both ends
     *  are poly, fans collapse back to one wire when poly is switched off, and wires move to the raw
     *  channels the new layout puts them on.  MIDI connections are left alone. `previousInputMap`/
     *  `previousOutputMap` are the module's raw->LogicalPort maps captured before the change — see
     *  GraphEditorCommands.cpp for why they're needed. Does not record undo state; the caller owns
     *  the surrounding transaction. */
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
    void loadPreset(juce::File file, bool append = false);
    // Loads a factory preset by index. Detaches existing module components (stopping their scope timers)
    // BEFORE the graph is cleared, so no ScopeComponent reads a freed VisualBuffer. Returns true if loaded.
    bool loadFactoryPreset(int index);

    // Clears the canvas to the empty state (New Patch). Detaches module components (stopping scope timers)
    // BEFORE clearing the graph — same safe ordering as loadFactoryPreset. The clear is recorded as an
    // undoable structural change when undoManager is present (Cmd+Z restores the prior patch).
    void newPatch();

    /** The per-loaded-file stash of top-level JSON keys this build doesn't understand (see
     *  `patchDocument` below). Exposed so the app's `.agsproj` save/load path can re-merge the very
     *  same stash a plain `.json` save/load already does — GraphEditor owns no file dialogs, and
     *  MainComponent owns no PatchDocument. */
    synth::PatchDocument& getPatchDocument() noexcept { return patchDocument; }

    /** GraphEditor's live set of Macros for the current patch (see Source/MacroSet.h). Exposed for
     *  the same reason as getPatchDocument() above: the app's project-bundle save/load path (owned
     *  by MainComponent/ProjectBundle) needs to reach it, and GraphEditor owns no file dialogs. */
    synth::MacroSet& getMacros() noexcept override { return macros; }

    // ---- Macros (P8-12, docs/macros.md) — owned by MacroGroupController since FRO77 PR2 --------
    // GraphEditor forwards its own (unchanged) public macro API to macroController_; the nested
    // types below are aliased so `GraphEditor::X` keeps compiling for every existing caller
    // (PreferencesSettingsTab, ModuleComponent, tests) unchanged. Full contracts live on
    // MacroGroupController.h now — see that header for the detailed "why" behind each method.
    using MacroPortOwner = MacroGroupController::MacroPortOwner;
    MacroPortOwner macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const {
        return macroController_.macroPortOwnerFor(nodeId);
    }

    // Layout / anti-overlap
    juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h,
                                      juce::AudioProcessorGraph::NodeID selfId) override;

    /** A free canvas slot at the LEFT edge, below every module currently on the canvas — where the
     *  timeline's add-track flow drops the "Track In" node it creates. Falls back to the canvas
     *  origin on an empty canvas. Anti-overlapped through resolvePlacement like any drop. */
    juce::Point<int> findLeftEdgeSlotBelowModules(int w, int h);
    // A module changed footprint in place (the Macro bank, when its "Knobs" count changes).
    // Drops any routing left on an output jack that is no longer visible, then pushes overlapping
    // neighbours clear. The resized module itself never moves.
    void handleModuleResized(ModuleComponent* moduleComp);

    /** Removes every connection leaving an output jack this node no longer shows. The other half of
     *  the max-channel/visible-port pattern (docs/modules.md): the module silences its hidden
     *  channels, and the owner unplugs them — a jack you cannot see is a jack you cannot unplug. */
    void dropRoutingsOnHiddenJacks(juce::AudioProcessorGraph::NodeID nodeId);

    /** MESSAGE THREAD. The audio device changed: re-point every Audio Input module at the
     *  engine's new input channel count, drop cables left on jacks that just disappeared, and
     *  re-measure the affected cards. Called from the owner's device-state-changed callback. */
    void refreshIoModulesAfterDeviceChange();

    /** Output-card identity treatment (docs/layout.md — module chrome): installs the callback
     *  MainComponent uses to describe where the signal actually goes (device name + sample rate +
     *  channel count, "Host audio" in HostMode::Hosted, or an empty string to hide the line). Set
     *  once; MainComponent already owns the Standalone-vs-Hosted framing (see how
     *  StatusBarComponent's device chrome is built) so GraphEditor/ModuleComponent stay ignorant of
     *  it and just render whatever string comes back. */
    void setOutputDeviceInfoProvider(std::function<juce::String()> provider) {
        outputDeviceInfoProvider = std::move(provider);
    }

    /** MESSAGE THREAD. Calls the provider above (a no-op if none is installed) and pushes the
     *  result into the Audio Output card's ModuleComponent, which repaints only if the text
     *  actually changed. Call once right after installing the provider (so the card is populated
     *  at startup) and again every time AudioEngine::onDeviceStateChanged fires — there is no
     *  timer polling this. */
    void refreshOutputDeviceInfo();

    // Dual I/O only remaps visible jacks onto raw ch0/ch1. A collapsed Audio cable that only
    // landed on the left leg (typical when the far end is Audio Output, which is not ModuleBase)
    // is completed to L→L / R→R so toggling Dual I/O on shows both jacks wired.
    void completeStereoPairConnections(ModuleComponent* moduleComp);
    void finalizeModuleDrag(ModuleComponent* module);
    void autoArrange();

    // ---- Multi-select (issue #156) ------------------------------------------------------
    // See GraphEditorSelection.cpp for the full gesture contract (pan/marquee/click/drag/clear/
    // delete keymap).

    const synth::ui::SelectionModel& getSelection() const override { return selection; }

    /** Selects a single module. When additive, toggles it instead and leaves the rest alone. */
    void selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive);
    void setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) override;
    void clearSelection();
    void selectAllModules();
    bool isNodeSelected(juce::AudioProcessorGraph::NodeID nodeId) const { return selection.contains(nodeId); }
    int getSelectionCount() const { return selection.size(); }
    std::vector<juce::AudioProcessorGraph::NodeID> getSelectedNodes() const { return selection.getSelected(); }

    /** Removes every selected module as ONE undoable change, so Cmd+Z restores the whole group.
     *  Also GraphCanvasHost::deleteSelection() — MacroGroupController's deleteMacroAndMembers/
     *  removeMacroPort (FRO77 PR2) select the nodes to remove, then call this through the host. */
    void deleteSelection() override;

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
    bool isSelectionDragActive() const override { return selectionDragActive; }

    bool isMacroChipDragActive() const { return macroChipDragId.isNotEmpty(); } // FRO19 test accessor
    /** FRO19: cancels a live drag when the component that armed it (ModuleComponent or
     *  MacroCardComponent) is destroyed/detached mid-gesture (see docs/layout_selection_canvas.md §1.4). */
    void cancelLiveDragGestures();

    // ---- Macros (P8-12) ------------------------------------------------------------------
    // See MacroGroupController.h's "Grouping / membership / collapse" section for what a Macro is
    // and the collapsed-macro selection/drag/delete model.

    /** Wraps the current selection in a new macro (Cmd+G). See
     *  MacroGroupController::groupSelectionIntoMacro for the full refusal/auto-port contract. */
    juce::String groupSelectionIntoMacro(bool autoCreatePorts = false) {
        return macroController_.groupSelectionIntoMacro(autoCreatePorts);
    }

    /** Builds a new collapsed macro from an explicit member-uuid list (T173a's addAudioTrack).
     *  NON-RECORDING — see MacroGroupController::addMacroForMembers for the full contract. */
    juce::String addMacroForMembers(const std::vector<juce::String>& memberUuids, const juce::String& name,
                                    juce::Point<int> origin) {
        return macroController_.addMacroForMembers(memberUuids, name, origin);
    }

    // ---- Macro auto-port preference (founder-review fix F5, docs/macros_implementation.md §7 item 6.1/6.2) ----
    // See GraphEditorMacroPrompts.cpp's requestGroupSelectionIntoMacro() for the tri-state/
    // persistence rationale.

    enum class MacroAutoPortPreference { Unset, AutoCreatePorts, LeaveCablesAsIs };

    void setMacroAutoPortPreference(MacroAutoPortPreference pref) noexcept { macroAutoPortPreference_ = pref; }
    MacroAutoPortPreference getMacroAutoPortPreference() const noexcept { return macroAutoPortPreference_; }

    /** True when grouping the CURRENT selection right now would cross at least one graph
     *  connection. See MacroGroupController::selectionHasCrossingMacroCable. */
    bool selectionHasCrossingMacroCable() const { return macroController_.selectionHasCrossingMacroCable(); }

    /** Cmd+G / right-click "Create Macro" / drag-group-into-macro's real entry point. Stays on
     *  GraphEditor (FRO77 PR2) — it shows a juce::Component::SafePointer<GraphEditor>-based async
     *  modal, which needs a genuine GraphEditor&; see MacroGroupController.h's class comment. Also
     *  GraphCanvasHost::requestGroupSelectionIntoMacro() — MacroGroupController::
     *  groupOrToggleSelectionMacros() calls back into this through the host for a selection that
     *  touches no macro yet. */
    void requestGroupSelectionIntoMacro() override;

    /** Test seam: when set, called INSTEAD of launching the real modal — `respond(createPorts,
     *  remember)` drives the completion exactly as a real button click would, with no DialogWindow
     *  or message loop involved. Production code leaves this null. */
    std::function<void(std::function<void(bool createPorts, bool remember)> respond)> macroAutoPortModalForTest;

    /** Ungroups (Cmd+Shift+G). See MacroGroupController::ungroupSelection. */
    void ungroupSelection() { macroController_.ungroupSelection(); }
    /** The controller itself, for the app to install its hooks on (FRO14's macro-rename hook). */
    MacroGroupController& getMacroController() noexcept { return macroController_; }

    /** T138: adds every uuid in `memberUuids` to the EXISTING macro `macroId`. See
     *  MacroGroupController::addSelectionToMacro. */
    void addSelectionToMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
        macroController_.addSelectionToMacro(macroId, memberUuids);
    }

    /** T138: removes every uuid in `memberUuids` from macro `macroId`. See
     *  MacroGroupController::removeSelectionFromMacro. */
    void removeSelectionFromMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids) {
        macroController_.removeSelectionFromMacro(macroId, memberUuids);
    }

    /** `nodeId`'s uuid, resolved to `removeSelectionFromMacro(macro->id, {uuid})`. See
     *  MacroGroupController::removeNodeFromMacro. */
    void removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId) { macroController_.removeNodeFromMacro(nodeId); }

    /** Toggles (Cmd+Alt+G) the collapsed state of every macro that owns at least one
     *  currently-selected node. See MacroGroupController::toggleSelectionMacrosCollapsed for the
     *  deterministic mixed-selection convergence rule. */
    void toggleSelectionMacrosCollapsed() { macroController_.toggleSelectionMacrosCollapsed(); }

    /** Cmd+G's single entry point: toggles the macros the selection touches, or groups the
     *  selection into a new macro when it touches none. See
     *  MacroGroupController::groupOrToggleSelectionMacros. */
    void groupOrToggleSelectionMacros() { macroController_.groupOrToggleSelectionMacros(); }

    /** Selects every member of `macroId`, replacing the current selection unless `additive`. See
     *  MacroGroupController::selectMacro. */
    void selectMacro(const juce::String& macroId, bool additive) { macroController_.selectMacro(macroId, additive); }

    /** True when every member of `macroId` is selected and nothing else is. */
    bool isMacroSelected(const juce::String& macroId) const { return macroController_.isMacroSelected(macroId); }

    /** The macro `nodeId` belongs to, or nullptr if it isn't a member of any. */
    const synth::Macro* macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const {
        return macroController_.macroForNode(nodeId);
    }

    /** Expands or collapses a macro. See MacroGroupController::setMacroCollapsed. */
    void setMacroCollapsed(const juce::String& macroId, bool collapsed) {
        macroController_.setMacroCollapsed(macroId, collapsed);
    }

    void renameMacro(const juce::String& macroId, const juce::String& newName) {
        macroController_.renameMacro(macroId, newName);
    }
    void setMacroColour(const juce::String& macroId, juce::Colour colour) {
        macroController_.setMacroColour(macroId, colour);
    }

    /** Async rename affordance that does NOT depend on a MacroCardComponent existing — used by the
     *  expanded-macro hull's right-click menu (buildMacroMenu's default "Rename..." handler),
     *  where there is no card to host an inline `TextEditor`. Prefilled with the macro's current
     *  name; empty/whitespace-only input cancels without renaming. See GraphEditorMacroPrompts.cpp
     *  for the AlertWindow-idiom rationale. */
    void promptRenameMacro(const juce::String& macroId);

    /** Test seam: when set, called INSTEAD of promptRenameMacro's real juce::AlertWindow — a real
     *  AlertWindow segfaults on a headless Linux CI runner with no display (same class of issue
     *  ModuleComponent::setShowContextMenuHookForTest's own comment documents for PopupMenu, and
     *  macroAutoPortModalForTest above already works around for the auto-port prompt). Production
     *  code leaves this null. */
    std::function<void(const juce::String& macroId)> promptRenameMacroForTest;

    /** Opens the shared synth::ui::ColourPickerPopup over `screenArea` (screen coordinates) for
     *  `macroId`. A no-op if `macroId` doesn't resolve. See GraphEditorMacroPrompts.cpp's
     *  buildMacroColourPicker for the live-preview/undo contract this launches. */
    void promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea);

    /** Where the recolour picker's favourites shelf persists to. Null (the default) means
     *  in-memory-only favourites, which is what a headless test with no ApplicationProperties
     *  gets — mirrors TimelineRulerComponent::setPropertiesFile exactly. */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    /** Removes the macro AND every one of its member nodes, as one undo step. See
     *  MacroGroupController::deleteMacroAndMembers. */
    void deleteMacroAndMembers(const juce::String& macroId) { macroController_.deleteMacroAndMembers(macroId); }

    // ---- Macro bypass/mute (P8-15d, T142, docs/macros_ports.md §5.6) -------------------------------
    //
    // "Bypass macro" / "Mute macro" are FAN-OUT COMMANDS over a macro's members, not a
    // macro-level reinterpretation of the contract — a macro has no processBlock and no
    // bypass/mute state of its own. See MacroGroupController.h for the full fan-out contract.

    using MacroToggleState = MacroGroupController::MacroToggleState;
    MacroToggleState macroBypassState(const juce::String& macroId) const {
        return macroController_.macroBypassState(macroId);
    }
    MacroToggleState macroMuteState(const juce::String& macroId) const {
        return macroController_.macroMuteState(macroId);
    }

    void setMacroBypassed(const juce::String& macroId, bool bypassed) {
        macroController_.setMacroBypassed(macroId, bypassed);
    }
    void setMacroMuted(const juce::String& macroId, bool muted) { macroController_.setMacroMuted(macroId, muted); }
    void toggleMacroBypassed(const juce::String& macroId) { macroController_.toggleMacroBypassed(macroId); }
    void toggleMacroMuted(const juce::String& macroId) { macroController_.toggleMacroMuted(macroId); }

    /** Canvas-space bounds of an EXPANDED macro's grouping hull. See
     *  MacroGroupController::macroHullBounds for the full port-exclusion/fallback contract. */
    juce::Rectangle<int> macroHullBounds(const juce::String& macroId) const {
        return macroController_.macroHullBounds(macroId);
    }

    /** The expanded macro whose hull contains `canvasPos`, or an empty string. Smallest hull
     *  wins when hulls overlap. */
    juce::String macroHullAt(juce::Point<int> canvasPos) const { return macroController_.macroHullAt(canvasPos); }

    /** Canvas-space bounds of an EXPANDED macro's name chip - the tab drawn on the hull's top
     *  edge, which doubles as the macro's drag handle. Empty rect if the macro is collapsed or
     *  unknown. The ONE definition: paint and hit-testing must never diverge. */
    juce::Rectangle<int> macroChipBounds(const juce::String& macroId) const {
        return macroController_.macroChipBounds(macroId);
    }

    /** The expanded macro whose name chip contains `canvasPos`, or an empty string. Smallest
     *  chip wins if two ever overlap. */
    juce::String macroChipAt(juce::Point<int> canvasPos) const { return macroController_.macroChipAt(canvasPos); }

    /** Canvas-space bounds of an EXPANDED macro's collapse button (founder-review fix G5). See
     *  MacroGroupController::macroCollapseButtonBounds. */
    juce::Rectangle<int> macroCollapseButtonBounds(const juce::String& macroId) const {
        return macroController_.macroCollapseButtonBounds(macroId);
    }

    /** The expanded macro whose collapse button contains `canvasPos`, or an empty string.
     *  Smallest button wins if two ever overlap (mirrors macroChipAt/macroHullAt). */
    juce::String macroCollapseButtonAt(juce::Point<int> canvasPos) const {
        return macroController_.macroCollapseButtonAt(canvasPos);
    }

    /** Test accessor: the live MacroCardComponent for `macroId`, or nullptr. Exposed so a test can
     *  move the real card directly (bypassing the drag gesture entirely) to prove
     *  rebuildVisibleCables() anchors on the card's CURRENT bounds rather than the persisted
     *  `macro.bounds`, which only updates on finalizeMacroCardDrag — see Fix 3/P8-12 follow-up. */
    MacroCardComponent* getMacroCardForTest(const juce::String& macroId) {
        return macroController_.getMacroCardForTest(macroId);
    }

    /** Builds the recolour popup with the EXACT onPreview/onCommit callbacks promptRecolourMacro
     *  uses, without launching a juce::CallOutBox — mirrors
     *  TimelineRulerComponent::createMarkerColourPickerForTest(). Null when `macroId` doesn't
     *  resolve. Test seam: a headless test drives the returned popup's preview/commit directly
     *  rather than duplicating the recolour logic. Stays on GraphEditor (FRO77 PR2) — see
     *  MacroGroupController.h's class comment. */
    std::unique_ptr<synth::ui::ColourPickerPopup> createMacroColourPickerForTest(const juce::String& macroId);

    /** The shared macro actions menu — right-click a collapsed card or right-click inside an
     *  expanded macro's hull both build this SAME menu (Fix 4/P8-12 follow-up), so the two paths
     *  cannot drift apart. Returns an empty menu if `macroId` doesn't resolve. See
     *  GraphEditorMacroPrompts.cpp's definition for the `renameAction`/`addCandidateSelection`
     *  parameter rationale. */
    juce::PopupMenu
    buildMacroMenu(const juce::String& macroId, std::function<void()> renameAction = nullptr,
                   const std::vector<juce::AudioProcessorGraph::NodeID>* addCandidateSelection = nullptr);

    /** Live bounds + colour category for the currently-resolvable MODULE members of `macroId`
     *  (a port node is excluded — founder-review fix G6). See
     *  MacroGroupController::macroMemberPreviews. */
    using MacroMemberPreview = MacroGroupController::MacroMemberPreview;
    std::vector<MacroMemberPreview> macroMemberPreviews(const juce::String& macroId) const {
        return macroController_.macroMemberPreviews(macroId);
    }

    /** Display names of `macroId`'s MODULE members, in order — a port node is excluded
     *  (founder-review fix G6). Feeds MacroCardComponent's tooltip. */
    juce::StringArray macroMemberNames(const juce::String& macroId) const {
        return macroController_.macroMemberNames(macroId);
    }

    /** Theme colour for a module category, matching how the canvas colours cables/cards by
     *  category (synth::ui::themeColourForCategory) — used by the collapsed card's content
     *  preview so its boxes read as the same colours expanding the macro would show. Falls back
     *  to token defaults under the stock LookAndFeel headless tests install, same as
     *  colourForCable. */
    juce::Colour categoryPreviewColour(synth::ui::ModuleCategory category) const;

    /** Status-bar surface for a refused macro action (nested-group Cmd+G, ungroup-with-nothing-
     *  selected). Owner installs; a no-op by default (e.g. in tests). Mirrors
     *  onSaveSnippetRequested's ownership split — GraphEditor owns no status bar. */
    std::function<void(const juce::String&)> onStatusMessage;

    // ---- Macro card drag (MacroCardComponent's own ComponentDragger calls these) ----
    //
    // Carries every one of a collapsed macro's (hidden) members along by the card's own drag
    // delta, reusing beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag exactly as a plain
    // multi-select drag does — see the "Macros" section comment above.
    void beginMacroCardDrag(const juce::String& macroId);
    void dragMacroCardBy(const juce::String& macroId, juce::Point<int> delta);
    /** Resolves the members' rigid-body snap AND the card's own position as one undo step. */
    void finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft);
    /** A press that never moved — mirrors cancelSelectionDrag, no re-resolve. */
    void cancelMacroCardDrag(const juce::String& macroId);

    // ---- Macro I/O (P8-15b, T140): the "Configure I/O" modal + the cable-drop convenience -----
    //
    // §7 items 3 and 5 of docs/macros_implementation.md, unified into ONE modal per an explicit founder request
    // rather than piecemeal "Add Input"/"Add Output"/"Rename"/"Reorder" menu actions. Every entry
    // point below is a single recordGraphAndMacroChange transaction, so add/remove/rename/reorder
    // and (the one that matters most) a shape change are each exactly one undo step — a shape
    // change is a delete-node + create-node + rewire landing together, never two undos. -----

    /** Adds a new port to `macroId`. See MacroGroupController::addMacroPort for the full
     *  construction-order/naming contract. */
    juce::String addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                              MacroPortShape shape, int voiceCount, const juce::String& portName) {
        return macroController_.addMacroPort(macroId, isInput, kind, shape, voiceCount, portName);
    }

    /** Removes the port fronted by `nodeUuid` from `macroId` by deleting its node (drops the
     *  cables). See MacroGroupController::removeMacroPort. */
    void removeMacroPort(const juce::String& macroId, const juce::String& nodeUuid) {
        macroController_.removeMacroPort(macroId, nodeUuid);
    }

    /** Deletes ONE macro port node directly while its macro stays alive, SPLICING the boundary
     *  cable back together (unlike removeMacroPort()). See MacroGroupController::deleteMacroPortNode. */
    void deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid) {
        macroController_.deleteMacroPortNode(macroId, nodeUuid);
    }

    /** Renames the port fronted by `nodeUuid`. Empty/whitespace-only `newName` is a no-op. */
    void renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid, const juce::String& newName) {
        macroController_.renameMacroPort(macroId, nodeUuid, newName);
    }

    /** Moves the port fronted by `nodeUuid` one step earlier/later in its own direction's draw
     *  order. The keyboard-accessible fallback (T153) for reorderMacroPortToIndex below. */
    void moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp) {
        macroController_.moveMacroPortOrder(macroId, nodeUuid, moveUp);
    }

    /** T152 drag-to-reorder: moves the port fronted by `nodeUuid` to `newIndexInGroup` (0-based,
     *  clamped) within its own direction group. See MacroGroupController::reorderMacroPortToIndex. */
    void reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid, int newIndexInGroup) {
        macroController_.reorderMacroPortToIndex(macroId, nodeUuid, newIndexInGroup);
    }

    /** Changes the shape of an existing audio/CV port as ONE edit (delete-node + create-node +
     *  rewire underneath). See MacroGroupController::changeMacroPortShape. */
    juce::String changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                      MacroPortShape newShape, int newVoiceCount) {
        return macroController_.changeMacroPortShape(macroId, nodeUuid, newShape, newVoiceCount);
    }

    /** T152: sets (or, with nullopt, clears back to the kind-tint default) the user colour for the
     *  port fronted by `nodeUuid`. */
    void changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                               std::optional<juce::Colour> newColour) {
        macroController_.changeMacroPortColour(macroId, nodeUuid, newColour);
    }

    /** Opens the "Configure I/O" modal (MacroPortConfigDialog) for `macroId` — the single entry
     *  point every port add/remove/rename/reorder/shape-change above is reached through when the
     *  user drives it from the UI; every method above is independently callable (and tested) with
     *  no dialog involved. No-op if `macroId` doesn't resolve. */
    void promptConfigureMacroIO(const juce::String& macroId);

    /** Opens a small "Rename Port" AlertWindow for the single port fronted by `nodeUuid` — the
     *  port node's own context menu's quicker alternative to opening the whole Configure I/O
     *  modal just to retype one name (founder-review fix G7). Empty/whitespace-only input cancels
     *  without renaming, same as promptRenameMacro's own convention; the actual mutation is
     *  renameMacroPort() (already independently tested with no dialog involved). No-op if
     *  `macroId` doesn't resolve. */
    void promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    // ---- Macro card jacks (P8-15c, T141, docs/macros_implementation.md §7 item 4) -----------------------------
    // See MacroGroupController::MacroCardPort for the full on-card-jack layout contract.
    using MacroCardPort = MacroGroupController::MacroCardPort;

    /** Every port on `macroId`'s collapsed card, laid out. Empty if `macroId` doesn't resolve or
     *  has no ports yet. See MacroGroupController::macroCardPortLayout. */
    std::vector<MacroCardPort> macroCardPortLayout(const juce::String& macroId) const {
        return macroController_.macroCardPortLayout(macroId);
    }

    /** The port whose jack contains `cardLocalPos`, or nullopt. See
     *  MacroGroupController::macroCardPortForPoint. */
    std::optional<MacroCardPort> macroCardPortForPoint(const juce::String& macroId,
                                                       juce::Point<int> cardLocalPos) const {
        return macroController_.macroCardPortForPoint(macroId, cardLocalPos);
    }

    // ---- Snippets (issue #156) ----

    /** Snippet JSON for the current selection, ready to hand to SnippetManager::saveSnippet. */
    juce::var extractSelectionSnippet(const juce::String& name);

    /** Inserts a snippet at a canvas position as one undoable change, then selects what landed.
     *  @return true when at least one module was added. */
    bool insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) override;

    /** Set by the owner (MainComponent) to prompt for a name and persist the snippet. Invoked
     *  from the canvas context menu; GraphEditor deliberately owns no file dialogs. */
    std::function<void()> onSaveSnippetRequested;

    /** Set by the owner to resolve a snippet name (from a library drag payload) to its JSON. */
    std::function<juce::var(const juce::String&)> snippetProvider;

    // FRO13: the channel macro menu's Save-preset/Set-default pair; 2nd arg true = set default.
    std::function<void(const juce::String& macroId, bool setAsDefault)> onTrackPresetMenuAction;

    /** right-click-any-knob -> "Automate '<Param>'" (ModuleComponent's generic auto-UI slider
     *  branch). Set by the owner (MainComponent::automateParameter) to resolve the node's uuid,
     *  find-or-create the doc's Automation track, bind a lane and open the automation strip —
     *  GraphEditor deliberately owns no TimelineDoc, mirroring onSaveSnippetRequested above. */
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onAutomateParameterRequested;

    /** A hosted-plugin card's "Open Editor" button (ModuleComponent's HostedPluginModule branch).
     *  Set by the owner (MainComponent) to resolve `nodeId` to its live HostedPluginModule and hand
     *  it to HostedPluginWindowManager::openEditorFor — mirrors onAutomateParameterRequested's
     *  shape exactly, for the same reason: GraphEditor owns neither the module lookup nor the
     *  window manager. */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpenPluginEditorRequested;

    // ---- Copy / paste / duplicate -------------------------------------------------------
    // All three run through the snippet pipeline (self-contained connections, modulation as
    // intent, ids renumbered on insert) — see docs/layout_selection_canvas.md §1.5.

    /** Copies the current selection into the in-app clipboard.
     *  @return false when the selection holds nothing copyable (empty, or only graph I/O nodes),
     *          in which case the previous clipboard contents are left alone. */
    bool copySelection();

    bool canPaste() const { return !clipboard.isEmpty(); }
    int getClipboardModuleCount() const { return clipboard.getModuleCount(); }

    /** Pastes at the next cascade position — one step down-right of wherever the last paste (or the
     *  copy itself) sat, so repeated pastes fan out instead of stacking on one pixel. */
    bool pasteClipboard();

    /** Pastes at an explicit canvas position (the canvas context menu's "Paste Here") and re-anchors
     *  the cascade there, so a following keyboard paste continues from the same place. */
    bool pasteClipboardAt(juce::Point<int> canvasPos);

    /** Copies the selection and immediately drops it back one step down-right, WITHOUT touching the
     *  clipboard — Cmd+D must not cost the user whatever they had copied. */
    bool duplicateSelection();

    // Drag-preview (grid + landing ghost shown during a module drag). Bodies live on
    // GraphDragDropController (FRO77 PR3); these stay one-line forwarders so every existing
    // caller (ModuleComponent, tests) keeps compiling unchanged.
    void beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId) {
        dragDropController_.beginDragPreview(w, h, selfId);
    }
    void updateDragPreview(juce::Point<int> desiredTopLeftCanvas) {
        dragDropController_.updateDragPreview(desiredTopLeftCanvas);
    }
    void endDragPreview() { dragDropController_.endDragPreview(); }

    // Test accessors for drag-preview state
    bool isDragPreviewActive() const { return dragDropController_.isDragPreviewActive(); }
    juce::Rectangle<int> getDragPreviewGhost() const { return dragDropController_.getDragPreviewGhost(); }

    /** Every alignment guide computed by the current drag preview — see
     *  GraphDragDropController::AlignmentGuide. Only GraphEditorCables.cpp's paint path reads
     *  this; the enable/disable preference (alignmentGuidesEnabled below) stays on GraphEditor. */
    const std::vector<GraphDragDropController::AlignmentGuide>& getAlignmentGuides() const {
        return dragDropController_.getAlignmentGuides();
    }

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled) { alignmentGuidesEnabled = enabled; }
    bool getAlignmentGuidesEnabled() const { return alignmentGuidesEnabled; }

    // Double-click a connected jack to disconnect (issue #216). On by default.
    void setDoubleClickPortDisconnectEnabled(bool enabled) { doubleClickPortDisconnectEnabled = enabled; }
    bool getDoubleClickPortDisconnectEnabled() const noexcept { return doubleClickPortDisconnectEnabled; }

    // T148 (docs/macros_implementation.md §7 item 9): auto-create a macro port when a dragged cable crosses a
    // macro boundary. On by default; a Preferences toggle (PreferencesSettingsTab,
    // "macroAutoCreatePortsOnDrag") lets a user turn this specific automation off, leaving
    // endConnectionDrag's plain connectPorts() behaviour exactly as it was before T148.
    void setAutoCreateMacroPortsOnDragEnabled(bool enabled) { autoCreateMacroPortsOnDragEnabled = enabled; }
    bool getAutoCreateMacroPortsOnDragEnabled() const noexcept { return autoCreateMacroPortsOnDragEnabled; }

    // T184 (P9-3c, docs/mixer.md §5.2): auto-creates a mixer channel on a qualifying MIDI connect;
    // Preferences ("mixerAutoCreateChannelOnConnect") can turn this off.
    void setAutoCreateChannelOnConnectEnabled(bool enabled) { autoCreateChannelOnConnectEnabled = enabled; }
    bool getAutoCreateChannelOnConnectEnabled() const noexcept { return autoCreateChannelOnConnectEnabled; }

    /** FRO26 (P9-3e, docs/mixer.md §5.13): "Create channels" for existing projects, one call per
     *  entry in `trackSourceNodeIds`. See GraphEditorChannels.cpp for the full
     *  trackSourceNodeIds/skip-condition/transaction rationale. */
    void createChannelsForUnchanneledTracks(const std::vector<juce::AudioProcessorGraph::NodeID>& trackSourceNodeIds);

    // ---- FRO25 (P9-3d, docs/mixer.md §5.8): "Make channel" / "Duplicate into this channel" ------

    /** "Make channel" for the chain starting at `source` (a track's own source node, or a trackless
     *  chain's root): runs synth::planMakeChannel/buildMakeChannel, then boxes the track's exclusive
     *  chain + new EQ/Compressor/Strip into ONE collapsed macro named `channelName`, and each merge
     *  point's bus channel into its own "<module> Bus" macro.
     *
     *  NO UNDO OF ITS OWN and no updateComponents() call — the caller wraps it in one transaction
     *  (MainComponent::makeChannelForNode, or requestMakeChannel's standalone fallback). Returns
     *  false with nothing touched when the chain already has a channel; reports a refusal (a node
     *  already in a macro, an inconsistent send topology) through onStatusMessage. See
     *  GraphEditorChannels.cpp for the port-creation/Master rationale. */
    bool makeChannelFromNode(juce::AudioProcessorGraph::NodeID source, const juce::String& channelName);

    /** True when "Make channel" on `source` would build something — the menu items' enabled state.
     *  Pure read (synth::planMakeChannel). */
    bool nodeNeedsChannel(juce::AudioProcessorGraph::NodeID source) const;

    // FRO13 (P9-7): true when memberUuid's macro is a mixer channel (synth::isChannelMacro) — a
    // const-callable query since getMacros() itself is non-const.
    bool isChannelMacroForTrack(const juce::String& memberUuid) const;

    /** The chain source the canvas/module "Make Channel" item acts on for the current selection
     *  (synth::resolveChannelSource), or an invalid NodeID when none/ambiguous. */
    juce::AudioProcessorGraph::NodeID channelSourceForSelection() const;

    /** The canvas/module menu item's action. MainComponent installs onMakeChannelRequested so the
     *  ONE undo step also covers the timeline and runs the reconcile pass; without it (a standalone
     *  GraphEditor) records its own graph+macro undo step around makeChannelFromNode. */
    void requestMakeChannel(juce::AudioProcessorGraph::NodeID source);
    std::function<void(juce::AudioProcessorGraph::NodeID)> onMakeChannelRequested;

    /** Appends "Make Channel" (enabled iff nodeNeedsChannel) when the selection resolves to a chain
     *  source; nothing otherwise. Shared by the canvas menu and ModuleComponent's module menu. */
    void addMakeChannelMenuItem(juce::PopupMenu& menu);

    /** Channel macros (a macro with a Channel Strip member) that module `nodeId` — itself in no
     *  macro — feeds from outside, directly, through a macro port, or through a modulation
     *  attenuverter, while ALSO feeding at least one other consumer: the "Duplicate into this
     *  channel" targets. Empty when `nodeId` isn't shared. Pure read. */
    std::vector<juce::String> duplicateIntoChannelTargets(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** "Duplicate into this channel": a copy of `nodeId` (parameters and extra state carried over,
     *  the duplicateSelection path) takes over every cable `nodeId` sends into macro `macroId`,
     *  receives the same inputs `nodeId` does (a modulation routing into it is re-created with the
     *  same amount), and joins the macro. Every other consumer stays on the original. NO UNDO OF
     *  ITS OWN and no updateComponents() call, same contract as makeChannelFromNode. Returns false
     *  with nothing touched when `macroId` isn't one of duplicateIntoChannelTargets(nodeId). See
     *  GraphEditorChannels.cpp for the boundary-port splice detail. */
    bool duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);

    /** The module menu item's action — same MainComponent-or-standalone undo split as
     *  requestMakeChannel. */
    void requestDuplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onDuplicateIntoChannelRequested;

    /** Appends the "Duplicate into '<channel>'" item (one target) or a "Duplicate into Channel"
     *  submenu (several) for `nodeId`; nothing when it has no targets. */
    void addDuplicateIntoChannelMenuItems(juce::PopupMenu& menu, juce::AudioProcessorGraph::NodeID nodeId);

    /** Test seam, the canvas counterpart to ModuleComponent::setShowContextMenuHookForTest: a real
     *  right-click on empty canvas builds the menu and hands it here instead of showing it. */
    void setShowCanvasContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showCanvasContextMenuHook_ = std::move(hook);
    }

    // T148 (docs/macros_implementation.md §7 item 9): auto-delete a macro port once its last cable is removed.
    // On by default; a Preferences toggle (PreferencesSettingsTab,
    // "macroAutoDeletePortsOnLastCable") lets a user turn this off, leaving a cable-less port in
    // place until it is removed by hand (Configure I/O or the port's own right-click Delete Port).
    void setAutoDeleteMacroPortsOnLastCableEnabled(bool enabled) { autoDeleteMacroPortsOnLastCableEnabled = enabled; }
    bool getAutoDeleteMacroPortsOnLastCableEnabled() const noexcept override {
        return autoDeleteMacroPortsOnLastCableEnabled;
    }

    // Default jack layout for newly created modules that expose the Dual I/O parameter.
    // false (default): one collapsed "Audio" jack. true: split Left/Right by default.
    void setDefaultDualIOForNewModules(bool enabled) { defaultDualIOForNewModules = enabled; }

    /** Re-lays every stereo-capable module already on the canvas to `dual`. Card heights do not
     *  move — the gutter reserves room for the dual layout in both states. See
     *  GraphEditorStereoWiring.cpp for why this stays separate from setDefaultDualIOForNewModules. */
    void applyDualIOToExistingModules(bool dual);

    /** Unhooks a collapsed split-block module's hidden right leg, RE-POINTING each cable onto the
     *  matching channel of the surviving left block wherever the far end still exposes it (and
     *  simply dropping it where it does not). Graph-level, so it works before the cards exist.
     *  No-op for FX pairs, whose collapsed jack legitimately still owns both raw legs. See
     *  GraphEditorStereoWiring.cpp for the collapse-level rationale. */
    void dropHiddenRightLegConnections(juce::AudioProcessorGraph::NodeID nodeId);

    /** The raw channel carrying `proc`'s right audio leg for wiring purposes, or -1 when it has none
     *  the user can reach. Asks the module (FX use ch1, split-block modules their own kRightBase),
     *  then requires the channel to be reachable from a VISIBLE jack — a collapsed split-block
     *  module still reports PortRole::Audio on its hidden block, and wiring that would create a
     *  cable nobody can unplug. Static so tests can pin it directly. */
    static int rightAudioLegOf(juce::AudioProcessor* proc, bool asInput);

    /** True when `rawChannel` is covered by one of the module's currently VISIBLE jacks (a jack's
     *  JackTarget spans `voiceSpan` consecutive raw channels, which is how a collapsed FX jack owns
     *  both of its legs). The wiring-side counterpart of handleModuleResized's exposure check. */
    static bool audioChannelReachableFromJack(const ModuleBase& mb, int rawChannel, bool isInput);
    bool getDefaultDualIOForNewModules() const noexcept { return defaultDualIOForNewModules; }

    /** Per-module-type overrides of the default above, keyed by module type (ModuleBase::getName(),
     *  e.g. "Reverb"). A type with no entry follows the global default. Read only from
     *  applyDefaultDualIOForNewModule — new modules only, exactly like the global default itself:
     *  changing this does NOT retro-apply to modules already on the canvas (there is no
     *  per-module counterpart to applyDualIOToExistingModules). Set from
     *  PreferencesSettingsTab::setGraphEditor / setDualIOOverrideForType, and from MainComponent at
     *  startup via PreferencesSettingsTab::loadDualIOPerModuleOverrides. */
    void setDualIOPerModuleOverrides(std::map<juce::String, bool> overrides) {
        dualIOPerModuleOverrides = std::move(overrides);
    }
    const std::map<juce::String, bool>& getDualIOPerModuleOverrides() const noexcept {
        return dualIOPerModuleOverrides;
    }

    // ---- Custom module titles ---- A user-set card title, stored as the node property
    // "displayName". See GraphEditorModuleTitles.cpp for why it is mirrored into neither the
    // processor nor ModuleBase::getName().

    /** The user's custom title for a node, or an empty string when it has none. */
    juce::String getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Sets (or, given blank/whitespace, clears) a node's custom title. Undoable. */
    void setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name);

    /** Title a card should paint: the custom one when set, else the auto-numbered module name.
     *  Also GraphCanvasHost::getModuleTitle() — MacroGroupController::macroMemberNames() (FRO77
     *  PR2) needs it and title resolution otherwise requires a live GraphEditor. */
    juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                juce::AudioProcessor* processor) const override;

    /** Commits and closes any open inline title editor, on any card.
     *
     *  Called from every canvas press path (GraphEditor::mouseDown for empty canvas and cables,
     *  ModuleComponent::mouseDown for any card) because the editor's own onFocusLost is NOT enough:
     *  almost nothing on this canvas wants keyboard focus, so clicking a module body or the
     *  background never takes focus away from the editor and the callback never fires. Clicking
     *  away has to commit from the presser's side instead. Escape still cancels. */
    void commitAnyOpenTitleRename();

    /** True when the visible jack already has at least one graph edge or mod routing. */
    bool isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const;

    // ---- Smart connections --------------------------------------------------
    // Proximity-based cable suggestions while placing a module, owned by SmartConnectionEngine
    // (Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h) since FRO77 PR1 — GraphEditor
    // just forwards. SmartConnectionMode/SmartSuggestion are aliased here so `GraphEditor::X`
    // keeps compiling for every existing caller (PreferencesSettingsTab, tests) unchanged.
    using SmartConnectionMode = SmartConnectionEngine::SmartConnectionMode;
    using SmartSuggestion = SmartConnectionEngine::SmartSuggestion;

    void setSmartConnectionMode(SmartConnectionMode mode) { smartConnections_.setSmartConnectionMode(mode); }
    SmartConnectionMode getSmartConnectionMode() const noexcept { return smartConnections_.getSmartConnectionMode(); }

    /** Tests set the override; production leaves it empty and reads the real keyboard. See
     *  SmartConnectionEngine::isInsertModifierDown for the CTRL/insert-in-series rationale. */
    void setInsertModifierOverrideForTests(std::optional<bool> down) {
        smartConnections_.setInsertModifierOverrideForTests(down);
    }
    bool isInsertModifierDown() const { return smartConnections_.isInsertModifierDown(); }

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    /** Wires two visible jacks the same way a completed cable-drag does (poly fan, MIDI,
     *  attenuverter for mono mod CV). When recordUndo is false the caller owns the transaction
     *  (e.g. inside an existing recordStructuralChange). Also GraphCanvasHost::connectPorts(). */
    void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack, juce::AudioProcessorGraph::NodeID dstId,
                      int dstJack, bool isMidi, bool recordUndo = true) override;

    // Test accessors
    int getSmartSuggestionCount() const noexcept { return smartConnections_.getSmartSuggestionCount(); }
    const std::vector<SmartSuggestion>& getSmartSuggestions() const noexcept {
        return smartConnections_.getSmartSuggestions();
    }
    bool nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const;
    /** Runs just the drag tick's modifier re-sample, so a test can exercise a press/release that
     *  happens without any mouse movement without needing a real 30 Hz timer. */
    void pumpDragModifierTickForTests() { refreshSuggestionsIfInsertModifierChanged(); }

    /** Test seam: GraphCanvasHost is a private base (only code holding a GraphCanvasHost& should
     *  reach GraphEditor through the narrow seam), so a test driving a SmartConnectionEngine of its
     *  own directly (rather than through GraphEditor's forwarders) needs an explicit way to get one.
     *  Production code never calls this — GraphEditor's own smartConnections_ member captures `*this`
     *  itself, inside the class, where the private base is accessible without help. */
    GraphCanvasHost& getCanvasHostForTest() { return *this; }

    /** Port centre inside a bounds rect — must agree with ModuleComponent::getPortCenter. */
    static juce::Point<int> estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                               bool isInput, bool isMidi);

    /** Audio-jack occupancy, for asserting that a reroute left nothing dangling. */
    bool isInputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const {
        return smartConnections_.isInputJackFree(nodeId, jack, false);
    }
    bool isOutputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const {
        return smartConnections_.isOutputJackFree(nodeId, jack, false);
    }

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
                                   const std::function<void(juce::AudioProcessor&)>& configure) override;

    /** Creates a Hosted Plugin node already pointed at `identity`. The actual load is asynchronous
     *  and resolves through the default backend's scan service, so a canvas with no service
     *  installed adds a placeholder rather than failing the add. See GraphEditorDragDrop.cpp for
     *  why this is a thin wrapper over addModuleAtCanvasPosition rather than a second add path. */
    void addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos) override;

    /** Canvas coordinates of the middle of the current view — where a clicked (rather than dragged)
     *  library row lands. */
    juce::Point<int> getViewportCentreInCanvasSpace() const;

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

    // ---- Cables (issue #157) ---- A "cable" is one wire as the USER sees it, which is not the
    // same thing as a graph edge — see GraphEditorTypes.h for the full rationale and the
    // CableId/VisibleCable structs' field-level docs (rule 4 of the FRO77 PR3 header trim).
    using CableId = graph_editor_types::CableId;
    using VisibleCable = graph_editor_types::VisibleCable;

    /** Enumerates every cable currently drawn on the canvas, in paint order.
     *  Memoized: the list is rebuilt when the canvas is asked to repaint (repaintCanvas()) and on
     *  every 30 Hz tick, never per-paint. Cable geometry is CANVAS-space, so zoom and pan cannot
     *  move a cable — a zoom gesture reuses the same list. Do not store the returned reference
     *  across a repaintCanvas(), a timerCallback() or any graph edit. */
    const std::vector<VisibleCable>& buildVisibleCables();

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

    /** Last folder a Wavetable card browsed to. Held here so a newly dropped Wavetable seeds
     *  its browser from wherever the user was last working; MainComponent owns the round trip
     *  to ApplicationProperties via onWavetableFolderChanged, keeping GraphEditor
     *  settings-free (same split as the cable-colour config above). */
    void rememberWavetableFolder(const juce::File& folder);
    juce::File getLastWavetableFolder() const noexcept { return lastWavetableFolder; }
    std::function<void(const juce::File&)> onWavetableFolderChanged;

    // Test accessors.
    int getVisibleCableCount() { return (int)buildVisibleCables().size(); }
    bool hasHoveredCable() const noexcept { return hoveredCableId.has_value(); }
    int getCableRebuildCountForTest() const noexcept { return cableRebuildCount; }

    // ---- Zoom gesture (raster freeze) test seams ----
    bool isZoomGestureActive() const noexcept { return zoomGestureActive; }
    /** Ends the zoom gesture now, as the settle timer would. Test seam: the VBlank driver does
     *  not tick in the headless runner. */
    void settleZoomNowForTest() { endZoomGesture(); }

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

    // ---- Minimap (issue #159) ----
    synth::ui::MinimapComponent minimap;
    // User preference, independent of resized()'s auto-hide-when-tiny effective visibility.
    bool minimapVisible = true;

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

    /** The NodeID the live drag preview is tracking, or an invalid NodeID before one starts —
     *  GraphDragDropController's own field, forwarded because GraphEditorCanvas.cpp's
     *  updateComponents() (a same-class caller, not a GraphCanvasHost one) still needs it by this
     *  name now that the field itself lives off GraphEditor (FRO77 PR3). */
    juce::AudioProcessorGraph::NodeID getDragPreviewSelfId() const {
        return dragDropController_.getDragPreviewSelfId();
    }

    void refreshSmartSuggestions() override;
    void applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo);
    void clearSmartSuggestions() override;
    void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor, const juce::String& moduleType) const override;
    /** Re-evaluates the suggestions when the insert modifier changed since the last drag tick.
     *  A modifier press/release is not a mouse move, so nothing else would notice it. */
    void refreshSuggestionsIfInsertModifierChanged();

    /** The current drag-preview fields, packaged for SmartConnectionEngine (see
     *  SmartConnectionEngine::DragPreviewState). The fields themselves moved onto
     *  GraphDragDropController in FRO77 PR3; this stays a private GraphEditor method (rather than
     *  callers reaching the controller directly) because GraphEditorSmartConnections.cpp's own
     *  refreshSmartSuggestions()/refreshSuggestionsIfInsertModifierChanged() call it unqualified,
     *  same as every other same-class forwarder on this page. */
    SmartConnectionEngine::DragPreviewState buildDragPreviewState() const {
        return dragDropController_.buildDragPreviewState();
    }

    // ---- GraphCanvasHost (private: only code holding a GraphCanvasHost& can call these) ----
    juce::AudioProcessorGraph& graph() override { return audioEngine.getGraph(); }
    AudioEngine& engine() override { return audioEngine; }
    ModuleComponent* moduleComponentFor(juce::AudioProcessorGraph::NodeID nodeId) override;
    juce::OwnedArray<ModuleComponent>& modules() override { return content.getModules(); }
    AppUndoManager* undo() override { return undoManager; }
    // repaintCanvas(), updateComponents() and connectPorts() are declared as GraphEditor's own
    // (public) methods above/below; matching GraphCanvasHost's pure virtuals makes those the
    // overrides too, with no separate declaration needed here. FRO77 PR2 adds eight more that
    // reuse an existing GraphEditor method the same way (getMacros(), getSelection(),
    // applySelectionChange() below, setSelectedNodes()/deleteSelection()/getModuleTitle()/
    // requestGroupSelectionIntoMacro() above) — only the four genuinely new ones are declared here.
    juce::OwnedArray<MacroCardComponent>& macroCards() override { return content.getMacroCards(); }
    void reportStatusMessage(const juce::String& message) override {
        if (onStatusMessage)
            onStatusMessage(message);
    }
    void clearModMatrixRows() override { modMatrix.clearRows(); }
    void requestRepaint() override { repaint(); }
    // FRO77 PR3 adds five more GraphCanvasHost methods for GraphDragDropController — see
    // GraphCanvasHost.h's own "PR3 additions" comment for which of these are genuinely new
    // (lookAndFeel/seedInsertModifierSample/canvasPositionOfLocalPoint/estimateModuleSizeForType/
    // resolveSnippetPayload, all declared here) versus dual-purpose `override`s declared alongside
    // the GraphEditor method they reuse (resolvePlacement, estimateSnippetSize, insertSnippetAt,
    // addHostedPluginAtCanvasPosition, addModuleAtCanvasPosition, isSelectionDragActive,
    // applyDefaultDualIOForNewModule, refreshSmartSuggestions, clearSmartSuggestions above).
    juce::LookAndFeel& lookAndFeel() override { return getLookAndFeel(); }
    void seedInsertModifierSample() override { smartConnections_.seedInsertModifierSample(); }
    juce::Point<int> canvasPositionOfLocalPoint(juce::Point<int> pointOnHost) const override {
        return content.getLocalPoint(this, pointOnHost).roundToInt();
    }
    juce::Point<int> estimateModuleSizeForType(const juce::String& typeName) const override {
        return estimateModuleSize(typeName);
    }
    juce::var resolveSnippetPayload(const juce::String& name) const override {
        return snippetProvider ? snippetProvider(name) : juce::var();
    }

    SmartConnectionEngine smartConnections_{*this};
    MacroGroupController macroController_{*this};
    GraphDragDropController dragDropController_{*this};

    juce::AudioProcessorGraph::NodeID draggingAttenuverterNodeId;
    float attenDragStartValue = 0.0f;

    // ---- Cable hover / colouring state (issue #157) ----
    // Only the ID is kept between frames: the geometry is rebuilt each paint anyway, and holding
    // a stale VisibleCable across a graph edit would dangle conceptually (ports move, nodes go).
    std::optional<CableId> hoveredCableId;
    synth::ui::CableColourMode cableColourMode = synth::ui::CableColourMode::BySignalType;
    synth::ui::CableColourOverrides cableColourOverrides;
    juce::File lastWavetableFolder;

    // ---- Selection state (issue #156) ----
    synth::ui::SelectionModel selection;

    // Copy/paste payload. In-app and in-memory only: it is never written to disk and never touches
    // the system clipboard, so Cmd+C on the canvas cannot silently destroy the user's copied text.
    synth::ui::ModuleClipboard clipboard;

    /** Inserts a clipboard-dialect payload at a canvas position, carrying non-parameter module
     *  state through. Shared by paste and duplicate; `insertSnippetAt` is the disk-snippet path and
     *  deliberately does not. */
    bool insertClipboardPayload(const juce::var& payload, juce::Point<int> canvasPos);

    /** Right-click on empty canvas: paste / select-all. Built here rather than inline in mouseDown
     *  so the menu stays out of the hit-testing path. */
    void showCanvasContextMenu(juce::Point<int> canvasPos);
    // FRO25: see setShowCanvasContextMenuHookForTest. Null = show the real async menu.
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

    // True while a click on empty canvas has not yet turned into a pan or marquee drag; a mouseUp
    // in that state is a plain click and clears the selection.
    bool pendingEmptyCanvasClick = false;

    // ---- Expanded-macro chip drag (P8-14) ----
    // The chip drag reuses beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag exactly like a
    // plain multi-select body-drag (see ModuleComponent::mouseDrag/mouseUp) - macroChipDragId is
    // non-empty for the duration of the gesture, and macroChipDragStartCanvasPos is the CANVAS-space
    // (post-zoom-transform) point the chip was pressed at, so the per-frame delta fed to
    // dragSelectionBy is correct at any zoom level.
    juce::String macroChipDragId;
    juce::Point<int> macroChipDragStartCanvasPos;
    /** Tracks the DraggingHandCursor set while hovering a chip, so mouseMove/mouseExit can reset it
     *  on the transition out rather than getting stuck (see mouseMove's cable-hover cursor, which
     *  this mirrors). */
    bool hoveringMacroChip = false;

    /** Bounding boxes of every rendered module, for marquee hit-testing and group collision. */
    std::vector<synth::LayoutUtil::Box> collectModuleBoxes(bool selectedOnly, bool excludeSelected) const;

    /** Footprint of the group a snippet drag payload would drop, for the landing ghost. Falls back
     *  to a single-module estimate when the payload can't be resolved. */
    juce::Point<int> estimateSnippetSize(const juce::String& payload) const override;

    /** Repaints only the module components whose selected state actually changed. Selection
     *  changes must never trigger a full-canvas repaint storm during a marquee drag. */
    void applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection) override;

    AppUndoManager* undoManager = nullptr;

    // Where the macro recolour picker's favourites shelf persists to — see setPropertiesFile.
    // Null (the default) keeps favourites in-memory only, which is what a headless test with no
    // ApplicationProperties gets.
    juce::PropertiesFile* propertiesFile_ = nullptr;

    // Top-level JSON keys the current build doesn't understand (e.g. a future "timeline"),
    // stashed on load and re-merged on save so re-saving with an older build never destroys a
    // newer build's data. Per-loaded-file: newPatch() clears it. Only the user preset save/load
    // path (savePreset/loadPreset) touches this — undo/redo, snippets, and AI apply must not.
    synth::PatchDocument patchDocument;

    // ---- Macros (P8-12) ----
    // Live macro grouping state for the current patch. Serialised by ProjectBundle exactly like
    // patchDocument/timeline above — GraphEditor owns it, MainComponent/ProjectBundle reach it
    // via getMacros(). newPatch() clears it, same lifecycle as patchDocument.
    synth::MacroSet macros;

    /** Syncs macro card components with `macros` and the visibility of their (possibly hidden)
     *  member ModuleComponents. Called at the end of updateComponents(), the same seam that
     *  syncs ModuleComponents themselves. Stays on GraphEditor (FRO77 PR2) — it constructs
     *  `new MacroCardComponent(*this, ...)`, which needs a genuine GraphEditor&; see
     *  MacroGroupController.h's class comment. Also GraphCanvasHost::syncMacroCards() —
     *  MacroGroupController::renameMacro()/setMacroColour() call it through the host afterward. */
    void syncMacroCards() override;

    /** Positions every EXPANDED macro's port widgets against macroHullBounds(). Moved into
     *  MacroGroupController (FRO77 PR2); this is a one-line forwarder kept because
     *  GraphEditorCanvas.cpp/GraphEditorDragDrop.cpp/GraphEditorSelection.cpp call it by its
     *  original name. See MacroGroupController::dockMacroPortWidgets for the full layout
     *  contract. */
    void dockMacroPortWidgets() { macroController_.dockMacroPortWidgets(); }

    /** The NodeID currently backing `memberUuid`, or an invalid NodeID if none does. General
     *  graph-uuid plumbing, not macro-specific — see MacroGroupController.h's class comment for
     *  why it lives there anyway. One-line forwarder kept because GraphEditorCables.cpp/
     *  Channels.cpp/Connections.cpp call it by its original name. */
    juce::AudioProcessorGraph::NodeID resolveMemberNodeId(const juce::String& memberUuid) const {
        return macroController_.resolveMemberNodeId(memberUuid);
    }

    /** The persistent "uuid" node property for `nodeId`, or an empty string if none. One-line
     *  forwarder — see resolveMemberNodeId's comment above. */
    juce::String nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const {
        return macroController_.nodeUuidFor(nodeId);
    }

    /** True if at least one of `macroId`'s members has a "muted" parameter. One-line forwarder
     *  kept because GraphEditorMacroPrompts.cpp's buildMacroMenu() (stays on GraphEditor) calls
     *  it by its original name. */
    bool macroHasMuteEligibleMember(const juce::String& macroId) const {
        return macroController_.macroHasMuteEligibleMember(macroId);
    }

    /** The rectangle to anchor a collapsed macro's boundary cables against. One-line forwarder kept
     *  because GraphEditorCables.cpp's rebuildVisibleCables() calls it by its original name. */
    juce::Rectangle<int> macroCableAnchorBounds(const synth::Macro& macro) const {
        return macroController_.macroCableAnchorBounds(macro);
    }

    /** Shared by promptRecolourMacro and createMacroColourPickerForTest. Stays on GraphEditor
     *  (FRO77 PR2) — see MacroGroupController.h's class comment (SafePointer<GraphEditor>). */
    std::unique_ptr<synth::ui::ColourPickerPopup> buildMacroColourPicker(const juce::String& macroId);

    /** Snapshot of `macroId`'s ports for the Configure I/O dialog. One-line forwarder kept
     *  because GraphEditorMacroPrompts.cpp's promptConfigureMacroIO() (stays on GraphEditor)
     *  calls it by its original name. */
    std::vector<synth::ui::MacroPortConfigDialog::PortRow> macroPortRowsForDialog(const juce::String& macroId) const {
        return macroController_.macroPortRowsForDialog(macroId);
    }

    /** The "shape from a dropped cable" convenience (§5.3): endConnectionDrag calls this by its
     *  original name — one-line forwarder into MacroGroupController::createMacroPortFromDroppedCable. */
    void createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput, bool isMidi,
                                         juce::AudioProcessorGraph::NodeID otherNodeId, int otherVisibleJack) {
        macroController_.createMacroPortFromDroppedCable(macroId, newPortIsInput, isMidi, otherNodeId,
                                                         otherVisibleJack);
    }

    // ---- Auto-create-ports-on-group (founder-review fix F5, docs/macros_implementation.md §7 item 6.1) --------
    // MacroPortCrossingEdge/MacroPortCrossingGroup below are aliased from MacroGroupController,
    // which now owns the crossing-plan math outright. See GraphEditorChannels.cpp's top-of-file
    // comment for which forwarders below still exist and why.

    using MacroPortCrossingEdge = MacroGroupController::MacroPortCrossingEdge;
    using MacroPortCrossingGroup = MacroGroupController::MacroPortCrossingGroup;

    /** The crossing plan a would-be macro's members (by NodeID) would need on creation. One-line
     *  forwarder — GraphEditorChannels.cpp calls it by its original name. */
    std::vector<MacroPortCrossingGroup>
    buildMacroPortCrossingPlan(const std::vector<juce::AudioProcessorGraph::NodeID>& memberNodeIds) const {
        return macroController_.buildMacroPortCrossingPlan(memberNodeIds);
    }

    /** Realises a crossing plan as actual macro ports. One-line forwarder — GraphEditorChannels.cpp
     *  calls it by its original name. */
    void spliceMacroPorts(const juce::String& macroId, const std::vector<MacroPortCrossingGroup>& plan) {
        macroController_.spliceMacroPorts(macroId, plan);
    }

    /** T138: the crossing plan for members newly ADDED to `macroId` (the mirror image of
     *  buildMacroPortCrossingPlanForRemovedMembers). One-line forwarder — GraphEditorChannels.cpp's
     *  duplicate-channel-strip path calls it by its original name. */
    std::vector<MacroPortCrossingGroup>
    buildMacroPortCrossingPlanForNewMembers(const juce::String& macroId,
                                            const std::vector<juce::String>& addedUuids) const {
        return macroController_.buildMacroPortCrossingPlanForNewMembers(macroId, addedUuids);
    }

    /** Which of `macroId`'s existing ports become interior (no longer cross the boundary) once
     *  `addedUuids` join. One-line forwarder — GraphEditorChannels.cpp calls it by its original
     *  name. */
    std::vector<juce::String> macroPortsThatBecomeInteriorOnAdd(const juce::String& macroId,
                                                                const std::vector<juce::String>& addedUuids) const {
        return macroController_.macroPortsThatBecomeInteriorOnAdd(macroId, addedUuids);
    }

    /** T138: the crossing plan for `removedUuids` leaving the macro `macroId` —
     *  removeSelectionFromMacro()'s auto-port counterpart. Computed off the REMAINING ordinary
     *  members (existing members minus `removedUuids`, minus this macro's own ports) as the
     *  "inside" set, so an edge to a departing member now reads as a genuine crossing; then filtered
     *  to keep only edges whose external endpoint is actually one of `removedUuids`. Empty if
     *  `macroId` doesn't resolve. See MacroGroupController::buildMacroPortCrossingPlanForRemovedMembers
     *  for the full contract — moved with no forwarder (no caller outside the former
     *  GraphEditorMacro*.cpp files). */

    /** Splices ONE port node back out of its macro, reconnecting the cable it proxied. One-line
     *  forwarder — GraphEditorChannels.cpp calls it by its original name. */
    void spliceOutMacroPort(synth::Macro& macro, const juce::String& portNodeUuid) {
        macroController_.spliceOutMacroPort(macro, portNodeUuid);
    }

    // ---- Auto-create-port-on-drag / auto-delete-on-last-cable (T148, docs/macros_implementation.md §7 item 9) ----

    /** True if `nodeId` resolves to a live macro member that itself fronts one of that macro's
     *  ports. One-line forwarder kept because GraphEditorCables.cpp/Commands.cpp call it by its
     *  original name. */
    bool nodeIsMacroPort(juce::AudioProcessorGraph::NodeID nodeId) const {
        return macroController_.nodeIsMacroPort(nodeId);
    }

    /** The endpoint-needs-a-port rule, applied to a completed cable-drag between two real jacks.
     *  See MacroGroupController::maybeAutoCreateMacroPortsForDrag for the full contract.
     *  One-line forwarder kept because GraphEditorConnections.cpp calls it by its original name. */
    bool maybeAutoCreateMacroPortsForDrag(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                          juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi,
                                          bool recordUndo = true) {
        return macroController_.maybeAutoCreateMacroPortsForDrag(srcId, srcJack, dstId, dstJack, isMidi, recordUndo);
    }

    // ---- Auto-create-channel-on-connect (T184, P9-3c, docs/mixer.md §5.2 "main workflow") ------

    /** True when `nodeId` resolves to a live TimelineMidiSource ("Track In") node — the one
     *  trigger condition endConnectionDrag checks before opening the T184 auto-channel path. */
    bool nodeIsTimelineMidiSource(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Searches from `searchFrom` for output feeds that don't yet reach a channel and, if it finds
     *  any, builds one (EQ/Compressor/Strip, and Master if newly spliced). NO UNDO OF ITS OWN and
     *  no updateComponents() call — the caller (already inside its own recordGraphAndMacroChange
     *  transaction) does both. See GraphEditorChannels.cpp for the BFS/layout/macro-membership
     *  rationale. */
    void maybeAutoCreateChannelAfterConnect(juce::AudioProcessorGraph::NodeID searchFrom);

    /** The auto-delete half of T148, the reverse of the auto-create above. See
     *  MacroGroupController::autoDeleteOrphanedMacroPort for the full contract. One-line forwarder
     *  kept because GraphEditorCables.cpp/Commands.cpp/Selection.cpp call it by its original
     *  name. */
    void autoDeleteOrphanedMacroPort(juce::AudioProcessorGraph::NodeID nodeId) {
        macroController_.autoDeleteOrphanedMacroPort(nodeId);
    }

    /** T154: the auto-delete-orphaned-port scan's candidate list for a BATCH node deletion. See
     *  MacroGroupController::macroPortDeletionNeighbors for the full contract. One-line forwarder
     *  kept because GraphEditorCommands.cpp/Selection.cpp call it by its original name. */
    std::vector<juce::AudioProcessorGraph::NodeID>
    macroPortDeletionNeighbors(const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const {
        return macroController_.macroPortDeletionNeighbors(deletedIds);
    }

    /** Launches the real "Create ports for the crossing cables?" modal
     *  (synth::ui::MacroAutoPortPromptDialog) and calls `respond(createPorts, remember)` once the
     *  user picks. Only reached from requestGroupSelectionIntoMacro() when
     *  macroAutoPortModalForTest is unset — see that member's comment. */
    void showMacroAutoPortModal(std::function<void(bool createPorts, bool remember)> respond);

    MacroAutoPortPreference macroAutoPortPreference_ = MacroAutoPortPreference::Unset;

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
    bool doubleClickPortDisconnectEnabled = true;
    bool autoCreateMacroPortsOnDragEnabled = true;
    bool autoDeleteMacroPortsOnLastCableEnabled = true;
    bool autoCreateChannelOnConnectEnabled = true;
    bool defaultDualIOForNewModules = false;
    std::map<juce::String, bool> dualIOPerModuleOverrides;

    void updateTransform();

    // Shared zoom math for mouseWheelMove and zoomAroundCentre — keeps the formula (and the
    // [0.1, 2.0] clamp) in exactly one place. `screenAnchor` is the point (in GraphEditor local/
    // screen coordinates) whose underlying canvas point must stay put under the cursor/centre.
    void applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor);

    // Internal: start the drop-landing animation for a newly placed module component.
    void animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos);

    // ---- Cable memo (perf) ----
    // The actual enumeration; buildVisibleCables() is the memoized public entry point above.
    std::vector<VisibleCable> rebuildVisibleCables();
    std::vector<VisibleCable> cablesCache;
    bool cablesCacheValid = false;
    int cableRebuildCount = 0; // test seam, see §11 paint-count pattern
    /** The single "the canvas changed" seam: drops the cable memo, then repaints. Every former
     *  `content.repaint()` in this file goes through here. Also GraphCanvasHost::repaintCanvas(). */
    void repaintCanvas() override;

    // ---- Zoom gesture (raster freeze) ----
    // While a zoom gesture is in flight every card's raster scale is pinned, so a wheel tick
    // resamples the cached images instead of re-rendering every panel + slider at a new scale.
    // The gesture ends kZoomSettleMs after the last zoom event and thaws with exactly one
    // crisp re-render. Time-bounded per §11: the driver has a no-op onUpdate (it requests zero
    // repaints of its own) and stops itself at t = 1.
    bool zoomGestureActive = false;
    synth::ui::AnimationDriver zoomSettleAnim;
    static constexpr double kZoomSettleMs = 140.0;
    void beginOrRefreshZoomGesture();
    void endZoomGesture();
    void setModuleRasterFrozen(bool frozen);

public:
    const std::vector<AudioEngine::ModulationDisplayInfo>& getCachedModDisplayInfo() const {
        return cachedModDisplayInfo;
    }

    const std::vector<AudioEngine::ModulationRouting>& getCachedModRoutings() const { return cachedModRoutings; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditor)
};
