// Concern: FRO278's selection-stepping command rows (select next/previous module on the graph canvas,
// next/previous track on the timeline). Split from MainComponentCommandTable.cpp to stay under the
// file-size cap; assembled by commandTable() there. Two explicit pairs instead of one pair routed by
// resolveEditSurface(): moving track focus itself changes what that reports, so a routed pair would
// flip surfaces between two presses of the same pad.
#include "MainComponent.h"

namespace {

bool stepModule(MainComponent& m, int direction) {
    if (m.getGraphEditor().selectAdjacentModule(direction))
        return true;
    m.getStatusBar().showMessage("No modules to select");
    return false;
}

bool stepTrack(MainComponent& m, int direction) {
    if (m.getTimelinePanel().selectAdjacentTrack(direction))
        return true;
    m.getStatusBar().showMessage("No tracks to select");
    return false;
}

} // namespace

std::vector<MainComponent::CommandSpec> MainComponent::buildSelectionStepCommandRows() {
    const auto timelineOpen = [](const MainComponent& m) { return m.isTimelineVisible; };
    return {
        {AppCommands::selectNextModule,
         "Select Next Module",
         "Select the next module on the graph canvas, left to right",
         "Edit",
         "selectNextModule",
         {},
         [](MainComponent& m) { return stepModule(m, 1); }},
        {AppCommands::selectPreviousModule,
         "Select Previous Module",
         "Select the previous module on the graph canvas, right to left",
         "Edit",
         "selectPreviousModule",
         {},
         [](MainComponent& m) { return stepModule(m, -1); }},
        {AppCommands::selectNextTrack, "Select Next Track", "Move timeline track selection down one track", "Edit",
         "selectNextTrack", timelineOpen, [](MainComponent& m) { return stepTrack(m, 1); }},
        {AppCommands::selectPreviousTrack, "Select Previous Track", "Move timeline track selection up one track",
         "Edit", "selectPreviousTrack", timelineOpen, [](MainComponent& m) { return stepTrack(m, -1); }},
    };
}
