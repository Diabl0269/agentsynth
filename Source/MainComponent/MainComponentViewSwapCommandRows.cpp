// MainComponentViewSwapCommandRows.cpp -- the "show this view in the main area" rows of the MainComponent command
// table (MainComponentCommandTable.cpp appends them last). Each is the swap icon's action on a key.
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
    return {
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
