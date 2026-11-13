// CustomFilterPerNote.h -- the Custom filter's per-note settings: whether each note (mono or poly) gets its own
// place on the morph map (and how it is picked), how fast a note glides there, and the By hand key map;
// plus their versioned JSON form, which rides inside the module's extra state as "perNote".
// Message-thread data; CustomFilterEngine publishes it to the audio thread.
#pragma once

#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace synth {

struct CustomFilterPerNote {
    /** How a new note picks its place on the map. Off: every voice shares the Morph X/Y cursor. */
    enum class Mode { Off = 0, ByHand, ByPitch, Random };
    static constexpr int kNumModes = 4;
    static constexpr int kVersion = 1;
    static constexpr int kNumKeys = 128; // MIDI notes
    static constexpr int8_t kUnassigned = -1;
    static constexpr float kMaxGlideSeconds = 0.5f; // smoothness 1
    static constexpr float kDefaultSmoothness = 0.25f;
    /** By pitch spreads this note range (inclusive) across the shapes; notes outside it clamp. */
    static constexpr int kByPitchLowNote = 36;  // C2
    static constexpr int kByPitchHighNote = 96; // C7

    Mode mode = Mode::Off;
    float smoothness = kDefaultSmoothness; // 0 = jump at the note's start, 1 = kMaxGlideSeconds glide
    std::array<int8_t, kNumKeys> keys = makeUnassignedKeys(); // By hand: shape index per note, or kUnassigned

    /** JSON spelling ("off", "byHand", "byPitch", "random"). */
    static const char* modeId(Mode mode);
    /** UI wording ("Off", "By hand", "By pitch", "Random"). */
    static const char* modeLabel(Mode mode);
    static std::array<int8_t, kNumKeys> makeUnassignedKeys();

    bool operator==(const CustomFilterPerNote& other) const;
    bool isDefault() const;

    /** `{"version":1,"mode":"byPitch","smoothness":0.25,"keys":[[60,1],[62,0]]}`; keys lists assigned notes only. */
    juce::var toVar() const;
    /** Never fails: a missing or newer object gives the defaults, an unknown mode Off, smoothness is
     *  clamped to 0..1, and key entries outside 0..127 or with a shape outside 0..kMaxShapes-1 are dropped. */
    static CustomFilterPerNote fromVar(const juce::var& v);
    /** The model-output reader: applies only "mode" (one of the four ids) and "smoothness" (a finite
     *  number, clamped to 0..1) onto `inOut`; the key map and every other key are ignored. Returns true when
     *  anything was applied. */
    static bool fromUntrustedVar(const juce::var& v, CustomFilterPerNote& inOut);
};

} // namespace synth
