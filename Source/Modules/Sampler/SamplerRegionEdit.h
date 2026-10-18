#pragma once

// The pure rules behind the Sampler's instrument map window: how recordings group into velocity layers, where the
// arrow keys go, and the edits a drop makes. No audio and no UI, so every rule is testable on its own.

#include "Modules/Sampler/SamplerInstrument.h"
#include <vector>

namespace synth::sampler {

/** One row of the map: the recordings sharing a velocity range (and attack or release trigger). */
struct MapLayer {
    int loVel = 1, hiVel = 127;
    bool release = false;
    std::vector<int> regions; // indices into the spec list, in key order
};

/** Rows, loud layer first and release-sound rows last. */
std::vector<MapLayer> mapLayers(const std::vector<RegionSpec>& specs);

/** The row's label: "Loud" and "Soft" for two attack layers (`attackRow` 0 is the loud one), "All velocities" for one,
    "Velocity 20 to 40" otherwise, "Release sounds" for a release row. */
juce::String layerName(const MapLayer& layer, int attackLayerCount, int attackRow);

enum class MapMove { Left, Right, Up, Down };

/** The region the arrow key `move` selects from `current`: the neighbour in the row, or the one in the next row that
    covers (or lies nearest to) the current recording's root key. `current` itself when there is nowhere to go. */
int moveSelection(const std::vector<RegionSpec>& specs, const std::vector<MapLayer>& layers, int current, MapMove move);

/** Region `index` plays `samplePath`; its loop points (frames of the old file) are cleared. */
std::vector<RegionSpec> replaceRecording(std::vector<RegionSpec> specs, int index, const juce::String& samplePath);

/** Adds a recording on `key` (a note in the file's name sets its root instead) over the velocity range. */
std::vector<RegionSpec> addRecording(std::vector<RegionSpec> specs, const juce::String& samplePath, int key, int loVel,
                                     int hiVel);

/** Without region `index`; unchanged when it would leave nothing. */
std::vector<RegionSpec> removeRegion(std::vector<RegionSpec> specs, int index);

/** Note name in the app's convention (C3 = 60). */
juce::String keyName(int note);

/** "<file>, keys C3 to E3, root D3, velocity 1 to 63" for the screen reader and tooltips. */
juce::String describeRegion(const RegionSpec& region);

} // namespace synth::sampler
