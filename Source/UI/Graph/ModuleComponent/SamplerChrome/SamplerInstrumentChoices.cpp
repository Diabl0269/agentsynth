#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentChoices.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include <algorithm>

namespace synth::ui {

namespace {

const char* const kBuiltIn = "Built in";
const char* const kYourFiles = "Your files";
const char* const kGetMore = "Get more";

void addSynthDrums(InstrumentChoices& result, bool ticked) {
    ModMatrixPicker::Item row;
    row.id = (int)result.choices.size() + 1;
    row.category = kBuiltIn;
    row.text = "Synth drums";
    row.searchText = "drums synthesized electronic";
    row.ticked = ticked;
    result.items.push_back(row);
    result.choices.push_back({InstrumentChoice::Kind::SynthDrums, {}, {}});
}

void addBuiltIn(InstrumentChoices& result, const juce::String& currentPack, const juce::String& category) {
    auto instruments = synth::sampler::SoundPackLibrary::listInstruments();
    if (category.isNotEmpty())
        instruments.erase(std::remove_if(instruments.begin(), instruments.end(),
                                         [&category](const auto& i) { return !i.category.equalsIgnoreCase(category); }),
                          instruments.end());
    if (instruments.empty()) {
        ModMatrixPicker::Item note;
        note.category = kBuiltIn;
        note.text =
            category.isEmpty() ? "No instruments installed yet" : "No " + category.toLowerCase() + " installed yet";
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

// Always the last row, so the way to the download page stays one click away once the core strip is gone.
void addGetMore(InstrumentChoices& result) {
    ModMatrixPicker::Item row;
    row.id = (int)result.choices.size() + 1;
    row.category = kGetMore;
    row.text = "Get more sounds...";
    row.searchText = "download sound packs instruments";
    row.detail = "Settings > Sound packs";
    row.accessibleName = "Get more sounds, opens Settings, Sound packs";
    result.items.push_back(row);
    result.choices.push_back({InstrumentChoice::Kind::GetMore, {}, {}});
}

} // namespace

// A new "Downloaded" group joins here, between the built-in rows and the user's own files.
InstrumentChoices collectInstrumentChoices(const juce::String& currentPack, const InstrumentChoiceOptions& options) {
    InstrumentChoices result;
    if (options.offerSynthDrums)
        addSynthDrums(result, options.noKitLoaded);
    addBuiltIn(result, currentPack, options.category);
    addYourFiles(result);
    addGetMore(result);
    return result;
}

} // namespace synth::ui
