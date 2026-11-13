// MainComponentMainViewMotion.cpp -- the motion of a main-view swap: the main area pictured before the dock swaps its
// views, the motion played over the finished layout. With the dock fully open on screen the leaving view shrinks into
// the panel and the arriving one grows out of it; with the dock closed or sliding (Opt+Cmd+1 with the panel hidden)
// there is no panel body to move through, so the main area cross-fades instead. Off screen it lands at once.
#include "MainComponent.h"

namespace {
bool dockFullyOpen(bool visible, float slideProgress) { return visible && slideProgress >= 1.0f; }
} // namespace

void MainComponent::beginMainViewSwapMotion() {
    if (welcomeScreen_ != nullptr && welcomeScreen_->isVisible()) {
        mainViewSwapMotion_.finishNow();
        mainViewSwapMotion_.discardPending();
        return;
    }
    const bool open = dockFullyOpen(isBottomDockVisible, timelineSlide_.getProgress());
    mainViewSwapMotion_.begin(mainAreaBounds_, open ? bottomDock.getPanelBodyBounds() + bottomDock.getPosition()
                                                    : juce::Rectangle<int>{});
}

void MainComponent::playMainViewSwapMotion() {
    const bool open = dockFullyOpen(isBottomDockVisible, timelineSlide_.getProgress());
    mainViewSwapMotion_.play(open ? bottomDock.getPanelBodyBounds() + bottomDock.getPosition()
                                  : juce::Rectangle<int>{});
}

// The dock reports a main-view change before it moves anything and after it has laid out; the motion in between holds
// the panel's resize strip still.
void MainComponent::wireMainViewHooks() {
    bottomDock.onMainViewWillChange = [this] { beginMainViewSwapMotion(); };
    bottomDock.onMainViewChanged = [this] { handleMainViewChanged(); };
    mainViewSwapMotion_.onRunningChanged = [this](bool running) { bottomDock.setResizeHandleActive(!running); };
}
