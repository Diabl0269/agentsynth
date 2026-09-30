#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include "UI/Mixer/MixerMirrorController.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class AppUndoManager;
class GraphEditor;
class AudioEngine;

namespace synth {
class MidiRemoteProjectDoc;
} // namespace synth

namespace synth::midi {
class RemoteEngine;
class MidiLearnController;
} // namespace synth::midi

// BottomDockComponent.h (docs/mixer/panel.md): the bottom dock's own tab strip.
//
// The ONE component MainComponent.h holds for the dock (see
// docs/mixer/panel.md#what-the-mixer-shows): owns `timelinePanel` by reference (NOT a
// copy/move -- MainComponent still owns and constructs it) and a MixerPanelComponent by value.
// MainComponent::resized()'s existing dock carve (`timelinePanel.setBounds(...)`) becomes
// `bottomDock.setBounds(...)` -- one line changed, not two new carve blocks; the open/close slide,
// height and persisted-visible state stay MainComponent's own (isBottomDockVisible/timelineSlide_),
// gating the whole dock rather than just the Timeline tab.
namespace synth::ui {

class BottomDockComponent : public juce::Component {
public:
    // MidiRemote is a third, always-offered tab -- unlike Mixer it has no OwnPanel/Window
    // placement variant, so there is no MidiRemote counterpart to mixerTabEnabled_
    // (see docs/control/midi-remote-ui.md#the-controllers-panel).
    enum class Tab { Timeline, Mixer, MidiRemote };

    // `appProperties`/`lookAndFeel`/`shortcutManager` are forwarded straight into
    // timelineHost_/mixerHost_ (both DetachablePanelHost) -- see that class for what each is
    // for. `lookAndFeel`/`shortcutManager` may be null in a headless test that never detaches a
    // panel (see docs/mixer/panel.md).
    BottomDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine, synth::TimelineDoc& doc,
                        AppUndoManager& undoManager, GraphEditor& graphEditor,
                        juce::ApplicationProperties& appProperties, synth::theme::AppLookAndFeel* lookAndFeel,
                        ShortcutManager* shortcutManager);

    /** Reads the persisted active tab and tab order once ("bottomDockActiveTab" /
     *  "bottomDockTabOrder", docs/layout/chrome.md's "Panel collapse and persistence" table);
     *  writes the former on every tab switch and the latter on every drag-reorder. */
    void setApplicationProperties(juce::ApplicationProperties* properties);

    /** MainComponent::reconcileTimelineAfterGraphChange -- fired after an insert-list mutation
     *  changes the graph (add/reorder/remove). */
    void setOnGraphTopologyChanged(std::function<void()> callback);
    /** MainComponent::makeChannelForNode -- fired by Direct's "Make channel" button. */
    void setOnMakeChannelForNode(std::function<void(juce::AudioProcessorGraph::NodeID)> callback);
    /** MainComponent wires this to performTrackEdit(setTrackArmed(...)) -- fired by the
     *  Arm key (rebindable "timelineArmFocusedTrack") when a linked strip is focused. Sibling
     *  forwarder to setOnGraphTopologyChanged/setOnMakeChannelForNode above: MainComponent talks
     *  to the dock, never reaches through getMixerPanel() to set the panel's own callback field. */
    void setOnArmTrack(std::function<void(synth::TrackId)> callback);
    /** MainComponent wires this to performTrackEdit(moveTrack) plus a mixer rebuild -- fired when a mixer
     *  track column is dropped in a new place. */
    void setOnMoveTrack(std::function<void(synth::TrackId, int newIndex)> callback);

    /** Whether the active tab has a side pane (today only the Mixer does). */
    bool hasActiveSidePane() const noexcept { return activeTab_ == Tab::Mixer; }
    /** Shows or hides the active tab's side pane; `forceOpen` only ever opens it. False, and nothing
     *  changes, when the active tab has none. */
    bool toggleActiveSidePane(bool forceOpen = false);

    Tab getActiveTab() const noexcept { return activeTab_; }
    bool isMixerTabActive() const noexcept { return activeTab_ == Tab::Mixer; }
    bool isMidiRemoteTabActive() const noexcept { return activeTab_ == Tab::MidiRemote; }
    /** Switches tabs (persisting the choice) and lays out; a no-op if already on `tab`. Does NOT
     *  open/close the dock itself -- that stays MainComponent's own isBottomDockVisible/
     *  beginPanelSlide() (see the class comment). */
    void setActiveTab(Tab tab);

    /** True while at least one tab is currently offered in the strip. MainComponent hides
     *  the whole dock when this goes false and reopens it when it comes back true (see
     *  bottomDockAutoHiddenByEmptyTabs_'s own comment). */
    bool hasAnyVisibleTab() const noexcept;

    /** Re-runs the mixer panel's snapshot + column rebuild -- call after every graph/timeline/
     *  macro change (MainComponent's existing reconcile funnel is the natural place). Also
     *  rebuilds the "both places" mirror view when one is open, so a mirror never shows a stale
     *  column set. */
    void rebuildMixer() {
        mixer_.rebuild();
        mixerMirror_.rebuildIfOpen();
    }
    /** Cheap in-place re-tint of every mixer column from the current track colours (docked view and, if open, the
     *  mirror) -- called on every TimelineDoc notification; a no-op unless a track colour changed. */
    void refreshMixerTrackColours() {
        mixer_.refreshTrackColours();
        mixerMirror_.refreshTrackColoursIfOpen();
    }
    MixerPanelComponent& getMixerPanel() noexcept { return mixer_; }
    // Const overload for resolveEditSurface(), a const member function.
    const MixerPanelComponent& getMixerPanel() const noexcept { return mixer_; }

    /** Re-syncs the docked mixer's + (if open) the "both places" mirror's mute/solo/pan-law
     *  visuals from something that changed them OUTSIDE either view's own click -- today, a hardware
     *  MIDI Remote solo press (MainComponent::wireMidiRemoteEngine's onNodeCommandApplied). The
     *  live click-to-click case (one view's own button) is instead cross-wired directly through
     *  mixer_.onLiveMixerStateChanged in the constructor below -- see MixerMirrorController.h's
     *  class comment for the full mechanism. */
    void refreshLiveMixerVisualsEverywhere() {
        mixer_.refreshLiveMixerVisuals();
        mixerMirror_.refreshLiveVisualsIfOpen();
    }

    /** Source/UI/CLAUDE.md's mixer-unbind invariant applies to EVERY live MixerPanelComponent,
     *  not only the docked one -- MainComponent wires this (not getMixerPanel().unbindAllColumns()
     *  directly) to GraphEditor::onBeforeDetachAllModuleComponents, so a mirror view open when a
     *  graph-replacing mutation (undo/redo restore, New Patch, Load, AI patch apply) runs is
     *  unbound first too. */
    void unbindAllMixerViews() {
        mixer_.unbindAllColumns();
        mixerMirror_.unbindIfOpen();
    }
    /** Sibling of unbindAllMixerViews() for MixerPanelComponent::rebuildIfUnbound()'s own
     *  contract (MainComponent wires this to GraphEditor::onGraphStructureChanged). */
    void rebuildIfUnboundMixerViews() {
        mixer_.rebuildIfUnbound();
        mixerMirror_.rebuildIfUnboundIfOpen();
    }

    /** Mirrors rebuildMixer() above -- see its call site's own comment. */
    void rebuildMidiRemote() { midiRemotePanel_.rebuildFromProfiles(); }

    /** The hook for TrackChannelLinkController::setMixerRevealHook: switches to the Mixer
     *  tab and reveals `stripId`'s column. False when nothing resolves (no such column --
     *  TrackChannelLinkController falls back to the canvas reveal then). Does NOT open the dock
     *  if it is closed -- the hook's caller does that first (MainComponent owns that state). */
    bool revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId);

    /** MainComponent::timerCallback's one new gated line -- the caller checks isMixerShowing()
     *  itself (Tab placement: isMixerTabActive() && isVisible(); Window placement: the mixer's
     *  DetachablePanelHost is detached -- its own window is a separate top-level Component, so
     *  THIS dock's isVisible() says nothing about whether that window is on screen), matching the
     *  Timeline panel's own precedent (docs/layout/rendering.md). */
    void refreshMeters() {
        mixer_.refreshMeters();
        mixerMirror_.refreshMetersIfOpen(); // Independent meter cadence, own MeterReader slot
    }

    /** "is the mixer panel showing anywhere a meter tick would be visible" --
     *  docked on the Mixer tab (`isMixerTabActive() && isVisible()`, the pre-existing check) OR
     *  detached into its own window (`getMixerHost().isDetached()` -- Window placement's
     *  `MixerPlacementController::revealOrToggle()` detaches the SAME `mixerHost_`, so this one
     *  check covers both the tab-strip's own detach button and that placement). Does NOT cover
     *  "Own panel" placement -- that strip is owned by `MixerPlacementController`, outside this
     *  dock entirely; its own `isOwnPanelShowing()` is the caller's second half of the OR (see
     *  MainComponent::timerCallback). A detached window, once opened, is treated as "showing"
     *  regardless of OS-level occlusion/minimize -- the same fidelity the pre-existing docked
     *  check already had (isVisible() doesn't know the app itself is minimized either). */
    bool isMixerShowing() const noexcept { return mixerHost_.isDetached() || (isMixerTabActive() && isVisible()); }

    /** Same "showing anywhere a tick would be visible" shape as isMixerShowing() above,
     *  for MainComponent::timerCallback's refreshMidiRemoteActivity() gate. */
    bool isMidiRemoteShowing() const noexcept {
        return midiRemoteHost_.isDetached() || (isMidiRemoteTabActive() && isVisible());
    }

    // Whether the Mixer tab itself is offered at all -- false when the Mixer
    // placement preference is "Own panel" or "Window" (MixerPlacementController owns mixerHost_
    // entirely in those modes; see that class). True (the default) is the existing Tab-placement
    // behaviour, unchanged.
    void setMixerTabEnabled(bool enabled);

    /** Re-applies visibility/z-order for the active tab, without a Mixer/MIDI Remote rebuild. */
    void refreshTabVisibility();

    /** "When a panel opens in its own window: move it there (default) / show it in both
     *  places" -- re-reads "detachedPanelBothPlaces" and, if it changed, live-transitions whatever
     *  is currently open (see the .cpp for the two transition directions). Scoped to the Mixer's Tab
     *  placement only (see docs/mixer/panel.md) -- a no-op for Own-panel/Window placement and for
     *  Timeline/Controllers, which have no mirror view. Call once at launch
     *  (setApplicationProperties() below already does) and again on every settings-file write, the
     *  same "MixerPlacementController::applyPlacementPreference() sibling" MainComponent's
     *  ChangeListener already re-runs mixerPlacement_ through. */
    void applyDetachBothPlacesPreference();

    /** BottomDockComponent's own theme re-skin pass extension point -- MainComponent's
     *  changeListenerCallback calls this alongside getMixerHost().refreshDetachedWindowTheme(). */
    void refreshMixerMirrorWindowTheme() { mixerMirror_.refreshThemeIfOpen(); }

    // ---- Mixer mirror test seams (BottomDockMixerMirrorTests.cpp) ----
    bool isMixerMirrorOpenForTest() const noexcept { return mixerMirror_.isOpen(); }
    MixerPanelComponent* getMixerMirrorPanelForTest() const noexcept { return mixerMirror_.getMirrorPanelForTest(); }
    bool isDetachBothPlacesEnabledForTest() const noexcept { return detachBothPlacesEnabled_; }

    /** The Timeline's own detach-to-window host -- always owned and shown here, in every Mixer
     *  placement (docs/mixer/panel.md's placement table: "Bottom dock: unaffected"). */
    synth::ui::DetachablePanelHost& getTimelineHost() noexcept { return timelineHost_; }
    /** The Mixer's detach-to-window host. Owned here always, but only PARENTED here in Tab
     *  placement -- MixerPlacementController reparents it into its own "Own panel" strip, or
     *  leaves it unparented (never eagerly shown) in "Window" placement until first reveal. */
    synth::ui::DetachablePanelHost& getMixerHost() noexcept { return mixerHost_; }
    /** The MIDI Remote panel's detach-to-window host, and the panel itself -- always owned and,
     *  since MidiRemote has no placement variant, always parented here (unlike getMixerHost()). */
    synth::ui::DetachablePanelHost& getMidiRemoteHost() noexcept { return midiRemoteHost_; }
    synth::ui::MidiRemotePanelComponent& getMidiRemotePanel() noexcept { return midiRemotePanel_; }

    /** MainComponent::wireMidiRemoteEngine(): wires the panel's live dependencies, which (like
     *  MidiLearnController itself) don't exist yet at THIS component's own construction time --
     *  see MidiRemotePanelComponent.h's class comment for why configure() exists instead of
     *  constructor params. */
    void configureMidiRemote(AudioEngine& audioEngine, synth::midi::RemoteEngine& remoteEngine,
                             synth::midi::MidiLearnController& learnController, synth::MidiRemoteProjectDoc& doc,
                             GraphEditor& graphEditor) {
        midiRemotePanel_.configure(audioEngine, remoteEngine, learnController, doc, graphEditor);
    }

    /** MainComponent::wireGraphEditorCallbacks()'s onEditMidiAssignmentRequested target: switches
     *  to the MidiRemote tab and selects the assignment for (nodeUuid, paramId). Does NOT open the
     *  dock if it's closed -- same contract as revealColumnForStrip() above, the caller does that
     *  first. Returns false if no assignment exists for that parameter yet. */
    bool selectMidiRemoteAssignment(const juce::String& nodeUuid, const juce::String& paramId) {
        setActiveTab(Tab::MidiRemote);
        return midiRemotePanel_.selectAssignmentForParameter(nodeUuid, paramId);
    }

    /** MainComponent::timerCallback's gated tick -- mirrors refreshMeters() below exactly. */
    void refreshMidiRemoteActivity() { midiRemotePanel_.refreshActivity(); }

    /** Fires whenever either host's detach state changes (docked<->detached, either direction --
     *  including a window's own close button). MainComponent hooks this to re-run its focus-region
     *  registration pass (docs/control/shortcuts.md "Focus regions") AND to keep
     *  isBottomDockVisible in sync with hasAnyVisibleTab(). Separate from either
     *  DetachablePanelHost's own onDetachedStateChanged, which this class's constructor already
     *  claims for applyTabVisibility() -- both fire from the one place, in that order. */
    std::function<void()> onPanelDetachStateChanged;
    /** Fires after every real tab switch, including a fallback switch applyTabVisibility() picks
     *  itself (a detach, a redock, or Mixer losing mixerTabEnabled_). */
    std::function<void()> onActiveTabChanged;
    /** The tab-strip buttons, in the user's current (drag-reorderable) tab order: what the MIDI
     *  Remote pick overlay lets clicks through to. */
    std::vector<juce::Component*> getTabButtons() {
        std::vector<juce::Component*> buttons;
        for (Tab t : tabOrder_)
            buttons.push_back(&buttonForTab(t));
        return buttons;
    }

    /** One tab currently offered in the strip, for the Cmd-hold shortcut hints. */
    struct StripTab {
        juce::TextButton* button{nullptr};
        juce::String actionId; // the shortcut action that shows this tab
        juce::String name;
    };
    /** The tabs offered in the strip right now, in the user's tab order; a tab detached to its own
     *  window (or a disabled Mixer) is absent. Independent of whether the dock itself is open. */
    std::vector<StripTab> getStripTabs();

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override; // the lifted tab of a reorder drag
    void lookAndFeelChanged() override;                 // refreshes the tab-strip detach button's themed icon

    /** Total tab-strip height, including the top PanelResizeHandle::kHeight px the handle overlaps. */
    static constexpr int kTabStripHeight = 22;

    // ---- Resizable height (top-edge grab strip, every tab) ----
    // Both callbacks carry the desired TOTAL dock height, UNCLAMPED; MainComponent clamps and persists.

    /** Fired on every drag step. */
    std::function<void(int desiredHeight)> onResizeHeight;
    /** Fired once on mouse-up after a real drag (never for a stray click): the cue to persist. */
    std::function<void(int desiredHeight)> onResizeHeightCommitted;
    /** Test seam: no OS mouse source exists headlessly, so tests drive events through the strip. */
    juce::Component& getResizeHandle() noexcept { return resizeHandle_; }
    bool isResizeHandleHovered() const noexcept { return resizeHandle_.isHovered(); }

    /** Test seam: this dock's own detach/redock button. */
    juce::DrawableButton& getDetachButtonForTest() noexcept { return detachButton_; }
    /** Test seam: the left edge (dock coordinates) the lifted tab is drawn at. */
    float getLiftedTabLeftForTest() const { return reorder_.getDraggedStart(); }
    /** Test seam: the Esc key press a real drag would receive from the window. */
    bool sendEscapeToTabDragForTest() {
        return reorderCancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), this);
    }
    /** Test seam: the tab strip's current drag-reorderable order. */
    const std::vector<Tab>& getTabOrderForTest() const noexcept { return tabOrder_; }
    /** Test seam: applies a reorder through the same persist + key-permute path a real
     *  drag-and-release ends in, without synthesizing mouse events. */
    void reorderTabsForTest(Tab dragged, Tab droppedOnto) {
        swapTabOrder(dragged, droppedOnto);
        persistTabOrder();
        permuteShortcutKeysForNewOrder();
        resized();
    }

private:
    // One tab-strip button; reports drags to its owner for drag-to-reorder (see .cpp).
    class DockTabButton : public juce::TextButton {
    public:
        DockTabButton(BottomDockComponent& owner, Tab tab, const juce::String& text)
            : juce::TextButton(text)
            , owner_(owner)
            , tab_(tab) {
            setMouseCursor(dragGrabCursor()); // a tab is a grab handle: the hand on hover and while dragging
        }
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override {
            owner_.beginTabDrag(tab_, e);
            juce::TextButton::mouseDown(e);
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            owner_.dragTab(e);
            juce::TextButton::mouseDrag(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            if (!owner_.endTabDrag())
                juce::TextButton::mouseUp(e);
            else
                setState(isMouseOver() ? buttonOver : buttonNormal); // the drag swallowed the click
        }

    private:
        BottomDockComponent& owner_;
        Tab tab_;
    };

    // `allowMixerRebuild` is false ONLY from the detach/redock callback
    // (onEitherHostDetachStateChanged) -- reparenting into/out of a DetachedPanelWindow doesn't
    // change which graph nodes the mixer shows, so a rebuild there was pure collateral damage: it
    // tore down and rebuilt every column, and with it every column's own latched clip-readout
    // state (MixerMeterReadout's running max/clip colour), resetting a mid-session "-inf" -> real
    // reading back to "-inf" on every single detach or redock. Every other caller (an actual tab
    // switch, or the Mixer tab regaining `mixerTabEnabled_`) keeps the pre-existing "catch up on
    // whatever changed while the Mixer tab was hidden" rebuild, unchanged (default true).
    void applyTabVisibility(bool allowMixerRebuild = true);
    void persistActiveTab();
    // The active tab's DetachablePanelHost -- whichever the tab-strip detach button acts
    // on (docs/mixer/panel.md: "the tab-strip button detaches whichever tab is active").
    synth::ui::DetachablePanelHost& activeHost() noexcept;
    void refreshDetachButton();

    // ---- "both places" -- the tab-strip detach button routes through the mirror instead
    // of a real detach only for the Mixer tab, and only while the preference is on.
    bool usesMixerMirrorForDetach() const noexcept { return activeTab_ == Tab::Mixer && detachBothPlacesEnabled_; }
    /** The detach button's own onClick target -- replaces the plain
     *  `activeHost().setDetached(!activeHost().isDetached())` toggle. */
    void toggleActiveHostDetach();
    /** Resolves the live LookAndFeel/native-window flag the same way a real Mixer detach already
     *  does, and opens the mirror against `mixer_`. */
    void openMixerMirror();

    // ---- Tab order, "which tabs are offered right now", and the fallback pick ------------
    synth::ui::DetachablePanelHost& hostForTab(Tab tab) noexcept;
    const synth::ui::DetachablePanelHost& hostForTab(Tab tab) const noexcept;
    juce::TextButton& buttonForTab(Tab tab) noexcept;
    /** True when `tab` belongs in the strip right now: not detached, and (Mixer only) not disabled
     *  by the placement preference. */
    bool isTabOfferedInStrip(Tab tab) const noexcept;
    /** `activeTab_` if it's still offered, else the first offered tab in tabOrder_, else
     *  `activeTab_` unchanged (hasAnyVisibleTab() is false -- nothing to fall back to). */
    Tab pickFallbackActiveTab() const noexcept;

    // ---- Drag-to-reorder (DockTabButton's own mouse overrides call these; see .cpp) ------------
    void beginTabDrag(Tab tab, const juce::MouseEvent& e);
    /** True while `tab` is being dragged past the drag threshold (its slot shows the dashed outline). */
    bool isTabLifted(Tab tab) const noexcept { return liftedTab_ == tab; }
    void paintTabSlot(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void dragTab(const juce::MouseEvent& e);
    /** True when the gesture was a real drag (the mouse-up must not also click the tab). */
    bool endTabDrag();
    void commitTabDrag();
    /** Esc mid-drag: the lifted tab returns to where it was picked up and nothing is committed. */
    void cancelTabDrag();
    /** Lays the offered tab buttons out at their static slots, in tabOrder_. */
    void layoutTabButtons();
    /** Moves the buttons to where the reorder animator says they are right now. */
    void applyReorderOffsets();
    void onReorderFrame();
    void startReorderFramesIfNeeded();
    float pointerXInDock(const juce::MouseEvent& e);
    void swapTabOrder(Tab a, Tab b);
    void persistTabOrder();
    /** Permutes the three tabs' own Cmd+digit key bindings to match tabOrder_'s new order. */
    void permuteShortcutKeysForNewOrder();
    static const char* actionIdForTab(Tab tab) noexcept;

    TimelinePanelComponent& timelinePanel_;
    MixerPanelComponent mixer_;
    // The panel and its host follow the same "declared before its host so the reference
    // is valid" rule as mixer_/mixerHost_ below.
    synth::ui::MidiRemotePanelComponent midiRemotePanel_;
    // Both panels' detach-to-window hosts. Declared after timelinePanel_/mixer_ so
    // both references are valid -- see DetachablePanelHost's own "held by reference, never
    // copied" contract.
    synth::ui::DetachablePanelHost timelineHost_;
    synth::ui::DetachablePanelHost mixerHost_;
    synth::ui::DetachablePanelHost midiRemoteHost_;
    // The Mixer's optional second live view -- see MixerMirrorController.h's own class
    // comment. Constructed once (its ConfigureFn closure captures the same graph/doc/macros/
    // undoManager/graphEditor/audioEngine references mixer_.configure() below already used), opened
    // lazily on the first "both places" detach.
    synth::ui::MixerMirrorController mixerMirror_;
    DockTabButton timelineTabButton_;
    DockTabButton mixerTabButton_;
    DockTabButton midiRemoteTabButton_;
    // Icon-only, embedded in this tab strip (not either host's own header -- see
    // DetachablePanelHost's class comment on why this is a separate button instance rather than a
    // literal shared one across three different parents).
    juce::DrawableButton detachButton_{"detachActiveTab", juce::DrawableButton::ImageFitted};
    // Added LAST in the constructor so it wins the hit test; forwards into onResizeHeight*.
    PanelResizeHandle resizeHandle_{*this};
    Tab activeTab_ = Tab::Timeline;
    bool mixerTabEnabled_ = true;
    juce::ApplicationProperties* appProperties_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    static constexpr const char* kActiveTabKey = "bottomDockActiveTab";
    // User-controlled visual/Cmd+N order, a permutation of all three Tab values regardless
    // of which are currently detached (a detached tab keeps its slot so redocking restores it there
    // rather than always appending it at the end).
    std::vector<Tab> tabOrder_{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    static constexpr const char* kTabOrderKey = "bottomDockTabOrder";
    // The tab currently drawn lifted (under the pointer, then gliding into its slot); empty outside
    // a drag that has cleared the drag threshold.
    std::optional<Tab> liftedTab_;
    // The strip area the tab buttons share, set by resized().
    juce::Rectangle<int> tabButtonsArea_;
    // The tabs offered when the press happened, in display order: the animator's item keys.
    std::vector<Tab> reorderTabs_;
    ReorderDragAnimator reorder_;
    ReorderFramePump reorderFrames_{*this};
    ReorderCancelKey reorderCancelKey_;
    bool tabDragCancelled_ = false; // Esc pressed in this gesture: the mouse-up must not click
    unsigned reorderGenerationSeen_ = 0;
    // "detachedPanelBothPlaces" -- see applyDetachBothPlacesPreference()'s own comment.
    // Read once in setApplicationProperties() (no live-transition side effects, nothing is open
    // yet) and re-read live thereafter through that same method.
    bool detachBothPlacesEnabled_ = false;
    static constexpr const char* kDetachBothPlacesKey = "detachedPanelBothPlaces";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BottomDockComponent)
};

} // namespace synth::ui
