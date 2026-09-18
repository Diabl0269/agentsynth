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
    std::function<void()> onBeforeDetachAllModuleComponents; // fires here AND from deleteSelection()
    void detachAllModuleComponents();

    void paint(juce::Graphics& g) override;
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

    juce::Rectangle<float> getVisibleCanvasRect() const;

    void centreViewOn(juce::Point<float> canvasPoint);

    void fitViewToModules();

    void zoomAroundCentre(float wheelDelta);
    synth::ui::MinimapModel buildMinimapModel();

    // ---- Locate Master (FRO45) ---- See GraphEditorTypes.h for the LocateMasterResult enum and
    // the founder-feedback rationale behind this stopgap (rule 4 of the FRO77 PR3 header trim).
    using LocateMasterResult = graph_editor_types::LocateMasterResult;

    bool hasLocatableMasterOrOutput() const;

    /** Selects Master, falling back to Audio Output when there is no Master yet, and pans it into view. */
    LocateMasterResult locateMasterOrOutput();

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);

    void clearModDropTargets();
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);

    // See GraphEditorTypes.h for the PolyLink struct's full field-level doc (rule 4 of the FRO77
    // PR3 header trim).
    using PolyLink = graph_editor_types::PolyLink;

    /** Which raw channels a cable dropped between two visible jacks should wire. */
    static PolyLink resolvePolyLink(const ModuleBase* source, int sourceVisibleJack, const ModuleBase* dest,
                                    int destVisibleJack);

    /** Re-evaluates every connection touching `module` after its poly parameter changed. */
    void rewireForPolyChange(ModuleComponent* module, const std::vector<LogicalPort>& previousInputMap,
                             const std::vector<LogicalPort>& previousOutputMap);
    void deleteModule(ModuleComponent* module);
    void requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId);
    void replaceModule(ModuleComponent* module, const juce::String& newModuleType);
    void updateModulePosition(ModuleComponent* module);

    // Preset Management
    void savePreset(juce::File file);
    void loadPreset(juce::File file, bool append = false);
    bool loadFactoryPreset(int index);

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

    // ---- Macros (P8-12, docs/macros/macros.md) — owned by MacroGroupController since FRO77 PR2 --------
    // GraphEditor forwards its own (unchanged) public macro API to macroController_; the nested
    // types below are aliased so `GraphEditor::X` keeps compiling for every existing caller
    // (PreferencesSettingsTab, ModuleComponent, tests) unchanged. Full contracts live on
    // MacroGroupController.h now — see that header for the detailed "why" behind each method.
    using MacroPortOwner = MacroGroupController::MacroPortOwner;
    MacroPortOwner macroPortOwnerFor(juce::AudioProcessorGraph::NodeID nodeId) const;

    // Layout / anti-overlap
    juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h,
                                      juce::AudioProcessorGraph::NodeID selfId) override;

    juce::Point<int> findLeftEdgeSlotBelowModules(int w, int h);
    void handleModuleResized(ModuleComponent* moduleComp);

    void dropRoutingsOnHiddenJacks(juce::AudioProcessorGraph::NodeID nodeId);

    void refreshIoModulesAfterDeviceChange();

    /** Output-card identity treatment (docs/layout/module-card.md): installs the callback
     *  MainComponent uses to describe where the signal actually goes (device name + sample rate +
     *  channel count, "Host audio" in HostMode::Hosted, or an empty string to hide the line). Set
     *  once; MainComponent already owns the Standalone-vs-Hosted framing (see how
     *  StatusBarComponent's device chrome is built) so GraphEditor/ModuleComponent stay ignorant of
     *  it and just render whatever string comes back. */
    void setOutputDeviceInfoProvider(std::function<juce::String()> provider) {
        outputDeviceInfoProvider = std::move(provider);
    }

    void refreshOutputDeviceInfo();

    void completeStereoPairConnections(ModuleComponent* moduleComp);
    void finalizeModuleDrag(ModuleComponent* module);
    void autoArrange();

    // ---- Multi-select (issue #156) ------------------------------------------------------
    // See GraphEditorSelection.cpp for the full gesture contract (pan/marquee/click/drag/clear/
    // delete keymap).

    const synth::ui::SelectionModel& getSelection() const override { return selection; }

    void selectModule(juce::AudioProcessorGraph::NodeID nodeId, bool additive);
    void setSelectedNodes(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) override;
    void clearSelection();
    void selectAllModules();
    bool isNodeSelected(juce::AudioProcessorGraph::NodeID nodeId) const { return selection.contains(nodeId); }
    int getSelectionCount() const { return selection.size(); }
    std::vector<juce::AudioProcessorGraph::NodeID> getSelectedNodes() const { return selection.getSelected(); }

    /** Removes every selected module as ONE undoable change. */
    void deleteSelection() override;

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
    /** Discards the recorded drag origins without re-resolving any position. */
    void cancelSelectionDrag();
    bool isSelectionDragActive() const override { return selectionDragActive; }

    bool isMacroChipDragActive() const { return macroChipDragId.isNotEmpty(); } // FRO19 test accessor
    void cancelLiveDragGestures();

    // ---- Cmd/Ctrl-drag macro reparent (FRO40, docs/macros/ports.md) ----------------------------
    // A live drag JOINS/LEAVES an expanded macro by crossing its hull border; see
    // ModuleComponentInteraction.cpp's mouseDrag/mouseUp for the gesture and
    // MacroGroupController::macroDragJoinOrLeaveTarget for the geometry query this is fed from.
    juce::String getMacroDragCandidateId() const noexcept { return macroDragCandidateId_; }
    /** The module a reparent drag is currently moving, or an invalid NodeID between gestures —
     *  same lifetime as the candidate above (see GraphEditorDragDrop.cpp). Lets paint tell WHICH
     *  macro a live drag is dragging a member out of, distinct from which macro it might join. */
    juce::AudioProcessorGraph::NodeID getMacroDragDraggedNodeId() const noexcept { return macroDragDraggedNodeId_; }
    void updateMacroDragCandidate(juce::AudioProcessorGraph::NodeID draggedNodeId, juce::Point<int> canvasCentre);
    void clearMacroDragCandidate();
    /** macroHullBounds(macroId), except while a reparent drag is dragging one of macroId's OWN
     *  members: then it's macroHullBoundsExcluding that member, so the hull visibly shrinks away
     *  from a module being pulled out instead of the live union chasing it. A macro the drag might
     *  JOIN (not the dragged module's current macro) always gets the ordinary live hull. Paint-only
     *  — macroHullAt hit-testing keeps using macroHullBounds. See GraphEditorDragDrop.cpp. */
    juce::Rectangle<int> paintedMacroHullBounds(const juce::String& macroId) const;
    /** The single-undo-step finalize: normal position finalize (finalizeModuleDrag) AND the
     *  membership mutation, as ONE recordGraphAndMacroChange transaction. `module` must not be
     *  touched again afterwards — see GraphEditorDragDrop.cpp's definition for why. */
    void finalizeMacroMembershipDrag(ModuleComponent* module, const juce::String& macroId, bool isJoin);

    // ---- Macros (P8-12) ------------------------------------------------------------------
    // See MacroGroupController.h's "Grouping / membership / collapse" section for what a Macro is
    // and the collapsed-macro selection/drag/delete model.

    juce::String groupSelectionIntoMacro(bool autoCreatePorts = false);

    /** NON-RECORDING: the caller owns the surrounding undo transaction. */
    juce::String addMacroForMembers(const std::vector<juce::String>& memberUuids, const juce::String& name,
                                    juce::Point<int> origin);

    // ---- Macro auto-port preference (founder-review fix F5, docs/macros/auto-ports.md) ----
    // See GraphEditorMacroPrompts.cpp's requestGroupSelectionIntoMacro() for the tri-state/
    // persistence rationale.

    enum class MacroAutoPortPreference { Unset, AutoCreatePorts, LeaveCablesAsIs };

    void setMacroAutoPortPreference(MacroAutoPortPreference pref) noexcept { macroAutoPortPreference_ = pref; }
    MacroAutoPortPreference getMacroAutoPortPreference() const noexcept { return macroAutoPortPreference_; }

    bool selectionHasCrossingMacroCable() const;

    /** Cmd+G / right-click "Create Macro" entry point; may show the auto-port modal. */
    void requestGroupSelectionIntoMacro() override;

    /** Test seam: when set, called INSTEAD of launching the real modal — `respond(createPorts,
     *  remember)` drives the completion exactly as a real button click would, with no DialogWindow
     *  or message loop involved. Production code leaves this null. */
    std::function<void(std::function<void(bool createPorts, bool remember)> respond)> macroAutoPortModalForTest;

    void ungroupSelection();
    /** The controller itself, for the app to install its hooks on (FRO14's macro-rename hook). */
    MacroGroupController& getMacroController() noexcept { return macroController_; }

    void addSelectionToMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids);

    void removeSelectionFromMacro(const juce::String& macroId, const std::vector<juce::String>& memberUuids);

    void removeNodeFromMacro(juce::AudioProcessorGraph::NodeID nodeId);

    void toggleSelectionMacrosCollapsed();

    void groupOrToggleSelectionMacros();

    void selectMacro(const juce::String& macroId, bool additive);

    bool isMacroSelected(const juce::String& macroId) const;

    const synth::Macro* macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const;

    void setMacroCollapsed(const juce::String& macroId, bool collapsed);

    void renameMacro(const juce::String& macroId, const juce::String& newName);
    void setMacroColour(const juce::String& macroId, juce::Colour colour);

    /** Async rename prompt for a macro with no card (e.g. the expanded hull menu). */
    void promptRenameMacro(const juce::String& macroId);

    /** Test seam: when set, called INSTEAD of promptRenameMacro's real juce::AlertWindow — a real
     *  AlertWindow segfaults on a headless Linux CI runner with no display (same class of issue
     *  ModuleComponent::setShowContextMenuHookForTest's own comment documents for PopupMenu, and
     *  macroAutoPortModalForTest above already works around for the auto-port prompt). Production
     *  code leaves this null. */
    std::function<void(const juce::String& macroId)> promptRenameMacroForTest;

    void promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea);

    /** Where the recolour picker's favourites shelf persists to. Null (the default) means
     *  in-memory-only favourites, which is what a headless test with no ApplicationProperties
     *  gets — mirrors TimelineRulerComponent::setPropertiesFile exactly. */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    void deleteMacroAndMembers(const juce::String& macroId);

    // ---- Macro bypass/mute (P8-15d, T142, docs/macros/ports.md#bypass-and-mute) -------------------------------
    //
    // "Bypass macro" / "Mute macro" are FAN-OUT COMMANDS over a macro's members, not a
    // macro-level reinterpretation of the contract — a macro has no processBlock and no
    // bypass/mute state of its own. See MacroGroupController.h for the full fan-out contract.

    using MacroToggleState = MacroGroupController::MacroToggleState;
    MacroToggleState macroBypassState(const juce::String& macroId) const;
    MacroToggleState macroMuteState(const juce::String& macroId) const;

    void setMacroBypassed(const juce::String& macroId, bool bypassed);
    void setMacroMuted(const juce::String& macroId, bool muted);
    void toggleMacroBypassed(const juce::String& macroId);
    void toggleMacroMuted(const juce::String& macroId);

    juce::Rectangle<int> macroHullBounds(const juce::String& macroId) const;

    juce::String macroHullAt(juce::Point<int> canvasPos) const;

    juce::Rectangle<int> macroChipBounds(const juce::String& macroId) const;

    juce::String macroChipAt(juce::Point<int> canvasPos) const;

    juce::Rectangle<int> macroCollapseButtonBounds(const juce::String& macroId) const;

    juce::String macroCollapseButtonAt(juce::Point<int> canvasPos) const;

    MacroCardComponent* getMacroCardForTest(const juce::String& macroId);

    std::unique_ptr<synth::ui::ColourPickerPopup> createMacroColourPickerForTest(const juce::String& macroId);

    /** The shared macro actions menu for a collapsed card or an expanded hull. */
    juce::PopupMenu
    buildMacroMenu(const juce::String& macroId, std::function<void()> renameAction = nullptr,
                   const std::vector<juce::AudioProcessorGraph::NodeID>* addCandidateSelection = nullptr);

    /** Live bounds + colour category for the currently-resolvable MODULE members of `macroId`
     *  (a port node is excluded — founder-review fix G6). See
     *  MacroGroupController::macroMemberPreviews. */
    using MacroMemberPreview = MacroGroupController::MacroMemberPreview;
    std::vector<MacroMemberPreview> macroMemberPreviews(const juce::String& macroId) const;

    juce::StringArray macroMemberNames(const juce::String& macroId) const;

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
    void finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft);
    void cancelMacroCardDrag(const juce::String& macroId);

    // ---- Macro I/O (P8-15b, T140): the "Configure I/O" modal + the cable-drop convenience -----
    //
    // docs/macros/configure-io.md, unified into ONE modal per an explicit founder request
    // rather than piecemeal "Add Input"/"Add Output"/"Rename"/"Reorder" menu actions. Every entry
    // point below is a single recordGraphAndMacroChange transaction, so add/remove/rename/reorder
    // and (the one that matters most) a shape change are each exactly one undo step — a shape
    // change is a delete-node + create-node + rewire landing together, never two undos. -----

    juce::String addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                              MacroPortShape shape, int voiceCount, const juce::String& portName);

    void removeMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    void deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid);

    void renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid, const juce::String& newName);

    void moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp);

    void reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid, int newIndexInGroup);

    juce::String changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                      MacroPortShape newShape, int newVoiceCount);

    void changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                               std::optional<juce::Colour> newColour);

    void promptConfigureMacroIO(const juce::String& macroId);

    /** Quick "Rename Port" prompt -- the one-name alternative to Configure I/O. */
    void promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    // ---- Macro card jacks (P8-15c, T141, docs/macros/ports.md#cable-rendering-across-the-boundary)
    // ----------------------------- See MacroGroupController::MacroCardPort for the full on-card-jack layout contract.
    using MacroCardPort = MacroGroupController::MacroCardPort;

    std::vector<MacroCardPort> macroCardPortLayout(const juce::String& macroId) const;

    std::optional<MacroCardPort> macroCardPortForPoint(const juce::String& macroId,
                                                       juce::Point<int> cardLocalPos) const;

    // ---- Snippets (issue #156) ----

    juce::var extractSelectionSnippet(const juce::String& name);

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
    // intent, ids renumbered on insert) — see docs/layout/snippets-clipboard.md.

    bool copySelection();

    bool canPaste() const { return !clipboard.isEmpty(); }
    int getClipboardModuleCount() const { return clipboard.getModuleCount(); }

    bool pasteClipboard();

    bool pasteClipboardAt(juce::Point<int> canvasPos);

    bool duplicateSelection();

    // Drag-preview (grid + landing ghost shown during a module drag). Bodies live on
    // GraphDragDropController (FRO77 PR3); these stay one-line forwarders so every existing
    // caller (ModuleComponent, tests) keeps compiling unchanged.
    void beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId);
    void updateDragPreview(juce::Point<int> desiredTopLeftCanvas);
    void endDragPreview();

    // Test accessors for drag-preview state
    bool isDragPreviewActive() const;
    juce::Rectangle<int> getDragPreviewGhost() const;

    const std::vector<GraphDragDropController::AlignmentGuide>& getAlignmentGuides() const;

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled) { alignmentGuidesEnabled = enabled; }
    bool getAlignmentGuidesEnabled() const { return alignmentGuidesEnabled; }

    // Double-click a connected jack to disconnect (issue #216). On by default.
    void setDoubleClickPortDisconnectEnabled(bool enabled) { doubleClickPortDisconnectEnabled = enabled; }
    bool getDoubleClickPortDisconnectEnabled() const noexcept { return doubleClickPortDisconnectEnabled; }

    // T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): auto-create a macro port when a dragged cable crosses a
    // macro boundary. On by default; a Preferences toggle (PreferencesSettingsTab,
    // "macroAutoCreatePortsOnDrag") lets a user turn this specific automation off, leaving
    // endConnectionDrag's plain connectPorts() behaviour exactly as it was before T148.
    void setAutoCreateMacroPortsOnDragEnabled(bool enabled) { autoCreateMacroPortsOnDragEnabled = enabled; }
    bool getAutoCreateMacroPortsOnDragEnabled() const noexcept { return autoCreateMacroPortsOnDragEnabled; }

    // T184 (P9-3c, docs/mixer/mixer.md#channels-follow-audio-not-tracks): auto-creates a mixer channel on a qualifying
    // MIDI connect; Preferences ("mixerAutoCreateChannelOnConnect") can turn this off.
    void setAutoCreateChannelOnConnectEnabled(bool enabled) { autoCreateChannelOnConnectEnabled = enabled; }
    bool getAutoCreateChannelOnConnectEnabled() const noexcept { return autoCreateChannelOnConnectEnabled; }

    void createChannelsForUnchanneledTracks(const std::vector<juce::AudioProcessorGraph::NodeID>& trackSourceNodeIds);

    // ---- FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): "Make channel" / "Duplicate into this
    // channel" ------

    /** "Make channel" for the chain starting at `source`; boxes it into a new collapsed macro. */
    bool makeChannelFromNode(juce::AudioProcessorGraph::NodeID source, const juce::String& channelName);

    bool nodeNeedsChannel(juce::AudioProcessorGraph::NodeID source) const;

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

    /** Test seam, the canvas counterpart to ModuleComponent::setShowContextMenuHookForTest: a real
     *  right-click on empty canvas builds the menu and hands it here instead of showing it. */
    void setShowCanvasContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showCanvasContextMenuHook_ = std::move(hook);
    }

    // T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): auto-delete a macro port once its last cable is removed.
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

    /** Re-lays every stereo-capable module already on the canvas to `dual`. */
    void applyDualIOToExistingModules(bool dual);

    /** Unhooks a collapsed split-block module's hidden right leg, re-pointing its cables. */
    void dropHiddenRightLegConnections(juce::AudioProcessorGraph::NodeID nodeId);

    static int rightAudioLegOf(juce::AudioProcessor* proc, bool asInput);

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

    void setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name);

    juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                juce::AudioProcessor* processor) const override;

    /** Commits and closes any open inline title editor, on any card. */
    void commitAnyOpenTitleRename();

    bool isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const;

    // ---- Smart connections --------------------------------------------------
    // Proximity-based cable suggestions while placing a module, owned by SmartConnectionEngine
    // (Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h) since FRO77 PR1 — GraphEditor
    // just forwards. SmartConnectionMode/SmartSuggestion are aliased here so `GraphEditor::X`
    // keeps compiling for every existing caller (PreferencesSettingsTab, tests) unchanged.
    using SmartConnectionMode = SmartConnectionEngine::SmartConnectionMode;
    using SmartSuggestion = SmartConnectionEngine::SmartSuggestion;

    void setSmartConnectionMode(SmartConnectionMode mode);
    SmartConnectionMode getSmartConnectionMode() const noexcept;

    /** Test override for the insert-modifier read; unset means read the real keyboard. */
    void setInsertModifierOverrideForTests(std::optional<bool> down);
    bool isInsertModifierDown() const;

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack, juce::AudioProcessorGraph::NodeID dstId,
                      int dstJack, bool isMidi, bool recordUndo = true) override;

    // Test accessors
    int getSmartSuggestionCount() const noexcept;
    const std::vector<SmartSuggestion>& getSmartSuggestions() const noexcept;
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

    static juce::Point<int> estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                               bool isInput, bool isMidi);

    /** Audio-jack occupancy, for asserting that a reroute left nothing dangling. */
    bool isInputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const;
    bool isOutputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const;

    // ---- Onboarding / UI Phase 5 helpers (headless-testable) ----

    /** Returns true when the canvas has no modules (empty state). Pure predicate.
     *  nodeCount is the number of non-Attenuverter nodes rendered as ModuleComponents. */
    static bool isCanvasEmpty(int nodeCount) noexcept { return nodeCount <= 0; }

    /** The final snapped + anti-overlapped position for a newly dropped module. */
    static juce::Point<int> computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                     const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                     synth::LayoutUtil::NodeID selfId);

    /** Called at the end of every updateComponents(), i.e. whenever the set of modules in the graph
     *  may have changed (add, delete, replace, preset load, undo). Owners use it to refresh UI that
     *  depends on what the patch contains — the module library greys out its singleton I/O rows. */
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

    /** Creates `name` at a canvas position, snapped and anti-overlapped, with undo recorded. */
    void addModuleAtCanvasPosition(const juce::String& name, juce::Point<int> dropPos,
                                   const std::function<void(juce::AudioProcessor&)>& configure) override;

    /** Creates a Hosted Plugin node already pointed at `identity`. */
    void addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos) override;

    juce::Point<int> getViewportCentreInCanvasSpace() const;

    // Mouse Overrides
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    bool keyPressed(const juce::KeyPress& key) override;

    juce::AudioProcessorGraph::NodeID getAttenuverterNodeAt(juce::Point<float> localPos);

    // ---- Cables (issue #157) ---- A "cable" is one wire as the USER sees it, which is not the
    // same thing as a graph edge — see GraphEditorTypes.h for the full rationale and the
    // CableId/VisibleCable structs' field-level docs (rule 4 of the FRO77 PR3 header trim).
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

    /** Cable colouring config. Owned by MainComponent / AppearanceSettingsTab (which persist it);
     *  GraphEditor just renders what it is handed, so it needs no ApplicationProperties. */
    void setCableColourMode(synth::ui::CableColourMode mode);
    synth::ui::CableColourMode getCableColourMode() const noexcept { return cableColourMode; }
    void setCableColourOverrides(const synth::ui::CableColourOverrides& overrides);
    const synth::ui::CableColourOverrides& getCableColourOverrides() const noexcept { return cableColourOverrides; }

    juce::Colour colourForCable(const VisibleCable& cable) const;

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

    juce::AudioProcessorGraph::NodeID getDragPreviewSelfId() const;

    void refreshSmartSuggestions() override;
    void applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo);
    void clearSmartSuggestions() override;
    void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor, const juce::String& moduleType) const override;
    void refreshSuggestionsIfInsertModifierChanged();

    SmartConnectionEngine::DragPreviewState buildDragPreviewState() const;

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
    void seedInsertModifierSample() override;
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

    /** Inserts a clipboard-dialect payload at a canvas position, carrying module state through. */
    bool insertClipboardPayload(const juce::var& payload, juce::Point<int> canvasPos);

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

    // FRO40: the macro a live Cmd/Ctrl-drag would JOIN or LEAVE if released now, empty for
    // neither — see the public accessor/mutators above.
    juce::String macroDragCandidateId_;
    // FRO40: which module that same drag is moving, invalid between gestures — paired lifetime
    // with macroDragCandidateId_ above (both set/cleared only by updateMacroDragCandidate/
    // clearMacroDragCandidate), so there is exactly one lifetime to reason about.
    juce::AudioProcessorGraph::NodeID macroDragDraggedNodeId_;

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

    std::vector<synth::LayoutUtil::Box> collectModuleBoxes(bool selectedOnly, bool excludeSelected) const;

    juce::Point<int> estimateSnippetSize(const juce::String& payload) const override;

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

    void syncMacroCards() override;

    std::unique_ptr<synth::ui::ColourPickerPopup> buildMacroColourPicker(const juce::String& macroId);

    // ---- Auto-create-channel-on-connect (T184, P9-3c, docs/mixer/mixer.md#channels-follow-audio-not-tracks "main
    // workflow") ------

    bool nodeIsTimelineMidiSource(juce::AudioProcessorGraph::NodeID nodeId) const;

    void maybeAutoCreateChannelAfterConnect(juce::AudioProcessorGraph::NodeID searchFrom);

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

    void applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor);

    void animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos);

    // ---- Cable memo (perf) ----
    std::vector<VisibleCable> rebuildVisibleCables();
    std::vector<VisibleCable> cablesCache;
    bool cablesCacheValid = false;
    int cableRebuildCount = 0; // test seam, see docs/layout/animation.md#the-paint-count-pattern
    void repaintCanvas() override;

    // ---- Zoom gesture (raster freeze) ----
    // While a zoom gesture is in flight every card's raster scale is pinned, so a wheel tick
    // resamples the cached images instead of re-rendering every panel + slider at a new scale.
    // The gesture ends kZoomSettleMs after the last zoom event and thaws with exactly one
    // crisp re-render. Time-bounded per docs/layout/animation.md#the-time-bounded-animation-rule: the driver has a
    // no-op onUpdate (it requests zero repaints of its own) and stops itself at t = 1.
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
