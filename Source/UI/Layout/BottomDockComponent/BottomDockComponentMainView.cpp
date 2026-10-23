// Concern: BottomDockComponent main view -- which view fills the main area, the holder its host moves
// into, and the canvas body the dock leaves open.
#include "BottomDockComponent.h"

#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <utility>

namespace synth::ui {

// The view that comes down takes over as the active tab when the dock was showing the one that went
// up, so the dock keeps showing something in the same place; otherwise the active tab stays (or
// falls back through applyTabVisibility() if it is no longer offered). A view open in its own window
// is refused: its panel lives in that window, not in the host that would move. So is the Mixer outside
// the Tab placement, where MixerPlacementController owns its host.
bool BottomDockComponent::setMainView(Tab view) { return changeMainView(view, false); }

// The swap icon's click. The view that went up leaves its slot to the one that came down, and that one is
// what the dock shows next, whichever tab was active before.
bool BottomDockComponent::swapTabToMain(Tab view) {
    if (!changeMainView(view, true))
        return false;
    const auto text = mainViewAnnouncement(view);
    if (announceHook_)
        announceHook_(text);
    else
        juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
    return true;
}

// The active tab is settled before onMainViewChanged fires, so the owner's layout (and the swap motion that starts
// from it) sees the final state of both places. onMainViewWillChange fires first, with nothing moved.
bool BottomDockComponent::changeMainView(Tab view, bool showLeavingView) {
    if (const auto* host = hostForTab(view); host != nullptr && host->isDetached())
        return false;
    if (view == Tab::Mixer && !mixerTabEnabled_)
        return false;
    if (tabModel_.getStoredMainView() == view)
        return false;
    // Choosing the Canvas while a suspended view holds the stored choice changes the setting, not the main area.
    const Tab previous = tabModel_.getMainView();
    if (previous != view && onMainViewWillChange)
        onMainViewWillChange();
    if (!tabModel_.setMainView(view))
        return false;
    persistTabOrder();
    persistMainView();
    placeMainViewHost();
    if ((activeTab_ == view || showLeavingView) && previous != view)
        setActiveTab(previous);
    else
        applyTabVisibility();
    if (onMainViewChanged)
        onMainViewChanged();
    return true;
}

void BottomDockComponent::setResizeHandleActive(bool active) { resizeHandle_.setInterceptsMouseClicks(active, false); }

// The digit keys follow the tab order: position N is the N-th view of the user's order that is not in the main
// area, so dragging a tab to a new place re-keys it. The main-area view has no number (Cmd+0 focuses it), and a
// tab in its own window keeps its slot, so its key still raises that window.
std::optional<BottomDockComponent::Tab> BottomDockComponent::tabAtKeyPosition(int position) const noexcept {
    int seen = 0;
    for (Tab t : tabModel_.getOrder()) {
        if (tabModel_.isInMain(t))
            continue;
        if (++seen == position)
            return t;
    }
    return std::nullopt;
}

int BottomDockComponent::keyPositionOf(Tab view) const noexcept {
    for (int position = 1; position <= kKeyPositions; ++position)
        if (tabAtKeyPosition(position) == view)
            return position;
    return 0;
}

const char* BottomDockComponent::showActionIdForPosition(int position) noexcept {
    static constexpr const char* ids[] = {"showDockTab1", "showDockTab2", "showDockTab3"};
    return position >= 1 && position <= kKeyPositions ? ids[position - 1] : nullptr;
}

const char* BottomDockComponent::swapActionIdForPosition(int position) noexcept {
    static constexpr const char* ids[] = {"swapDockTab1ToMain", "swapDockTab2ToMain", "swapDockTab3ToMain"};
    return position >= 1 && position <= kKeyPositions ? ids[position - 1] : nullptr;
}

juce::String BottomDockComponent::showActionIdForTab(Tab view) const {
    const char* id = showActionIdForPosition(keyPositionOf(view));
    return id != nullptr ? id : "";
}

juce::String BottomDockComponent::swapActionIdForTab(Tab view) const {
    const char* id = swapActionIdForPosition(keyPositionOf(view));
    return id != nullptr ? id : "";
}

juce::String BottomDockComponent::shortcutTextFor(const juce::String& actionId) const {
    return shortcutTextSource_ && actionId.isNotEmpty() ? shortcutTextSource_(actionId) : juce::String();
}

juce::String BottomDockComponent::mainViewAnnouncement(Tab view) {
    return juce::String(nameForTab(view)) + " in the main area";
}

// A stored main view whose panel is in its own window, or is the Mixer outside the Tab placement, cannot fill the
// main area: the Canvas does for the session, and the stored view (and its setting) comes back with its panel.
bool BottomDockComponent::refreshMainViewAvailability() {
    const Tab stored = tabModel_.getStoredMainView();
    const auto* host = hostForTab(stored);
    const bool available =
        stored == Tab::Canvas || (host != nullptr && !host->isDetached() && (stored != Tab::Mixer || mixerTabEnabled_));
    if (tabModel_.isMainViewSuspended() == !available)
        return false;
    tabModel_.setMainViewSuspended(!available);
    placeMainViewHost();
    return true;
}

void BottomDockComponent::setMainAreaHolder(juce::Component* holder) {
    mainAreaHolder_ = holder;
    placeMainViewHost();
    applyTabVisibility(false);
}

// A JUCE child is clipped to its parent, so the dock cannot show a view over the main area itself: the
// view's whole host moves into MainComponent's holder (the Own-panel placement's move, generalised), and
// with it the panel's side pane and detach state. The Mixer goes only in the Tab placement; in Own panel
// or Window its host belongs to MixerPlacementController.
DetachablePanelHost* BottomDockComponent::mainAreaHostFor(Tab view) noexcept {
    if (mainAreaHolder_ == nullptr || (view == Tab::Mixer && !mixerTabEnabled_))
        return nullptr;
    return hostForTab(view);
}

// The leaving host lands hidden, like any tab that is not active; applyTabVisibility() (which every caller
// runs next) fades it in if it is. The arriving host lands shown, whatever fade it was in the middle of.
void BottomDockComponent::placeMainViewHost() {
    auto* wanted = mainAreaHostFor(getMainView());
    if (wanted != nullptr && wanted->isDetached())
        wanted = nullptr;
    if (wanted == hostInMain_)
        return;
    if (auto* leaving = std::exchange(hostInMain_, nullptr)) {
        addAndMakeVisible(*leaving);
        fadeForHost(*leaving).snapTo(false);
        resizeHandle_.toFront(false);
    }
    if (wanted == nullptr)
        return;
    hostInMain_ = wanted;
    mainAreaHolder_->addAndMakeVisible(*wanted);
    fadeForHost(*wanted).snapTo(true);
    // Catches up on whatever changed while it was a hidden tab, as a switch to its tab does (applyTabVisibility).
    if (wanted == &midiRemoteHost_)
        midiRemotePanel_.rebuildFromProfiles();
    else if (wanted == &mixerHost_)
        mixer_->rebuildWhenShown();
}

FadeVisibility& BottomDockComponent::fadeForHost(const DetachablePanelHost& host) noexcept {
    if (&host == &mixerHost_)
        return mixerFade_;
    return &host == &midiRemoteHost_ ? midiRemoteFade_ : timelineFade_;
}

juce::Rectangle<int> BottomDockComponent::getCanvasBodyBounds() const {
    if (!isCanvasBodyShowing())
        return {};
    return getLocalBounds().withTrimmedTop(kTabStripHeight);
}

// The canvas shows through the dock's body (it is a sibling underneath, not a child), so a click there
// belongs to the canvas.
bool BottomDockComponent::hitTest(int x, int y) { return !getCanvasBodyBounds().contains(x, y); }

void BottomDockComponent::persistMainView() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kMainViewKey,
                                                BottomDockTabModel::keyFor(tabModel_.getStoredMainView()));
    appProperties_->getUserSettings()->saveIfNeeded();
}

} // namespace synth::ui
