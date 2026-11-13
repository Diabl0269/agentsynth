#include "Modules/Sampler/SamplerRegionEdit.h"
#include "Modules/Sampler/SamplerNoteNames.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <tuple>

namespace synth::sampler {

namespace {

constexpr int kMiddleCOctave = 3;
constexpr int kFirstVelocity = 1;
constexpr int kLastVelocity = 127;

int distanceToKeys(const RegionSpec& r, int key) {
    if (key < r.loKey)
        return r.loKey - key;
    return key > r.hiKey ? key - r.hiKey : 0;
}

// The region of `layer` that covers `key` or lies nearest to it.
int nearestInLayer(const std::vector<RegionSpec>& specs, const MapLayer& layer, int key) {
    int best = layer.regions.front();
    int bestDistance = 1 << 20;
    for (const int index : layer.regions) {
        const int d = distanceToKeys(specs[(size_t)index], key);
        if (d < bestDistance) {
            bestDistance = d;
            best = index;
        }
    }
    return best;
}

} // namespace

std::vector<MapLayer> mapLayers(const std::vector<RegionSpec>& specs) {
    std::map<std::tuple<bool, int, int>, MapLayer> byRange; // release last, then loud first
    for (int i = 0; i < (int)specs.size(); ++i) {
        const RegionSpec& r = specs[(size_t)i];
        auto& layer = byRange[{r.releaseTrigger, -r.loVel, -r.hiVel}];
        layer.loVel = r.loVel;
        layer.hiVel = r.hiVel;
        layer.release = r.releaseTrigger;
        layer.regions.push_back(i);
    }
    std::vector<MapLayer> layers;
    for (auto& entry : byRange) {
        auto& regions = entry.second.regions;
        std::stable_sort(regions.begin(), regions.end(), [&specs](int a, int b) {
            return std::tie(specs[(size_t)a].loKey, specs[(size_t)a].hiKey) <
                   std::tie(specs[(size_t)b].loKey, specs[(size_t)b].hiKey);
        });
        layers.push_back(std::move(entry.second));
    }
    return layers;
}

juce::String layerName(const MapLayer& layer, int attackLayerCount, int attackRow) {
    if (layer.release)
        return "Release sounds";
    if (attackLayerCount <= 1)
        return "All velocities";
    if (attackLayerCount == 2)
        return attackRow == 0 ? "Loud" : "Soft";
    return "Velocity " + juce::String(layer.loVel) + " to " + juce::String(layer.hiVel);
}

int moveSelection(const std::vector<RegionSpec>& specs, const std::vector<MapLayer>& layers, int current,
                  MapMove move) {
    for (size_t row = 0; row < layers.size(); ++row) {
        const auto& regions = layers[row].regions;
        const auto at = std::find(regions.begin(), regions.end(), current);
        if (at == regions.end())
            continue;
        if (move == MapMove::Left)
            return at == regions.begin() ? current : *(at - 1);
        if (move == MapMove::Right)
            return at + 1 == regions.end() ? current : *(at + 1);
        const bool up = move == MapMove::Up;
        if (up ? row == 0 : row + 1 == layers.size())
            return current;
        const auto& target = layers[up ? row - 1 : row + 1];
        return nearestInLayer(specs, target, specs[(size_t)current].rootKey);
    }
    return current;
}

std::vector<RegionSpec> replaceRecording(std::vector<RegionSpec> specs, int index, const juce::String& samplePath) {
    if (index >= 0 && index < (int)specs.size()) {
        auto& r = specs[(size_t)index];
        r.samplePath = samplePath;
        r.loopStart = r.loopEnd = -1;
    }
    return specs;
}

std::vector<RegionSpec> addRecording(std::vector<RegionSpec> specs, const juce::String& samplePath, int key, int loVel,
                                     int hiVel) {
    RegionSpec r;
    r.samplePath = samplePath;
    r.loKey = r.hiKey = juce::jlimit(0, 127, key);
    const auto hints = hintsFromFileName(juce::File(samplePath).getFileNameWithoutExtension());
    r.rootKey = hints.note.value_or(r.loKey);
    r.loVel = juce::jlimit(kFirstVelocity, kLastVelocity, loVel);
    r.hiVel = juce::jlimit(r.loVel, kLastVelocity, hiVel);
    specs.push_back(r);
    return specs;
}

std::vector<RegionSpec> removeRegion(std::vector<RegionSpec> specs, int index) {
    if (specs.size() > 1 && index >= 0 && index < (int)specs.size())
        specs.erase(specs.begin() + index);
    return specs;
}

juce::String keyName(int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, kMiddleCOctave); }

juce::String describeRegion(const RegionSpec& r) {
    juce::String text = juce::File(r.samplePath).getFileName() + ", keys " + keyName(r.loKey) + " to " +
                        keyName(r.hiKey) + ", root " + keyName(r.rootKey) + ", velocity " + juce::String(r.loVel) +
                        " to " + juce::String(r.hiVel);
    return r.releaseTrigger ? text + ", release sound" : text;
}

} // namespace synth::sampler
