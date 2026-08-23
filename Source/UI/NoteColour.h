#pragma once

#include "Theme/Theme.h"
#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

// Piano-roll note colour resolution — the single source of truth for what colour a note body is
// drawn in. Mirrors CableColour.h's structure: a pure function over (theme, note state,
// overrides), no GUI state, no LookAndFeel, headless-testable by construction (see
// NoteColourTests.cpp). PianoRollComponent::paintNote is expected to call resolveNoteColour
// instead of hand-rolling the velocity/selection/mute treatment inline.
namespace synth::ui {

// A sparse per-pitch-class override layer, exactly like CableColourOverrides: an unset entry
// means "use the theme's noteFill", so a theme switch still moves any pitch class the user has
// not explicitly pinned. Indexed by pitch % 12 (0 = C, 1 = C#, ... 11 = B) rather than by MIDI
// note number — a per-octave override table would be 128 entries for something that reads as
// "colour every C" to a user.
struct NoteColourOverrides {
    std::array<std::optional<juce::Colour>, 12> perPitchClass{};

    void set(int pitchClass, juce::Colour c) { perPitchClass[(size_t)(((pitchClass % 12) + 12) % 12)] = c; }
    void clear(int pitchClass) { perPitchClass[(size_t)(((pitchClass % 12) + 12) % 12)].reset(); }

    bool hasAny() const noexcept {
        for (const auto& o : perPitchClass)
            if (o.has_value())
                return true;
        return false;
    }

    void clearAll() { perPitchClass = {}; }
};

struct NotePaint {
    juce::Colour fill;
    juce::Colour border;
};

// Muted-note dimming, mirroring TrackColour.h's kMutedTrack* treatment: desaturate and dim
// rather than change hue, so a muted note is still identifiable while reading as "off".
inline constexpr float kMutedNoteSaturation = 0.3f;
inline constexpr float kMutedNoteAlpha = 0.45f;

// THE resolver. Precedence, highest first:
//   1. outOfScale forces the noteOutOfScale family for fill (a warning takes priority over any
//      colour choice — a note the user picked purple for is still wrong if it's off-scale).
//   2. a per-pitch-class override wins over the theme's noteFill.
//   3. the theme's noteFill.
// Velocity brightness and the selected/muted treatments apply on top of whichever fill won,
// exactly like PianoRollComponent::paintNote's existing formula (kept identical here so moving
// the call site to this resolver is not a visual change).
inline NotePaint resolveNoteColour(const synth::theme::Colors& colors, int pitch, int velocity, bool selected,
                                   bool muted, bool outOfScale, const NoteColourOverrides& overrides) noexcept {
    juce::Colour baseFill = colors.noteFill;
    if (outOfScale)
        baseFill = colors.noteOutOfScale;
    else if (const auto& o = overrides.perPitchClass[(size_t)(((pitch % 12) + 12) % 12)])
        baseFill = *o;

    const float brightness = juce::jlimit(0.2f, 1.0f, (float)velocity / 127.0f);
    juce::Colour fill = baseFill.withMultipliedBrightness(0.5f + 0.7f * brightness).withAlpha(selected ? 0.95f : 0.8f);
    juce::Colour border = selected ? colors.noteSelected : colors.noteBorder;

    if (muted) {
        fill = fill.withSaturation(fill.getSaturation() * kMutedNoteSaturation).withMultipliedAlpha(kMutedNoteAlpha);
        border = border.withSaturation(border.getSaturation() * kMutedNoteSaturation).withMultipliedAlpha(kMutedNoteAlpha);
    }

    return {fill, border};
}

//==============================================================================
// Persistence — mirrors CableColour.h's loadCableColourMode/saveCableColourMode idiom. Lives
// here (not in a settings tab) so every reader/writer of this key agrees on the format.
//==============================================================================

inline const char* noteColourOverridesKey() noexcept { return "pianoRollNoteColourOverrides"; }

// 12 comma-separated slots (one per pitch class, C first), each either an empty string (no
// override) or a juce::Colour::toString() value. An absent key or a key with the wrong slot
// count is treated as "no overrides at all" rather than partially applied — a malformed value
// in one slot must not corrupt the other eleven.
inline NoteColourOverrides loadNoteColourOverrides(juce::PropertiesFile& props) {
    NoteColourOverrides out;
    const auto raw = props.getValue(noteColourOverridesKey(), {});
    if (raw.isEmpty())
        return out;

    const auto tokens = juce::StringArray::fromTokens(raw, ",", "");
    if (tokens.size() != 12)
        return out; // malformed slot count -> treat as absent, never half-apply

    for (int i = 0; i < 12; ++i) {
        const auto& tok = tokens[i];
        if (tok.isNotEmpty())
            out.perPitchClass[(size_t)i] = juce::Colour::fromString(tok);
    }
    return out;
}

inline void saveNoteColourOverrides(juce::PropertiesFile& props, const NoteColourOverrides& o) {
    juce::StringArray tokens;
    for (int i = 0; i < 12; ++i)
        tokens.add(o.perPitchClass[(size_t)i] ? o.perPitchClass[(size_t)i]->toString() : juce::String());
    props.setValue(noteColourOverridesKey(), tokens.joinIntoString(","));
    props.saveIfNeeded();
}

} // namespace synth::ui
