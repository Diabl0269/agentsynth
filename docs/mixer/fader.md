# Mixer fader: taper, drag conventions (FRO150)

Split out of [`mixer.md`](mixer.md) §5.10 (near that file's own line cap) to keep this ticket's
detail out of it — see that section's own "Fader" paragraph for the one-line pointer back here.
Covers `Source/UI/Mixer/MixerFader.{h,cpp}`, `Source/UI/Mixer/MixerFaderTaper.h` and
`Source/UI/Mixer/MixerFaderSlider.{h,cpp}`.

## Taper: UI-only, the parameter stays linear dB

`MixerFader` binds a `juce::Slider` (LinearVertical) to a `ChannelStripModule`/`MasterModule`
`gain` `AudioParameterFloat*` via `juce::SliderParameterAttachment` — unchanged since FRO11. What
FRO150 changes is only how the slider's on-screen THUMB POSITION relates to that value: instead of
a straight `(db - min) / (max - min)`, it follows a Cubase-like taper, the same idea the meter
already has (`MixerMeterScale.h`, §5.10's Meters subsection) but a **separate, unrelated mapping**
— Cubase's own fader and meter read differently, and the meter file's own comment already promised
it would never be reused here.

The bound **parameter never changes shape** — still `ChannelStripModule::kMinGainDb`/`kMaxGainDb`
(`-60..+12`, linear in dB). Presets, automation, AI patches and the plugin host all read/write that
linear value and must never see anything else. Only `MixerFaderSlider`'s **drag pixel math** and
its **on-screen thumb position** go through the taper.

**Mechanism.** `Source/UI/Mixer/MixerFaderTaper.h` is a pure, header-only, unit-tested pair —
`faderDbToFraction`/`faderFractionToDb` — monotonic piecewise-linear interpolation through 11
breakpoints (`detail::kFaderTaperBreakpoints`), measured off Cubase's own MixConsole fader and
extended up to our own `+12` dB top:

| dB | +12 | +6 | 0 | -5 | -10 | -15 | -20 | -30 | -40 | -50 | -60 |
|----|-----|-----|-----|------|------|------|------|------|------|------|------|
| position | 1.00 | 0.90 | **0.71** | 0.537 | 0.409 | 0.308 | 0.23 | 0.13 | 0.073 | 0.04 | 0.00 |

0 dB sits at exactly 0.71 — the anchor the whole table is built from. `MixerFader::bind()` installs
these as a custom `juce::NormalisableRange<double>`'s `convertTo0to1`/`convertFrom0to1` (the exact
JUCE seam meant for a non-linear slider mapping — it only ever affects the 0..1 proportion used for
the thumb's drawn position and for interpreting drag distance, never the slider's own `getValue()`).
`snapToLegalValue` is re-expressed as a lambda too, at the same 0.1 dB grid the param's own interval
already used.

**Ordering trap.** `juce::SliderParameterAttachment`'s own constructor calls
`slider.setNormalisableRange()` (built from the param's OWN linear mapping) and overwrites
`slider.textFromValueFunction`, as a side effect of construction. `bind()` must construct
`attachment_` FIRST and install the taper range + the dB-formatted accessibility text AFTER, or the
attachment silently undoes both — `MixerFaderTests.cpp`'s
`TextFromValueFunctionStillReportsDbAfterBind` regression test pins this. Swapping the range this
way never disturbs the slider's current value (`juce::Slider::Pimpl::setNormalisableRange` is
exactly `normRange = newRange`).

## Shift-drag: fine adjustment at 1/8 rate

Cubase's MixConsole convention: holding Shift during a fader drag slows it to 1/8 of the normal
rate. `MixerFaderSlider` (a `juce::Slider` subclass) implements this itself rather than configuring
anything on stock `juce::Slider` — see the design note below for why.

The rate is anchored to **wherever the drag currently is**, not the original mouse-down point:
`mouseDown` records the press position and the slider's value-at-press as an anchor;
`mouseDrag` computes `(anchorY - currentY) / trackLength`, in **proportion space** (the slider's
0..1 thumb position, via `getNormalisableRange().convertTo0to1/convertFrom0to1`), scaled by 1/8
when Shift is held, then adds that to the anchor's own proportion. Doing the math in proportion
space (not dB space) is what makes a fixed pixel delta cover more dB near the bottom of the taper
than near 0 dB — the taper's whole point — and what makes the Shift rate exactly 1/8 of on-screen
travel regardless of where on the taper the drag started.

`trackLength` is `getLookAndFeel().getSliderLayout(*this).sliderBounds.getHeight()`, **not**
`getHeight()`: `LookAndFeel_V2::getSliderLayout` (inherited by `AppLookAndFeel`, which never
overrides it) reduces the slider bounds by the thumb's own radius on each end
(`sliderBounds.reduce(0, thumbIndent)`) before `juce::Slider::Pimpl` positions the thumb inside
them — the same quantity that base class's own `sliderRegionSize` uses. Dividing by the raw
component height instead measures a longer track than the thumb can actually travel, so the thumb
lags the cursor over a long drag (visible at typical fader widths, where the thumb radius caps at
12 px — up to 24 px of missing travel against `getHeight()`). `MixerFaderDragFixture::trackLengthPx()`
reads the same call so drag tests stay correct under whatever `LookAndFeel` resolves.

**No jump on toggle.** Whenever Shift's held state changes mid-drag, `mouseDrag` re-anchors at the
CURRENT mouse position and value before applying the new rate — everything already dragged under
the old rate is baked into the new anchor, so only the RATE changes from that point on, never the
value itself. `MixerFaderDragTests.cpp`'s `TogglingShiftMidDragNeverJumpsTheValue` drives this with
a real press/no-shift-drag/shift-toggle/shift-drag/release-toggle sequence.

Shift+mouse-wheel gets the same 1/8 factor, applied the same way `mouseDrag` applies it: to a
proportion-space step scaled by `MouseWheelDetails::deltaY` (mirroring
`juce::Slider::Pimpl::getMouseWheelDelta`'s own `0.15 * deltaY` sensitivity), not a fixed dB amount
per wheel *event*. A fixed per-event step was tried first and broke on trackpads — a two-finger
scroll delivers dozens of small-`deltaY` events per gesture, and treating every one of them as a
full step sent the fader flying tens of dB in a single flick
(`ManySmallWheelDeltasAccumulateReasonablyLikeATrackpad` pins this). The resulting dB step is
floored to at least one 0.1 dB grid tick in the requested direction whenever the wheel moves at
all, matching `juce::Slider`'s own `jmax(interval, abs(delta))` floor: without it, a `deltaY` small
enough that its scaled/taper-mapped raw delta rounds under the 0.1 dB grid would snap straight back
to the value it started from and a real notch would move nothing
(`EveryNonZeroWheelNotchMovesAtLeastOneGridStep`).

**Why a full custom `mouseDown`/`mouseDrag`/`mouseUp`/`mouseDoubleClick`/`mouseWheelMove`, not
`juce::Slider`'s own velocity-mode/modifier facilities?** `MixerFaderTests.cpp`'s own header
comment already documents that driving synthesized `MouseEvent`s through a STOCK `juce::Slider`'s
mouseDown/mouseDrag hung this suite in CI — its internal mouse handling (`juce::Slider::Pimpl`)
reaches into platform mouse-capture/cursor code the rest of this codebase's real-mouse tests never
touch. None of JUCE's velocity-mode options give "1/8 while Shift is held, anchored to wherever the
drag currently is, no jump on toggle" either. `MixerFaderSlider` therefore reimplements all five
overrides from scratch and never calls the `juce::Slider` base versions, so headless tests can
drive them directly and deterministically (`MixerFaderDragTests.cpp`).

**Gesture bracketing.** `MixerFaderSlider` has no direct access to the bound `AudioParameterFloat`,
so it calls `juce::Slider`'s own public `onDragStart`/`onDragEnd` `std::function` members (not the
private `Slider::Listener` `sliderDragStarted`/`sliderDragEnded` `SliderParameterAttachment` itself
listens for — those are only ever invoked from the same internal Pimpl machinery this class
bypasses). `MixerFader::bind()` wires `onDragStart`/`onDragEnd` to the bound param's
`beginChangeGesture()`/`endChangeGesture()` directly — the same pattern `MixerFader::nudge()`
already uses for the keyboard path (docs/mixer.md §5.16). Every gesture `MixerFaderSlider` starts —
a drag, Cmd-click/double-click reset, a wheel step — collapses to exactly ONE undo step through the
existing `MixerFader::parameterGestureChanged` bracket, with no second mechanism. A whole drag
(however many `mouseDrag` calls) is one step because `onDragStart`/`onDragEnd` fire exactly once
each, from `mouseDown`/`mouseUp`.

## Cmd-click and double-click: reset to 0 dB

Cubase's convention: Cmd-click (Ctrl-click on Windows — `juce::ModifierKeys::isCommandDown()` is
already the platform-correct check) and double-click both reset the fader to 0 dB, as one undo
step. Both are implemented directly in `MixerFaderSlider::mouseDown`/`mouseDoubleClick` (not via
`juce::Slider::setDoubleClickReturnValue`, for the same reason the drag path avoids the `Slider`
base class — a physical double-click's two preceding mouseDown/mouseUp pairs would otherwise also
need to interoperate with whatever `setDoubleClickReturnValue`'s internal state machine does, and
this codebase already needs its own `mouseDown` override for Cmd-click). Both call the same
`resetToZero()` helper, bracketed by `onDragStart`/`onDragEnd` like any other gesture. A plain click
with no modifiers (no Cmd, not a double-click) starts an ordinary drag, unaffected.

## Keyboard nudge unaffected

`MixerFader::nudge(deltaDb)` (§5.16, FRO18) still operates purely in dB on the bound parameter —
it never touches the slider or the taper at all, so keyboard Up/Down behaviour is exactly as before
this ticket.
