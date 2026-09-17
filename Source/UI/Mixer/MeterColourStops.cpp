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

} // namespace synth::ui
