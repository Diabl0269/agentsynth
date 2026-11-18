#pragma once

namespace synth::ui {

/** How far in from a tick toggle's left edge AppLookAndFeel::paintTickToggle draws its box. A row that wants the box
    (not the component) flush with a text edge places the component this much left of that edge. */
constexpr int kTickToggleBoxInset = 4;

/** The least contrast the box's outline (off) and fill (on) keep against the surface behind them: WCAG's ratio for
    non-text marks. */
constexpr double kTickToggleMinContrast = 3.0;

} // namespace synth::ui
