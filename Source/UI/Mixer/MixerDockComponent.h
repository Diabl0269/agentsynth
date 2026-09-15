#pragma once

#include "MixerPanelComponent/MixerPanelComponent.h"
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

    MixerDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine, synth::TimelineDoc& doc,
                       AppUndoManager& undoManager, GraphEditor& graphEditor);

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

    void resized() override;

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

    TimelinePanelComponent& timelinePanel_;
    MixerPanelComponent mixer_;
    juce::TextButton timelineTabButton_{"Timeline"};
    juce::TextButton mixerTabButton_{"Mixer"};
    Tab activeTab_ = Tab::Timeline;
    juce::ApplicationProperties* appProperties_ = nullptr;
    static constexpr const char* kActiveTabKey = "bottomDockActiveTab";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerDockComponent)
};

} // namespace synth::ui
