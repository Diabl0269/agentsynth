// Concern: BottomDockComponent detach-to-window, each tab's detach button and the Mixer "both places"
// mirror (docs/mixer/panel.md).
#include "BottomDockComponent.h"

#include "ShortcutManager/ShortcutManager.h"
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
            toggleTabDetach(Tab::Mixer); // detachBothPlacesEnabled_ is already true -> opens the mirror
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
    refreshDetachIcons();
}

// A tab's detach button: routes the Mixer tab through the mirror instead of a real detach while "both places" is on.
void BottomDockComponent::toggleTabDetach(Tab tab) {
    if (!isTabOfferedInStrip(tab))
        return; // the view in the main area has no tab, and a window that is already open redocks from itself
    if (usesMixerMirrorForDetach(tab)) {
        if (mixerMirror_.isOpen())
            mixerMirror_.close();
        else
            openMixerMirror();
        return;
    }
    // The view in the main area never detaches from here: it has no tab.
    if (auto* host = hostForTab(tab); host != nullptr && host != hostInMain_)
        host->setDetached(!host->isDetached());
}

void BottomDockComponent::openMixerMirror() {
    // Same dynamic_cast-with-null-fallback convention applyIcon() uses -- headless tests (no real LookAndFeel
    // installed yet) leave the mirror window unthemed, matching a real detach's own fallback.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    mixerMirror_.open(*mixer_, lf, shortcutManager_, mixerHost_.onAppShortcutFallback,
                      mixerHost_.isCreatingNativeWindows());
}

synth::ui::IconButton* BottomDockComponent::getDetachIcon(Tab tab) noexcept {
    return buttonForTab(tab).getDetachIcon();
}

juce::String BottomDockComponent::detachTitleForTab(Tab tab) {
    return tab == Tab::Canvas ? juce::String() : juce::String("Detach ") + nameForTab(tab) + " to its own window";
}

// The button is a plain button, never a Tab stop of its own (the strip is the one stop; Option+Return presses the
// active tab's), named like its tooltip so a screen reader reads "Detach Mixer to its own window" for the button
// inside the Mixer tab. While the Mixer's second view is open the same button closes it.
void BottomDockComponent::refreshDetachIcons() {
    static const auto pressKey = ShortcutManager::keyPressToDisplayString(
        juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::altModifier, 0));
    for (Tab t : tabModel_.getOrder()) {
        auto* icon = getDetachIcon(t);
        if (icon == nullptr)
            continue;
        const bool closesMirror = usesMixerMirrorForDetach(t) && mixerMirror_.isOpen();
        const juce::String title = closesMirror ? juce::String("Close the second Mixer window") : detachTitleForTab(t);
        icon->setTitle(title);
        icon->setTooltip(synth::ui::formatShortcutHint(title, pressKey));
    }
}

} // namespace synth::ui
