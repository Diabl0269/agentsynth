// Concern: MixerMirrorController's open/close state machine: building, configuring and
// copy-wiring the mirror MixerPanelComponent, and hosting it in a DetachedPanelWindow it owns
// directly (there is no dock slot to reparent it out of -- see the class comment).
#include "MixerMirrorController.h"

namespace synth::ui {

MixerMirrorController::MixerMirrorController(juce::ApplicationProperties& appProperties, ConfigureFn configure)
    : appProperties_(appProperties)
    , configure_(std::move(configure)) {
    headerTitle_.setText("Mixer", juce::dontSendNotification);
    headerButton_.setClickingTogglesState(false);
    headerButton_.setButtonText({}); // icon-only -- same convention DetachablePanelHost's own button uses
    headerButton_.setTooltip("Close mixer window");
    headerButton_.setTitle("Close mixer window");
    headerButton_.onClick = [this] { close(); };
}

MixerMirrorController::~MixerMirrorController() = default;

void MixerMirrorController::open(MixerPanelComponent& sourcePanel, synth::theme::AppLookAndFeel* lookAndFeel,
                                 ShortcutManager* shortcutManager,
                                 std::function<bool(const juce::KeyPress&)> appShortcutFallback,
                                 bool createsNativeWindow) {
    if (window_ != nullptr) {
        window_->toFront(true);
        return;
    }
    if (mirror_ == nullptr) {
        mirror_ = std::make_unique<MixerPanelComponent>();
        configure_(*mirror_);
    }
    // Re-copied on every open() (idempotent, cheap) rather than only the first time -- MainComponent
    // may rewire a callback on the docked panel between an earlier close() and this open().
    mirror_->copyWiringFrom(sourcePanel);
    // Cross-wired here rather than folded into copyWiringFrom()'s blanket copy -- each
    // instance's onLiveMixerStateChanged must point at the OTHER instance's refreshLiveMixerVisuals(),
    // never its own (copyWiringFrom() copying it verbatim would make the mirror refresh itself
    // instead of the dock). Safe to capture sourcePanel by reference: both it and mirror_ are owned
    // by the same BottomDockComponent for their entire overlapping lifetime, same contract as this
    // controller's own ConfigureFn capturing the engine/doc/undoManager/graphEditor by reference.
    mirror_->onLiveMixerStateChanged = [&sourcePanel] { sourcePanel.refreshLiveMixerVisuals(); };
    mirror_->rebuild();

    window_ = std::make_unique<DetachedPanelWindow>(*mirror_, headerButton_, headerTitle_, "mixerMirrorWindowBounds",
                                                    &appProperties_, lookAndFeel, shortcutManager);
    window_->onCloseRequested = [this] { close(); };
    window_->onAppShortcut = std::move(appShortcutFallback);
    window_->registerHostedPanelFocusRegion("mixerMirror", *mirror_);

    // Same native-peer promotion DetachablePanelHost::setDetached(true) performs -- see that
    // method's own comment: setVisible(true) alone never creates one, and this must run first.
    if (createsNativeWindow && juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        window_->addToDesktop();
    window_->setVisible(true);
    window_->toFront(true);

    if (onOpenedOrClosed)
        onOpenedOrClosed();
}

void MixerMirrorController::close() {
    if (window_ == nullptr)
        return;
    window_.reset();
    if (onOpenedOrClosed)
        onOpenedOrClosed();
}

} // namespace synth::ui
