#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <utility>
#include <vector>

// VelocityLaneMath — the piano roll velocity strip's pure maths, free of any Component so it is
// unit-testable on its own: the y <-> velocity mapping, which stick a press lands on, the velocity
// a pen stroke or a straight-line ramp gives each stick it crosses, and the humanize offset. See
// VelocityLaneMath.cpp for each function's contract and docs/timeline/piano-roll.md#velocity-strip.
namespace synth::ui::velocitylane {

constexpr int kMinVelocity = 1;
constexpr int kMaxVelocity = 127;

int clampVelocity(int velocity) noexcept;

// `top` is the y of velocity 127, `bottom` the y of velocity 1 (top < bottom).
float yForVelocity(int velocity, float top, float bottom) noexcept;
int velocityForY(float y, float top, float bottom) noexcept;

// One stick as the maths sees it: its note, its x in whole pixels, and the y of its head.
struct StickPoint {
    synth::NoteId id;
    int x = 0;
    float headY = 0.0f;
};

// Index of the stick a press at `pos` grabs, or nullopt when none is within `tolerancePx`.
std::optional<size_t> pickStick(const std::vector<StickPoint>& sticks, juce::Point<float> pos, int tolerancePx);

// Every stick whose x lies on the segment `from` -> `to` (inclusive), with the velocity the line's
// y gives at that x.
std::vector<std::pair<synth::NoteId, int>> lineVelocities(const std::vector<StickPoint>& sticks,
                                                          juce::Point<float> from, juce::Point<float> to, float top,
                                                          float bottom);

// `velocity` plus a random offset in [-range, +range], clamped to [1, 127].
int humanizedVelocity(int velocity, int range, juce::Random& random);

} // namespace synth::ui::velocitylane
