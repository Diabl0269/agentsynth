// Concern: turning a track's automatable parameters into the add-automation picker's items and back.
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"

#include <algorithm>
#include <map>

namespace synth::ui {

// ModMatrixPicker opens a header whenever the category changes, so a module's rows must be adjacent: the
// parameters are regrouped by module (title and placement), the track's own modules first and the ones outside it
// after, each set in order of first appearance and each module's own order kept. A header says where the module
// lives ("Oscillator 2 · in Track 1", "Oscillator 1 · shared") because the starter patch's lookalike module would
// otherwise read as the track's own, and a lane on it stays behind when the track is duplicated.
AddAutomationChoices collectAddAutomationChoices(TrackHeaderHost& host, const synth::TimelineDoc& doc,
                                                 synth::TrackId track) {
    using Placement = TrackHeaderHost::AutomatableParameter::Placement;
    std::vector<TrackHeaderHost::AutomatableParameter> offered;
    for (auto& parameter : host.getAutomatableParameters(track))
        if (parameter.nodeUuid.isEmpty() || doc.getLaneForParam(parameter.nodeUuid, parameter.paramId) == nullptr)
            offered.push_back(std::move(parameter));

    const auto* trackInfo = doc.getTrack(track);
    const juce::String trackName = trackInfo != nullptr ? trackInfo->name : juce::String();
    const auto dot = juce::String::fromUTF8(" \xC2\xB7 ");
    const auto placementLabel = [&](Placement placement) {
        switch (placement) {
        case Placement::InTrack:
            return dot + "in " + (trackName.isNotEmpty() ? trackName : juce::String("this track"));
        case Placement::Shared:
            return dot + "shared";
        case Placement::Unspecified:
            break;
        }
        return juce::String();
    };

    using Key = std::pair<juce::String, Placement>;
    std::vector<Key> keys;
    std::map<Key, std::vector<size_t>> byKey;
    for (size_t i = 0; i < offered.size(); ++i) {
        const Key key{offered[i].moduleTitle, offered[i].placement};
        if (byKey.find(key) == byKey.end())
            keys.push_back(key);
        byKey[key].push_back(i);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        return (a.second == Placement::Shared) < (b.second == Placement::Shared);
    });

    AddAutomationChoices choices;
    for (const auto& key : keys) {
        const auto& title = key.first;
        const auto heading = title + placementLabel(key.second);
        for (const auto index : byKey[key]) {
            auto& parameter = offered[index];
            const auto name = parameter.parameterName.isNotEmpty() ? parameter.parameterName : parameter.paramId;
            choices.parameters.push_back(parameter);
            ModMatrixPicker::Item item{(int)choices.parameters.size(), heading, name, title};
            if (key.second != Placement::Unspecified)
                item.accessibleName = name + ", " + heading;
            choices.items.push_back(std::move(item));
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
