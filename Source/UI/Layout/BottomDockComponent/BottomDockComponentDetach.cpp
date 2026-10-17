// Concern: BottomDockComponent detach-to-window, the tab strip's detach button and the Mixer "both places"
// mirror (docs/mixer/panel.md).
#include "BottomDockComponent.h"

#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

namespace synth::ui {

void BottomDockComponent::applyDetachBothPlacesPreference() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const bool enabled = appProperties_->getUserSettings()->getValue(kDetachBothPlacesKey, "move") == "both";
    if (enabled == detachBothPlacesEnabled_)
        return;
    detachBothPlacesEnabled_ = enabled;
    // Scoped to Tab placement (mixerTabEnabled_) -- Own-panel/Window placement's own detach state is
    // MixerPlacementController's, not this preference's, to transition (see this method's own doc
    // comment on BottomDockComponent.h).
    if (mixerTabEnabled_) {
        if (enabled && mixerHost_.isDetached()) {
            // Was "move" (the real host detached, tab gone from the strip) -- redock it, switch
            // back to it (redocking alone does not: applyTabVisibility()'s fallback pick never
            // un-does an earlier fallback just because Mixer became offered again) and open a
            // mirror instead, so both places show it as the preference now promises.
            mixerHost_.setDetached(false);
            setActiveTab(Tab::Mixer);
            toggleActiveHostDetach(); // Mixer is active + detachBothPlacesEnabled_ is already true -> opens the mirror
        } else if (!enabled && mixerMirror_.isOpen()) {
            // Was "both" -- close the mirror and detach the real host instead, which is exactly
            // the "a detached tab leaves the strip" behaviour the default preference restores.
            mixerMirror_.close();
            if (!isInMainArea(Tab::Mixer)) // a host in the main area stays there; it cannot detach from it yet
                mixerHost_.setDetached(true);
        }
    } else if (mixerMirror_.isOpen()) {
        // A mirror should never outlive Tab placement -- see setMixerTabEnabled()'s own comment.
        mixerMirror_.close();
    }
    refreshDetachButton();
}

// Replaces the plain activeHost().setDetached(!isDetached()) toggle -- routes the Mixer
// tab through the mirror instead of a real detach while "both places" is on.
void BottomDockComponent::toggleActiveHostDetach() {
    if (usesMixerMirrorForDetach()) {
        if (mixerMirror_.isOpen())
            mixerMirror_.close();
        else
            openMixerMirror();
        return;
    }
    if (auto* host = activeHost())
        host->setDetached(!host->isDetached());
}

void BottomDockComponent::openMixerMirror() {
    // Same dynamic_cast-with-null-fallback convention refreshDetachButton()/applyIcon() use --
    // headless tests (no real LookAndFeel installed yet) leave the mirror window unthemed, matching
    // a real detach's own fallback.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    mixerMirror_.open(*mixer_, lf, shortcutManager_, mixerHost_.onAppShortcutFallback,
                      mixerHost_.isCreatingNativeWindows());
}

void BottomDockComponent::refreshDetachButton() {
    // Same wording as the tooltip -- without an explicit setTitle(), the ctor's
    // "detachActiveTab" component name leaks as the AX title (ButtonAccessibilityHandler::
    // getTitle()'s getButtonText() fallback). While "both places" drives the Mixer tab, the
    // button's own state tracks the MIRROR (mixerHost_ itself never actually detaches then).
    // The canvas has no window to open into, so the button is hidden while its tab is active.
    const auto* host = activeHost();
    detachButton_.setVisible(host != nullptr);
    const bool detached = usesMixerMirrorForDetach() ? mixerMirror_.isOpen() : host != nullptr && host->isDetached();
    const juce::String label = detached ? "Dock back" : "Open in window";
    detachButton_.setTooltip(label);
    detachButton_.setTitle(label);
    // Same dynamic_cast-with-null-fallback convention DetachablePanelHost::applyIcon uses.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto base = lf->getIcon(synth::theme::Icon::ActionDetachWindow);
    if (base == nullptr)
        return;
    const auto& colors = lf->getTheme().colors;
    auto hoverIcon = base->createCopy();
    hoverIcon->replaceColour(colors.textMuted, colors.textPrimary);
    detachButton_.setImages(base.get(), hoverIcon.get(), hoverIcon.get());
}

void BottomDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

} // namespace synth::ui
