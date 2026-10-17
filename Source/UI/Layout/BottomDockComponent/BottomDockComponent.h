#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/BottomDockComponent/BottomDockTabMotion.h"
#include "UI/Layout/BottomDockTabModel.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderDragAnimator.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include "UI/Mixer/MixerMirrorController.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

class AppUndoManager;
class GraphEditor;
class AudioEngine;

namespace synth {
class MidiRemoteProjectDoc;
} // namespace synth

namespace synth::ui {
class ColourPickerPopup;
} // namespace synth::ui

namespace synth::midi {
class RemoteEngine;
class MidiLearnController;
} // namespace synth::midi

// BottomDockComponent.h (docs/mixer/panel.md): the bottom dock's own tab strip.
//
// The ONE component MainComponent.h holds for the dock (see
// docs/mixer/panel.md#what-the-mixer-shows): owns `timelinePanel` by reference (NOT a
// copy/move -- MainComponent still owns and constructs it) and a MixerPanelComponent by unique_ptr (so this header
// needn't include it). MainComponent::resized()'s existing dock carve (`timelinePanel.setBounds(...)`) becomes
// `bottomDock.setBounds(...)` -- one line changed, not two new carve blocks; the open/close slide,
// height and persisted-visible state stay MainComponent's own (isBottomDockVisible/timelineSlide_),
// gating the whole dock rather than just the Timeline tab.
namespace synth::ui {

class MixerPanelComponent;
class TimelinePanelComponent;

class BottomDockComponent : public juce::Component {
public:
    // The four views (BottomDockTabModel.h); the one in the main area is never offered as a tab.
    using Tab = DockView;

    // `appProperties`/`lookAndFeel`/`shortcutManager` are forwarded straight into
    // timelineHost_/mixerHost_ (both DetachablePanelHost) -- see that class for what each is
    // for. `lookAndFeel`/`shortcutManager` may be null in a headless test that never detaches a
    // panel (see docs/mixer/panel.md).
    BottomDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine, synth::TimelineDoc& doc,
                        AppUndoManager& undoManager, GraphEditor& graphEditor,
                        juce::ApplicationProperties& appProperties, synth::theme::AppLookAndFeel* lookAndFeel,
                        ShortcutManager* shortcutManager);
    ~BottomDockComponent() override;

    /** Reads the persisted active tab, tab order and main view once ("bottomDockActiveTab" /
     *  "bottomDockTabOrder" / "mainView", docs/layout/chrome.md's "Panel collapse and persistence"
     *  table); writes the active tab on every tab switch, the order on every drag-reorder and both
     *  the order and the main view on every setMainView(). */
    void setApplicationProperties(juce::ApplicationProperties* properties);

    /** True while the Canvas tab is the active one, i.e. the canvas fills the dock's body (only possible
     *  once another view is in the main area). The caller also checks the dock itself is showing. */
    bool isCanvasBodyShowing() const noexcept { return activeTab_ == Tab::Canvas && getMainView() != Tab::Canvas; }
    /** The Canvas tab's body in this dock's own coordinates: everything under the tab strip. Empty unless
     *  isCanvasBodyShowing(). The canvas itself is not a child of the dock, so MainComponent lays it into
     *  this rect and the dock lets mouse events through it. */
    juce::Rectangle<int> getCanvasBodyBounds() const;
    /** Fires after the canvas body appears or disappears (a tab switch or a main-view change), once the
     *  dock has laid out, so MainComponent can place the canvas. */
    std::function<void()> onCanvasBodyChanged;

    /** The view in the main area now: Canvas unless setMainView() moved another one up, and Canvas again while
     *  that view cannot be there (see getStoredMainView()). */
    Tab getMainView() const noexcept { return tabModel_.getMainView(); }
    /** The view the user put in the main area, the one saved in the settings. It differs from getMainView() only
     *  while it is in its own window or the Mixer is outside the Tab placement: the Canvas fills the main area for
     *  the session so it is never empty, and the stored view returns when its panel is docked again. */
    Tab getStoredMainView() const noexcept { return tabModel_.getStoredMainView(); }
    /** Moves `view` into the main area; the view it replaces takes `view`'s tab slot. False, and nothing
     *  changes, when `view` is already there, is open in its own window, or is the Mixer outside the Tab placement. */
    bool setMainView(Tab view);
    /** The swap icon's action: setMainView(view), then the view that was in the main area becomes the active tab.
     *  False, and nothing changes, when setMainView() refuses. */
    bool swapTabToMain(Tab view);
    /** The rebindable action behind each view's "show it in the main area" key (the swap icon's shortcut). */
    static const char* swapActionIdForTab(Tab view) noexcept;
    /** What a screen reader is told after a swap, e.g. "Timeline in the main area". */
    static juce::String mainViewAnnouncement(Tab view);
    /** The key shown in `view`'s swap icon tooltip (its swap action's binding, empty when unbound). Pushed by
     *  MainComponent on every shortcut change: the dock is built before the shortcut table exists. */
    void setSwapShortcutText(Tab view, const juce::String& text);
    /** Fires after every successful setMainView(), once the dock has laid out; MainComponent relays out the main area.
     */
    std::function<void()> onMainViewChanged;
    /** MainComponent's main-area container. While the Timeline or MIDI Remote is the main view its host is a
     *  child of `holder`, and comes back here when it leaves. Set once; `holder` must outlive this dock. */
    void setMainAreaHolder(juce::Component* holder);
    /** True while `view`'s host sits in the main-area holder: on screen whether or not this dock is. */
    bool isInMainArea(Tab view) const noexcept { return hostInMain_ != nullptr && hostForTab(view) == hostInMain_; }

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
    /** The mixer strip menu's "Duplicate Track" (MainComponent::duplicateTrack). */
    void setOnDuplicateTrack(std::function<void(synth::TrackId)> callback);
    /** MainComponent wires this to the track colour picker its Timeline swatch opens -- fired by a mixer column's
     *  colour dot, so one pick recolours the track, its macro and both views. */
    void
    setOnBuildTrackColourPicker(std::function<std::unique_ptr<synth::ui::ColourPickerPopup>(synth::TrackId)> callback);

    /** Whether the active tab has a side pane (the Mixer's zones list and the Timeline's routing view do). */
    bool hasActiveSidePane() const noexcept { return activeTab_ == Tab::Mixer || activeTab_ == Tab::Timeline; }
    /** Shows or hides the active tab's side pane; `forceOpen` only ever opens it. False, and nothing
     *  changes, when the active tab has none. */
    bool toggleActiveSidePane(bool forceOpen = false);

    Tab getActiveTab() const noexcept { return activeTab_; }
    bool isMixerTabActive() const noexcept { return activeTab_ == Tab::Mixer; }
    bool isMidiRemoteTabActive() const noexcept { return activeTab_ == Tab::MidiRemote; }
    /** Switches tabs (persisting the choice) and lays out; a no-op if already on `tab` or `tab` is the
     *  main view. Does NOT open/close the dock itself -- that stays MainComponent's own
     *  isBottomDockVisible/beginPanelSlide() (see the class comment). */
    void setActiveTab(Tab tab);

    /** True while at least one tab is currently offered in the strip. MainComponent hides
     *  the whole dock when this goes false and reopens it when it comes back true (see
     *  bottomDockAutoHiddenByEmptyTabs_'s own comment). */
    bool hasAnyVisibleTab() const noexcept;

    /** Re-runs the mixer panel's snapshot + column rebuild -- call after every graph/timeline/
     *  macro change (MainComponent's existing reconcile funnel is the natural place). Also
     *  rebuilds the "both places" mirror view when one is open, so a mirror never shows a stale
     *  column set. A hidden docked mixer only remembers to rebuild and does it when shown. */
    void rebuildMixer();
    /** Cheap in-place re-tint of every mixer column from the current track colours (docked view and, if open, the
     *  mirror) -- called on every TimelineDoc notification; a no-op unless a track colour changed. */
    void refreshMixerTrackColours();
    MixerPanelComponent& getMixerPanel() noexcept;
    // Const overload for resolveEditSurface(), a const member function.
    const MixerPanelComponent& getMixerPanel() const noexcept;

    /** Re-syncs the docked mixer's + (if open) the "both places" mirror's mute/solo/pan-law
     *  visuals from something that changed them OUTSIDE either view's own click -- today, a hardware
     *  MIDI Remote solo press (MainComponent::wireMidiRemoteEngine's onNodeCommandApplied). The
     *  live click-to-click case (one view's own button) is instead cross-wired directly through
     *  mixer_.onLiveMixerStateChanged in the constructor below -- see MixerMirrorController.h's
     *  class comment for the full mechanism. */
    void refreshLiveMixerVisualsEverywhere();

    /** Source/UI/CLAUDE.md's mixer-unbind invariant applies to EVERY live MixerPanelComponent,
     *  not only the docked one -- MainComponent wires this (not getMixerPanel().unbindAllColumns()
     *  directly) to GraphEditor::onBeforeDetachAllModuleComponents, so a mirror view open when a
     *  graph-replacing mutation (undo/redo restore, New Patch, Load, AI patch apply) runs is
     *  unbound first too. */
    void unbindAllMixerViews();
    /** unbindAllMixerViews() for a restore that frees only `doomed` (MixerPanelComponent::unbindColumnsFor). */
    void unbindMixerViewsFor(const std::vector<juce::AudioProcessorGraph::NodeID>& doomed);
    /** Sibling of unbindAllMixerViews() for MixerPanelComponent::rebuildIfStale()'s own
     *  contract (MainComponent wires this to GraphEditor::onGraphStructureChanged and to every recorded edit). */
    void rebuildIfStaleMixerViews();

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
    void refreshMeters();

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
    bool isMixerShowing() const noexcept {
        return mixerHost_.isDetached() || isInMainArea(Tab::Mixer) || (isMixerTabActive() && isVisible());
    }

    /** Same "showing anywhere a tick would be visible" shape as isMixerShowing() above (the main area
     *  included), for MainComponent::timerCallback's refreshMidiRemoteActivity() gate. */
    bool isMidiRemoteShowing() const noexcept {
        return midiRemoteHost_.isDetached() || isInMainArea(Tab::MidiRemote) ||
               (isMidiRemoteTabActive() && isVisible());
    }
    /** The same for the Timeline: its playhead poll and its edit commands. */
    bool isTimelineShowing() const noexcept {
        return timelineHost_.isDetached() || isInMainArea(Tab::Timeline) ||
               (activeTab_ == Tab::Timeline && isVisible());
    }

    // Whether the Mixer tab itself is offered at all -- false when the Mixer
    // placement preference is "Own panel" or "Window" (MixerPlacementController owns mixerHost_
    // entirely in those modes; see that class). True (the default) is the existing Tab-placement
    // behaviour, unchanged. Disabling it while the Mixer is the main view puts the canvas back in main.
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
    DetachedPanelWindow* getMixerMirrorWindowForTest() const noexcept { return mixerMirror_.getMirrorWindowForTest(); }

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

    /** Holds (or releases) edits in every panel's detached window while a project loads; `onRefused` runs on a
     *  refused click. The docked panels are covered by one overlay over the whole dock instead. */
    void setDetachedEditsBlocked(bool blocked, const std::function<void()>& onRefused) {
        for (auto* host : {&timelineHost_, &mixerHost_, &midiRemoteHost_})
            host->setEditsBlocked(blocked, onRefused);
    }
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
        for (Tab t : tabModel_.getOrder())
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
    bool hitTest(int x, int y) override;

    /** Total tab-strip height, including the top PanelResizeHandle::kHeight px the handle overlaps. */
    static constexpr int kTabStripHeight = 24;

    // ---- Resizable height (top-edge grab strip, every tab) ----
    // Both callbacks carry the desired TOTAL dock height, UNCLAMPED; MainComponent clamps and persists.

    /** Fired on every drag step. */
    std::function<void(int desiredHeight)> onResizeHeight;
    /** Fired once on mouse-up after a real drag (never for a stray click): the cue to persist. */
    std::function<void(int desiredHeight)> onResizeHeightCommitted;
    /** Test seam: no OS mouse source exists headlessly, so tests drive events through the strip. */
    juce::Component& getResizeHandle() noexcept { return resizeHandle_; }
    bool isResizeHandleHovered() const noexcept { return resizeHandle_.isHovered(); }

    // ---- Tab strip keyboard + screen-reader access
    // (docs/layout/chrome.md#tab-strip-keyboard-and-screen-reader-access) ----
    /** The strip's single Tab stop and focus-region root: Left/Right/Home/End switch the active tab,
     *  Return moves focus into its panel. Plain keys only. */
    juce::Component& getTabStripFocus() noexcept { return stripFocus_; }
    /** Panel Return moves focus to; null while no tab is offered. */
    juce::Component* getActivePanelRoot() noexcept;
    /** What a screen reader is told about one tab. JUCE has no tab role; a radio button, the
     *  mutually exclusive selectable item, is the closest one it can expose. */
    struct TabAccessibility {
        juce::String title;
        juce::AccessibilityRole role;
        bool selected;
        juce::String swapTitle;   // the name (and tooltip) of the swap button inside the tab
        juce::String detachTitle; // the same for the detach button; empty for the Canvas, which has no window
    };
    static TabAccessibility describeTab(Tab tab, Tab activeTab);
    /** Replaces the panel focus grab, which needs a native window a headless test lacks. */
    void setPanelFocusHookForTest(std::function<void(juce::Component&)> hook) { panelFocusHook_ = std::move(hook); }

    /** The swap button inside `tab`'s strip button (the Canvas tab's included). Disabled while `tab` cannot go to the
     *  main area (it is open in its own window); not shown while `tab` is not offered. */
    synth::ui::IconButton& getSwapIcon(Tab tab) noexcept;
    /** Test seam: receives each swap announcement instead of the screen reader. */
    void setAnnouncementHookForTest(std::function<void(const juce::String&)> hook) { announceHook_ = std::move(hook); }
    /** The detach button inside `tab`'s strip button, left of its swap button; null for the Canvas, which has no
     *  window to open into. Pressing it opens the tab's panel in its own window (with "both places" on, the Mixer's
     *  opens a second view of it instead, and the button then closes that view). */
    synth::ui::IconButton* getDetachIcon(Tab tab) noexcept;
    /** True while the Mixer's "both places" second view is open; raiseMixerMirror() brings its window forward
     *  without taking focus from the docked view. */
    bool isMixerMirrorOpen() const noexcept { return mixerMirror_.isOpen(); }
    void raiseMixerMirror() { mixerMirror_.raiseIfOpen(); }
    /** Test seam: puts a running tab-strip motion at `elapsedMs` after it began (landing it when that is past its
     *  end), since no VBlank reaches an off-screen strip. */
    void stepTabMotionForTest(double elapsedMs);
    bool isTabMotionRunningForTest() const noexcept { return tabMotion_.isRunning(); }
    /** Test seam: the host component of `tab` (its alpha and visibility show a tab cross-fade); never Canvas. */
    juce::Component& getTabHostForTest(Tab tab) noexcept { return *hostForTab(tab); }
    /** Test seam: the left edge (dock coordinates) the lifted tab is drawn at. */
    float getLiftedTabLeftForTest() const { return reorder_.getDraggedStart(); }
    /** Test seam: the Esc key press a real drag would receive from the window. */
    bool sendEscapeToTabDragForTest() {
        return reorderCancelKey_.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), this);
    }
    /** Test seam: the tab strip's current drag-reorderable order (all four views). */
    const std::vector<Tab>& getTabOrderForTest() const noexcept { return tabModel_.getOrder(); }
    /** Test seam: applies a reorder through the same persist path a real drag-and-release ends in,
     *  without synthesizing mouse events. */
    void reorderTabsForTest(Tab dragged, Tab droppedOnto) {
        tabModel_.swapSlots(dragged, droppedOnto);
        persistTabOrder();
        resized();
    }

    /** The detach-to-window host of `tab`; null for Canvas, which has none. */
    synth::ui::DetachablePanelHost* hostForTab(Tab tab) noexcept;
    const synth::ui::DetachablePanelHost* hostForTab(Tab tab) const noexcept;

private:
    // One tab-strip button; reports drags to its owner for drag-to-reorder (see .cpp).
    class DockTabButton : public juce::TextButton {
    public:
        DockTabButton(BottomDockComponent& owner, Tab tab, const juce::String& text)
            : juce::TextButton(text)
            , owner_(owner)
            , tab_(tab) {
            setTitle(text);
            setWantsKeyboardFocus(false); // the strip's one Tab stop is stripFocus_
            addAndMakeVisible(swapIcon_);
            swapIcon_.setWantsKeyboardFocus(false);
            swapIcon_.onClick = [this] { owner_.swapTabToMain(tab_); };
            if (hasDetachIcon()) {
                addAndMakeVisible(detachIcon_);
                detachIcon_.setWantsKeyboardFocus(false);
                detachIcon_.onClick = [this] { owner_.toggleTabDetach(tab_); };
            }
        }
        /** The row of small icon buttons at the tab's right edge (swap rightmost, detach to its left); the label
         *  keeps clear of it. The Canvas has no window, so its row has no detach button. */
        synth::ui::IconButton& getSwapIcon() noexcept { return swapIcon_; }
        synth::ui::IconButton* getDetachIcon() noexcept { return hasDetachIcon() ? &detachIcon_ : nullptr; }
        void setIconRowShown(bool shown) {
            swapIcon_.setVisible(shown);
            detachIcon_.setVisible(shown && hasDetachIcon());
        }
        void resized() override;
        void paint(juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        void mouseDown(const juce::MouseEvent& e) override {
            owner_.beginTabDrag(tab_, e);
            juce::TextButton::mouseDown(e);
        }
        // The normal arrow on hover and press; the grab hand only once the press is a real drag.
        void mouseDrag(const juce::MouseEvent& e) override {
            owner_.dragTab(e);
            followDragCursor(*this, owner_.isTabDragging());
            juce::TextButton::mouseDrag(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            endDragCursor(*this);
            if (!owner_.endTabDrag())
                juce::TextButton::mouseUp(e);
            else
                setState(isMouseOver() ? buttonOver : buttonNormal); // the drag swallowed the click
        }

    private:
        bool hasDetachIcon() const noexcept { return tab_ != Tab::Canvas; }

        BottomDockComponent& owner_;
        Tab tab_;
        synth::ui::IconButton swapIcon_{"swapMainView", synth::theme::Glyph::Swap};
        synth::ui::IconButton detachIcon_{"detachTab", synth::theme::Glyph::Detach};
    };

    // The strip's focusable leaf: transparent, behind the tab buttons, never hit by the mouse.
    class TabStripFocus : public juce::Component {
    public:
        explicit TabStripFocus(BottomDockComponent& owner);
        bool keyPressed(const juce::KeyPress& key) override { return owner_.handleTabStripKey(key); }
        void modifierKeysChanged(const juce::ModifierKeys&) override { owner_.repaintTabStrip(); }
        void focusGained(FocusChangeType) override { owner_.repaintTabStrip(); }
        void focusLost(FocusChangeType) override { owner_.repaintTabStrip(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        BottomDockComponent& owner_;
    };

    bool handleTabStripKey(const juce::KeyPress& key);
    void selectTabAt(int offeredIndex);
    void repaintTabStrip() { repaint(0, 0, getWidth(), kTabStripHeight); }
    void paintTabFocusRing(juce::Graphics& g);
    /** The ring on the swap icon while Shift is held, on the detach icon while Option is held: the icon Return presses.
     */
    void paintIconFocusRing(juce::Graphics& g);
    /** Shift+Return on the strip: presses the active tab's swap icon. */
    bool pressActiveSwapIcon();
    /** Option+Return on the strip: presses the active tab's detach icon. */
    bool pressActiveDetachIcon();
    /** Re-derives each swap icon's enabled state, tooltip and name. */
    void refreshSwapIcons();
    /** Re-derives each detach icon's tooltip and name (it reads "Close ..." while the Mixer's second view is open). */
    void refreshDetachIcons();
    void setLiftedTab(std::optional<Tab> tab);
    static const char* nameForTab(Tab tab) noexcept;
    static juce::String swapTitleForTab(Tab tab);
    static juce::String detachTitleForTab(Tab tab);

    // `allowMixerRebuild` is false ONLY from the detach/redock callback
    // (onEitherHostDetachStateChanged) -- reparenting into/out of a DetachedPanelWindow doesn't
    // change which graph nodes the mixer shows, so a rebuild there was pure collateral damage: it
    // tore down and rebuilt every column, and with it every column's own latched clip-readout
    // state (MixerMeterReadout's running max/clip colour), resetting a mid-session "-inf" -> real
    // reading back to "-inf" on every single detach or redock. Every other caller (an actual tab
    // switch, or the Mixer tab regaining `mixerTabEnabled_`) keeps the pre-existing "catch up on
    // whatever changed while the Mixer tab was hidden" rebuild, unchanged (default true).
    void applyTabVisibility(bool allowMixerRebuild = true);
    // The host that belongs in the main-area holder for `view`; null for the canvas, and for the Mixer for now.
    synth::ui::DetachablePanelHost* mainAreaHostFor(Tab view) noexcept;
    // Moves hosts between this dock and the main-area holder so the main view's host is the one in the holder.
    void placeMainViewHost();
    /** Suspends or resumes the stored main view as its panel leaves or returns to the dock; true when that changed
     *  the view in the main area. The caller runs applyTabVisibility() and onMainViewChanged afterwards. */
    bool refreshMainViewAvailability();
    synth::ui::FadeVisibility& fadeForHost(const synth::ui::DetachablePanelHost& host) noexcept;
    void persistActiveTab();

    // ---- "both places" -- a tab's detach button routes through the mirror instead
    // of a real detach only for the Mixer tab, and only while the preference is on.
    bool usesMixerMirrorForDetach(Tab tab) const noexcept { return tab == Tab::Mixer && detachBothPlacesEnabled_; }
    /** A detach button's onClick target: opens `tab`'s panel in its own window (or closes it again), or toggles the
     *  Mixer's second view. Never touches the view in the main area, which has no tab. */
    void toggleTabDetach(Tab tab);
    /** Resolves the live LookAndFeel/native-window flag the same way a real Mixer detach already
     *  does, and opens the mirror against `mixer_`. */
    void openMixerMirror();

    // ---- Tab order, "which tabs are offered right now", and the fallback pick ------------
    DockTabButton& buttonForTab(Tab tab) noexcept;
    /** True when `tab` belongs in the strip right now: not the main view, not detached, and (Mixer
     *  only) not disabled by the placement preference. */
    bool isTabOfferedInStrip(Tab tab) const noexcept;
    /** `activeTab_` if it's still offered, else the first offered tab in the strip order, else
     *  `activeTab_` unchanged (hasAnyVisibleTab() is false -- nothing to fall back to). */
    Tab pickFallbackActiveTab() const noexcept;

    // ---- Drag-to-reorder (DockTabButton's own mouse overrides call these; see .cpp) ------------
    void beginTabDrag(Tab tab, const juce::MouseEvent& e);
    /** True while `tab` is being dragged past the drag threshold (its slot shows the dashed outline). */
    bool isTabLifted(Tab tab) const noexcept { return liftedTab_ == tab; }
    /** True while a tab press has become a real drag (past the threshold, not cancelled by Esc). */
    bool isTabDragging() const noexcept { return reorder_.isDragging(); }
    void paintTabSlot(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void dragTab(const juce::MouseEvent& e);
    /** True when the gesture was a real drag (the mouse-up must not also click the tab). */
    bool endTabDrag();
    void commitTabDrag();
    /** Esc mid-drag: the lifted tab returns to where it was picked up and nothing is committed. */
    void cancelTabDrag();
    /** Lays the offered tab buttons out at their static slots, in the strip order; a change of the offered set
     *  on screen is played as a tab motion instead (BottomDockComponentTabMotion.cpp). */
    void layoutTabButtons();
    /** The tabs in the strip right now, in the strip order. */
    std::vector<Tab> offeredTabs() const;
    void startTabMotion(const std::vector<Tab>& before, const std::vector<Tab>& after);
    /** Places the buttons where the running motion says they are. */
    void placeTabsForMotion();
    void onTabMotionFrame();
    /** Lands the motion: every button back to its static look, the target set laid out. */
    void finishTabMotion();
    /** Moves the buttons to where the reorder animator says they are right now. */
    void applyReorderOffsets();
    void onReorderFrame();
    void startReorderFramesIfNeeded();
    float pointerXInDock(const juce::MouseEvent& e);
    void persistTabOrder();
    void persistMainView();
    static const char* actionIdForTab(Tab tab) noexcept;

    TimelinePanelComponent& timelinePanel_;
    std::unique_ptr<MixerPanelComponent> mixer_;
    // The panel and its host follow the same "declared before its host so the reference
    // is valid" rule as mixer_/mixerHost_ below.
    synth::ui::MidiRemotePanelComponent midiRemotePanel_;
    // Both panels' detach-to-window hosts. Declared after timelinePanel_/mixer_ so
    // both references are valid -- see DetachablePanelHost's own "held by reference, never
    // copied" contract.
    synth::ui::DetachablePanelHost timelineHost_;
    synth::ui::DetachablePanelHost mixerHost_;
    synth::ui::DetachablePanelHost midiRemoteHost_;
    // A tab switch cross-fades the hosts (animation.md, "Fading things in and out").
    synth::ui::FadeVisibility timelineFade_{&timelineHost_};
    synth::ui::FadeVisibility mixerFade_{&mixerHost_};
    synth::ui::FadeVisibility midiRemoteFade_{&midiRemoteHost_};
    // The Mixer's optional second live view -- see MixerMirrorController.h's own class
    // comment. Constructed once (its ConfigureFn closure captures the same graph/doc/macros/
    // undoManager/graphEditor/audioEngine references mixer_.configure() below already used), opened
    // lazily on the first "both places" detach.
    synth::ui::MixerMirrorController mixerMirror_;
    // Declared before the tab buttons so it sits behind them.
    TabStripFocus stripFocus_{*this};
    std::function<void(juce::Component&)> panelFocusHook_;
    DockTabButton timelineTabButton_;
    DockTabButton mixerTabButton_;
    DockTabButton midiRemoteTabButton_;
    // Offered only while the canvas is not the main view (it then has no panel in the dock yet).
    DockTabButton canvasTabButton_;
    // Added LAST in the constructor so it wins the hit test; forwards into onResizeHeight*.
    PanelResizeHandle resizeHandle_{*this};
    Tab activeTab_ = Tab::Timeline;
    bool canvasBodyWasShowing_ = false;                    // what onCanvasBodyChanged last reported
    juce::Component* mainAreaHolder_ = nullptr;            // MainComponent's; null keeps every host here
    synth::ui::DetachablePanelHost* hostInMain_ = nullptr; // the host currently parented to mainAreaHolder_
    bool mixerTabEnabled_ = true;
    juce::ApplicationProperties* appProperties_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    std::function<void(const juce::String&)> announceHook_;
    juce::String swapShortcutTexts_[4];
    static constexpr const char* kActiveTabKey = "bottomDockActiveTab";
    // The user-controlled strip order (a permutation of all four views, the main view and detached
    // tabs keeping their slots) and the main view.
    BottomDockTabModel tabModel_;
    static constexpr const char* kTabOrderKey = "bottomDockTabOrder";
    static constexpr const char* kMainViewKey = "mainView";
    // The tab currently drawn lifted (under the pointer, then gliding into its slot); empty outside
    // a drag that has cleared the drag threshold.
    std::optional<Tab> liftedTab_;
    // The strip area the tab buttons share, set by resized().
    juce::Rectangle<int> tabButtonsArea_;
    // The tabs offered when the press happened, in display order: the animator's item keys.
    std::vector<Tab> reorderTabs_;
    ReorderDragAnimator reorder_;
    ReorderFramePump reorderFrames_{*this};
    // Tabs coming and going: the offered set the buttons were last laid out for, and the move to a new one.
    BottomDockTabMotion tabMotion_;
    ReorderFramePump tabMotionFrames_{*this};
    std::vector<Tab> laidOutTabs_;
    double tabMotionStartMs_ = 0.0;
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
