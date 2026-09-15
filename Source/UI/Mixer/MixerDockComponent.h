#pragma once

#include "MixerPanelComponent/MixerPanelComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager;
class GraphEditor;
class AudioEngine;

// MixerDockComponent.h -- FRO11 (P9-5, docs/mixer.md §5.9): the bottom dock's own tab strip.
//
// The ONE component MainComponent.h holds for this ticket (see the plan's file-size-budget
// constraint, docs/mixer_implementation.md item 4): owns `timelinePanel` by reference (NOT a
// copy/move -- MainComponent still owns and constructs it) and a MixerPanelComponent by value.
// MainComponent::resized()'s existing dock carve (`timelinePanel.setBounds(...)`) becomes
// `mixerDock.setBounds(...)` -- one line changed, not two new carve blocks; the open/close slide,
// height and persisted-visible state stay MainComponent's own (isTimelineVisible/timelineSlide_),
// unchanged and now gating the whole dock rather than just the Timeline tab (P9-6 owns the rename
// to a dock-neutral name -- see the plan's own note on this).
namespace synth::ui {

class MixerDockComponent : public juce::Component {
public:
    enum class Tab { Timeline, Mixer };

    // FRO12 (P9-6, docs/mixer.md §5.9): `appProperties`/`lookAndFeel`/`shortcutManager` are
    // forwarded straight into timelineHost_/mixerHost_ (both DetachablePanelHost) -- see that
    // class for what each is for. `lookAndFeel`/`shortcutManager` may be null in a headless test
    // that never detaches a panel.
    MixerDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine, synth::TimelineDoc& doc,
                       AppUndoManager& undoManager, GraphEditor& graphEditor,
                       juce::ApplicationProperties& appProperties, synth::theme::AppLookAndFeel* lookAndFeel,
                       ShortcutManager* shortcutManager);

    /** Reads the persisted active tab once ("bottomDockActiveTab", default "timeline" --
     *  docs/layout.md's "Panel collapse and persistence" table); writes it on every tab switch. */
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
    /** Switches tabs (persisting the choice) and lays out; a no-op if already on `tab`. Does NOT
     *  open/close the dock itself -- that stays MainComponent's own isTimelineVisible/
     *  beginPanelSlide() (see the class comment). */
    void setActiveTab(Tab tab);

    /** Re-runs the mixer panel's snapshot + column rebuild -- call after every graph/timeline/
     *  macro change (MainComponent's existing reconcile funnel is the natural place). */
    void rebuildMixer() { mixer_.rebuild(); }
    MixerPanelComponent& getMixerPanel() noexcept { return mixer_; }

    /** THE P9-5 HOOK for TrackChannelLinkController::setMixerRevealHook: switches to the Mixer
     *  tab and reveals `stripId`'s column. False when nothing resolves (no such column --
     *  TrackChannelLinkController falls back to the canvas reveal then). Does NOT open the dock
     *  if it is closed -- the hook's caller does that first (MainComponent owns that state). */
    bool revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId);

    /** MainComponent::timerCallback's one new gated line -- the caller checks
     *  isMixerTabActive() && isShowing() itself, matching the Timeline panel's own precedent
     *  (docs/layout_visuals_animation.md §2). */
    void refreshMeters() { mixer_.refreshMeters(); }

    // FRO12 (P9-6): whether the Mixer tab itself is offered at all -- false when the Mixer
    // placement preference is "Own panel" or "Window" (MixerPlacementController owns mixerHost_
    // entirely in those modes; see that class). Forces the active tab back to Timeline if it was
    // Mixer. True (the default) is the existing Tab-placement behaviour, unchanged.
    void setMixerTabEnabled(bool enabled);

    /** The Timeline's own detach-to-window host -- always owned and shown here, in every Mixer
     *  placement (docs/mixer.md §5.9's placement table: "Timeline dock: unaffected"). */
    synth::ui::DetachablePanelHost& getTimelineHost() noexcept { return timelineHost_; }
    /** The Mixer's detach-to-window host. Owned here always, but only PARENTED here in Tab
     *  placement -- MixerPlacementController reparents it into its own "Own panel" strip, or
     *  leaves it unparented (never eagerly shown) in "Window" placement until first reveal. */
    synth::ui::DetachablePanelHost& getMixerHost() noexcept { return mixerHost_; }

    /** Fires whenever either host's detach state changes (docked<->detached, either direction --
     *  including a window's own close button). MainComponent hooks this to re-run its focus-region
     *  registration pass (docs/shortcuts.md "Focus regions": a detached region must stop appearing
     *  in the DOCKED window's Tab-cycle order). Separate from either DetachablePanelHost's own
     *  onDetachedStateChanged, which this class's constructor already claims for
     *  applyTabVisibility() -- both fire from the one place, in that order. */
    std::function<void()> onPanelDetachStateChanged;

    void resized() override;
    void lookAndFeelChanged() override; // refreshes the tab-strip detach button's themed icon

    /** Height of the tab strip above the timeline panel's own content -- the amount
     *  MainComponentSetupTimeline.cpp's onResizeHeight/onResizeHeightCommitted wiring must add to
     *  the panel's self-reported desired content height (TimelinePanelComponent::ResizeHandle::
     *  desiredHeightFor, which stays agnostic of any owner chrome) to get MainComponent's own
     *  total dock-carve height. Public because the panel resize's whole point is a stable pixel
     *  contract between this component and its caller, not an implementation detail. */
    static constexpr int kTabStripHeight = 22;

private:
    void applyTabVisibility();
    void persistActiveTab();
    // FRO12: the active tab's DetachablePanelHost -- whichever the tab-strip detach button acts
    // on (docs/mixer.md §5.9: "the tab-strip button detaches whichever tab is active").
    synth::ui::DetachablePanelHost& activeHost() noexcept;
    void refreshDetachButton();

    TimelinePanelComponent& timelinePanel_;
    MixerPanelComponent mixer_;
    // FRO12 (P9-6): both panels' detach-to-window hosts. Declared after timelinePanel_/mixer_ so
    // both references are valid -- see DetachablePanelHost's own "held by reference, never
    // copied" contract.
    synth::ui::DetachablePanelHost timelineHost_;
    synth::ui::DetachablePanelHost mixerHost_;
    juce::TextButton timelineTabButton_{"Timeline"};
    juce::TextButton mixerTabButton_{"Mixer"};
    // FRO12: icon-only, embedded in this tab strip (not either host's own header -- see
    // DetachablePanelHost's class comment on why this is a separate button instance rather than a
    // literal shared one across three different parents).
    juce::DrawableButton detachButton_{"detachActiveTab", juce::DrawableButton::ImageFitted};
    Tab activeTab_ = Tab::Timeline;
    bool mixerTabEnabled_ = true;
    juce::ApplicationProperties* appProperties_ = nullptr;
    static constexpr const char* kActiveTabKey = "bottomDockActiveTab";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerDockComponent)
};

} // namespace synth::ui
