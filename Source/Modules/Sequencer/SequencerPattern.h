#pragma once

#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

constexpr int kMaxSequencerSteps = 128;

/** What a Poly Sequencer step plays. The first eight keep the names and order of the original chord list. */
enum class SequencerChord : std::uint8_t {
    Unison,
    Major,
    Minor,
    Maj7,
    Min7,
    Fifths,
    Octaves,
    Random,
    // Built from the sequencer's scale at the (snapped) root.
    ScaleTriad,
    ScaleSeventh,
    ScaleSus2,
    ScaleSus4,
    ScaleAdd9,
    ScaleSixth,
    // Fixed qualities, for borrowing outside the key.
    Dom7,
    Dim,
    Aug,
    Sus2,
    Sus4,
    Sixth,
    Min6,
    Add9,
    Dim7,
    HalfDim7,
    MinMaj7,
};

constexpr int kSequencerChordCount = 25;
constexpr int kLegacySequencerChordCount = 8;

/** The saved / AI-facing name of a chord ("Unison", "5ths", "Scale Triad", "Dom7"). */
juce::String chordId(SequencerChord chord);
/** Exact match of a chordId; nullopt for anything else. */
std::optional<SequencerChord> chordFromId(const juce::String& id);

struct SequencerStep {
    std::uint8_t pitch = 60;
    bool on = true;
    float gate = 0.5f;
    std::uint8_t velocity = 100;
    std::uint8_t chance = 100;
    std::uint8_t repeat = 1;
    bool slide = false;
    float nudge = 0.0f;
    std::uint8_t condHit = 1;
    std::uint8_t condOf = 1;
    float filter = 0.5f;
    SequencerChord chord = SequencerChord::Unison;

    bool operator==(const SequencerStep&) const = default;
};

struct SequencerPattern {
    std::array<SequencerStep, kMaxSequencerSteps> steps{};
    std::uint32_t seed = 0;
    std::uint16_t customMask = 0xFFF;
    juce::String customName;

    /** The factory pattern (steps 9..128 repeat steps 1..8); `seed` is left for the caller. */
    static SequencerPattern makeDefault(bool poly);

    /** The JSON saved as the module's state: only fields that differ from a default step, trailing default steps
     *  trimmed. */
    juce::var toVar(bool poly) const;

    /** Never trusts `v`: every value is clamped into range, unknown keys, wrong types and steps past 128 are ignored,
     *  and anything missing keeps the factory pattern. A missing seed becomes `fallbackSeed`. */
    static SequencerPattern fromVar(const juce::var& v, bool poly, std::uint32_t fallbackSeed);
};

} // namespace synth
