// MainComponentViewSwapCommandRows.cpp -- the "show this view in the main area" rows of the MainComponent command
// table (MainComponentCommandTable.cpp appends them last): the positional Cmd+N / Opt+Cmd+N / Cmd+0 keys, then each
// view's own swap action.
#include "MainComponent.h"

std::vector<MainComponent::CommandSpec> MainComponent::buildViewSwapCommandRows() {
    using Tab = synth::ui::BottomDockComponent::Tab;
    // A refused swap (the view is already in the main area, open in its own window, or the Mixer outside the Tab
    // placement) changes nothing, and the key is still consumed.
    const auto swapTo = [](Tab view) {
        return [view](MainComponent& m) {
            m.bottomDock.swapTabToMain(view);
            return true;
        };
    };
    const auto showAt = [](int position) {
        return [position](MainComponent& m) {
            m.showDockTabAtPosition(position);
            return true;
        };
    };
    const auto swapAt = [](int position) {
        return [position](MainComponent& m) {
            m.swapDockTabAtPositionToMain(position);
            return true;
        };
    };
    return {
        {AppCommands::showDockTab1,
         "Show Tab 1",
         "Show the first bottom-panel tab, in the order the tabs are in now",
         "View",
         "showDockTab1",
         {},
         showAt(1)},
        {AppCommands::showDockTab2,
         "Show Tab 2",
         "Show the second bottom-panel tab, in the order the tabs are in now",
         "View",
         "showDockTab2",
         {},
         showAt(2)},
        {AppCommands::showDockTab3,
         "Show Tab 3",
         "Show the third bottom-panel tab, in the order the tabs are in now",
         "View",
         "showDockTab3",
         {},
         showAt(3)},
        {AppCommands::swapDockTab1ToMain,
         "Show Tab 1 in the Main Area",
         "Swap the first bottom-panel tab into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapDockTab1ToMain",
         {},
         swapAt(1)},
        {AppCommands::swapDockTab2ToMain,
         "Show Tab 2 in the Main Area",
         "Swap the second bottom-panel tab into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapDockTab2ToMain",
         {},
         swapAt(2)},
        {AppCommands::swapDockTab3ToMain,
         "Show Tab 3 in the Main Area",
         "Swap the third bottom-panel tab into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapDockTab3ToMain",
         {},
         swapAt(3)},
        {AppCommands::focusMainView,
         "Focus Main View",
         "Bring the app window forward and focus the main view",
         "View",
         "focusMainView",
         {},
         [](MainComponent& m) {
             m.focusMainView();
             return true;
         }},
        {AppCommands::swapTimelineToMain,
         "Show Timeline in the Main Area",
         "Swap the Timeline into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapTimelineToMain",
         {},
         swapTo(Tab::Timeline)},
        {AppCommands::swapMixerToMain,
         "Show Mixer in the Main Area",
         "Swap the Mixer into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapMixerToMain",
         {},
         swapTo(Tab::Mixer)},
        {AppCommands::swapMidiRemoteToMain,
         "Show Controllers in the Main Area",
         "Swap the Controllers into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapMidiRemoteToMain",
         {},
         swapTo(Tab::MidiRemote)},
        {AppCommands::swapCanvasToMain,
         "Show Canvas in the Main Area",
         "Swap the patch canvas back into the main area; the view that was there goes to the bottom panel",
         "View",
         "swapCanvasToMain",
         {},
         swapTo(Tab::Canvas)},
    };
}
