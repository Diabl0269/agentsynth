#include "Modules/Sampler/SamplerInstrument.h"
#include <cmath>
#include <map>
#include <set>

namespace synth::sampler {

namespace {

constexpr double kMaxSecondsPerSample = 60.0;
constexpr float kHalfPi = 1.57079632679f;

size_t bytesOf(const SampleData& s) {
    return (size_t)s.audio.getNumChannels() * (size_t)s.audio.getNumSamples() * sizeof(float);
}

// Same key, trigger kind and round-robin slot, whatever the velocity.
bool matchesSlot(const RegionSpec& r, int note, bool releaseTrigger, int roundRobinCounter) {
    if (note < r.loKey || note > r.hiKey || r.releaseTrigger != releaseTrigger)
        return false;
    return r.seqLength <= 1 || r.seqPosition == (roundRobinCounter % r.seqLength) + 1;
}

// True when another region of the same slot starts (or ends) right at the border.
bool hasLayerNeighbour(const Instrument& inst, int note, bool releaseTrigger, int roundRobinCounter, int neighbourLo,
                       int neighbourHi) {
    for (const auto& other : inst.regions) {
        if (!matchesSlot(other, note, releaseTrigger, roundRobinCounter))
            continue;
        if ((neighbourLo >= 0 && other.loVel == neighbourLo) || (neighbourHi >= 0 && other.hiVel == neighbourHi))
            return true;
    }
    return false;
}

// Gain of `r` at `velocity` given the equal-power band centred on each border that has a neighbouring layer.
float layerGain(const Instrument& inst, const RegionSpec& r, int note, int velocity, bool releaseTrigger,
                int roundRobinCounter) {
    const float width = 2.0f * (float)kVelocityCrossfade;
    float gain = 1.0f;
    if (velocity > r.hiVel - kVelocityCrossfade) {
        const float edge = (float)r.hiVel + 0.5f;
        const bool banded = r.hiVel < 127 && velocity < r.hiVel + kVelocityCrossfade + 1 &&
                            hasLayerNeighbour(inst, note, releaseTrigger, roundRobinCounter, r.hiVel + 1, -1);
        if (banded)
            gain *= std::cos(((float)velocity - (edge - (float)kVelocityCrossfade)) / width * kHalfPi);
        else if (velocity > r.hiVel)
            return 0.0f;
    }
    if (velocity < r.loVel + kVelocityCrossfade) {
        const float edge = (float)r.loVel - 0.5f;
        const bool banded = r.loVel > 1 && velocity > r.loVel - kVelocityCrossfade - 1 &&
                            hasLayerNeighbour(inst, note, releaseTrigger, roundRobinCounter, -1, r.loVel - 1);
        if (banded)
            gain *= std::sin(((float)velocity - (edge - (float)kVelocityCrossfade)) / width * kHalfPi);
        else if (velocity < r.loVel)
            return 0.0f;
    }
    return gain;
}

} // namespace

size_t Instrument::memoryBytes() const {
    std::set<const SampleData*> seen;
    size_t total = 0;
    for (const auto& r : regions)
        if (r.sample != nullptr && seen.insert(r.sample.get()).second)
            total += bytesOf(*r.sample);
    return total;
}

Instrument::Ptr buildInstrument(const juce::String& name, const std::vector<RegionSpec>& specs,
                                juce::StringArray& errors, size_t memoryBudgetBytes, const Instrument* reuse) {
    std::map<juce::String, SampleData::Ptr> decoded;
    size_t total = 0;
    const auto reusable = [reuse](const juce::String& path) -> SampleData::Ptr {
        if (reuse != nullptr)
            for (const auto& r : reuse->regions)
                if (r.samplePath == path)
                    return r.sample;
        return nullptr;
    };
    Instrument::Ptr inst = new Instrument();
    inst->name = name;
    for (const auto& spec : specs) {
        auto it = decoded.find(spec.samplePath);
        if (it == decoded.end()) {
            SampleData::Ptr sample = reusable(spec.samplePath);
            if (sample == nullptr)
                sample = decodeSampleFile(juce::File(spec.samplePath), kMaxSecondsPerSample);
            if (sample == nullptr)
                errors.add("Could not read " + spec.samplePath);
            else
                total += bytesOf(*sample);
            if (total > memoryBudgetBytes) {
                errors.add("The samples need more than " + juce::String((juce::int64)(memoryBudgetBytes >> 20)) +
                           " MB of memory.");
                return nullptr;
            }
            it = decoded.emplace(spec.samplePath, sample).first;
        }
        if (it->second == nullptr)
            continue;
        Region region;
        static_cast<RegionSpec&>(region) = spec;
        region.sample = it->second;
        inst->regions.push_back(std::move(region));
    }
    if (inst->regions.empty()) {
        errors.add("No playable samples.");
        return nullptr;
    }
    return inst;
}

int selectRegions(const Instrument& inst, int note, int velocity, bool releaseTrigger, int roundRobinCounter,
                  std::array<RegionHit, kMaxHitsPerNote>& out) {
    int count = 0;
    for (int i = 0; i < (int)inst.regions.size() && count < kMaxHitsPerNote; ++i) {
        const RegionSpec& r = inst.regions[(size_t)i];
        if (!matchesSlot(r, note, releaseTrigger, roundRobinCounter))
            continue;
        if (velocity < r.loVel - kVelocityCrossfade || velocity > r.hiVel + kVelocityCrossfade)
            continue;
        const float gain = layerGain(inst, r, note, velocity, releaseTrigger, roundRobinCounter);
        if (gain > 1.0e-4f)
            out[(size_t)count++] = {i, gain};
    }
    return count;
}

} // namespace synth::sampler
