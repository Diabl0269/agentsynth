#pragma once

#include <cstdint>
#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/** The lanes a sequencer card can stack under its grid, in chip order. */
enum class SequencerLane : std::uint8_t { Pitch, Gate, Velocity, Chance, Repeat, Slide, Nudge, Condition, Filter };

constexpr int kSequencerLaneCount = 9;

constexpr std::uint16_t sequencerLaneBit(SequencerLane lane) noexcept { return (std::uint16_t)(1u << (unsigned)lane); }

/** What a new card shows: Pitch and Velocity. */
constexpr std::uint16_t kDefaultSequencerLanes =
    sequencerLaneBit(SequencerLane::Pitch) | sequencerLaneBit(SequencerLane::Velocity);

constexpr std::uint16_t kAllSequencerLanes = (std::uint16_t)((1u << kSequencerLaneCount) - 1u);

/** The saved name of a lane ("pitch", "gate", ...). */
inline const char* sequencerLaneId(SequencerLane lane) noexcept {
    constexpr const char* ids[kSequencerLaneCount] = {"pitch", "gate",  "velocity",  "chance", "repeat",
                                                      "slide", "nudge", "condition", "filter"};
    return ids[(size_t)lane];
}

inline std::optional<SequencerLane> sequencerLaneFromId(const juce::String& id) {
    for (int i = 0; i < kSequencerLaneCount; ++i)
        if (id == sequencerLaneId((SequencerLane)i))
            return (SequencerLane)i;
    return std::nullopt;
}

/** Slide is a mono-only lane (the Poly Sequencer has no legato). */
constexpr bool sequencerLaneAvailable(SequencerLane lane, bool poly) noexcept {
    return !(poly && lane == SequencerLane::Slide);
}

} // namespace synth
