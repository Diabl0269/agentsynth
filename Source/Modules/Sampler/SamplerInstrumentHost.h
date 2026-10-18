#pragma once

// What a card needs from a module that plays a loaded Instrument (the Sampler, the Drum Kit): load one, clear it, and
// notice that it changed.

#include <juce_core/juce_core.h>

namespace synth::sampler {

class InstrumentHost {
public:
    virtual ~InstrumentHost() = default;

    /** Each load decodes now; false keeps the loaded instrument and getInstrumentMessages() says why. */
    virtual bool loadInstrumentFromSfz(const juce::File& sfz) = 0;
    virtual bool loadInstrumentFromPack(const juce::String& packId, const juce::String& instrumentId) = 0;
    virtual bool loadInstrumentFromFiles(const juce::StringArray& absolutePaths) = 0;
    virtual void clearInstrument() = 0;

    virtual juce::String getInstrumentName() const = 0;
    /** Bumped by every publish and by every load attempt, success or failure. */
    virtual int getInstrumentGeneration() const noexcept = 0;
    virtual int getInstrumentLoadSerial() const noexcept = 0;
    virtual juce::StringArray getInstrumentMessages() const = 0;
    /** True while a project open is still decoding the instrument. */
    virtual bool isInstrumentLoadPending() const noexcept = 0;
};

} // namespace synth::sampler
