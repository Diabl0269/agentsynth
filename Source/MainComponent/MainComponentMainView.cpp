// MainComponentMainView.cpp -- the view in the main area when it is not the canvas: the holder the bottom dock
// moves that view's panel host into, its place in the layout, and the gates that must count a view there as
// showing even while the dock is closed. Which view is in main lives in BottomDockComponent (BottomDockTabModel).
#include "MainComponent.h"

using synth::ui::BottomDockComponent;

// The holder takes the rect the canvas has as the main view; the canvas itself goes to the dock's Canvas tab
// (layoutCanvas). Hidden while it holds nothing (the canvas, or a view the main area cannot host yet), so an
// empty holder never sits over the canvas.
void MainComponent::layoutMainView(juce::Rectangle<int> mainArea) {
    mainAreaBounds_ = mainArea;
    layoutCanvas(mainArea);
    const bool holding = mainViewHolder_.getNumChildComponents() > 0;
    mainViewHolder_.setVisible(holding);
    if (holding)
        mainViewHolder_.setBounds(mainArea);
}

// A swap moves a host into or out of the holder, and the Tab order follows the layout. The motion starts last: the
// layout it plays over is final by then.
void MainComponent::handleMainViewChanged() {
    rebuildFocusRegions();
    resized();
    playMainViewSwapMotion();
}

// isBottomDockVisible alone used to answer this; a view in the main area is on screen with the dock closed.
bool MainComponent::isViewOnScreen(BottomDockComponent::Tab view) const {
    return bottomDock.isInMainArea(view) || (isBottomDockVisible && bottomDock.getActiveTab() == view);
}

// The Timeline commands (snap grid, track step, zoom, the locator keys) were gated on the dock being open; the
// Timeline in the main area takes them with the dock closed.
bool MainComponent::isTimelineReachable() const {
    return isBottomDockVisible || bottomDock.isInMainArea(BottomDockComponent::Tab::Timeline);
}

void MainComponent::ensureDockOpenFor(BottomDockComponent::Tab view) {
    if (!bottomDock.isInMainArea(view))
        ensureBottomDockOpen();
}
