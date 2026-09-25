// Concern: FRO12 (P9-6) -- the Mixer placement preference (Tab/Own panel/Window) and moving
// mixerHost_ between its three homes. FRO231 adds the Own panel's own slide, persisted height and
// resize handle, kept entirely inside this class.
#include "MixerPlacementController.h"

#include <algorithm>

namespace synth::ui {

namespace {
// Same length as MainComponent::kPanelSlideMs, so the Own panel and the bottom dock move alike.
constexpr double kSlideMs = 190.0;
} // namespace

MixerPlacementController::MixerPlacementController(BottomDockComponent& bottomDock,
                                                   juce::ApplicationProperties& appProperties)
    : bottomDock_(bottomDock)
    , appProperties_(appProperties) {
    setVisible(false); // Own-panel's strip; hidden until applyPlacementPreference() says otherwise
    restorePersistedHeight();

    // Added while the strip has no host child yet; applyPlacement(OwnPanel) fronts it again after
    // reparenting the host in, so it always wins the hit test over the hosted panel's own header.
    addChildComponent(ownHandle_);
    ownHandle_.setComponentID("ownPanelResizeHandle");
    ownHandle_.onResize = [this](int desiredHeight) { setOwnPanelHeight(desiredHeight, false); };
    ownHandle_.onResizeCommitted = [this](int desiredHeight) { setOwnPanelHeight(desiredHeight, true); };
}

// MainComponent constructs this before its ApplicationProperties has storage parameters, so the
// constructor's attempt finds no settings file; applyPlacementPreference() (called once the
// properties exist) retries until one read succeeds. Once only: a later settings-file write must
// not overwrite a height the user is dragging but has not committed yet.
void MixerPlacementController::restorePersistedHeight() {
    if (heightRestored_ || appProperties_.getUserSettings() == nullptr)
        return;
    heightRestored_ = true;
    // Absent -> the default, which is also the floor.
    ownPanelHeight_ =
        std::max(kOwnPanelMinHeight, appProperties_.getUserSettings()->getIntValue(kOwnPanelHeightKey, 0));
}

int MixerPlacementController::clampHeight(int desiredHeight, int windowHeight, int reservedForDock) noexcept {
    // Window not laid out yet: only the floor applies, so a persisted height survives construction
    // and is capped by the first real layout pass (same rule as MainComponent::clampTimelinePanelHeight).
    const int maxHeight = windowHeight > 0 ? std::max(kOwnPanelMinHeight, (windowHeight * 3) / 4 - reservedForDock)
                                           : std::max(kOwnPanelMinHeight, desiredHeight);
    return juce::jlimit(kOwnPanelMinHeight, maxHeight, desiredHeight);
}

void MixerPlacementController::setLayoutContext(int windowHeight, int reservedForDock) noexcept {
    windowHeight_ = windowHeight;
    reservedForDock_ = reservedForDock;
}

// The STORED height is the user's wish and is never rewritten by a layout pass: it is re-clamped
// against the live context on every read instead, so a height set on a big window (or while the
// dock was closed and reserved nothing) comes back once there is room again.
int MixerPlacementController::effectiveHeight() const noexcept {
    return clampHeight(ownPanelHeight_, windowHeight_, reservedForDock_);
}

int MixerPlacementController::getOwnPanelHeight() const noexcept { return effectiveHeight(); }

int MixerPlacementController::getCarveHeight() const noexcept {
    return placement_ == Placement::OwnPanel ? slide_.sizeBetween(0, effectiveHeight()) : 0;
}

void MixerPlacementController::setOwnPanelHeight(int desiredHeight, bool persist) {
    const int clamped = clampHeight(desiredHeight, windowHeight_, reservedForDock_);
    if (clamped != ownPanelHeight_) {
        ownPanelHeight_ = clamped;
        if (onLayoutNeeded)
            onLayoutNeeded(); // one pass per drag callback: user-driven, not a free-running repaint
    }
    // Persist only on drag end (persist == true): the per-step callback writes no settings.
    if (persist && appProperties_.getUserSettings() != nullptr) {
        appProperties_.getUserSettings()->setValue(kOwnPanelHeightKey, ownPanelHeight_);
        appProperties_.saveIfNeeded();
    }
}

MixerPlacementController::Placement MixerPlacementController::readPersistedPlacement() const {
    if (appProperties_.getUserSettings() == nullptr)
        return Placement::Tab;
    const auto s = appProperties_.getUserSettings()->getValue(kMixerPlacementKey, "tab");
    if (s == "ownPanel")
        return Placement::OwnPanel;
    if (s == "window")
        return Placement::Window;
    return Placement::Tab;
}

void MixerPlacementController::applyPlacementPreference() {
    restorePersistedHeight();
    const auto desired = readPersistedPlacement();
    // The FIRST call must run applyPlacement() even when `desired` already matches placement_'s
    // Tab default: MainComponent's addAndMakeVisible(*this) (addCanvasAndPanels(), which runs
    // before this is ever called) makes the strip visible unconditionally, so Tab/Window
    // placement's own hideStripAtRest() has to run at least once to correct that, or the strip's
    // isVisible() stays stuck true (zero-height, so harmless to look at, but wrong -- and a stale
    // flag callers like isOwnPanelShowing() rely on).
    if (everApplied_ && desired == placement_)
        return; // already there -- also what stops this from doing real work on every window
                // move/resize's persist-triggered ChangeListener re-notification (see
                // DetachedPanelWindow::persistBounds)
    everApplied_ = true;
    applyPlacement(desired);
}

void MixerPlacementController::applyPlacement(Placement placement) {
    auto& host = bottomDock_.getMixerHost();
    switch (placement) {
    case Placement::Tab:
        bottomDock_.addAndMakeVisible(host); // reclaims it (no-op if already there)
        host.setEmbeddedHeader(true);
        bottomDock_.setMixerTabEnabled(true);
        hideStripAtRest();
        break;
    case Placement::Window:
        // Stays parented inside bottomDock_ (harmless -- it renders nothing once detached; see
        // DetachablePanelHost::resized()), just hidden there via the disabled tab. Never
        // eagerly detached here -- "opened on first reveal", see revealOrToggle().
        bottomDock_.addAndMakeVisible(host);
        host.setEmbeddedHeader(true);
        bottomDock_.setMixerTabEnabled(false);
        hideStripAtRest();
        break;
    case Placement::OwnPanel:
        bottomDock_.setMixerTabEnabled(false);
        addAndMakeVisible(host); // reparents INTO this strip
        host.setEmbeddedHeader(false);
        // Shown at once with NO slide: a restore / preference change is not a toggle, and this
        // may run before the window exists.
        slideAnim_.stop(updater_);
        open_ = true;
        slide_.snapTo(1.0f);
        setVisible(true);
        ownHandle_.setVisible(true);
        ownHandle_.toFront(false); // the host was just added on top of it
        break;
    }
    placement_ = placement;
    resized();
    if (onLayoutNeeded)
        onLayoutNeeded(); // the carve changed; nothing else re-runs MainComponent's layout for a live change
}

// Tab / Window: the strip is closed with no animation and takes no room.
void MixerPlacementController::hideStripAtRest() {
    slideAnim_.stop(updater_);
    open_ = false;
    slide_.snapTo(0.0f);
    ownHandle_.setVisible(false);
    setVisible(false);
}

bool MixerPlacementController::revealOrToggle() {
    switch (placement_) {
    case Placement::Tab:
        return false; // caller keeps its own open/close-the-dock behaviour
    case Placement::OwnPanel:
        open_ = !open_;
        beginSlide();
        return true;
    case Placement::Window:
        bottomDock_.getMixerHost().setDetached(!bottomDock_.getMixerHost().isDetached());
        return true;
    }
    return false;
}

// A toggle only moves the intent and the slide's fraction; the strip's geometry comes out of the
// fraction in MainComponent::resized() (getCarveHeight()), same as the bottom dock.
void MixerPlacementController::beginSlide() {
    // Opening: visible BEFORE the first frame. Closing: stays visible for the whole slide and is
    // hidden only in finishSlide(), so "close" is an animation and not a vanish.
    if (open_)
        setVisible(true);

    // No VBlank reaches an off-screen component, so an off-screen toggle lands NOW. The controller
    // may itself be hidden (a closed strip), so ask the PARENT whether the window is showing.
    const bool canAnimate =
        forceAnimateForTest_ || (getParentComponent() != nullptr && getParentComponent()->isShowing());
    if (!slide_.retarget(open_ ? 1.0f : 0.0f, canAnimate)) {
        finishSlide();
        return;
    }

    // Frame 0 at the fraction's current value, before the first VBlank paints stale bounds.
    if (onLayoutNeeded)
        onLayoutNeeded();
    slideAnim_.start(
        updater_, kSlideMs, easeInOutCubic, [this](float t) { applySlideFrame(t); }, [this] { finishSlide(); });
}

// The per-frame body: advance the fraction, then let MainComponent re-lay-out (which is the single
// geometry authority -- no repaint() here, moving the bounds already invalidates what changed).
void MixerPlacementController::applySlideFrame(float t) {
    slide_.applyTweenAt(t);
    if (onLayoutNeeded)
        onLayoutNeeded();
}

void MixerPlacementController::finishSlide() {
    slideAnim_.stop(updater_); // time-bounded: nothing stays registered with the VBlank updater
    slide_.finish();           // pin the exact end value; the last frame need not be t == 1
    if (!open_)
        setVisible(false); // hidden only once the slide is done, BEFORE the final carve
    if (onLayoutNeeded)
        onLayoutNeeded();
}

void MixerPlacementController::resized() {
    ownHandle_.setBounds(0, 0, getWidth(), PanelResizeHandle::kHeight);
    if (placement_ != Placement::OwnPanel)
        return;
    // The host starts below the handle so a grab never lands on its own header/buttons.
    bottomDock_.getMixerHost().setBounds(getLocalBounds().withTrimmedTop(PanelResizeHandle::kHeight));
}

} // namespace synth::ui
