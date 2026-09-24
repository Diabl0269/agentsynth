// Concern: FRO278's "select next/previous module" ordering -- which module a step lands on, kept
// free of GraphEditor so it is unit-testable against plain rectangles.
#pragma once

#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui {

struct StepModule {
    juce::AudioProcessorGraph::NodeID id;
    juce::Rectangle<float> bounds; // canvas coordinates
};

/** The module a step of `direction` (+1 next, -1 previous) from the current selection lands on.
 *
 *  Modules are ordered left to right by centre x (top to bottom on a tie, then node id), which is
 *  how a patch reads. Nothing selected starts at the first (next) or last (previous) module. With
 *  a selection, the step is taken from its last (next) or first (previous) member in that order,
 *  and CLAMPS at the ends rather than wrapping -- the rule a held key already follows for track
 *  focus. Returns a zero NodeID when there are no modules. */
inline juce::AudioProcessorGraph::NodeID adjacentModule(std::vector<StepModule> modules,
                                                        const std::vector<juce::AudioProcessorGraph::NodeID>& selected,
                                                        int direction) {
    if (modules.empty())
        return {};

    std::sort(modules.begin(), modules.end(), [](const StepModule& a, const StepModule& b) {
        const auto ca = a.bounds.getCentre();
        const auto cb = b.bounds.getCentre();
        if (ca.x != cb.x)
            return ca.x < cb.x;
        if (ca.y != cb.y)
            return ca.y < cb.y;
        return a.id.uid < b.id.uid;
    });

    const int last = (int)modules.size() - 1;
    int anchor = -1;
    for (int i = 0; i <= last; ++i) {
        if (std::find(selected.begin(), selected.end(), modules[(size_t)i].id) == selected.end())
            continue;
        if (anchor < 0 || direction > 0)
            anchor = i; // previous wants the first member, next the last
        if (direction <= 0)
            break;
    }

    if (anchor < 0)
        return modules[(size_t)(direction > 0 ? 0 : last)].id;
    return modules[(size_t)std::clamp(anchor + (direction > 0 ? 1 : -1), 0, last)].id;
}

} // namespace synth::ui
