// MainComponentDockFocus.cpp -- where keyboard focus goes after the bottom-panel shortcuts (Cmd+J,
// Cmd+1/2/3/4) run: onto the dock's tab strip when the pane is showing, back to the view in the main area
// when the pane closes around the focus. The shortcuts' dispatch rows are in MainComponentCommandTable.cpp.
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPlacementController.h"

namespace {
constexpr const char* kDockTabsRegion = "dockTabs";
int mainWindowRaiseCount = 0;
} // namespace

int MainComponent::getMainWindowRaiseCountForTest() noexcept { return mainWindowRaiseCount; }

// Focus goes only to a strip that is on screen: the "dockTabs" region is open while the dock is showing
// with at least one docked tab, so a detached tab (its window is raised instead) or a hidden pane is
// left alone.
void MainComponent::focusDockTabStripIfShowing() {
    const auto* region = focusRegions_.findById(kDockTabsRegion);
    if (region != nullptr && region->isCurrentlyOpen())
        focusRegions_.focusRegionById(kDockTabsRegion);
}

// Cmd+J. Opening the pane puts focus on its tab strip so the arrow keys switch tab at once. Closing it
// would leave focus on a hidden component (the Canvas tab's canvas included), so a press that closes the
// pane around the focus hands focus to the view in the main area.
void MainComponent::toggleBottomPanelFromShortcut() {
    const auto* focused = getFocusedComponentForMenu();
    const bool focusWasInDock =
        focused != nullptr &&
        (focused == &bottomDock || bottomDock.isParentOf(focused) ||
         (bottomDock.isCanvasBodyShowing() && (focused == &graphEditor || graphEditor.isParentOf(focused))));
    // The button's own handler, run now: triggerClick() only posts the click, and the focus decision below
    // needs the new open/closed state.
    if (toggleBottomPanelButton.onClick)
        toggleBottomPanelButton.onClick();
    if (isBottomDockVisible)
        focusDockTabStripIfShowing();
    else if (focusWasInDock)
        focusMainViewIfOpen();
}

// Only a region that is open: focusing a closed one would run its `open` callback and bring the pane back.
void MainComponent::focusMainViewIfOpen() {
    const auto mainRegion = getMainViewFocusRegionId();
    const auto* region = focusRegions_.findById(mainRegion);
    if (region != nullptr && region->isCurrentlyOpen())
        focusRegions_.focusRegionById(mainRegion);
}

// Raises the app window over any detached panel window, taking keyboard focus with it.
void MainComponent::raiseMainWindow() {
    ++mainWindowRaiseCount;
    if (auto* top = getTopLevelComponent())
        top->toFront(true);
}

// Cmd+N. A tab that is detached raises its own window and focuses its panel, and the Mixer outside the Tab
// placement is revealed elsewhere (its own panel or window): neither has a docked strip to focus. With "show it in
// both places" on, the Mixer's second window is raised and the docked copy gets the focus. The view in the
// main area has no tab; the key focuses it there instead. Everything but a detached tab first raises the main
// window, because the key may have been pressed in a detached one.
void MainComponent::showBottomDockTabFromShortcut(synth::ui::BottomDockComponent::Tab tab) {
    using Tab = synth::ui::BottomDockComponent::Tab;
    const auto* host = bottomDock.hostForTab(tab);
    const bool detached = host != nullptr && host->isDetached();
    if (!detached)
        raiseMainWindow();
    if (bottomDock.getMainView() == tab) {
        focusMainViewIfOpen();
        return;
    }
    const bool mixerRevealedElsewhere =
        tab == Tab::Mixer && mixerPlacement_.getPlacement() != synth::ui::MixerPlacementController::Placement::Tab;
    showBottomDockTab(tab);
    if (tab == Tab::Mixer && !mixerRevealedElsewhere)
        bottomDock.raiseMixerMirror(); // before the focus moves: raising takes none
    if (!mixerRevealedElsewhere && !detached)
        focusDockTabStripIfShowing();
}

void MainComponent::showDockTabAtPosition(int position) {
    if (const auto tab = bottomDock.tabAtKeyPosition(position))
        showBottomDockTabFromShortcut(*tab);
}

// A tab in its own window cannot be swapped (setMainView refuses it), so only the others raise the main window.
void MainComponent::swapDockTabAtPositionToMain(int position) {
    const auto tab = bottomDock.tabAtKeyPosition(position);
    if (!tab)
        return;
    const auto* host = bottomDock.hostForTab(*tab);
    if (host == nullptr || !host->isDetached())
        raiseMainWindow();
    bottomDock.swapTabToMain(*tab);
}

void MainComponent::focusMainView() {
    raiseMainWindow();
    focusMainViewIfOpen();
}
