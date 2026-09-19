#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Mixer/MixerDockComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// MixerPlacementController.h -- FRO12 (P9-6, docs/mixer/panel.md): owns the Mixer placement
// preference (Tab beside the Timeline / Own panel / Window) and moves MixerDockComponent's
// mixerHost_ (the SAME MixerPanelComponent instance throughout -- DetachablePanelHost's own
// "never copied" contract) between the three homes it can live in. The ONE collaborator
// MainComponent.h adds for this ticket (root CLAUDE.md's file-size-budget constraint) -- every
// placement-specific line lives here, not spread across MainComponent's own members.
//
// | Placement     | Mixer lives                                | Timeline dock | Detach state         |
// |---------------|---------------------------------------------|---------------|-----------------------|
// | Tab (default) | MixerDockComponent's own tab strip           | unaffected    | tab-strip button      |
// | Own panel     | this class's own second bottom strip (IS-A)  | unaffected    | its own header shown  |
// | Window        | a DetachedPanelWindow, opened on first reveal| unaffected    | detached; lazy at launch|
//
// mixerHost_ stays parented inside MixerDockComponent for BOTH Tab and Window placements (in
// Window mode it is simply hidden there via MixerDockComponent::setMixerTabEnabled(false) -- its
// resized() renders nothing once detached anyway) -- only "Own panel" actually reparents it, into
// THIS component (which IS the second strip; MainComponent adds and bounds it directly).
//
// "Own panel" ships without the Timeline dock's animated open/close slide or persisted height in
// this ticket -- a plain visible/hidden strip at a fixed height (an explicit scope cut, matching
// the plan's own "no resize handle" cut for the same row). The MixerDockComponent/isTimelineVisible
// rename mentioned in FRO11's own comments stays deferred -- see docs/mixer/panel.md#the-three-placements.
class MixerPlacementController : public juce::Component {
public:
    enum class Placement { Tab, OwnPanel, Window };

    // Takes only mixerDock + appProperties -- mixerHost_ already carries its own LookAndFeel/
    // ShortcutManager pointers (set once, at MixerDockComponent's own construction), so this
    // controller never needs either directly.
    MixerPlacementController(MixerDockComponent& mixerDock, juce::ApplicationProperties& appProperties);

    /** Re-reads "mixerPlacement" and moves the Mixer to wherever it now says. Call once at launch
     *  (after both panels exist) and again on every settings-file write, so a live Preferences
     *  change applies immediately with no restart -- see MainComponent::changeListenerCallback's
     *  settings branch. Idempotent: a no-op when the persisted value already matches. */
    void applyPlacementPreference();

    Placement getPlacement() const noexcept { return placement_; }

    /** Own-panel's fixed height -- see the class comment on the scope cut. MainComponent's
     *  resized() carves this much off the bottom, below the Timeline dock, whenever this is
     *  showing. */
    static constexpr int kOwnPanelHeight = 220;
    bool isOwnPanelShowing() const noexcept { return placement_ == Placement::OwnPanel && isVisible(); }

    /** "Reveal" the Mixer regardless of placement -- MainComponent::performToggleMixerPanel funnels
     *  through here first. Tab placement: false, unhandled (the caller keeps its existing
     *  open/close-the-dock behaviour). Own panel: toggles this strip's own visibility, true (handled
     *  here). Window: opens (first reveal) / closes the DetachedPanelWindow, true (handled here). */
    bool revealOrToggle();

    static constexpr const char* kMixerPlacementKey = "mixerPlacement";

    void resized() override;

private:
    void applyPlacement(Placement placement);
    Placement readPersistedPlacement() const;

    MixerDockComponent& mixerDock_;
    juce::ApplicationProperties& appProperties_;
    Placement placement_ = Placement::Tab; // matches MixerDockComponent's own already-Tab default

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPlacementController)
};

} // namespace synth::ui
