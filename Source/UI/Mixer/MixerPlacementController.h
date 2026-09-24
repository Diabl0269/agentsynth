#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/PanelResizeHandle.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Mixer/MixerDockComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
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
// "Own panel" (FRO231) slides open and closed like the Timeline dock, has a persisted user height
// ("mixerOwnPanelHeight") and its own top-edge PanelResizeHandle. The slide is this class's own
// PanelSlide + AnimationDriver (MainComponent's three fractions are not touched); it calls
// onLayoutNeeded each frame and MainComponent::resized() reads getCarveHeight(). The
// MixerDockComponent/isTimelineVisible rename mentioned in FRO11's own comments stays deferred --
// see docs/mixer/panel.md#the-three-placements.
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

    /** Default AND minimum Own-panel height. */
    static constexpr int kOwnPanelMinHeight = 220;
    static constexpr const char* kOwnPanelHeightKey = "mixerOwnPanelHeight";

    /** OwnPanel placement and visible -- true for the whole open/close slide, not just at rest. */
    bool isOwnPanelShowing() const noexcept { return placement_ == Placement::OwnPanel && isVisible(); }

    /** Pixels MainComponent::resized() carves off the bottom: the slide fraction times the height,
     *  0 outside OwnPanel placement. */
    int getCarveHeight() const noexcept;
    /** The current full (open) height, clamped against the last layout context. */
    int getOwnPanelHeight() const noexcept;
    /** Clamps, stores and (if `persist`) writes the height; lays out live via onLayoutNeeded. */
    void setOwnPanelHeight(int desiredHeight, bool persist);
    /** [kOwnPanelMinHeight, max(min, 3/4 of the window - reservedForDock)]. */
    static int clampHeight(int desiredHeight, int windowHeight, int reservedForDock) noexcept;
    /** MainComponent::resized() feeds the window height and what the dock keeps (0 when closed). */
    void setLayoutContext(int windowHeight, int reservedForDock) noexcept;

    /** Fired per slide frame, at slide end and on every height change: MainComponent re-lays out. */
    std::function<void()> onLayoutNeeded;

    /** The strip's top-edge grab handle, and the slide's state -- test seams (no OS mouse source or
     *  VBlank exists headlessly). */
    juce::Component& getResizeHandle() noexcept { return ownHandle_; }
    float getSlideProgressForTest() const noexcept { return slide_.getProgress(); }
    float getSlideTweenStartForTest() const noexcept { return slide_.getTweenStart(); }
    bool isSlideAnimatingForTest() const noexcept { return slideAnim_.isRunning(); }
    void setSlideProgressForTest(float progress) noexcept { slide_.snapTo(progress); }
    /** Makes the next toggle start a real tween even off-screen (no VBlank then delivers frames). */
    void forceSlideAnimationForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    /** Stands in for one VBlank frame at eased progress `t`, and for the slide's completion. */
    void applySlideFrameForTest(float t) { applySlideFrame(t); }
    void finishSlideForTest() { finishSlide(); }

    /** "Reveal" the Mixer regardless of placement -- MainComponent::performToggleMixerPanel funnels
     *  through here first. Tab placement: false, unhandled (the caller keeps its existing
     *  open/close-the-dock behaviour). Own panel: slides this strip open/closed, true (handled
     *  here). Window: opens (first reveal) / closes the DetachedPanelWindow, true (handled here). */
    bool revealOrToggle();

    static constexpr const char* kMixerPlacementKey = "mixerPlacement";

    void resized() override;

private:
    void applyPlacement(Placement placement);
    Placement readPersistedPlacement() const;
    void restorePersistedHeight();
    void hideStripAtRest();
    void beginSlide();
    void applySlideFrame(float t);
    void finishSlide();
    int effectiveHeight() const noexcept;

    MixerDockComponent& mixerDock_;
    juce::ApplicationProperties& appProperties_;
    Placement placement_ = Placement::Tab; // matches MixerDockComponent's own already-Tab default
    // Added last in the constructor so it wins the hit test over the hosted panel's own header.
    PanelResizeHandle ownHandle_{*this};
    PanelSlide slide_;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver slideAnim_;
    bool open_ = false; // the OwnPanel open/closed INTENT; slide_ is where the strip currently is
    int ownPanelHeight_ = kOwnPanelMinHeight;
    bool heightRestored_ = false;
    bool forceAnimateForTest_ = false;
    int windowHeight_ = 0;
    int reservedForDock_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPlacementController)
};

} // namespace synth::ui
