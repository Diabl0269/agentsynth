// Concern: FRO146 -- MeterColourStops' theme lookup, stop-set maintenance, zone selection and
// positional band iteration.
#include "MeterColourStops.h"

#include "UI/Theme/Theme.h"
#include <algorithm>

namespace synth::ui {

MeterColourStops MeterColourStops::fromTheme(const synth::theme::Colors& colors) {
    // Ascending by dbFrom. The low zone's own dbFrom (kMeterMinDb, the scale floor) is cosmetic --
    // colourForDb()/forEachBand() both treat the FIRST stop's dbFrom as -inf regardless of its
    // literal value -- but kMeterMinDb reads better than an arbitrary sentinel.
    return MeterColourStops({
        {kMeterMinDb, colors.meterFill},
        {kMidFromDb, colors.meterMid},
        {kHighFromDb, colors.meterHigh},
        {kClipFromDb, colors.meterClip},
    });
}

void MeterColourStops::setStops(std::vector<MeterColourStop> stops) {
    // Stable: two equal-dbFrom stops keep their original relative order, so std::unique below
    // (which keeps the first of each run) keeps whichever of them the CALLER listed first.
    std::stable_sort(stops.begin(), stops.end(),
                     [](const MeterColourStop& a, const MeterColourStop& b) { return a.dbFrom < b.dbFrom; });
    stops.erase(std::unique(stops.begin(), stops.end(),
                            [](const MeterColourStop& a, const MeterColourStop& b) { return a.dbFrom == b.dbFrom; }),
                stops.end());
    if (stops.empty())
        stops.push_back({kMeterMinDb, juce::Colour()}); // never leave colourForDb/forEachBand with nothing
    stops_ = std::move(stops);
}

juce::Colour MeterColourStops::colourForDb(float db) const noexcept {
    // stops_ is never empty (setStops()'s own guarantee, upheld by every constructor).
    juce::Colour result = stops_.front().colour; // treated as -inf, see the class comment
    for (const auto& stop : stops_) {
        if (db < stop.dbFrom)
            break;
        result = stop.colour;
    }
    return result;
}

void MeterColourStops::forEachBand(
    float fromDb, float toDb,
    const std::function<void(float bandFromDb, float bandToDb, juce::Colour colour)>& callback) const {
    if (callback == nullptr || !(toDb > fromDb))
        return;

    // Walk the stops in ascending order, clipping each one's own span (up to the NEXT stop's
    // dbFrom, or toDb for the last stop) to [fromDb, toDb) as we go. A stop entirely below
    // `fromDb` is skipped without emitting anything for it; the first stop whose span reaches
    // into the requested range starts exactly at `fromDb` (or wherever `cursor` already is),
    // never at its own dbFrom -- which is exactly colourForDb()'s "first stop is -inf" contract,
    // applied per-stop instead of only to the very first one.
    float cursor = fromDb;
    for (size_t i = 0; i < stops_.size(); ++i) {
        if (cursor >= toDb)
            break;
        const float bandEnd = (i + 1 < stops_.size()) ? std::min(toDb, stops_[i + 1].dbFrom) : toDb;
        if (bandEnd <= cursor)
            continue;
        callback(cursor, bandEnd, stops_[i].colour);
        cursor = bandEnd;
    }
}

//==============================================================================
// Persistence (FRO147) -- see the header's own comment on each function.
//==============================================================================

namespace {

// Strict float parse for a persisted dB token: containsOnly guards against anything that isn't a
// digit/sign/dot, then a manual scan rejects the shapes containsOnly alone lets through (a sign
// after position 0, more than one sign, more than one dot, or "-"/"." with no digits) --
// juce::String::getFloatValue() itself returns 0 on total garbage, which would otherwise silently
// manufacture a stop at 0 dB instead of failing the whole key.
std::optional<float> strictParseMeterDb(const juce::String& s) {
    if (s.isEmpty() || !s.containsOnly("-0123456789."))
        return std::nullopt;
    int dashes = 0, dots = 0;
    for (int i = 0; i < s.length(); ++i) {
        if (s[i] == '-') {
            if (i != 0)
                return std::nullopt;
            ++dashes;
        } else if (s[i] == '.') {
            ++dots;
        }
    }
    if (dashes > 1 || dots > 1 || s == "-" || s == ".")
        return std::nullopt;

    const float v = s.getFloatValue();
    // A small tolerance past the scale's own ends -- the editor clamps to [kMeterMinDb,
    // kMeterMaxDb] before it ever writes a value, so anything further out is corrupt, not a
    // rounding artefact.
    if (v < kMeterMinDb - 0.05f || v > kMeterMaxDb + 0.05f)
        return std::nullopt;
    return v;
}

// Exactly 8 hex digits -- same strictness as ColourPickerPopup.h's parseFavouriteColours, minus
// the "0x" prefix allowance (serializeMeterColourStops() never emits one, so accepting it here
// would only widen what "round-trips" without ever being produced).
std::optional<juce::Colour> strictParseMeterArgb(const juce::String& s) {
    if (s.length() != 8 || !s.containsOnly("0123456789abcdefABCDEF"))
        return std::nullopt;
    return juce::Colour((juce::uint32)s.getHexValue64());
}

} // namespace

juce::String serializeMeterColourStops(const MeterColourStops& stops) {
    juce::StringArray tokens;
    for (const auto& stop : stops.getStops())
        tokens.add(juce::String(stop.dbFrom, 1) + ":" +
                   juce::String::toHexString((juce::int64)stop.colour.getARGB()).paddedLeft('0', 8).toUpperCase());
    return tokens.joinIntoString(",");
}

std::optional<MeterColourStops> parseMeterColourStops(const juce::String& raw) {
    if (raw.isEmpty())
        return std::nullopt;

    const auto tokens = juce::StringArray::fromTokens(raw, ",", "");
    if (tokens.isEmpty() || tokens.size() > MeterColourStops::kMaxStops)
        return std::nullopt; // 0 or >kMaxStops -- never half-apply a corrupt count

    std::vector<MeterColourStop> parsed;
    parsed.reserve((size_t)tokens.size());
    for (const auto& token : tokens) {
        const auto parts = juce::StringArray::fromTokens(token, ":", "");
        if (parts.size() != 2)
            return std::nullopt;
        const auto db = strictParseMeterDb(parts[0]);
        const auto colour = strictParseMeterArgb(parts[1]);
        if (!db.has_value() || !colour.has_value())
            return std::nullopt; // one bad token corrupts the whole key, never a partial apply
        parsed.push_back({*db, *colour});
    }
    // The vector constructor sorts/dedups/never-empties -- a file we wrote ourselves is already
    // sorted and unique, so this is a normalisation safety net for a hand-edited settings file,
    // never a silent way to "fix" a count that already failed the check above.
    return MeterColourStops(std::move(parsed));
}

std::optional<MeterColourStops> loadMeterColourStopsOverride(juce::PropertiesFile& props) {
    if (!props.containsKey(meterColourStopsKey()))
        return std::nullopt;
    return parseMeterColourStops(props.getValue(meterColourStopsKey(), {}));
}

void writeMeterColourStopsOverride(juce::PropertiesFile& props, const MeterColourStops& stops) {
    props.setValue(meterColourStopsKey(), serializeMeterColourStops(stops));
}

void saveMeterColourStopsOverride(juce::PropertiesFile& props, const MeterColourStops& stops) {
    writeMeterColourStopsOverride(props, stops);
    props.saveIfNeeded();
}

void clearMeterColourStopsOverride(juce::PropertiesFile& props) {
    props.removeValue(meterColourStopsKey());
    props.saveIfNeeded();
}

} // namespace synth::ui
