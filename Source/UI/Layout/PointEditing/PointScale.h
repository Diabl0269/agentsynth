// PointScale.h -- scaling a group of selected dots about the middle of the selection, one axis at a time
// (Ctrl+Alt+arrows, docs/control/point-editing.md). Pure math shared by the curve editor and the automation lanes.
#pragma once

#include <vector>

namespace synth::ui {

/** One press: +1 spreads the dots apart, -1 pulls them together. A fine press (Shift held) takes a smaller step.
 *  Together is the exact inverse of apart, so a spread then a squeeze lands where it began. */
double pointScaleFactor(int side, bool fine);

/** The interval a dot may land in along the axis being scaled. */
struct ScaleLimits {
    double lo = 0.0;
    double hi = 0.0;
};

/** `values` scaled by `factor` about the middle of their lowest and highest, each dot clamped to its own limits
 *  (parallel to `values`). A value axis has no order to keep, so a dot at a limit stays there while the rest go on. */
std::vector<double> scaleValuesAbout(const std::vector<double>& values, const std::vector<ScaleLimits>& limits,
                                     double factor);

/** `positions` (ascending) scaled by `factor` about the middle of the first and last, as one block: the factor is
 *  shortened until every dot stays within its limits (the unselected neighbours on either side, already kept `gap`
 *  away by the caller) and no two dots come closer than `minGap`, so the order never changes. Unchanged when the
 *  block is already against a limit. */
std::vector<double> scalePositionsAbout(const std::vector<double>& positions, const std::vector<ScaleLimits>& limits,
                                        double minGap, double factor);

} // namespace synth::ui
