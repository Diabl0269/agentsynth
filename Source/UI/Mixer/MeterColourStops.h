#pragma once

#include "MixerMeterScale.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

namespace synth::theme {
struct Colors;
}

// MeterColourStops.h -- FRO146: the meter's level-to-colour zone model, isolated from every
// painter that uses it (MixerMeter, ChannelChipComponent) so a follow-up ticket can make the
// stops user-editable (Settings > Appearance -- add/drag/remove) by building a different
// MeterColourStops -- no painter would need to change. Backed by a sorted, arbitrary-length
// vector rather than a fixed count for exactly that reason.
//
// Hard band edges (docs/mixer/mixer.md meters section): below -18 dB = low (the pre-existing
// `meterFill` token, kept as the low zone's colour for theme back-compat), -18..-6 dB = mid,
// -6..0 dB = high, above 0 dB = clip -- that is what fromTheme()'s default four stops encode.
//
// POSITIONAL BANDS, not a single whole-bar colour (Cubase/most DAWs' own meter convention): a bar
// reaching +4 dB paints low/mid/high/clip stacked bottom-to-top (or left-to-right for the
// horizontal channel chip); one reaching only -10 dB paints low, then part of mid, and stops
// there. forEachBand() below is what every painter uses to do this.

namespace synth::ui {

struct MeterColourStop {
    float dbFrom;
    juce::Colour colour;
};

class MeterColourStops {
public:
    static constexpr float kMidFromDb = -18.0f;
    static constexpr float kHighFromDb = -6.0f;
    static constexpr float kClipFromDb = 0.0f;

    /** FRO147: the Settings > Appearance editor's own ceiling on stop count -- a UI-level limit,
     *  not one setStops()/forEachBand() enforce themselves (they stay correct for any count, per
     *  the class comment above). Kept here so the editor and its persistence agree on one number. */
    static constexpr int kMaxStops = 8;

    /** Default-constructs to a single, safe fallback stop -- see setStops()'s own "never empty"
     *  guarantee. Real instances come from fromTheme() or the vector constructor below. */
    MeterColourStops() = default;

    /** Sorts, dedups and guarantees non-empty -- see setStops(). */
    explicit MeterColourStops(std::vector<MeterColourStop> stops) { setStops(std::move(stops)); }

    /** Built from a theme's own four tokens (meterFill/meterMid/meterHigh/meterClip), ascending by
     *  dbFrom -- the default four-zone model. An arbitrary stop count (the Settings > Appearance
     *  follow-up's whole point) goes through setStops()/the vector constructor instead. */
    static MeterColourStops fromTheme(const synth::theme::Colors& colors);

    /** Replaces the stop set: sorted ascending by dbFrom (stable -- ties keep their ORIGINAL,
     *  pre-sort relative order), exact-dbFrom duplicates collapsed to whichever of them sorted
     *  first, and never left empty -- an empty or all-duplicate input falls back to one
     *  transparent stop spanning the whole scale, so colourForDb()/forEachBand() always have at
     *  least one stop to work with. Any count is valid: 1 (a single flat colour), 2, 6, ... */
    void setStops(std::vector<MeterColourStop> stops);
    const std::vector<MeterColourStop>& getStops() const noexcept { return stops_; }

    /** The zone colour for `db` -- the stop with the greatest dbFrom that is still <= db, or the
     *  FIRST (lowest) stop for anything below it (its own dbFrom is treated as -inf, never a hard
     *  floor a quieter value could fall through). */
    juce::Colour colourForDb(float db) const noexcept;

    /** Positional colour bands (see the class comment): calls `callback(bandFromDb, bandToDb,
     *  colour)` once per constant-colour sub-range of [fromDb, toDb), left-to-right in dB order,
     *  clipped to the requested range -- a caller wanting only the portion of the scale a bar
     *  actually reaches passes that bar's own displayed dB as `toDb`. A no-op when
     *  `fromDb >= toDb` or `callback` is empty. */
    void forEachBand(float fromDb, float toDb,
                     const std::function<void(float bandFromDb, float bandToDb, juce::Colour colour)>& callback) const;

private:
    std::vector<MeterColourStop> stops_{{kMeterMinDb, juce::Colour()}};
};

//==============================================================================
// Persistence -- FRO147: a GLOBAL user override, mirroring CableColour.h's / NoteColour.h's own
// "unset means follow the theme" idiom. Lives here (not in the settings tab) so the tab, the
// AppLookAndFeel cache that painters read, and MainComponent's startup restore cannot disagree
// about the storage format.
//==============================================================================

/** The ApplicationProperties key. Absent -> meters follow the active theme (MeterColourStops::
 *  fromTheme()); present -> the custom stops are used regardless of theme, until Reset removes
 *  the key. */
inline const char* meterColourStopsKey() noexcept { return "meterColourStops"; }

/** "db:ARGBHEX,db:ARGBHEX,..." ascending by dbFrom, db to one decimal place (matches the editor's
 *  own 0.5 dB snap), ARGB as 8 uppercase hex digits. */
juce::String serializeMeterColourStops(const MeterColourStops& stops);

/** The strict inverse of serializeMeterColourStops(): nullopt on ANY malformed token (a bad float,
 *  a non-8-hex-digit colour, a slot count of 0 or over kMaxStops) rather than partially applying
 *  what parsed -- one corrupted stop must not leave a caller with a scale-breaking partial model.
 *  A well-formed result still runs through the MeterColourStops(vector) constructor, so an
 *  out-of-order or dbFrom-duplicate file still normalises rather than misbehaving. */
std::optional<MeterColourStops> parseMeterColourStops(const juce::String& raw);

/** nullopt when the key is absent OR malformed -- both mean "no override", i.e. follow the theme.
 *  Callers never see the difference between "never set" and "corrupted"; both fall back the same
 *  way (NoteColour.h's "malformed -> treat as absent" rule). */
std::optional<MeterColourStops> loadMeterColourStopsOverride(juce::PropertiesFile& props);

/** Writes the key WITHOUT forcing a disk flush -- safe to call on every frame of a live drag
 *  (juce::PropertiesFile still fires its ChangeBroadcaster synchronously-enough for a live-apply
 *  repaint, and debounces the actual disk write on its own timer). Pair with
 *  saveMeterColourStopsOverride() at a gesture's commit point so the edit survives a crash before
 *  that timer fires. */
void writeMeterColourStopsOverride(juce::PropertiesFile& props, const MeterColourStops& stops);

/** writeMeterColourStopsOverride() + an explicit saveIfNeeded() -- the commit-point call (mouse
 *  up, a colour picked, a stop added/removed, a keyboard nudge). */
void saveMeterColourStopsOverride(juce::PropertiesFile& props, const MeterColourStops& stops);

/** Removes the key (never re-writes the theme's current stops as a new pin) so meters go back to
 *  following the active theme -- "Reset to Theme". */
void clearMeterColourStopsOverride(juce::PropertiesFile& props);

} // namespace synth::ui
