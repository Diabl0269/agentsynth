#pragma once

// A multi-sample instrument: recordings mapped over key and velocity ranges, and the audio-thread region lookup.

#include "Modules/Sampler/SamplerSampleData.h"
#include <array>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::sampler {

struct RegionSpec {
    enum class Loop { None, OneShot, Continuous, Sustain };

    juce::String samplePath;
    int loKey = 0, hiKey = 127, rootKey = 60, loVel = 1, hiVel = 127;
    float tuneCents = 0.0f, volumeDb = 0.0f;
    float keytrackCents = 100.0f; // cents of pitch per key from the root key; 0 plays every key as recorded
    Loop loop = Loop::None;
    juce::int64 loopStart = -1, loopEnd = -1; // frames; -1 = whole file
    int seqLength = 1, seqPosition = 1;       // round robin: plays when seqPosition == slot + 1
    bool releaseTrigger = false;              // plays on key release instead of key press
    float ampReleaseSeconds = -1.0f;          // -1 = use the module's Release knob
};

struct Region : RegionSpec {
    SampleData::Ptr sample;
};

struct Instrument : juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<Instrument>;
    juce::String name;
    std::vector<Region> regions;

    /** Float audio held by the distinct samples (a sample shared by regions counts once). */
    size_t memoryBytes() const;
};

inline constexpr size_t kDefaultMemoryBudget = 512u * 1024u * 1024u;
inline constexpr int kMaxHitsPerNote = 4;
inline constexpr int kVelocityCrossfade = 8;

/** Decodes every distinct sample once (60 s cap each), except those `reuse` already holds under the same path.
    Undecodable regions are dropped with a line in `errors`; null when nothing survives or the audio exceeds
    `memoryBudgetBytes`. Message thread. */
Instrument::Ptr buildInstrument(const juce::String& name, const std::vector<RegionSpec>& specs,
                                juce::StringArray& errors, size_t memoryBudgetBytes = kDefaultMemoryBudget,
                                const Instrument* reuse = nullptr);

struct RegionHit {
    int regionIndex = 0;
    float gain = 1.0f; // crossfade gain only; the region's volume is applied by the voice
};

/** Regions that sound for this press (or release) and their crossfade gains; returns the count written to `out`.
    `velocity` is 1..127. Allocation-free. */
int selectRegions(const Instrument& inst, int note, int velocity, bool releaseTrigger, int roundRobinCounter,
                  std::array<RegionHit, kMaxHitsPerNote>& out);

} // namespace synth::sampler
