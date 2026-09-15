#pragma once

#include "DetachedPanelWindow.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// DetachablePanelHost.h -- FRO12 (P9-6, docs/mixer.md §5.9): the ONE mechanism that moves a panel
// between its dock slot and its own top-level window, shared by the Timeline and the Mixer panel.
// Holds `panel` BY REFERENCE -- the same Component instance its owner already constructed, never
// copied or recreated -- so detach/redock never disturbs the panel's own live state (scroll
// position, zoom, selection). Reparenting is not a graph replacement, so it is orthogonal to (and
// never bypasses) docs/mixer.md §5.3's own "unbind before any graph replacement" seam
// (GraphEditor::onBeforeDetachAllModuleComponents -> MixerPanelComponent::unbindAllColumns()).
//
// Docked, this draws a small header strip (title + a right-aligned icon-only detach button) above
// the slot where `panel` lives -- UNLESS setEmbeddedHeader(true) is set, in which case this host
// draws NO header of its own while docked (its owner has embedded its own equivalent control into
// its own chrome instead -- see MixerDockComponent's tab strip in Tab placement). Detached, `panel`
// (and the header's button + title) are reparented into a DetachedPanelWindow, which carries its
// own copy of the header strip with the button now in "Dock back" state; closing that window (or
// clicking the button again) redocks everything here.
class DetachablePanelHost : public juce::Component {
public:
    // `boundsKey` is the ApplicationProperties key a detached window's position/size persists
    // under ("timelineWindowBounds" / "mixerWindowBounds"). `lookAndFeel`/`shortcutManager` are
    // forwarded to every DetachedPanelWindow this host builds (may be null in a headless test that
    // doesn't need themed icons or Tab-cycling).
    DetachablePanelHost(juce::Component& panel, juce::String title, juce::String boundsKey,
                        juce::ApplicationProperties* appProperties, synth::theme::AppLookAndFeel* lookAndFeel,
                        ShortcutManager* shortcutManager);
    ~DetachablePanelHost() override;

    /** Moves `panel` (and the header's button/title) between the dock slot and a
     *  DetachedPanelWindow. Idempotent -- calling with the current state is a no-op. */
    void setDetached(bool detached);
    bool isDetached() const noexcept { return window_ != nullptr; }

    juce::DrawableButton& getDetachButton() noexcept { return detachButton_; }

    /** When true, this host draws no header strip of its own while DOCKED -- its owner has
     *  embedded getDetachButton() into its own chrome (MixerDockComponent's tab strip in Tab
     *  placement). Has no effect on the DETACHED window's header, which always shows one
     *  regardless. Safe to flip at runtime (a live Preferences placement change). */
    void setEmbeddedHeader(bool embedded);
    bool isEmbeddedHeader() const noexcept { return embeddedHeader_; }

    /** When true, setDetached(true) gives the freshly built DetachedPanelWindow a REAL native
     *  top-level window (a peer), so it actually shows up on screen -- see the FRO12 follow-up bug
     *  fixed here: `DetachedPanelWindow` is built `addToDesktop=false` (a deliberate headless-test
     *  seam -- see that class's header comment) and nothing ever promoted it, so `setVisible(true)`
     *  alone left the panel detached from its dock with no window anywhere (JUCE only creates a
     *  peer from a TopLevelWindow constructor's own addToDesktop=true, or an explicit
     *  addToDesktop() call -- never from setVisible()). `Main.cpp`'s `MainWindow` and
     *  `PluginEditor.cpp`'s `AgentSynthPluginEditor` -- the app's and the plugin's only REAL
     *  construction sites for a `MainComponent` -- call this true, once, right after construction.
     *  Every headless test builds a `MainComponent`/`MixerDockComponent`/`DetachablePanelHost`
     *  directly and leaves this at its default of false, so `setDetached(true)` there stays exactly
     *  as before: no native peer, ever (`DetachRedockStateTests.cpp` detaches a real, off-screen
     *  `MainComponent` this way). */
    void setCreatesNativeWindows(bool shouldCreate) noexcept { createsNativeWindows_ = shouldCreate; }
    bool isCreatingNativeWindows() const noexcept { return createsNativeWindows_; }

    /** The one focus region the hosted panel resolves to once detached -- id + root are exactly
     *  what the owner would otherwise pass to MainComponent::registerFocusRegions(). Stored so a
     *  later setDetached(true) can hand it to the freshly built window (see
     *  DetachedPanelWindow::registerHostedPanelFocusRegion). Call once, before the first detach. */
    void setHostedPanelFocusRegion(juce::String id, juce::Component& root);

    /** Fires after every setDetached() call completes (docked or detached), including one driven
     *  by the DETACHED window's own close button -- e.g. so MixerDockComponent can refresh its tab
     *  strip, or the owner can re-run registerFocusRegions(). */
    std::function<void()> onDetachedStateChanged;

    void resized() override;
    void paint(juce::Graphics&) override;
    void lookAndFeelChanged() override;

    static constexpr int kHeaderStripHeight = 22; // == MixerDockComponent::kTabStripHeight

    // ---- Testing hooks (DetachablePanelHostTests.cpp) ----
    DetachedPanelWindow* getDetachedWindowForTest() const { return window_.get(); }
    // Not const: juce::SettableTooltipClient::getTooltip() isn't const either.
    juce::String getButtonTooltipForTest() { return detachButton_.getTooltip(); }
    juce::String getButtonTextForTest() const { return detachButton_.getButtonText(); }
    bool isDetachButtonVisibleForTest() const noexcept { return detachButton_.isVisible(); }
    juce::Component& getPanelForTest() noexcept { return panel_; }

protected:
    // ---- Native-window seam (DetachablePanelHostTests.cpp's "native window" group) ----
    // setDetached(true) calls these two, in that order, to decide whether the freshly built window
    // gets a real native peer and to perform that call. Split into two overridable points (rather
    // than folding the display check into the first) so a test subclass can simulate "no primary
    // display" or "call reached" deterministically on ANY runner -- including a developer's Mac,
    // which always has a real display -- without this base implementation ever creating one.
    virtual bool hasPrimaryDisplayForNativeWindow() const {
        // A genuinely headless runner (Linux CI, no Xvfb) has zero displays; dereferencing one
        // segfaults rather than returning a degenerate result -- same guard
        // DetachedPanelWindow::restoreBoundsOrDefault() already uses.
        return juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    }
    virtual void addDetachedWindowToDesktop(DetachedPanelWindow& window) {
        // The flag-less TopLevelWindow overload -- it derives its style flags from
        // getDesktopWindowStyleFlags() (native title bar / resizable, matching what
        // DetachedPanelWindow's constructor already configured via setUsingNativeTitleBar/
        // setResizable) rather than us guessing them again here.
        window.addToDesktop();
    }

private:
    void toggleDetach() { setDetached(!isDetached()); }
    void applyIcon();
    void applyTooltip();

    juce::Component& panel_;
    juce::String title_;
    juce::String boundsKey_;
    juce::ApplicationProperties* appProperties_ = nullptr;
    synth::theme::AppLookAndFeel* lookAndFeel_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;

    juce::Label titleLabel_;
    juce::DrawableButton detachButton_{"detachPanel", juce::DrawableButton::ImageFitted};
    bool embeddedHeader_ = false;
    bool createsNativeWindows_ = false;
    juce::String focusRegionId_;
    juce::Component* focusRegionRoot_ = nullptr;
    std::unique_ptr<DetachedPanelWindow> window_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachablePanelHost)
};

} // namespace synth::ui
