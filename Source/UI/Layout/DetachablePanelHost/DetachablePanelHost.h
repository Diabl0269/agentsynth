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
    juce::String focusRegionId_;
    juce::Component* focusRegionRoot_ = nullptr;
    std::unique_ptr<DetachedPanelWindow> window_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachablePanelHost)
};

} // namespace synth::ui
