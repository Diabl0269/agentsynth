// MainComponentMainViewMotion.cpp -- the motion of a main-view swap: the main area pictured before the dock swaps its
// views, the motion played over the finished layout. It runs only with the dock fully open on screen (the swap icon's
// and Opt+Cmd+1-4's usual case); a swap with the dock closed or sliding, or off screen, lands at once.
#include "MainComponent.h"

void MainComponent::beginMainViewSwapMotion() {
    if (!isBottomDockVisible || timelineSlide_.getProgress() < 1.0f ||
        (welcomeScreen_ != nullptr && welcomeScreen_->isVisible())) {
        mainViewSwapMotion_.finishNow();
        mainViewSwapMotion_.discardPending();
        return;
    }
    mainViewSwapMotion_.begin(mainAreaBounds_, bottomDock.getPanelBodyBounds() + bottomDock.getPosition());
}

void MainComponent::playMainViewSwapMotion() {
    if (!isBottomDockVisible || timelineSlide_.getProgress() < 1.0f) {
        mainViewSwapMotion_.discardPending();
        return;
    }
    mainViewSwapMotion_.play(bottomDock.getPanelBodyBounds() + bottomDock.getPosition());
}

// The dock reports a main-view change before it moves anything and after it has laid out; the motion in between holds
// the panel's resize strip still.
void MainComponent::wireMainViewHooks() {
    bottomDock.onMainViewWillChange = [this] { beginMainViewSwapMotion(); };
    bottomDock.onMainViewChanged = [this] { handleMainViewChanged(); };
    mainViewSwapMotion_.onRunningChanged = [this](bool running) { bottomDock.setResizeHandleActive(!running); };
}
