#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme {
struct Theme;
}

namespace synth::ui {

// The ring's stroke width for `theme`: 1.5x the border weight on a dark theme, 2.5x on a light one,
// where a thin accent line on a white control beside its grey border reads as no ring at all.
float focusRingThickness(const synth::theme::Theme& theme);

// The clear gap between a ring and the control it stands off from (a fader's cap, a ticked box, a tab filled with
// the accent): the ring's outer edge sits kFocusRingGap + focusRingThickness outside the control.
inline constexpr float kFocusRingGap = 3.0f;

// The clear band of the surface colour a fill steps in by, between the ring's inner edge and the fill, while a control
// whose on or selected state is filled with the accent shows its ring (the bottom dock's active tab, a switch's chosen
// segment, a lit toolbar chip). The control's size does not change; only its fill does.
inline constexpr float kFocusFillGap = 2.0f;

// A component whose fill is painted by its children (a switch's segments) sets this on each child: the child then
// steps its fill in while its parent shows the ring.
inline const juce::Identifier kFocusRingOwnerIsParentProperty{"focusRingOwnerIsParent"};

// A component that paints its own band between fill and ring (the dock tab) sets this so its fill does not step in too.
inline const juce::Identifier kFocusFillHasOwnBandProperty{"focusFillHasOwnBand"};

// Whether `fill` would swallow the ring's accent stroke (it is the accent, or too near it to tell apart).
bool fillHidesFocusRing(juce::Colour fill, const synth::theme::Theme& theme);

// Whether `comp` (or, with kFocusRingOwnerIsParentProperty set, its parent) shows its ring now, so a fill that
// would hide the ring steps in.
bool showsFocusFillStep(const juce::Component& comp);

// How far a stepped-in fill stands in from the control's edge: the ring's stroke plus kFocusFillGap, or 0 while
// showsFocusFillStep is false.
float focusFillInset(const juce::Component& comp, const synth::theme::Theme& theme);

// A fill that is the accent to the edge (a lit toggle pill, an icon toggle, a lit key) steps in from the ring: `fill`
// limited to `ringArea` less the inset above, scaled by `amount` (0..1, how far the control has eased to its lit look),
// so the surface shows as a band between the fill and the ring. `fill` itself while the ring is not showing; the same
// geometry as the stepped-in text buttons and toolbar chips (steppedInFill in AppLookAndFeelButtons.cpp). A rim the
// control draws in the accent steps in with its fill.
juce::Rectangle<float> focusSteppedFill(const juce::Component& comp, const synth::theme::Theme& theme,
                                        juce::Rectangle<float> fill, juce::Rectangle<float> ringArea,
                                        float amount = 1.0f);

// Tests: every stepped-in fill steps in as if its component showed the ring, without the ring itself, so the sweep
// can paint the control's at-rest image with the band and take the ring out of the difference.
void setFocusFillStepShownForTest(bool shown);

// The ring for a caller with a theme but no component (the fader painter): the same stroke paintFocusRingAlways draws.
void paintFocusRingForTheme(juce::Graphics& g, const synth::theme::Theme& theme, juce::Rectangle<float> area,
                            float cornerRadius = 0.0f);

// Strokes the same ring as paintFocusRing without asking whether `comp` holds focus, for a caller that
// decides that itself (a ring that follows a tracked item rather than the component, or a test with no
// native window to take focus). `comp` only supplies the theme.
void paintFocusRingAlways(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                          float cornerRadius = 0.0f);

// Strokes the keyboard-focus ring of a small control: a solid, full-alpha `accent` ring (1.5x the
// theme's border weight) around `area`, inset by half its thickness so it never clips against the
// component's own edge. Draws nothing unless `comp` itself holds keyboard focus (descendants do not
// count). `cornerRadius` 0 draws a square ring. The colour comes from the component's AppLookAndFeel
// theme, falling back to orange when it has none. Use this for buttons, toggles, knobs and sliders;
// whole panels use paintFocusRegionOutline (FocusRegion.h), which is softer.
void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, const juce::Component& comp,
                    float cornerRadius = 0.0f);

// Whether `comp` paints its focus ring now: it holds keyboard focus, or a test asked every ring to show.
bool showsFocusRing(const juce::Component& comp);

// Tests: every paintFocusRing paints as if its component held focus (a headless window cannot take focus).
void setFocusRingsShownForTest(bool shown);

} // namespace synth::ui
