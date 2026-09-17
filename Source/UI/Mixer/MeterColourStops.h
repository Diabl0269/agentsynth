#pragma once

#include "MixerMeterScale.h"
#include <functional>
#include <juce_graphics/juce_graphics.h>
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
// Hard band edges (docs/mixer.md meters section): below -18 dB = low (the pre-existing
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

} // namespace synth::ui
