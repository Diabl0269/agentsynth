// Concern: turning a track's automatable parameters into the add-automation picker's items and back.
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"

#include <map>

namespace synth::ui {

// ModMatrixPicker opens a header whenever the category changes, so a module's rows must be adjacent: the
// parameters are regrouped by module title, modules in order of first appearance and each module's own
// order kept.
AddAutomationChoices collectAddAutomationChoices(TrackHeaderHost& host, const synth::TimelineDoc& doc,
                                                 synth::TrackId track) {
    std::vector<TrackHeaderHost::AutomatableParameter> offered;
    for (auto& parameter : host.getAutomatableParameters(track))
        if (parameter.nodeUuid.isEmpty() || doc.getLaneForParam(parameter.nodeUuid, parameter.paramId) == nullptr)
            offered.push_back(std::move(parameter));

    std::vector<juce::String> titles;
    std::map<juce::String, std::vector<size_t>> byTitle;
    for (size_t i = 0; i < offered.size(); ++i) {
        const auto& title = offered[i].moduleTitle;
        if (byTitle.find(title) == byTitle.end())
            titles.push_back(title);
        byTitle[title].push_back(i);
    }

    AddAutomationChoices choices;
    for (const auto& title : titles) {
        for (const auto index : byTitle[title]) {
            auto& parameter = offered[index];
            const auto name = parameter.parameterName.isNotEmpty() ? parameter.parameterName : parameter.paramId;
            choices.parameters.push_back(parameter);
            choices.items.push_back({(int)choices.parameters.size(), title, name, title});
        }
    }
    return choices;
}

std::unique_ptr<ModMatrixPicker>
buildAddAutomationPicker(const AddAutomationChoices& choices, const juce::String& trackName,
                         std::function<void(const TrackHeaderHost::AutomatableParameter&)> onPick) {
    auto parameters = choices.parameters;
    auto picker = std::make_unique<ModMatrixPicker>("parameter", choices.items, 0,
                                                    [parameters, onPick = std::move(onPick)](int id) {
                                                        if (id >= 1 && id <= (int)parameters.size() && onPick)
                                                            onPick(parameters[(size_t)(id - 1)]);
                                                    });
    picker->setAccessibleNames("Add automation to " + trackName, "Search " + trackName + " parameters");
    return picker;
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModMatrixPicker>)>& laneParameterPickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
