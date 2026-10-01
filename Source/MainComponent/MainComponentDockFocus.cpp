// MainComponentDockFocus.cpp -- where keyboard focus goes after the bottom-panel shortcuts (Cmd+T,
// Cmd+1/2/3) run: onto the dock's tab strip when the pane is showing, back to the canvas when the pane
// closes around the focus. The shortcuts' dispatch rows are in MainComponentCommandTable.cpp.
#include "MainComponent.h"
#include "UI/Mixer/MixerPlacementController.h"

namespace {
constexpr const char* kDockTabsRegion = "dockTabs";
constexpr const char* kCanvasRegion = "canvas";
} // namespace

// Focus goes only to a strip that is on screen: the "dockTabs" region is open while the dock is showing
// with at least one docked tab, so a detached tab (its window is raised instead) or a hidden pane is
// left alone.
void MainComponent::focusDockTabStripIfShowing() {
    const auto* region = focusRegions_.findById(kDockTabsRegion);
    if (region != nullptr && region->isCurrentlyOpen())
        focusRegions_.focusRegionById(kDockTabsRegion);
}

// Cmd+T. Opening the pane puts focus on its tab strip so the arrow keys switch tab at once. Closing it
// would leave focus on a hidden component, so a press that closes the pane around the focus hands focus
// to the canvas.
void MainComponent::toggleBottomPanelFromShortcut() {
    const auto* focused = getFocusedComponentForMenu();
    const bool focusWasInDock = focused != nullptr && (focused == &bottomDock || bottomDock.isParentOf(focused));
    // The button's own handler, run now: triggerClick() only posts the click, and the focus decision below
    // needs the new open/closed state.
    if (toggleBottomPanelButton.onClick)
        toggleBottomPanelButton.onClick();
    if (isBottomDockVisible)
        focusDockTabStripIfShowing();
    else if (focusWasInDock)
        focusRegions_.focusRegionById(kCanvasRegion);
}

// Cmd+1/2/3. A tab that is detached raises its own window, and the Mixer outside the Tab placement is
// revealed elsewhere (its own panel or window): neither has a docked strip to focus.
void MainComponent::showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab tab) {
    using Tab = synth::ui::BottomDockComponent::Tab;
    const bool mixerRevealedElsewhere =
        tab == Tab::Mixer && mixerPlacement_.getPlacement() != synth::ui::MixerPlacementController::Placement::Tab;
    const auto& host = tab == Tab::Timeline ? bottomDock.getTimelineHost()
                       : tab == Tab::Mixer  ? bottomDock.getMixerHost()
                                            : bottomDock.getMidiRemoteHost();
    const bool detached = host.isDetached();
    showBottomDockTab(tab);
    if (!mixerRevealedElsewhere && !detached)
        focusDockTabStripIfShowing();
}
