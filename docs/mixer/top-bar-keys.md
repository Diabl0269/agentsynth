# Mixer top bar keys

The mixer panel keeps real focus on its root (it is the region's one focusable leaf, [panel](panel.md#keyboard-navigation-and-accessibility)),
and plain Up and Down nudge the focused fader. **Cmd+Up** (`mixerFocusTopBar`, rebindable, Mixer category; Ctrl+Up off
the Mac, where Cmd is Ctrl) puts a painted ring on the first stop of the top bar instead. It is checked before the
arrows and the row keys, so it also works from row mode (which it leaves).

The ring is `synth::ui::KeyRing` (`Source/UI/Layout/KeyRing.{h,cpp}`), the same class the Timeline's top bar uses
([focus regions](../control/focus-regions.md)); `MixerPanelKeyboard.cpp` owns the stop list and the key wiring.

## Stops

In reading order, not by position (`MixerPanelComponent::topBarStops()`, `KeyRing::Order::given`): the side-pane
button, Inserts, Sends, EQ, Reset Meters, Add Bus, Dock back (own window only), then the side pane's filter field and
All / Tracks / Buses chips, then the master column's pan law button. A stop that is hidden or disabled (the side pane
closed, no dock-back button) is skipped. The toolbar travels with the panel, so the ring works in a detached window.

## Keys while the ring is up

`MixerPanelComponent::keyPressed()` gives the ring the key first.

| Key | Does |
|-----|------|
| Left / Right / Home / End | Move the ring (no wrap) |
| Return / Space | Press the ringed control. A menu opens; the filter field takes keyboard focus and the ring is done |
| Down / Escape | Leave the ring; the arrows are the fader's again |
| Up | Swallowed, so a second Up never nudges the fader |
| Any chord | Not the ring's |

Losing focus leaves the ring. The ring is painted in `paintOverChildren`, so no child covers it.

## Screen reader

The panel's accessibility handler has role group and a value: the ringed control's name while the ring is up, empty
otherwise (`KeyRing::createAccessibilityHandler`). Entering, moving and leaving notify `valueChanged`. The panel's title
("Mixer") and its row description are unchanged.

The hidden `mixerTopBarRing` child reports `topBarRing` to test mode, the same name (`Tools/ui-verify/scenarios/mixer-top-bar-keys.yaml`).
Tests: `Tests/UI/Mixer/MixerTopBarRingTests.cpp`.
