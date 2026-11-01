// Concern: turning the host's LFOs into the add-modulator picker's rows and a pick back into "new or this one".
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"

#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kNewLfoItemId = 1;
constexpr int kNewEnvelopeItemId = 1000000; // above any n + 2

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
// A "New ..." row from the source catalogue, or nothing when the catalogue has no such type.
void addNewRow(AddModulatorChoices& choices, int id, const juce::String& typeName, const juce::String& text,
               const juce::String& searchText) {
    for (const auto& type : newModuleSources()) {
        if (type.typeName != typeName)
            continue;
        ModMatrixPicker::Item item;
        item.id = id;
        item.category = "New";
        item.text = text;
        item.searchText = searchText;
        item.isNew = true;
        choices.items.push_back(item);
        choices.newSources.push_back({id, type.typeName, type.channel});
        return;
    }
}
} // namespace

namespace {
// The one row builder behind both pickers: "New LFO" / "New envelope" (the "New" group), and every LFO and envelope
// output under "LFOs" and "Envelopes", the current source (when there is one) ticked and disabled. Add modulator
// has no current source and lists "New" first; Change source lists it last.
AddModulatorChoices collectSourceChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                         const juce::String& currentSourceUuid, int currentSourceChannel,
                                         const juce::String& parameterName, bool newFirst) {
    AddModulatorChoices choices;
    choices.lfos = std::move(lfos);
    const auto addNewRows = [&choices] {
        addNewRow(choices, kNewLfoItemId, "LFO", "New LFO", "new add lfo");
        addNewRow(choices, kNewEnvelopeItemId, "ADSR", "New envelope", "new add env envelope adsr");
    };
    if (newFirst)
        addNewRows();
    for (const bool envelopes : {false, true}) {
        for (size_t i = 0; i < choices.lfos.size(); ++i) {
            const auto& source = choices.lfos[i];
            if (source.isEnvelope != envelopes)
                continue;
            auto item = itemFor(source, (int)i + 2, parameterName);
            item.category = envelopes ? "Envelopes" : "LFOs";
            if (currentSourceUuid.isNotEmpty() && source.uuid == currentSourceUuid &&
                source.sourceChannel == currentSourceChannel) {
                item.ticked = true;
                item.enabled = false;
            }
            choices.items.push_back(std::move(item));
        }
    }
    if (!newFirst)
        addNewRows();
    return choices;
}
} // namespace

AddModulatorChoices collectAddModulatorChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& parameterName) {
    return collectSourceChoices(std::move(lfos), {}, 0, parameterName, /*newFirst=*/true);
}

AddModulatorChoices collectChangeSourceChoices(std::vector<TrackHeaderHost::LfoChoice> lfos,
                                               const juce::String& currentSourceUuid, int currentSourceChannel,
                                               const juce::String& parameterName) {
    return collectSourceChoices(std::move(lfos), currentSourceUuid, currentSourceChannel, parameterName,
                                /*newFirst=*/false);
}

std::unique_ptr<ModMatrixPicker> buildAddModulatorPicker(const AddModulatorChoices& choices,
                                                         const juce::String& parameterName,
                                                         std::function<void(const AddModulatorPick&)> onPick) {
    auto lfos = choices.lfos;
    auto newSources = choices.newSources;
    auto picker = std::make_unique<ModMatrixPicker>(
        "modulator", choices.items, 0, [lfos, newSources, onPick = std::move(onPick)](int id) {
            if (!onPick)
                return;
            const auto fresh = std::find_if(newSources.begin(), newSources.end(),
                                            [id](const AddModulatorChoices::NewSource& s) { return s.id == id; });
            if (fresh != newSources.end())
                onPick({true, {}, fresh->channel, fresh->typeName});
            else if (id >= 2 && id - 2 < (int)lfos.size())
                onPick({false, lfos[(size_t)(id - 2)].uuid, lfos[(size_t)(id - 2)].sourceChannel, {}});
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
