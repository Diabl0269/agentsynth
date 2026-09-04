#pragma once

#include "../AppUndoManager.h"
#include "../AudioEngine.h"
#include "../MacroSet.h"
#include "../Modules/MacroPortShape.h"
#include "../PatchDocument.h"
#include "../Plugin/Hosting/HostedPluginBackend.h"
#include "CableColour.h"
#include "ColourPickerPopup.h"
#include "LayoutUtil.h"
#include "MacroPortConfigDialog.h"
#include "ModuleClipboard.h"
#include "SelectionModel.h"
#include "UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class ModuleComponent;
class MacroCardComponent;
#include "MinimapComponent.h"
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
    void lookAndFeelChanged() override;

    void timerCallback() override;
    void updateComponents();
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

    /** Multiplies zoom around the centre of the visible area, clamped to the same [0.1, 2.0]
     *  range as wheel zoom, so the point under the centre stays put. */
    void zoomAroundCentre(float wheelDelta);

    // Test accessor. Non-const because it calls buildVisibleCables(), which is non-const.
    synth::ui::MinimapModel buildMinimapModel();

    // Interactions
    void beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                             juce::Point<int> screenPos);
    void dragConnection(juce::Point<int> screenPos);
    void endConnectionDrag(juce::Point<int> screenPos);

    /** Drops the pending modulation drop-target highlight on every card. */
    void clearModDropTargets();
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

    /** The per-loaded-file stash of top-level JSON keys this build doesn't understand (see
     *  `patchDocument` below). Exposed so the app's `.agsproj` save/load path can re-merge the very
     *  same stash a plain `.json` save/load already does — GraphEditor owns no file dialogs, and
     *  MainComponent owns no PatchDocument. */
    synth::PatchDocument& getPatchDocument() noexcept { return patchDocument; }

    /** GraphEditor's live set of Macros for the current patch (see Source/MacroSet.h). Exposed
     *  for the same reason as getPatchDocument() above: the app's project-bundle save/load path
     *  (owned by MainComponent/ProjectBundle) needs to reach it, and GraphEditor owns no file
     *  dialogs of its own. */
    synth::MacroSet& getMacros() noexcept { return macros; }

    // Layout / anti-overlap
    juce::Point<int> resolvePlacement(juce::Point<int> desired, int w, int h, juce::AudioProcessorGraph::NodeID selfId);

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

    // ---- Macros (P8-12) ------------------------------------------------------------------
    //
    // A Macro is a named, coloured, collapsible container: membership plus presentation, no
    // graph change (giving a Macro its own ports is the later, separate P8-15). Flat model — a
    // node already in a macro cannot be grouped into a second one; groupSelectionIntoMacro()
    // refuses that directly (a menu item, or Cmd+G when the whole selection is ungrouped) with a
    // status message rather than doing something ad hoc. Cmd+G itself (P8-14,
    // groupOrToggleSelectionMacros) never hits that refusal for a selection that already touches
    // a macro — it toggles instead, see that method's comment. Membership is by node UUID, so it
    // survives save/load and undo/redo exactly like everything else in synth::MacroSet.
    //
    // Collapsed-macro selection/drag/delete are deliberately NOT a parallel mechanism: selecting
    // a macro selects its members in the ordinary SelectionModel, so beginSelectionDrag/
    // dragSelectionBy/finalizeSelectionDrag and deleteSelection all just work, unchanged, on a
    // collapsed macro's members exactly as they do on any other multi-selection.

    /** Wraps the current selection in a new macro (Cmd+G). Refused via onStatusMessage — no undo
     *  entry, selection untouched — when fewer than two nodes are selected or any selected node
     *  already belongs to a macro (nested macros are out of scope for P8-12).
     *  @return the new macro's id, or an empty string if refused. */
    juce::String groupSelectionIntoMacro();

    /** Ungroups (Cmd+Shift+G): dissolves every macro that owns at least one currently-selected
     *  node, keeping their member modules exactly where they are and expanding them back to
     *  individual cards. Refused via onStatusMessage (no-op) if the selection touches no macro. */
    void ungroupSelection();

    /** Toggles (Cmd+Alt+G) the collapsed state of every macro that owns at least one
     *  currently-selected node — a single reversible command rather than a collapse/expand pair
     *  (see AppCommands::collapseMacro's comment for why a toggle is safe here: the label is
     *  static, "Collapse / Expand Macro").
     *
     *  DETERMINISTIC RULE for a selection that spans more than one macro in different states: if
     *  ANY of them is expanded, collapse them ALL; otherwise expand them all. That means a mixed
     *  selection always converges toward "collapsed" first, which is the direction a user reaching
     *  for this shortcut after selecting a pile of modules almost always wants, and it keeps the
     *  command idempotent-feeling (pressing it repeatedly settles rather than oscillates once
     *  every touched macro is collapsed).
     *
     *  Refused via onStatusMessage (no-op) only when the selection touches NO macro at all. */
    void toggleSelectionMacrosCollapsed();

    /** Cmd+G's single entry point: toggles the macros the selection touches, or groups the
     *  selection into a new macro when it touches none. See the comment on the implementation
     *  for why one key carries both verbs. */
    void groupOrToggleSelectionMacros();

    /** Selects every member of `macroId` — so drag/delete reuse the plain multi-select path
     *  unchanged — replacing the current selection unless `additive`. No-op if the id is
     *  unknown. */
    void selectMacro(const juce::String& macroId, bool additive);

    /** True when every member of `macroId` is selected and nothing else is — the state a click
     *  on its collapsed card produces. */
    bool isMacroSelected(const juce::String& macroId) const;

    /** The macro `nodeId` belongs to, or nullptr if it isn't a member of any. Lets a caller that
     *  only has a node (e.g. ModuleComponent's right-click menu) offer macro-scoped actions
     *  without reaching into GraphEditor's private uuid-resolution machinery itself. */
    const synth::Macro* macroForNode(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Expands or collapses a macro. Collapsing hides its member ModuleComponents (they stay
     *  alive, just invisible, so their positions keep tracking a card drag) and shows one card
     *  in their place; expanding reverses that. No graph change either way. Undoable. */
    void setMacroCollapsed(const juce::String& macroId, bool collapsed);

    void renameMacro(const juce::String& macroId, const juce::String& newName);
    void setMacroColour(const juce::String& macroId, juce::Colour colour);

    /** Async rename affordance that does NOT depend on a MacroCardComponent existing — used by the
     *  expanded-macro hull's right-click menu (buildMacroMenu's default "Rename..." handler),
     *  where there is no card to host an inline `TextEditor`. Mirrors
     *  MainComponent::promptSaveSnippet's `juce::AlertWindow` idiom exactly (SafePointer +
     *  ModalCallbackFunction + a unique_ptr taken inside the callback). Prefilled with the
     *  macro's current name; empty/whitespace-only input cancels without renaming. The collapsed
     *  card keeps its own nicer inline rename (MacroCardComponent::beginRename) — this is only
     *  for the case that has no card. */
    void promptRenameMacro(const juce::String& macroId);

    /** Opens the shared synth::ui::ColourPickerPopup over `screenArea` (screen coordinates) for
     *  `macroId` — the same picker the timeline ruler's marker menu and the track header swatch
     *  use (TimelineRulerComponent::buildMarkerColourPicker is the exact pattern this mirrors).
     *  Live preview while the user drags (writes straight to the macro, no undo step, so dragging
     *  never floods the undo stack); ONE undo step on commit, recorded via setMacroColour so the
     *  undo restores the ORIGINAL colour rather than the last preview value. A no-op if `macroId`
     *  doesn't resolve. */
    void promptRecolourMacro(const juce::String& macroId, juce::Rectangle<int> screenArea);

    /** Where the recolour picker's favourites shelf persists to. Null (the default) means
     *  in-memory-only favourites, which is what a headless test with no ApplicationProperties
     *  gets — mirrors TimelineRulerComponent::setPropertiesFile exactly. */
    void setPropertiesFile(juce::PropertiesFile* props) noexcept { propertiesFile_ = props; }

    /** Removes the macro AND every one of its member nodes, as one undo step (right-click "Delete"
     *  on a collapsed card, or Delete/Backspace while its members are the whole selection). This
     *  is the "delete a macro deletes its modules" path — ungroupSelection() above is the
     *  opposite: it keeps the modules. */
    void deleteMacroAndMembers(const juce::String& macroId);

    // ---- Macro bypass/mute (P8-15d, T142, docs/macros.md §5.6) -------------------------------
    //
    // "Bypass macro" / "Mute macro" are FAN-OUT COMMANDS over a macro's members, not a
    // macro-level reinterpretation of the contract — a macro has no processBlock and no
    // bypass/mute state of its own. Each fan-out calls ModuleBase::setBypassed/setMuted (already
    // parameter writes via setValueNotifyingHost) on every member, so every member's own
    // processBlock honours the two-branch bypass/mute contract exactly as it already does for a
    // per-module toggle — there is nothing new to get wrong. The whole fan-out is ONE undo step
    // (recordStructuralChange, the same before/after graph-JSON snapshot applySmartSuggestions
    // uses to batch several connections into one step), not one undo per member.

    /** Tri-state read of `macroId`'s members' bypass (or mute) state, feeding §5.6's
     *  "mixed-state members show an indeterminate indicator" requirement. `Muted` additionally
     *  skips (does not count as ON or OFF) any member with no "muted" parameter at all
     *  (ModuleBase::hasMuteParameter() — Macro In/Out and their MIDI variants among them, exactly
     *  like Track In / Rec Tap / Track Audio) — such a member has nothing to report. A macro that
     *  resolves to zero queryable members (unknown id, or every member skipped) reads AllOff. */
    enum class MacroToggleState { AllOff, AllOn, Mixed };
    MacroToggleState macroBypassState(const juce::String& macroId) const;
    MacroToggleState macroMuteState(const juce::String& macroId) const;

    /** Sets `ModuleBase::setBypassed(bypassed)` on every member of `macroId`, as one undo step.
     *  A mixed starting state still lands every member on the SAME target state — that is the
     *  point of the command (§5.6). No-op (no undo entry) if `macroId` doesn't resolve or has no
     *  members that are ModuleBase instances. */
    void setMacroBypassed(const juce::String& macroId, bool bypassed);

    /** Sets `ModuleBase::setMuted(muted)` on every member of `macroId` that HAS a "muted"
     *  parameter (ModuleBase::hasMuteParameter()) — a member without one is left alone rather
     *  than crashing on `setMuted`'s unchecked dereference, matching §7 item 1's note that this
     *  fan-out has to guard against members like Macro In/Out that carry no mute parameter. One
     *  undo step; a no-op (no undo entry) if `macroId` doesn't resolve or no member is
     *  mutable-eligible. */
    void setMacroMuted(const juce::String& macroId, bool muted);

    /** Toggles `macroId`'s bypass state: mirrors toggleSelectionMacrosCollapsed's deterministic
     *  convergence rule (see its own header comment for the rationale) — `Mixed` or `AllOff`
     *  converges to bypassing every member; `AllOn` converges to clearing every member. Refused
     *  via onStatusMessage (no-op) if `macroId` doesn't resolve. */
    void toggleMacroBypassed(const juce::String& macroId);

    /** Toggles `macroId`'s mute state with the same convergence rule as toggleMacroBypassed,
     *  computed over mute-eligible members only (see macroMuteState). Refused via onStatusMessage
     *  (no-op) if `macroId` resolves to no member with a "muted" parameter. */
    void toggleMacroMuted(const juce::String& macroId);

    /** Canvas-space bounds of an EXPANDED macro's grouping hull (the union of its live member
     *  ModuleComponent bounds, expanded by the hull margin), or an empty rect if the macro is
     *  collapsed / unknown / has no resolvable members. The ONE definition — paint and
     *  hit-testing must never diverge. */
    juce::Rectangle<int> macroHullBounds(const juce::String& macroId) const;

    /** The expanded macro whose hull contains `canvasPos`, or an empty string. Smallest hull
     *  wins when hulls overlap. */
    juce::String macroHullAt(juce::Point<int> canvasPos) const;

    /** Canvas-space bounds of an EXPANDED macro's name chip - the tab drawn on the hull's top
     *  edge, which doubles as the macro's drag handle. Empty rect if the macro is collapsed or
     *  unknown. The ONE definition: paint and hit-testing must never diverge. */
    juce::Rectangle<int> macroChipBounds(const juce::String& macroId) const;

    /** The expanded macro whose name chip contains `canvasPos`, or an empty string. Smallest
     *  chip wins if two ever overlap. */
    juce::String macroChipAt(juce::Point<int> canvasPos) const;

    /** Test accessor: the live MacroCardComponent for `macroId`, or nullptr. Exposed so a test can
     *  move the real card directly (bypassing the drag gesture entirely) to prove
     *  rebuildVisibleCables() anchors on the card's CURRENT bounds rather than the persisted
     *  `macro.bounds`, which only updates on finalizeMacroCardDrag — see Fix 3/P8-12 follow-up. */
    MacroCardComponent* getMacroCardForTest(const juce::String& macroId);

    /** Builds the recolour popup with the EXACT onPreview/onCommit callbacks promptRecolourMacro
     *  uses, without launching a juce::CallOutBox — mirrors
     *  TimelineRulerComponent::createMarkerColourPickerForTest(). Null when `macroId` doesn't
     *  resolve. Test seam: a headless test drives the returned popup's preview/commit directly
     *  rather than duplicating the recolour logic. */
    std::unique_ptr<synth::ui::ColourPickerPopup> createMacroColourPickerForTest(const juce::String& macroId);

    /** The shared macro actions menu — right-click a collapsed card or right-click inside an
     *  expanded macro's hull both build this SAME menu (Fix 4/P8-12 follow-up), so the two paths
     *  cannot drift apart. Returns an empty menu if `macroId` doesn't resolve.
     *
     *  `renameAction`, when supplied, replaces the default "Rename..." item's handler — the
     *  collapsed card passes its own inline-editor opener (MacroCardComponent::beginRename) here;
     *  every other caller (the hull menu) leaves it empty and gets promptRenameMacro's dialog,
     *  since there is no card to host an inline editor there. */
    juce::PopupMenu buildMacroMenu(const juce::String& macroId, std::function<void()> renameAction = nullptr);

    /** Live bounds + colour category for the currently-resolvable members of `macroId`, in
     *  member order — the data the collapsed card's content preview (Fix 6/P8-12 follow-up)
     *  scales into its card. A member uuid that doesn't resolve to a live ModuleComponent is
     *  skipped rather than drawn as a blank box. */
    struct MacroMemberPreview {
        juce::Rectangle<int> bounds;
        synth::ui::ModuleCategory category = synth::ui::ModuleCategory::Utility;
    };
    std::vector<MacroMemberPreview> macroMemberPreviews(const juce::String& macroId) const;

    /** Display names of `macroId`'s members, in order — feeds MacroCardComponent's tooltip so a
     *  collapsed macro's contents are discoverable without expanding it. */
    juce::StringArray macroMemberNames(const juce::String& macroId) const;

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
    // §7 items 3 and 5 of docs/macros.md, unified into ONE modal per an explicit founder request
    // rather than piecemeal "Add Input"/"Add Output"/"Rename"/"Reorder" menu actions. Every entry
    // point below is a single recordGraphAndMacroChange transaction, so add/remove/rename/reorder
    // and (the one that matters most) a shape change are each exactly one undo step — a shape
    // change is a delete-node + create-node + rewire landing together, never two undos. -----

    /** Adds a new port to `macroId`: constructs the matching Macro In/Out or Macro MIDI In/Out
     *  node (never by the module library or replace menu — the same internal-only construction
     *  every other §5.1 node type gets), gives it `shape`/`voiceCount` (ignored for
     *  MacroPortKind::Midi, which carries no shape — §5.1), adds it as a macro member, and
     *  appends a synth::MacroPort fronting it. `shape`/`voiceCount` are set on the node BEFORE it
     *  is wired into the live graph, honouring §5.3's "decided at construction, then fixed" rule.
     *  `portName` empty falls back to a direction/kind default ("Input"/"Output"/"MIDI In"/
     *  "MIDI Out"). Returns the new port's node uuid, or an empty string if `macroId` doesn't
     *  resolve or the node type failed to construct. */
    juce::String addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                              MacroPortShape shape, int voiceCount, const juce::String& portName);

    /** Removes the port fronted by `nodeUuid` from `macroId` by deleting its node — reuses the
     *  ordinary multi-select delete path exactly like deleteMacroAndMembers does for a whole
     *  macro, so undo/dirty/timeline-reconcile behave identically, and macros.retainOnly() (run
     *  from updateComponents()) drops the now-orphaned synth::MacroPort as part of the same step. */
    void removeMacroPort(const juce::String& macroId, const juce::String& nodeUuid);

    /** Renames the port fronted by `nodeUuid`. Empty/whitespace-only `newName` is a no-op (mirrors
     *  promptRenameMacro's own convention) rather than clearing the name. Touches only `macros` —
     *  never the graph — so this pushes a MacroSnapshotAction alone. */
    void renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid, const juce::String& newName);

    /** Moves the port fronted by `nodeUuid` one step earlier/later in its OWN direction's draw
     *  order (inputs are reordered against other inputs, outputs against other outputs — the two
     *  sides of the card, §5.4) by swapping `order` with its neighbour. A no-op at either end of
     *  its group. */
    void moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp);

    /** Changes the shape of an existing audio/CV port — the one operation §5.3 calls out as
     *  needing to read as ONE edit despite being delete-node + create-node + rewire underneath.
     *  Snapshots every graph connection touching the old node, constructs a fresh node of the
     *  SAME direction/kind with the NEW shape, removes the old node, replays each saved connection
     *  onto the new node's matching raw channel ONLY when that channel is still active under the
     *  new shape (role == PortRole::Audio) — exactly "a jack that disappears takes its cables with
     *  it" (dropRoutingsOnHiddenJacks' rule), applied honestly rather than silently adapted at the
     *  boundary (§5.3's closing rule) — then rewrites the macro's member/port entries to the new
     *  uuid, keeping name/order/direction/kind untouched. A no-op (returns empty) for a MIDI-kind
     *  port, which has no shape to change (§5.1). Returns the new node's uuid. */
    juce::String changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                      MacroPortShape newShape, int newVoiceCount);

    /** Opens the "Configure I/O" modal (MacroPortConfigDialog) for `macroId` — the single entry
     *  point every port add/remove/rename/reorder/shape-change above is reached through when the
     *  user drives it from the UI; every method above is independently callable (and tested) with
     *  no dialog involved. No-op if `macroId` doesn't resolve. */
    void promptConfigureMacroIO(const juce::String& macroId);

    // ---- Macro card jacks (P8-15c, T141, docs/macros.md §7 item 4) -----------------------------

    /** One port's on-card jack, in the collapsed card's OWN local coordinates (add the live
     *  card's top-left — macroCableAnchorBounds — for canvas coords). Inputs run down the card's
     *  left edge, outputs down its right edge, each side ordered by MacroPort::order — the SAME
     *  order macroPortRowsForDialog sorts by, so a port's jack position and its row in the
     *  Configure I/O dialog always agree on which port is "first". The ONE layout definition:
     *  MacroCardComponent::paint(), buildVisibleCables()'s boundary-cable anchoring, and
     *  endConnectionDrag()'s jack hit-test all read this rather than recomputing it, so the drawn
     *  dot, the anchored cable and the drop target can never drift apart. */
    struct MacroCardPort {
        juce::String nodeUuid;
        bool isInput = false;
        synth::MacroPortKind kind = synth::MacroPortKind::AudioCV;
        juce::String name;
        juce::Point<int> jackPos; // card-local
    };

    /** Every port on `macroId`'s collapsed card, laid out. Empty if `macroId` doesn't resolve or
     *  has no ports yet (a macro with no configured ports draws no jacks — just the plain P8-12
     *  card). */
    std::vector<MacroCardPort> macroCardPortLayout(const juce::String& macroId) const;

    /** The port whose jack contains `cardLocalPos` (within the same click tolerance a module's
     *  own jack uses), or nullopt. `cardLocalPos` is in the card's OWN local coordinates, matching
     *  MacroCardPort::jackPos — a caller starting from a canvas/screen point converts via the live
     *  card's bounds first (see endConnectionDrag). */
    std::optional<MacroCardPort> macroCardPortForPoint(const juce::String& macroId,
                                                       juce::Point<int> cardLocalPos) const;

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
    //
    // All three run through the snippet pipeline rather than a parallel copy format, which is what
    // makes the wiring right: a snippet keeps only connections with BOTH endpoints inside the
    // group and re-expresses modulation as intent, and insertion renumbers every id. The copies
    // therefore wire to each other, never back to the originals, and nothing is spliced into the
    // surrounding patch — the same rule a snippet dropped from the library follows.

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

    // Double-click a connected jack to disconnect (issue #216). On by default.
    void setDoubleClickPortDisconnectEnabled(bool enabled) { doubleClickPortDisconnectEnabled = enabled; }
    bool getDoubleClickPortDisconnectEnabled() const noexcept { return doubleClickPortDisconnectEnabled; }

    // Default jack layout for newly created modules that expose the Dual I/O parameter.
    // false (default): one collapsed "Audio" jack. true: split Left/Right by default.
    void setDefaultDualIOForNewModules(bool enabled) { defaultDualIOForNewModules = enabled; }

    /** Re-lays every stereo-capable module already on the canvas to `dual`.
     *
     *  Deliberately separate from setDefaultDualIOForNewModules: that one is also called at startup
     *  and whenever the Settings window opens, and retro-applying there would rewrite the user's
     *  patch (collapsing the factory preset's voice modules on every launch). Only a deliberate
     *  change of the preference calls this. Card heights do not move — the gutter reserves room for
     *  the dual layout in both states. */
    void applyDualIOToExistingModules(bool dual);

    /** Unhooks a collapsed split-block module's hidden right leg, RE-POINTING each cable onto the
     *  matching channel of the surviving left block wherever the far end still exposes it (and
     *  simply dropping it where it does not). Graph-level, so it works before the cards exist.
     *  No-op for FX pairs, whose collapsed jack legitimately still owns both raw legs.
     *
     *  The re-point is what keeps a collapse level across the stereo field: without it, collapsing
     *  the default patch's VCA starved the whole FX tail's right channel and the mix jumped left. */
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

    // ---- Custom module titles -----------------------------------------------
    // A user-set card title, stored as the node property "displayName". Message-thread ONLY and
    // display-only, which is why — unlike "uuid" — it is deliberately NOT mirrored into the
    // processor: nothing on the audio thread reads a card title, so there is no lock-free read to
    // make sound, and adding a mirror would only create a second copy to keep in sync. Do not
    // "fix" this by mirroring it.
    //
    // It is also deliberately NOT the processor's own name: ModuleBase::getName() is the
    // auto-numbered "Chorus 2" that AudioEngine::updateModuleNames() recomputes wholesale on every
    // graph change (it strips trailing digits to renumber), so a custom title written there would
    // be clobbered by the next node added. The numbered name stays the fallback.

    /** The user's custom title for a node, or an empty string when it has none. */
    juce::String getModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Sets (or, given blank/whitespace, clears) a node's custom title. Undoable. */
    void setModuleDisplayName(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& name);

    /** Title a card should paint: the custom one when set, else the auto-numbered module name. */
    juce::String getModuleTitle(juce::AudioProcessorGraph::NodeID nodeId, juce::AudioProcessor* processor) const;

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
        return cursorCanvasPos - juce::Point<int>(dragPreviewW / 2, dragPreviewH / 2);
    }

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
    // Proximity-based cable suggestions while placing a module. One setting covers Off /
    // library-only / free-main-I/O moves / all moves (see SmartConnectionMode).
    enum class SmartConnectionMode { Off, NewOnly, NewAndUnwired, AllMoves };

    /** One suggested cable shown as a frosted preview during drag; applied on drop.
     *
     *  `isInsert` turns the same record into an insert-in-series: the ghost is spliced into cabling
     *  that already exists rather than given a jack of its own. Two cable SETS then come with it —
     *  `doomedLinks` (upstream → sink, to be removed, drawn dashed) and `upstreamCables`
     *  (upstream → ghost, replacing them) — plus this record's own ghostJack → neighborJack.
     *
     *  Both sets describe the WHOLE insert group, not just this record's leg, and both are already
     *  deduped, so applying them once per suggestion is idempotent. They are deliberately not
     *  per-leg: a jack pair dropped by the fan dedupe must NOT take its doomed link with it, or the
     *  cable it represented survives and sums into the sink alongside the ghost's output.
     *
     *  Only offered for the graph's terminal audio sink; see refreshSmartSuggestions. */
    struct SmartSuggestion {
        /** One cable of an insert, at visible-jack level. Endpoints are for preview paint only. */
        struct InsertLink {
            int fromJack = 0; // upstream visible OUTPUT jack
            int toJack = 0;   // sink visible input jack (doomed), or ghost visible input jack (new)
            juce::Point<float> p1{}, p2{};

            bool operator==(const InsertLink& o) const noexcept { return fromJack == o.fromJack && toJack == o.toJack; }
        };

        /** When true the dragged module is the cable source; when false it is the destination. */
        bool ghostIsSource = true;
        juce::AudioProcessorGraph::NodeID neighborId{};
        int ghostJack = 0;
        int neighborJack = 0;
        bool isMidi = false;
        juce::Point<float> p1{}, p2{}; // head endpoints; mainPreviewLegs is what actually gets drawn
        synth::ui::CableSignal signal = synth::ui::CableSignal::Audio;
        synth::ui::ModuleCategory sourceCategory = synth::ui::ModuleCategory::Utility;

        /** Every frosted segment the preview must draw, so it shows exactly what the drop will wire.
         *  ONE suggestion is not one drawn cable: connectPorts fans a collapsed jack across a whole
         *  raw pair, and when the far end fronts those raws as two separate visible jacks (the
         *  terminal sink does — it has no ModuleBase to group them) that is two cables on screen.
         *  Resolved from the same PolyLink connectPorts uses and deduped to distinct visible jack
         *  pairs, because N graph edges through one jack pair are still one cable. */
        std::vector<InsertLink> mainPreviewLegs;     // ghostJack → neighborJack
        std::vector<InsertLink> upstreamPreviewLegs; // upstream → ghost (insert only)

        // ---- Insert-in-series (audio only; ghostIsSource is always true) ----
        bool isInsert = false;
        juce::AudioProcessorGraph::NodeID upstreamId{}; // node whose cabling gets rerouted
        std::vector<InsertLink> doomedLinks;            // upstream → sink, every one to remove
        std::vector<InsertLink> upstreamCables;         // upstream → ghost, replacing them
        synth::ui::ModuleCategory upstreamCategory = synth::ui::ModuleCategory::Utility;

        bool operator==(const SmartSuggestion& o) const noexcept {
            return ghostIsSource == o.ghostIsSource && neighborId == o.neighborId && ghostJack == o.ghostJack &&
                   neighborJack == o.neighborJack && isMidi == o.isMidi && isInsert == o.isInsert &&
                   upstreamId == o.upstreamId && doomedLinks == o.doomedLinks && upstreamCables == o.upstreamCables &&
                   mainPreviewLegs == o.mainPreviewLegs && upstreamPreviewLegs == o.upstreamPreviewLegs;
        }
        bool operator!=(const SmartSuggestion& o) const noexcept { return !(*this == o); }
    };

    void setSmartConnectionMode(SmartConnectionMode mode) { smartConnectionMode = mode; }
    SmartConnectionMode getSmartConnectionMode() const noexcept { return smartConnectionMode; }

    /** CTRL turns a proximity suggestion into an insert-in-series. Ctrl on every platform (it is
     *  the literal Control key on macOS too, NOT Cmd) — Cmd was tried first and lost, because
     *  Cmd-click is the additive-selection modifier and the two gestures are indistinguishable at
     *  mouse-down.
     *
     *  Sampled LIVE on every drag tick rather than latched at mouse-down, so BOTH orderings work:
     *  press-then-Ctrl (the modifier is picked up on the next tick) and Ctrl-then-press (the
     *  deferred classification in `ModuleComponent::mouseDown` arms a drag as well as a selection
     *  toggle, and this read simply sees Ctrl already down).
     *
     *  Tests set the override; production leaves it empty and reads the real keyboard. */
    void setInsertModifierOverrideForTests(std::optional<bool> down) { insertModifierOverride = down; }
    bool isInsertModifierDown() const {
        return insertModifierOverride.has_value() ? *insertModifierOverride
                                                  : juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown();
    }

    /** Persist / restore helpers (Preferences tab + MainComponent launch restore). */
    static SmartConnectionMode smartConnectionModeFromString(const juce::String& s);
    static juce::String smartConnectionModeToString(SmartConnectionMode mode);

    /** Wires two visible jacks the same way a completed cable-drag does (poly fan, MIDI,
     *  attenuverter for mono mod CV). When recordUndo is false the caller owns the transaction
     *  (e.g. inside an existing recordStructuralChange). */
    void connectPorts(juce::AudioProcessorGraph::NodeID srcId, int srcJack, juce::AudioProcessorGraph::NodeID dstId,
                      int dstJack, bool isMidi, bool recordUndo = true);

    // Test accessors
    int getSmartSuggestionCount() const noexcept { return (int)smartSuggestions.size(); }
    const std::vector<SmartSuggestion>& getSmartSuggestions() const noexcept { return smartSuggestions; }
    bool nodeHasCables(juce::AudioProcessorGraph::NodeID nodeId) const;
    /** Runs just the drag tick's modifier re-sample, so a test can exercise a press/release that
     *  happens without any mouse movement without needing a real 30 Hz timer. */
    void pumpDragModifierTickForTests() { refreshSuggestionsIfInsertModifierChanged(); }

    /** Port centre inside a bounds rect — must agree with ModuleComponent::getPortCenter. */
    static juce::Point<int> estimatePortCenter(juce::AudioProcessor* proc, juce::Rectangle<int> bounds, int jack,
                                               bool isInput, bool isMidi);

    /** Audio-jack occupancy, for asserting that a reroute left nothing dangling. */
    bool isInputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const {
        return isInputJackFree(nodeId, jack, false);
    }
    bool isOutputJackFreeForTests(juce::AudioProcessorGraph::NodeID nodeId, int jack) const {
        return isOutputJackFree(nodeId, jack, false);
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
                                   const std::function<void(juce::AudioProcessor&)>& configure);

    /** Creates a Hosted Plugin node already pointed at `identity`.
     *
     *  Deliberately a thin wrapper over addModuleAtCanvasPosition rather than a second add path: the
     *  identity is set through the same `configure` hook the Sampler's dropped file uses, so it is in
     *  place before the node joins the graph and is therefore inside the undo snapshot — undo/redo of
     *  a plugin add behaves exactly like undo/redo of any other module add, including remembering
     *  WHICH plugin on redo. The actual load is asynchronous and resolves through the default
     *  backend's scan service, so a canvas with no service installed adds a placeholder rather than
     *  failing the add. */
    void addHostedPluginAtCanvasPosition(const synth::PluginIdentity& identity, juce::Point<int> dropPos);

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

    // Drag-preview state (grid + landing ghost)
    bool dragPreviewActive = false;
    int dragPreviewW = 0, dragPreviewH = 0;
    juce::AudioProcessorGraph::NodeID dragPreviewSelfId{};
    juce::Rectangle<int> dragPreviewGhost;
    // The un-de-overlapped rect under the cursor. Suggestion candidacy is judged from this, so
    // aiming at a gap narrower than the card still counts; the card still LANDS at dragPreviewGhost.
    juce::Rectangle<int> dragPreviewAim;
    // Library-drag probe: jack metadata for a module that does not exist on the canvas yet.
    bool dragPreviewIsSnippet = false;
    std::unique_ptr<juce::AudioProcessor> dragPreviewProbe;

    // Smart-connection suggestions for the active drag preview.
    SmartConnectionMode smartConnectionMode = SmartConnectionMode::NewAndUnwired;
    std::optional<bool> insertModifierOverride; // tests only; empty means read the real keyboard
    // Last modifier state the drag tick saw, so a press/release that happens WITHOUT a mouse move
    // still re-evaluates the suggestions exactly once (see timerCallback).
    bool lastSampledInsertModifier = false;
    std::vector<SmartSuggestion> smartSuggestions;
    static constexpr float kSmartConnectionProximityPx = 96.0f;

    void refreshSmartSuggestions();
    void applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo);
    void clearSmartSuggestions();
    bool shouldOfferSmartConnections() const;
    void applyDefaultDualIOForNewModule(juce::AudioProcessor& processor, const juce::String& moduleType) const;
    /** Re-evaluates the suggestions when the insert modifier changed since the last drag tick.
     *  A modifier press/release is not a mouse move, so nothing else would notice it. */
    void refreshSuggestionsIfInsertModifierChanged();
    bool isInputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const;
    bool isOutputJackFree(juce::AudioProcessorGraph::NodeID nodeId, int jack, bool isMidi) const;
    bool areJacksAlreadyConnected(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                                  juce::AudioProcessorGraph::NodeID dstId, int dstJack, bool isMidi) const;

    /** The cabling feeding an audio input jack, at CABLE level (visible output jacks of the feeding
     *  node, never raw graph edges). */
    struct UpstreamLink {
        juce::AudioProcessorGraph::NodeID nodeId{};
        /** Every distinct visible OUTPUT jack of that ONE node feeding the destination jack.
         *
         *  More than one is normal, not exotic: a dual upstream's Left and Right legs both landing
         *  on a collapsed mono input is our own canonical dual-to-mono wiring — it is what the Dual
         *  I/O toggle rewire and a hand-dragged pair of cables both produce. Refusing multi-feed
         *  outright meant insert silently did nothing for that extremely common shape. Feeds from
         *  DIFFERENT nodes are still refused: that is a hand-built mix, and rerouting it would
         *  change what sums where. */
        std::vector<int> jacks;
    };

    /** Resolves the cabling currently feeding `dstJack`, or nullopt when the jack is free, is fed
     *  from more than one NODE (a hand-built mix), or is fed through a mod routing / attenuverter
     *  chain (neither is ever silently rerouted). Insert-in-series needs this to succeed. */
    std::optional<UpstreamLink> findSingleUpstreamAudioLink(juce::AudioProcessorGraph::NodeID dstId, int dstJack) const;

    /** Removes the audio cable between two visible jacks — the exact inverse of connectPorts, so a
     *  collapsed stereo wire drops both raw legs. Caller owns the undo transaction. */
    void disconnectAudioLink(juce::AudioProcessorGraph::NodeID srcId, int srcJack,
                             juce::AudioProcessorGraph::NodeID dstId, int dstJack);

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
    juce::Point<int> estimateSnippetSize(const juce::String& payload) const;

    /** Repaints only the module components whose selected state actually changed. Selection
     *  changes must never trigger a full-canvas repaint storm during a marquee drag. */
    void applySelectionChange(const std::vector<juce::AudioProcessorGraph::NodeID>& newSelection);

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
     *  syncs ModuleComponents themselves. */
    void syncMacroCards();

    /** The raw collapse/expand mutation, with NO undo recording of its own — so a caller that
     *  toggles several macros at once (toggleSelectionMacrosCollapsed) can wrap the whole batch
     *  in ONE recordGraphAndMacroChange instead of one per macro. setMacroCollapsed is the
     *  single-macro, self-recording wrapper around this. */
    void applyMacroCollapsed(const juce::String& macroId, bool collapsed);

    /** The NodeID currently backing `memberUuid`, or an invalid NodeID if none does. Macros
     *  reference members by persistent uuid (Source/CLAUDE.md's uuid-mirroring invariant); this
     *  is the one place that resolves a macro member back to a live graph node. */
    juce::AudioProcessorGraph::NodeID resolveMemberNodeId(const juce::String& memberUuid) const;

    /** The persistent "uuid" node property for `nodeId`, or an empty string if the node doesn't
     *  exist or was never assigned one. */
    juce::String nodeUuidFor(juce::AudioProcessorGraph::NodeID nodeId) const;

    /** Every member of `macroId` that currently resolves to a live, ModuleBase-backed graph node,
     *  in member order (§5.6 bypass/mute fan-out, T142). The ONE place that decides "what counts
     *  as a fannable member" — macroBypassState/macroMuteState and setMacroBypassed/setMacroMuted
     *  all read this rather than re-resolving members themselves, so the state a menu item shows
     *  and the members the command actually touches can never disagree. Empty if `macroId`
     *  doesn't resolve. */
    std::vector<juce::AudioProcessorGraph::NodeID> resolvedMacroMemberModuleNodes(const juce::String& macroId) const;

    /** True if at least one of `macroId`'s members has a "muted" parameter
     *  (ModuleBase::hasMuteParameter()) — used to decide whether a mute fan-out is a real no-op
     *  (a macro made entirely of Macro In/Out-like nodes) rather than silently doing nothing. */
    bool macroHasMuteEligibleMember(const juce::String& macroId) const;

    /** The rectangle to anchor a collapsed macro's boundary cables against: the LIVE
     *  MacroCardComponent's bounds while its card exists (so a cable tracks the card mid-drag,
     *  before finalizeMacroCardDrag writes the settled position back to `macro.bounds`), falling
     *  back to the persisted `macro.bounds` when no card component exists (headless tests build a
     *  MacroSet without ever constructing GraphEditor's card components). The ONE place
     *  rebuildVisibleCables reads a collapsed macro's position from — do not read `macro.bounds`
     *  directly for cable anchoring anywhere else. */
    juce::Rectangle<int> macroCableAnchorBounds(const synth::Macro& macro) const;

    /** Shared by promptRecolourMacro and createMacroColourPickerForTest: builds the popup with
     *  the real preview/commit wiring, without launching a juce::CallOutBox. Mirrors
     *  TimelineRulerComponent::buildMarkerColourPicker. Null when `macroId` doesn't resolve. */
    std::unique_ptr<synth::ui::ColourPickerPopup> buildMacroColourPicker(const juce::String& macroId);

    // ---- Macro I/O (P8-15b) helpers ----

    /** The module-library type name to construct for a port of this direction/kind — "Macro In"/
     *  "Macro Out"/"Macro MIDI In"/"Macro MIDI Out" (§5.1). */
    static juce::String macroPortNodeTypeName(bool isInput, synth::MacroPortKind kind);

    /** Fallback port name when the caller supplies none/blank. */
    static juce::String defaultMacroPortName(bool isInput, synth::MacroPortKind kind);

    /** One past the highest existing `order` among `macro`'s ports sharing `isInput` (0 if none) —
     *  a fresh port's draw order, keeping it last on its own side of the card. */
    static int nextMacroPortOrder(const synth::Macro& macro, bool isInput);

    /** The "shape from a dropped cable" convenience (§5.3): endConnectionDrag calls this when a
     *  cable is released over a COLLAPSED macro card with no jack underneath it (none exist until
     *  T141) rather than on another module's port. Creates a Mono (or, for a MIDI-sourced drag,
     *  MIDI) port sized from the cable's own direction/kind, adds it to `macroId`, and wires the
     *  EXTERNAL end of the drag straight to the new node's raw channel 0 — all as ONE
     *  recordGraphAndMacroChange transaction. Deliberately does NOT also wire the new port to any
     *  interior member: the macro is collapsed, so there is nothing visible to guess a target
     *  from — see docs/macros.md §5.3. Shape inference from the dragged cable's own poly/stereo
     *  fan is deferred (T140 report) — this always creates Mono; the modal remains the way to get
     *  Stereo/Poly-N from this gesture. */
    void createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput, bool isMidi,
                                         juce::AudioProcessorGraph::NodeID otherNodeId, int otherVisibleJack);

    /** Snapshot of `macroId`'s ports for the Configure I/O dialog: inputs before outputs, then
     *  each side by its own `order` — live shape/voice count read off the actual node for an
     *  AudioCV port (a MacroPort carries no shape of its own, §5.2). Empty if `macroId` doesn't
     *  resolve. */
    std::vector<synth::ui::MacroPortConfigDialog::PortRow> macroPortRowsForDialog(const juce::String& macroId) const;

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
     *  `content.repaint()` in this file goes through here. */
    void repaintCanvas();

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
