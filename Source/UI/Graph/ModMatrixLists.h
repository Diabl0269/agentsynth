#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace synth::ui {

/** What the Mod Matrix's pickers list, built once for all its rows and only when something asks: a row's closed box
 *  needs one label, not the whole list. Message thread only. */
class ModMatrixLists {
public:
    explicit ModMatrixLists(juce::AudioProcessorGraph& graph)
        : graph_(graph) {}

    /** The next read rebuilds. Call when a module was added, removed or renamed, or the wording changes. */
    void invalidate();
    /** Flat wording ("Filter: Cutoff", no categories) or grouped ("Filter - Cutoff" under its category). */
    void setFlat(bool flat);

    const std::vector<ModMatrixPicker::Item>& sources();
    const std::vector<ModMatrixPicker::Item>& destinations();
    /** The closed box's text for a listed id; empty when the graph has no such row. */
    juce::String sourceLabel(int id);
    juce::String destinationLabel(int id);

private:
    struct List {
        bool built = false;
        std::vector<ModMatrixPicker::Item> items;
        std::unordered_map<int, juce::String> labels;
        std::unordered_set<int> missing;
    };
    void buildSources();
    void buildDestinations();
    void index(List& list);
    juce::String label(List& list, int id, void (ModMatrixLists::*build)());

    juce::AudioProcessorGraph& graph_;
    bool flat_ = false;
    List sources_;
    List destinations_;
};

} // namespace synth::ui
