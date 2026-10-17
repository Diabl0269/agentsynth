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
// the Tab placement, where MixerPlacementController owns its host. Nothing in the app calls this with a
// view other than the canvas yet (MainComponent::setMainViewForTest does).
bool BottomDockComponent::setMainView(Tab view) {
    if (const auto* host = hostForTab(view); host != nullptr && host->isDetached())
        return false;
    if (view == Tab::Mixer && !mixerTabEnabled_)
        return false;
    const Tab previous = tabModel_.getMainView();
    if (!tabModel_.setMainView(view))
        return false;
    persistTabOrder();
    persistMainView();
    placeMainViewHost();
    if (activeTab_ == view)
        setActiveTab(previous);
    else
        applyTabVisibility();
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

// Writes the stored value, which can differ from the effective one (BottomDockTabModel::load).
void BottomDockComponent::persistMainView() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kMainViewKey,
                                                BottomDockTabModel::keyFor(tabModel_.getStoredMainView()));
    appProperties_->getUserSettings()->saveIfNeeded();
}

} // namespace synth::ui
