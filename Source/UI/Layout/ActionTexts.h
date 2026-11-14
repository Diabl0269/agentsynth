#pragma once

// ActionTexts.h (docs/development/accessibility.md#the-rule): the label and tooltip of every action that has a button
// on more than one surface (a module card and the window it opens), so one action reads the same everywhere. A tooltip
// is only the explanation: the label is already on the button (ButtonTooltip.h).
namespace synth::ui::actiontext {

inline constexpr const char* kShowSpectrum = "Show Spectrum";
inline constexpr const char* kShowSpectrumFilterTip =
    "Draw the filter's output behind the curve, before any volume stage, so a sound still running into the filter "
    "keeps it moving after the notes stop";
inline constexpr const char* kShowSpectrumEqTip = "Draw the EQ's live output behind the curve";

inline constexpr const char* kPickShape = "Pick shape";
inline constexpr const char* kPickShapeTip = "Choose a saved or factory shape from a list with a small picture of "
                                             "each; the selected shape takes its curve (Cmd+Z undoes it)";

inline constexpr const char* kDone = "Done";
inline constexpr const char* kDoneCloseWindowTip = "Close this window (Esc)";

} // namespace synth::ui::actiontext
