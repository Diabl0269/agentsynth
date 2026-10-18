// Concern: the command-table rows of the triplet and dotted Set Grid values, one per entry of snapNoteValueActions().
#include "MainComponent.h"
#include "ShortcutManager/SnapNoteValueActions.h"

std::vector<MainComponent::CommandSpec> MainComponent::buildSnapNoteValueCommandRows() {
    std::vector<CommandSpec> rows;
    for (const auto& action : snapNoteValueActions()) {
        const juce::CommandID command = action.command;
        rows.push_back({command, nullptr, "Set the timeline's snap grid", "Timeline", action.actionId,
                        [](const MainComponent& m) { return m.isTimelineReachable(); },
                        [command](MainComponent& m) { return m.applySnapCommand(command); }});
    }
    return rows;
}
