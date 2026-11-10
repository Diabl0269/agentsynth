#pragma once

#include <algorithm>

// ScrollIntoView -- where a timeline view should start so a span of beats is on screen. Shared by everything that
// brings a thing into view as the keys move it (a clip picked with the keys, a lane dot carried by a held Alt+arrow),
// so "how far in from the edge does it land" is one decision.
namespace synth::ui {

// The fraction of the view a span is placed in from the edge it came from, so what is next to it stays partly in
// sight.
constexpr double kScrollIntoViewMargin = 0.1;

// The first visible beat that shows [start, end] in a view `visibleBeats` wide that starts at `first` now: `first`
// when it is already in; otherwise the span is placed `margin` of the view in from the edge it left by (or its start
// at the left edge when it is too wide for that). Never below beat 0.
inline double firstBeatShowing(double first, double visibleBeats, double start, double end,
                               double margin = kScrollIntoViewMargin) noexcept {
    const bool fits = end - start <= (1.0 - margin) * visibleBeats;
    double newFirst = first;
    if (start < first)
        newFirst = fits ? start - margin * visibleBeats : start;
    else if (end > first + visibleBeats)
        newFirst = fits ? end - (1.0 - margin) * visibleBeats : start;
    return std::max(0.0, newFirst);
}

} // namespace synth::ui
