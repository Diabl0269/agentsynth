// SamplerModuleInstrument.cpp -- the Sampler's instrument loading and editing entry points, kept out of the header.
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"

bool SamplerModule::loadInstrumentFromSfz(const juce::File& sfz) {
    return loadInstrument(synth::sampler::InstrumentSource::fromSfz(sfz));
}

bool SamplerModule::loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId) {
    return loadInstrumentFromPack(packId, instrumentId, 0.0f);
}

bool SamplerModule::loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId, float gainDb) {
    auto source = synth::sampler::InstrumentSource::fromPack(packId, instrumentId);
    source.gainDb = gainDb;
    return loadInstrument(source);
}

bool SamplerModule::loadInstrumentFromFiles(const juce::StringArray& absolutePaths) {
    return !absolutePaths.isEmpty() && loadInstrument(synth::sampler::InstrumentSource::fromFiles(absolutePaths));
}

const std::vector<synth::sampler::RegionSpec>& SamplerModule::getInstrumentRegions() const {
    return instrumentSlot.regions();
}

bool SamplerModule::editInstrumentRegions(const std::vector<synth::sampler::RegionSpec>& specs) {
    return instrumentSlot.replaceRegions(specs);
}

synth::sampler::InstrumentSource::Kind SamplerModule::getInstrumentKind() const { return instrumentSlot.loadedKind(); }

bool SamplerModule::isDrumKitInstrument() const {
    const auto& source = instrumentSlot.sourceToSave();
    if (source.kind != synth::sampler::InstrumentSource::Kind::Pack)
        return false;
    const auto found = synth::sampler::SoundPackLibrary::find(source.packId, source.instrumentId);
    return found.has_value() && found->category == "Drums";
}
