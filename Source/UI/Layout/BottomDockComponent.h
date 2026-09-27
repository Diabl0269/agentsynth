#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
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

// BottomDockComponent.h -- FRO11 (P9-5, docs/mixer/panel.md): the bottom dock's own tab strip.
//
// The ONE component MainComponent.h holds for this ticket (see the plan's file-size-budget
// constraint, docs/mixer/panel.md#what-the-mixer-shows): owns `timelinePanel` by reference (NOT a
// copy/move -- MainComponent still owns and constructs it) and a MixerPanelComponent by value.
// MainComponent::resized()'s existing dock carve (`timelinePanel.setBounds(...)`) becomes
// `bottomDock.setBounds(...)` -- one line changed, not two new carve blocks; the open/close slide,
// height and persisted-visible state stay MainComponent's own (isBottomDockVisible/timelineSlide_),
// gating the whole dock rather than just the Timeline tab -- the rename to a dock-neutral name
// (FRO232) is done; this class and isBottomDockVisible both carry it now.
namespace synth::ui {

class BottomDockComponent : public juce::Component {
public:
    // FRO131 (docs/control/midi-remote-ui.md#the-controllers-panel): MidiRemote is a third,
    // always-offered tab -- unlike Mixer it has no OwnPanel/Window placement variant, so there is
    // no MidiRemote counterpart to mixerTabEnabled_.
    enum class Tab { Timeline, Mixer, MidiRemote };

    // FRO12 (P9-6, docs/mixer/panel.md): `appProperties`/`lookAndFeel`/`shortcutManager` are
    // forwarded straight into timelineHost_/mixerHost_ (both DetachablePanelHost) -- see that
    // class for what each is for. `lookAndFeel`/`shortcutManager` may be null in a headless test
    // that never detaches a panel.
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
    /** FRO18: MainComponent wires this to performTrackEdit(setTrackArmed(...)) -- fired by the
     *  Arm key (rebindable "timelineArmFocusedTrack") when a linked strip is focused. Sibling
     *  forwarder to setOnGraphTopologyChanged/setOnMakeChannelForNode above: MainComponent talks
     *  to the dock, never reaches through getMixerPanel() to set the panel's own callback field. */
    void setOnArmTrack(std::function<void(synth::TrackId)> callback);

    Tab getActiveTab() const noexcept { return activeTab_; }
    bool isMixerTabActive() const noexcept { return activeTab_ == Tab::Mixer; }
    bool isMidiRemoteTabActive() const noexcept { return activeTab_ == Tab::MidiRemote; }
    /** Switches tabs (persisting the choice) and lays out; a no-op if already on `tab`. Does NOT
     *  open/close the dock itself -- that stays MainComponent's own isBottomDockVisible/
     *  beginPanelSlide() (see the class comment). */
    void setActiveTab(Tab tab);

    /** FRO333: true while at least one tab is currently offered in the strip. MainComponent hides
     *  the whole dock when this goes false and reopens it when it comes back true (see
     *  bottomDockAutoHiddenByEmptyTabs_'s own comment). */
    bool hasAnyVisibleTab() const noexcept;

    /** Re-runs the mixer panel's snapshot + column rebuild -- call after every graph/timeline/
     *  macro change (MainComponent's existing reconcile funnel is the natural place). */
    void rebuildMixer() { mixer_.rebuild(); }
    MixerPanelComponent& getMixerPanel() noexcept { return mixer_; }
    // FRO227: const overload for resolveEditSurface(), a const member function.
    const MixerPanelComponent& getMixerPanel() const noexcept { return mixer_; }

    /** FRO263: mirrors rebuildMixer() above -- see its call site's own comment. */
    void rebuildMidiRemote() { midiRemotePanel_.rebuildFromProfiles(); }

    /** THE P9-5 HOOK for TrackChannelLinkController::setMixerRevealHook: switches to the Mixer
     *  tab and reveals `stripId`'s column. False when nothing resolves (no such column --
     *  TrackChannelLinkController falls back to the canvas reveal then). Does NOT open the dock
     *  if it is closed -- the hook's caller does that first (MainComponent owns that state). */
    bool revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId);

    /** MainComponent::timerCallback's one new gated line -- the caller checks isMixerShowing()
     *  itself (Tab placement: isMixerTabActive() && isVisible(); Window placement: the mixer's
     *  DetachablePanelHost is detached -- its own window is a separate top-level Component, so
     *  THIS dock's isVisible() says nothing about whether that window is on screen), matching the
     *  Timeline panel's own precedent (docs/layout/rendering.md). */
    void refreshMeters() { mixer_.refreshMeters(); }

    /** FRO146 follow-up: "is the mixer panel showing anywhere a meter tick would be visible" --
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

    /** FRO131: same "showing anywhere a tick would be visible" shape as isMixerShowing() above,
     *  for MainComponent::timerCallback's refreshMidiRemoteActivity() gate. */
    bool isMidiRemoteShowing() const noexcept {
        return midiRemoteHost_.isDetached() || (isMidiRemoteTabActive() && isVisible());
    }

    // FRO12 (P9-6): whether the Mixer tab itself is offered at all -- false when the Mixer
    // placement preference is "Own panel" or "Window" (MixerPlacementController owns mixerHost_
    // entirely in those modes; see that class). True (the default) is the existing Tab-placement
    // behaviour, unchanged.
    void setMixerTabEnabled(bool enabled);

    /** Re-applies visibility/z-order for the active tab, without a Mixer/MIDI Remote rebuild. */
    void refreshTabVisibility();

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
     *  registration pass (docs/control/shortcuts.md "Focus regions") AND (FRO333) to keep
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

    void resized() override;
    void lookAndFeelChanged() override; // refreshes the tab-strip detach button's themed icon

    /** Total tab-strip height, including the top PanelResizeHandle::kHeight px the handle overlaps. */
    static constexpr int kTabStripHeight = 22;
    /** FRO338: height of the Mixer-only toolbar row (Add bus/Reset Meters) carved from the TOP of
     *  the Mixer tab's own content area, below the tab strip -- Timeline/Controllers never carve
     *  it, so their content keeps the dock's full remaining height regardless of activeTab_. */
    static constexpr int kMixerToolbarHeight = 24;

    // ---- Resizable height (top-edge grab strip, every tab) ----
    // Both callbacks carry the desired TOTAL dock height, UNCLAMPED; MainComponent clamps and persists.

    /** Fired on every drag step. */
    std::function<void(int desiredHeight)> onResizeHeight;
    /** Fired once on mouse-up after a real drag (never for a stray click): the cue to persist. */
    std::function<void(int desiredHeight)> onResizeHeightCommitted;
    /** Test seam: no OS mouse source exists headlessly, so tests drive events through the strip. */
    juce::Component& getResizeHandle() noexcept { return resizeHandle_; }
    bool isResizeHandleHovered() const noexcept { return resizeHandle_.isHovered(); }

    /** FRO15 test seam: the "Add bus" button (FRO338: its own Mixer-only toolbar row above the
     *  mixer content, not the tab strip -- see resized()'s own comment). */
    juce::TextButton& getAddBusButtonForTest() noexcept { return addBusButton_; }
    /** FRO146 test seam: the "Reset Meters" button, same FRO338 toolbar row as "Add bus" above --
     *  resets every column's (and Master's) clip readout, same as an Option/Alt-click on any one of
     *  them. */
    juce::TextButton& getResetMetersButtonForTest() noexcept { return resetMetersButton_; }
    /** FRO228 test seam: this dock's own detach/redock button. */
    juce::DrawableButton& getDetachButtonForTest() noexcept { return detachButton_; }
    /** FRO333 test seam: the tab strip's current drag-reorderable order. */
    const std::vector<Tab>& getTabOrderForTest() const noexcept { return tabOrder_; }
    /** FRO333 test seam: drives the same swap-under-cursor + persist + key-permute path a real
     *  drag-and-release does, without synthesizing mouse events. */
    void reorderTabsForTest(Tab dragged, Tab droppedOnto) {
        swapTabOrder(dragged, droppedOnto);
        persistTabOrder();
        permuteShortcutKeysForNewOrder();
        resized();
    }

private:
    // FRO333: one tab-strip button; reports drags to its owner for reorder-by-swap (see .cpp).
    class DockTabButton : public juce::TextButton {
    public:
        DockTabButton(BottomDockComponent& owner, Tab tab, const juce::String& text)
            : juce::TextButton(text)
            , owner_(owner)
            , tab_(tab) {}
        void mouseDown(const juce::MouseEvent& e) override {
            owner_.beginTabDrag();
            juce::TextButton::mouseDown(e);
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            owner_.dragTab(tab_, e);
            // FRO338: only once the gesture is a real drag (JUCE's own move threshold, the same
            // gate dragTab() itself uses) -- a stray click never shows the dragging-hand cursor.
            if (e.mouseWasDraggedSinceMouseDown())
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            juce::TextButton::mouseDrag(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            if (!owner_.endTabDrag())
                juce::TextButton::mouseUp(e);
        }

    private:
        BottomDockComponent& owner_;
        Tab tab_;
    };

    // FRO146 follow-up: `allowMixerRebuild` is false ONLY from the detach/redock callback
    // (onEitherHostDetachStateChanged) -- reparenting into/out of a DetachedPanelWindow doesn't
    // change which graph nodes the mixer shows, so a rebuild there was pure collateral damage: it
    // tore down and rebuilt every column, and with it every column's own latched clip-readout
    // state (MixerMeterReadout's running max/clip colour), resetting a mid-session "-inf" -> real
    // reading back to "-inf" on every single detach or redock. Every other caller (an actual tab
    // switch, or the Mixer tab regaining `mixerTabEnabled_`) keeps the pre-existing "catch up on
    // whatever changed while the Mixer tab was hidden" rebuild, unchanged (default true).
    void applyTabVisibility(bool allowMixerRebuild = true);
    void persistActiveTab();
    // FRO12: the active tab's DetachablePanelHost -- whichever the tab-strip detach button acts
    // on (docs/mixer/panel.md: "the tab-strip button detaches whichever tab is active").
    synth::ui::DetachablePanelHost& activeHost() noexcept;
    void refreshDetachButton();

    // ---- FRO333: tab order, "which tabs are offered right now", and the fallback pick ----
    synth::ui::DetachablePanelHost& hostForTab(Tab tab) noexcept;
    const synth::ui::DetachablePanelHost& hostForTab(Tab tab) const noexcept;
    juce::TextButton& buttonForTab(Tab tab) noexcept;
    /** True when `tab` belongs in the strip right now: not detached, and (Mixer only) not disabled
     *  by the placement preference. */
    bool isTabOfferedInStrip(Tab tab) const noexcept;
    /** `activeTab_` if it's still offered, else the first offered tab in tabOrder_, else
     *  `activeTab_` unchanged (hasAnyVisibleTab() is false -- nothing to fall back to). */
    Tab pickFallbackActiveTab() const noexcept;

    // ---- FRO333: drag-to-reorder (DockTabButton's own mouse overrides call these; see .cpp) ----
    void beginTabDrag() noexcept { tabDragReordered_ = false; }
    void dragTab(Tab dragged, const juce::MouseEvent& e);
    bool endTabDrag();
    void swapTabOrder(Tab a, Tab b);
    void persistTabOrder();
    /** Permutes the three tabs' own Cmd+digit key bindings to match tabOrder_'s new order. */
    void permuteShortcutKeysForNewOrder();
    static const char* actionIdForTab(Tab tab) noexcept;

    TimelinePanelComponent& timelinePanel_;
    MixerPanelComponent mixer_;
    // FRO131: the panel and its host follow the same "declared before its host so the reference
    // is valid" rule as mixer_/mixerHost_ below.
    synth::ui::MidiRemotePanelComponent midiRemotePanel_;
    // FRO12 (P9-6): both panels' detach-to-window hosts. Declared after timelinePanel_/mixer_ so
    // both references are valid -- see DetachablePanelHost's own "held by reference, never
    // copied" contract.
    synth::ui::DetachablePanelHost timelineHost_;
    synth::ui::DetachablePanelHost mixerHost_;
    synth::ui::DetachablePanelHost midiRemoteHost_;
    DockTabButton timelineTabButton_;
    DockTabButton mixerTabButton_;
    DockTabButton midiRemoteTabButton_;
    // FRO12: icon-only, embedded in this tab strip (not either host's own header -- see
    // DetachablePanelHost's class comment on why this is a separate button instance rather than a
    // literal shared one across three different parents).
    juce::DrawableButton detachButton_{"detachActiveTab", juce::DrawableButton::ImageFitted};
    // FRO15 (docs/mixer/sends-and-buses.md): "Add bus" is visible only on the Mixer tab -- it has no
    // meaning while the Timeline tab is showing. FRO338: laid out in its own toolbar row above the
    // mixer content, not carved from the tab strip (see resized()'s own comment).
    juce::TextButton addBusButton_{"+ Bus"};
    // FRO146: sits next to "+ Bus" (same Mixer-tab-only visibility) -- resets every column's clip
    // readout (docs/mixer/mixer.md meters section's "Meter Peak Level" reset action).
    juce::TextButton resetMetersButton_{"Reset Meters"};
    // Added LAST in the constructor so it wins the hit test; forwards into onResizeHeight*.
    PanelResizeHandle resizeHandle_{*this};
    Tab activeTab_ = Tab::Timeline;
    bool mixerTabEnabled_ = true;
    juce::ApplicationProperties* appProperties_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    static constexpr const char* kActiveTabKey = "bottomDockActiveTab";
    // FRO333: user-controlled visual/Cmd+N order, a permutation of all three Tab values regardless
    // of which are currently detached (a detached tab keeps its slot so redocking restores it there
    // rather than always appending it at the end).
    std::vector<Tab> tabOrder_{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
    static constexpr const char* kTabOrderKey = "bottomDockTabOrder";
    // True from a mouseDown that turns into a real reorder swap (see dragTab()); read once by
    // endTabDrag() and reset there.
    bool tabDragReordered_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BottomDockComponent)
};

} // namespace synth::ui
