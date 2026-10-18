#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentChoices.h"
#include "Modules/Sampler/SoundPackLibrary.h"

namespace synth::ui {

namespace {

const char* const kBuiltIn = "Built in";
const char* const kYourFiles = "Your files";

void addBuiltIn(InstrumentChoices& result, const juce::String& currentPack) {
    const auto instruments = synth::sampler::SoundPackLibrary::listInstruments();
    if (instruments.empty()) {
        ModMatrixPicker::Item note;
        note.category = kBuiltIn;
        note.text = "No instruments installed yet";
        note.enabled = false;
        result.items.push_back(note);
        return;
    }
    for (const auto& instrument : instruments) {
        ModMatrixPicker::Item row;
        row.id = (int)result.choices.size() + 1;
        row.category = kBuiltIn;
        row.text = instrument.name;
        row.searchText = instrument.category;
        row.detail = instrument.packName;
        row.ticked = currentPack == instrument.packId + "/" + instrument.instrumentId;
        result.items.push_back(row);
        result.choices.push_back({InstrumentChoice::Kind::Pack, instrument.packId, instrument.instrumentId});
    }
}

void addYourFiles(InstrumentChoices& result) {
    const auto add = [&result](InstrumentChoice::Kind kind, const juce::String& text) {
        ModMatrixPicker::Item row;
        row.id = (int)result.choices.size() + 1;
        row.category = kYourFiles;
        row.text = text;
        result.items.push_back(row);
        result.choices.push_back({kind, {}, {}});
    };
    add(InstrumentChoice::Kind::SfzFile, "Load an instrument file (.sfz)...");
    add(InstrumentChoice::Kind::Recordings, "Load recordings...");
}

} // namespace

// New groups ("Downloaded", "Get more") join here, between the built-in rows and the user's own files.
InstrumentChoices collectInstrumentChoices(const juce::String& currentPack) {
    InstrumentChoices result;
    addBuiltIn(result, currentPack);
    addYourFiles(result);
    return result;
}

} // namespace synth::ui
