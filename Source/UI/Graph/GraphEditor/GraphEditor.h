#pragma once

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Modules/MacroPortShape.h"
#include "PatchDocument.h"
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
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class ModuleComponent;
class MacroCardComponent;
namespace synth::ui {
class ColourPickerPopup; // a unique_ptr return type only; 89 files include this header
}
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

    // ---- Minimap ----
    void setMinimapVisible(bool shouldBeVisible);
    void toggleMinimapVisibility();
    bool isMinimapVisible() const noexcept { return minimapVisible; }
    synth::ui::MinimapComponent& getMinimap() noexcept { return minimap; }

    juce::Rectangle<float> getVisibleCanvasRect() const;

    void centreViewOn(juce::Point<float> canvasPoint);

    void fitViewToModules();

    void zoomAroundCentre(float wheelDelta);
    synth::ui::MinimapModel buildMinimapModel();

    // ---- Locate Master ---- See GraphEditorTypes.h for the LocateMasterResult enum.
    using LocateMasterResult = graph_editor_types::LocateMasterResult;

    bool hasLocatableMasterOrOutput() const;

    /** Selects Master, falling back to Audio Output when there is none yet, and pans into view. */
    LocateMasterResult locateMasterOrOutput();

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);

    void clearModDropTargets();
    void disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi);

    // See GraphEditorTypes.h for the PolyLink struct's full field-level doc.
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
     *  `patchDocument` below). Exposed so the app's `.agsproj` save/load path can re-merge it —
     *  GraphEditor owns no file dialogs, and MainComponent owns no PatchDocument. */
    synth::PatchDocument& getPatchDocument() noexcept { return patchDocument; }

    /** GraphEditor's live set of Macros for the current patch (Source/MacroSet.h). Exposed for the
     *  same reason as getPatchDocument() above. */
    synth::MacroSet& getMacros() noexcept override { return macros; }

    // ---- Macros (docs/macros/macros.md) — owned by MacroGroupController ----
    // GraphEditor forwards its own (unchanged) public macro API to macroController_; the nested
    // types below are aliased so `GraphEditor::X` keeps compiling for every existing caller.
    // Full contracts live on MacroGroupController.h.
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
    void setOutputDeviceInfoProvider(std::function<juce::String()> provider) {
        outputDeviceInfoProvider = std::move(provider);
    }

    void refreshOutputDeviceInfo();

    void completeStereoPairConnections(ModuleComponent* moduleComp);
    void finalizeModuleDrag(ModuleComponent* module);
    void autoArrange();

    // ---- Multi-select ----
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

    bool isMacroChipDragActive() const { return macroChipDragId.isNotEmpty(); } // test accessor
    void cancelLiveDragGestures();

    // ---- Cmd/Ctrl-drag macro reparent (docs/macros/ports.md) ----
    // A live drag JOINS/LEAVES an expanded macro by crossing its hull border; see
    // ModuleComponentInteraction.cpp's mouseDrag/mouseUp for the gesture.
    juce::String getMacroDragCandidateId() const noexcept { return macroDragCandidateId_; }
    /** The module a reparent drag is currently moving, invalid between gestures — see
     *  GraphEditorDragDrop.cpp. */
    juce::AudioProcessorGraph::NodeID getMacroDragDraggedNodeId() const noexcept { return macroDragDraggedNodeId_; }
    void updateMacroDragCandidate(juce::AudioProcessorGraph::NodeID draggedNodeId, juce::Point<int> canvasCentre);
    void clearMacroDragCandidate();
    /** Paint-only hull bounds; see GraphEditorDragDrop.cpp for the exclusion rule during a live
     *  reparent drag. Hit-testing keeps using macroHullBounds. */
    juce::Rectangle<int> paintedMacroHullBounds(const juce::String& macroId) const;
    /** The single-undo-step finalize (position + membership). `module` must not be touched again
     *  afterwards — see GraphEditorDragDrop.cpp. */
    void finalizeMacroMembershipDrag(ModuleComponent* module, const juce::String& macroId, bool isJoin);

    // ---- Macros ----
    // See MacroGroupController.h's "Grouping / membership / collapse" section for what a Macro is
    // and the collapsed-macro selection/drag/delete model.

    // ---- Macro auto-port preference (docs/macros/auto-ports.md) ----
    // See GraphEditorMacroPrompts.cpp's requestGroupSelectionIntoMacro() for the tri-state/
    // persistence rationale.
    enum class MacroAutoPortPreference { Unset, AutoCreatePorts, LeaveCablesAsIs };

    void setMacroAutoPortPreference(MacroAutoPortPreference pref) noexcept { macroAutoPortPreference_ = pref; }
    MacroAutoPortPreference getMacroAutoPortPreference() const noexcept { return macroAutoPortPreference_; }

    /** Cmd+G / right-click "Create Macro" entry point; may show the auto-port modal. */
    void requestGroupSelectionIntoMacro() override;

    /** Test seam: replaces the real modal when set — see GraphEditorMacroPrompts.cpp. Null in
     *  production. */
    std::function<void(std::function<void(bool createPorts, bool remember)> respond)> macroAutoPortModalForTest;

    /** The controller itself, for the app to install its hooks on. */
    MacroGroupController& getMacroController() noexcept { return macroController_; }
    /** Const overload — FRO254: many migrated call sites reach the controller from a const
     *  GraphEditor method (e.g. a read-only predicate), which the non-const overload can't serve. */
    const MacroGroupController& getMacroController() const noexcept { return macroController_; }
    /** The controller itself, for the app to install its hooks on. */
    SmartConnectionEngine& getSmartConnections() noexcept { return smartConnections_; }
    /** Const overload — see getMacroController()'s const overload above for why. */
    const SmartConnectionEngine& getSmartConnections() const noexcept { return smartConnections_; }
    /** The controller itself, for the app to install its hooks on. */
    GraphDragDropController& getDragDropController() noexcept { return dragDropController_; }
    /** Const overload — see getMacroController()'s const overload above for why. */
    const GraphDragDropController& getDragDropController() const noexcept { return dragDropController_; }

    /** Async rename prompt for a macro with no card (e.g. the expanded hull menu). */
    void promptRenameMacro(const juce::String& macroId);

    /** Test seam: replaces the real modal when set — see promptRenameMacro's definition. Null in
     *  production. */
    std::function<void(const juce::String& macroId)> promptRenameMacroForTest;

    void promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea);

    /** Where the recolour picker's favourites shelf persists to. Null (the default) means
     *  in-memory-only favourites, which is what a headless test with no ApplicationProperties
     *  gets — mirrors TimelineRulerComponent::setPropertiesFile exactly. */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    // ---- Macro bypass/mute (docs/macros/ports.md#bypass-and-mute) ----
    //
    // "Bypass macro" / "Mute macro" are FAN-OUT COMMANDS over a macro's members, not a
    // macro-level reinterpretation of the contract — a macro has no processBlock and no
    // bypass/mute state of its own. See MacroGroupController.h for the full fan-out contract.
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

    // ---- Macro card drag (MacroCardComponent's own ComponentDragger calls these) ----
    // Carries every one of a collapsed macro's (hidden) members along by the card's own drag
    // delta, reusing beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag exactly as a plain
    // multi-select drag does.
    void beginMacroCardDrag(const juce::String& macroId);
    void dragMacroCardBy(const juce::String& macroId, juce::Point<int> delta);
    void finalizeMacroCardDrag(const juce::String& macroId, juce::Point<int> newCardTopLeft);
    void cancelMacroCardDrag(const juce::String& macroId);

    // ---- Macro I/O: the "Configure I/O" modal + the cable-drop convenience ----
    // docs/macros/configure-io.md, unified into ONE modal rather than piecemeal "Add Input"/
    // "Add Output"/"Rename"/"Reorder" menu actions. Every entry point below is a single
    // recordGraphAndMacroChange transaction, so add/remove/rename/reorder and a shape change are
    // each exactly one undo step.

    // FRO254 exception: NOT a pure forwarder (unlike the rest of this file's former macro API) —
    // its body does real work beyond the pass-through call (repaintMacroPortColourTargets() +
    // clearMacroPortColourPreview() below), so it stays on GraphEditor rather than moving to
    // MacroGroupController::changeMacroPortColour, which callers must not call directly.
    void changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                               std::optional<juce::Colour> newColour);

    // Per-port jack colour: neither paint surface (the collapsed card, the port's docked widget) repaints
    // on its own, so these force BOTH. A live picker previews view-layer-only; the commit disarms it.
    struct MacroPortRecolourTargets { // card/widget null when the macro is collapsed/absent
        MacroCardComponent* card = nullptr;
        ModuleComponent* widget = nullptr;
    };
    // Repaint BOTH surfaces; return the (possibly-null) pair for a headless reach check -- commit + repaint.
    MacroPortRecolourTargets repaintMacroPortColourTargets(const juce::String& macroId, const juce::String& nodeUuid);
    // Arm the view-layer-only PREVIEW (no stored colour); idempotent -- an unchanged tick repaints nothing.
    void previewMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid, juce::Colour colour);
    // Disarm the armed preview so the jack falls back to stored; a real no-op (no repaint) when unarmed.
    void clearMacroPortColourPreview(const juce::String& macroId, const juce::String& nodeUuid);
    // Teardown BACKSTOP for a picker abandoned with no commit (its CallOutBox outlives the dialog).
    void cancelArmedMacroPortColourPreview();
    // The shared lookup every path runs through, so a preview and its commit can never diverge.
    MacroPortRecolourTargets findMacroPortRecolourTargets(const juce::String& macroId, const juce::String& nodeUuid);

    void promptConfigureMacroIO(const juce::String& macroId);

    /** Quick "Rename Port" prompt -- the one-name alternative to Configure I/O. */
    void promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    // ---- Macro card jacks (docs/macros/ports.md#cable-rendering-across-the-boundary) ----
    // See MacroGroupController::MacroCardPort for the full on-card-jack layout contract.
    using MacroCardPort = MacroGroupController::MacroCardPort;

    // ---- Snippets ----
    juce::var extractSelectionSnippet(const juce::String& name);

    bool insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) override;

    // Prompts for a name and persists the snippet; invoked from the canvas context menu.
    // GraphEditor deliberately owns no file dialogs.
    std::function<void()> onSaveSnippetRequested;
    // Resolves a snippet name (from a library drag payload) to its JSON.
    std::function<juce::var(const juce::String&)> snippetProvider;
    // The channel macro menu's Save-preset/Set-default pair; 2nd arg true = set default.
    std::function<void(const juce::String& macroId, bool setAsDefault)> onTrackPresetMenuAction;

    // right-click-any-knob -> "Automate '<Param>'". Set by MainComponent::automateParameter;
    // GraphEditor owns no TimelineDoc, mirroring onSaveSnippetRequested above.
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onAutomateParameterRequested;

    // ---- MIDI Learn (docs/control/midi-remote-ui.md#the-learn-interaction) ----
    // Set by MainComponent::wireGraphEditorCallbacks(); GraphEditor owns no RemoteEngine/doc.
    std::function<std::map<juce::String, juce::String>(juce::AudioProcessorGraph::NodeID)>
        onQueryMidiMappingsForNode; // mapped paramID -> display label; absent means unmapped
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onMidiLearnRequested;
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)> onMidiForgetRequested;
    std::function<void(juce::AudioProcessorGraph::NodeID, const juce::String&)>
        onEditMidiAssignmentRequested; // unset until the MIDI Remote panel exists
    // Pushes/clears the armed breathing outline onto the target ModuleComponent, if on screen.
    void setMidiLearnArmed(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    void clearMidiLearnArmed();

    // A hosted-plugin card's "Open Editor" button; resolves `nodeId` to its HostedPluginModule and
    // hands it to HostedPluginWindowManager::openEditorFor -- same reason as the callbacks above.
    std::function<void(juce::AudioProcessorGraph::NodeID)> onOpenPluginEditorRequested;

    // ---- Copy / paste / duplicate ----
    // All three run through the snippet pipeline (self-contained connections, modulation as
    // intent, ids renumbered on insert) — see docs/layout/snippets-clipboard.md.
    bool copySelection();

    bool canPaste() const { return !clipboard.isEmpty(); }
    int getClipboardModuleCount() const { return clipboard.getModuleCount(); }

    bool pasteClipboard();

    bool pasteClipboardAt(juce::Point<int> canvasPos);

    bool duplicateSelection();

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled) { alignmentGuidesEnabled = enabled; }
    bool getAlignmentGuidesEnabled() const { return alignmentGuidesEnabled; }

    // Double-click a connected jack to disconnect. On by default.
    void setDoubleClickPortDisconnectEnabled(bool enabled) { doubleClickPortDisconnectEnabled = enabled; }
    bool getDoubleClickPortDisconnectEnabled() const noexcept { return doubleClickPortDisconnectEnabled; }

    // docs/macros/auto-ports.md#ports-on-a-cable-drag: auto-create a macro port when a dragged
    // cable crosses a macro boundary. On by default; Preferences ("macroAutoCreatePortsOnDrag")
    // can turn this off.
    void setAutoCreateMacroPortsOnDragEnabled(bool enabled) { autoCreateMacroPortsOnDragEnabled = enabled; }
    bool getAutoCreateMacroPortsOnDragEnabled() const noexcept { return autoCreateMacroPortsOnDragEnabled; }

    // docs/mixer/mixer.md#channels-follow-audio-not-tracks: auto-creates a mixer channel on a
    // qualifying MIDI connect; Preferences ("mixerAutoCreateChannelOnConnect") can turn this off.
    void setAutoCreateChannelOnConnectEnabled(bool enabled) { autoCreateChannelOnConnectEnabled = enabled; }
    bool getAutoCreateChannelOnConnectEnabled() const noexcept { return autoCreateChannelOnConnectEnabled; }

    void createChannelsForUnchanneledTracks(const std::vector<juce::AudioProcessorGraph::NodeID>& trackSourceNodeIds);

    // ---- "Make channel" / "Duplicate into this channel" (docs/mixer/mixer.md#make-channel-and-shared-modules) ----
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

    // docs/macros/auto-ports.md#ports-on-a-cable-drag: auto-delete a macro port once its last
    // cable is removed. On by default; Preferences ("macroAutoDeletePortsOnLastCable") lets a
    // user turn this off, leaving a cable-less port in place until removed by hand.
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

    /** Per-module-type overrides of the default above, keyed by module type name; a type with no
     *  entry follows the global default. Applies only to NEW modules — does not retro-apply to
     *  modules already on the canvas. */
    void setDualIOPerModuleOverrides(std::map<juce::String, bool> overrides) {
        dualIOPerModuleOverrides = std::move(overrides);
    }
    const std::map<juce::String, bool>& getDualIOPerModuleOverrides() const noexcept {
        return dualIOPerModuleOverrides;
    }

    // ---- Custom module titles ---- A user-set card title, stored as the node property
    // "displayName". See GraphEditorModuleTitles.cpp for why it is mirrored into neither.
    /** The user's custom title for a node, or an empty string when it has none. */
    juce::String getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const;

    void setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name);

    juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId,
                                juce::AudioProcessor* processor) const override;

    /** Commits and closes any open inline title editor, on any card. */
    void commitAnyOpenTitleRename();

    bool isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const;

    // ---- Smart connections ----
    // Proximity-based cable suggestions while placing a module, owned by SmartConnectionEngine
    // (Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.h) — GraphEditor just forwards.
    // SmartConnectionMode/SmartSuggestion are aliased here so `GraphEditor::X` keeps compiling for
    // every existing caller unchanged.
    using SmartConnectionMode = SmartConnectionEngine::SmartConnectionMode;
    using SmartSuggestion = SmartConnectionEngine::SmartSuggestion;

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack, juce::AudioProcessorGraph::NodeID dstId,
                      int dstJack, bool isMidi, bool recordUndo = true) override;

    bool nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const;
    /** Runs just the drag tick's modifier re-sample, so a test can exercise a press/release that
     *  happens without any mouse movement without needing a real 30 Hz timer. */
    void pumpDragModifierTickForTests() {
        smartConnections_.refreshSuggestionsIfInsertModifierChanged(dragDropController_.buildDragPreviewState());
    }

    /** Test seam: exposes the private GraphCanvasHost base for a test driving a
     *  SmartConnectionEngine of its own directly. Production code never calls this. */
    GraphCanvasHost& getCanvasHostForTest() { return *this; }

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

    // ---- Cables ---- A "cable" is one wire as the USER sees it, which is not the same thing as a
    // graph edge — see GraphEditorTypes.h for the full rationale and the CableId/VisibleCable
    // structs' field-level docs.
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

    // Test accessors.
    int getVisibleCableCount() { return (int)buildVisibleCables().size(); }
    bool hasHoveredCable() const noexcept { return hoveredCableId.has_value(); }
    int getCableRebuildCountForTest() const noexcept { return cableRebuildCount; }

    // ---- Zoom gesture (raster freeze) test seams ----
    bool isZoomGestureActive() const noexcept { return zoomGestureActive; }
    // Ends the zoom gesture now, as the settle timer would (the VBlank driver doesn't tick headless).
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

    // ---- Minimap ----
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

    void refreshSmartSuggestions() override;
    void clearSmartSuggestions() override;
    void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor, const juce::String& moduleType) const override;

    // ---- GraphCanvasHost (private: only code holding a GraphCanvasHost& can call these) ----
    juce::AudioProcessorGraph& graph() override { return audioEngine.getGraph(); }
    AudioEngine& engine() override { return audioEngine; }
    ModuleComponent* moduleComponentFor(juce::AudioProcessorGraph::NodeID nodeId) override;
    juce::OwnedArray<ModuleComponent>& modules() override { return content.getModules(); }
    AppUndoManager* undo() override { return undoManager; }
    // Most GraphCanvasHost pure virtuals are satisfied by an existing same-signature GraphEditor
    // method declared elsewhere -- only the genuinely new ones are declared here (see
    // GraphCanvasHost.h for the full split, incl. GraphDragDropController's dual-purpose overrides).
    juce::OwnedArray<MacroCardComponent>& macroCards() override { return content.getMacroCards(); }
    void reportStatusMessage(const juce::String& message) override {
        if (onStatusMessage)
            onStatusMessage(message);
    }
    void clearModMatrixRows() override { modMatrix.clearRows(); }
    void requestRepaint() override { repaint(); }
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

    void showCanvasContextMenu(juce::Point<int> canvasPos);
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

    // The macro a live Cmd/Ctrl-drag would JOIN or LEAVE if released now, empty for neither — see
    // the public accessor/mutators above.
    juce::String macroDragCandidateId_;
    // Which module that same drag is moving, invalid between gestures — paired lifetime with
    // macroDragCandidateId_ above (both set/cleared only together), so there is exactly one
    // lifetime to reason about.
    juce::AudioProcessorGraph::NodeID macroDragDraggedNodeId_;

    // True while a click on empty canvas has not yet turned into a pan or marquee drag; a mouseUp
    // in that state is a plain click and clears the selection.
    bool pendingEmptyCanvasClick = false;

    // ---- Expanded-macro chip drag ----
    // Reuses beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag exactly like a plain
    // multi-select body-drag; macroChipDragId is non-empty for the gesture's duration, and
    // macroChipDragStartCanvasPos is the CANVAS-space point the chip was pressed at, so the
    // per-frame delta fed to dragSelectionBy is correct at any zoom level.
    juce::String macroChipDragId;
    juce::Point<int> macroChipDragStartCanvasPos;
    /** Tracks the DraggingHandCursor set while hovering a chip, so mouseMove/mouseExit can reset
     *  it on the transition out rather than getting stuck. */
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

    // ---- Macros ----
    // Live macro grouping state for the current patch. Serialised by ProjectBundle exactly like
    // patchDocument/timeline above — GraphEditor owns it, MainComponent/ProjectBundle reach it
    // via getMacros(). newPatch() clears it, same lifecycle as patchDocument.
    synth::MacroSet macros;

    void syncMacroCards() override;

    std::unique_ptr<synth::ui::ColourPickerPopup> buildMacroColourPicker(const juce::String& macroId);

    // ---- Auto-create-channel-on-connect (docs/mixer/mixer.md#channels-follow-audio-not-tracks) ----
    bool nodeIsTimelineMidiSource(juce::AudioProcessorGraph::NodeID nodeId) const;

    void maybeAutoCreateChannelAfterConnect(juce::AudioProcessorGraph::NodeID searchFrom);

    void showMacroAutoPortModal(std::function<void(bool createPorts, bool remember)> respond);

    MacroAutoPortPreference macroAutoPortPreference_ = MacroAutoPortPreference::Unset;

    std::vector<AudioEngine::ModulationDisplayInfo> cachedModDisplayInfo;
    std::vector<AudioEngine::ModulationRouting> cachedModRoutings;

    // ---- Animation members ----
    // Drop-landing tween: animates the newly dropped module from drop point to snapped position.
    // Both must be class members so they outlive the VBlank frame callbacks.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver dropLandingAnim;

    // Mod-matrix panel ease: animates the panel bounds on show/hide. modMatrixTargetBounds tracks
    // the target so the final position can be set on complete.
    synth::ui::AnimationDriver modMatrixAnim;
    juce::Rectangle<int> modMatrixTargetBounds;

    // ---- Alignment guides ---- During drag previews, store guide positions for visual feedback.
    struct AlignmentGuide {
        juce::Point<float> start; // line start point (canvas coords)
        juce::Point<float> end;   // line end point (canvas coords)
        int type;                 // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
    };
    std::vector<AlignmentGuide> alignmentGuides;

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
    // resamples the cached images instead of re-rendering every panel + slider at a new scale. The
    // gesture ends kZoomSettleMs after the last zoom event and thaws with exactly one crisp
    // re-render. Time-bounded (docs/layout/animation.md#the-time-bounded-animation-rule).
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
