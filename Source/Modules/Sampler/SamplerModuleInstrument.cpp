// SamplerModuleInstrument.cpp -- the Sampler's instrument loading and editing entry points, kept out of the header.
#include "Modules/SamplerModule.h"

bool SamplerModule::loadInstrumentFromSfz(const juce::File& sfz) {
    return loadInstrument(synth::sampler::InstrumentSource::fromSfz(sfz));
}

bool SamplerModule::loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId) {
    return loadInstrument(synth::sampler::InstrumentSource::fromPack(packId, instrumentId));
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
