// Concern: turning the host's LFOs into the add-modulator picker's rows and a pick back into "new or this one".
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kNewLfoItemId = 1;

juce::String joinTargets(const std::vector<juce::String>& targets) {
    juce::StringArray names;
    for (const auto& target : targets)
        names.add(target);
    return names.joinIntoString(", ");
}

ModMatrixPicker::Item itemFor(const TrackHeaderHost::LfoChoice& lfo, int id, const juce::String& parameterName) {
    ModMatrixPicker::Item item;
    item.id = id;
    item.text = lfo.macroName.isNotEmpty()
                    ? lfo.name + juce::String::fromUTF8(" \xc2\xb7 inside macro ") + lfo.macroName
                    : lfo.name;
    const auto moves = joinTargets(lfo.targets);
    item.detail = lfo.movesThisParameter ? "already moves " + parameterName
                  : moves.isNotEmpty()   ? "moves " + moves
                                         : juce::String("not connected yet");
    item.searchText = lfo.name + " " + lfo.macroName + " " + moves;
    item.enabled = !lfo.movesThisParameter;
    return item;
}
} // namespace

AddModulatorChoices collectAddModulatorChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& parameterName) {
    AddModulatorChoices choices;
    choices.lfos = std::move(lfos);
    ModMatrixPicker::Item fresh;
    fresh.id = kNewLfoItemId;
    fresh.text = "New LFO";
    fresh.detail = "adds one beside " + parameterName;
    fresh.searchText = "new add modulator";
    choices.items.push_back(fresh);
    for (size_t i = 0; i < choices.lfos.size(); ++i)
        choices.items.push_back(itemFor(choices.lfos[i], (int)i + 2, parameterName));
    return choices;
}

AddModulatorChoices collectChangeSourceChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& currentSourceUuid,
                                               const juce::String& parameterName) {
    lfos.erase(std::remove_if(lfos.begin(), lfos.end(),
                              [&](const TrackHeaderHost::LfoChoice& lfo) { return lfo.uuid == currentSourceUuid; }),
               lfos.end());
    auto choices = collectAddModulatorChoices(std::move(lfos), parameterName);
    choices.items.erase(choices.items.begin()); // "New LFO"
    return choices;
}

std::unique_ptr<ModMatrixPicker> buildAddModulatorPicker(const AddModulatorChoices& choices,
                                                         const juce::String& parameterName,
                                                         std::function<void(const AddModulatorPick&)> onPick) {
    auto lfos = choices.lfos;
    auto picker =
        std::make_unique<ModMatrixPicker>("modulator", choices.items, 0, [lfos, onPick = std::move(onPick)](int id) {
            if (!onPick)
                return;
            if (id == kNewLfoItemId)
                onPick({true, {}});
            else if (id >= 2 && id - 2 < (int)lfos.size())
                onPick({false, lfos[(size_t)(id - 2)].uuid});
        });
    picker->setAccessibleNames("Add modulator to " + parameterName, "Search modulators for " + parameterName);
    return picker;
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModMatrixPicker>)>& addModulatorPickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}

std::function<void(std::unique_ptr<ModMatrixPicker>)>& changeSourcePickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
