# Mixer Fader

The mixer fader's taper and its drag conventions. Covers `Source/UI/Mixer/MixerFader.{h,cpp}`,
`Source/UI/Mixer/MixerFaderTaper.h` and `Source/UI/Mixer/MixerFaderSlider.{h,cpp}`. The column the
fader sits in is [`docs/mixer/panel.md`](panel.md); the meter's own, separate taper is
[`docs/mixer/meters.md`](meters.md).

---

## The taper is UI only and the parameter stays linear dB

`MixerFader` binds a `juce::Slider` (LinearVertical) to a `ChannelStripModule`/`MasterModule` `gain`
`AudioParameterFloat*` via `juce::SliderParameterAttachment`. What the taper changes is **only how the
slider's on-screen THUMB POSITION relates to that value**: instead of a straight
`(db - min) / (max - min)`, it follows a Cubase-like taper — the same idea the meter has
(`MixerMeterScale.h`) but a **separate, unrelated mapping**, because Cubase's own fader and meter read
differently and the meter's own header comment promises its mapping is never reused here.

**The bound parameter never changes shape** — still `ChannelStripModule::kMinGainDb`/`kMaxGainDb`,
`-60..+12`, linear in dB. **Presets, automation, AI patches and the plugin host all read and write
that linear value and must never see anything else.** Only `MixerFaderSlider`'s drag pixel maths and
its on-screen thumb position go through the taper.

**Mechanism.** `Source/UI/Mixer/MixerFaderTaper.h` is a pure, header-only, unit-tested pair —
`faderDbToFraction` and `faderFractionToDb` — a monotonic piecewise-linear interpolation through 11
breakpoints (`detail::kFaderTaperBreakpoints`), measured off Cubase's own MixConsole fader and
extended up to our own `+12` dB top:

| dB | +12 | +6 | 0 | -5 | -10 | -15 | -20 | -30 | -40 | -50 | -60 |
|----|-----|-----|-----|------|------|------|------|------|------|------|------|
| position | 1.00 | 0.90 | **0.71** | 0.537 | 0.409 | 0.308 | 0.23 | 0.13 | 0.073 | 0.04 | 0.00 |

**0 dB sits at exactly 0.71** — the anchor the whole table is built from. `MixerFader::bind()`
installs the pair as a custom `juce::NormalisableRange<double>`'s
`convertTo0to1`/`convertFrom0to1` — the exact JUCE seam meant for a non-linear slider mapping, which
only ever affects the 0 to 1 proportion used for the thumb's drawn position and for interpreting drag
distance, never the slider's own `getValue()`. `snapToLegalValue` is re-expressed as a lambda too, at
the same 0.1 dB grid the parameter's own interval already used. Swapping the range never disturbs the
slider's current value, since `juce::Slider::Pimpl::setNormalisableRange` is exactly
`normRange = newRange`.

**Ordering trap.** `juce::SliderParameterAttachment`'s own constructor calls
`slider.setNormalisableRange()` — built from the parameter's OWN linear mapping — and overwrites
`slider.textFromValueFunction`, as a side effect of construction. **`bind()` must construct
`attachment_` FIRST and install the taper range plus the dB-formatted accessibility text AFTER**, or
the attachment silently undoes both.

## Shift drag for fine adjustment

Cubase's MixConsole convention: **holding Shift during a fader drag slows it to 1/8 of the normal
rate.** `MixerFaderSlider` (a `juce::Slider` subclass) implements this itself rather than configuring
anything on stock `juce::Slider`.

**The rate is anchored to wherever the drag currently is, not the original mouse-down point.**
`mouseDown` records the press position and the slider's value-at-press as an anchor; `mouseDrag`
computes `(anchorY - currentY) / trackLength` **in proportion space** — the slider's 0 to 1 thumb
position, via `getNormalisableRange().convertTo0to1`/`convertFrom0to1` — scaled by 1/8 when Shift is
held, then adds that to the anchor's own proportion. Doing the maths in proportion space rather than dB
space is what makes a fixed pixel delta cover more dB near the bottom of the taper than near 0 dB, and
what makes the Shift rate exactly 1/8 of on-screen travel regardless of where on the taper the drag
started.

**`trackLength` is `getLookAndFeel().getSliderLayout(*this).sliderBounds.getHeight()`, NOT
`getHeight()`.** `LookAndFeel_V2::getSliderLayout` — inherited by `AppLookAndFeel`, which never
overrides it — reduces the slider bounds by the thumb's own radius on each end
(`sliderBounds.reduce(0, thumbIndent)`) before `juce::Slider::Pimpl` positions the thumb inside them,
the same quantity that base class's own `sliderRegionSize` uses. Dividing by the raw component height
instead measures a longer track than the thumb can actually travel, so the thumb lags the cursor over
a long drag — visible at typical fader widths, where the thumb radius caps at 12 px, so up to 24 px of
missing travel. `MixerFaderDragFixture::trackLengthPx()` reads the same call so drag tests stay
correct under whatever `LookAndFeel` resolves.

**No jump on toggle.** Whenever Shift's held state changes mid-drag, `mouseDrag` **re-anchors at the
CURRENT mouse position and value before applying the new rate** — everything already dragged under the
old rate is baked into the new anchor, so only the RATE changes from that point on, never the value
itself.

**Shift plus mouse wheel gets the same 1/8 factor**, applied the same way: to a proportion-space step
scaled by `MouseWheelDetails::deltaY`, mirroring `juce::Slider::Pimpl::getMouseWheelDelta`'s own
`0.15 * deltaY` sensitivity — **not a fixed dB amount per wheel event**. A fixed per-event step broke
on trackpads: a two-finger scroll delivers dozens of small-`deltaY` events per gesture, and treating
every one as a full step sent the fader flying tens of dB in a single flick. The resulting dB step is
**floored to at least one 0.1 dB grid tick in the requested direction whenever the wheel moves at
all**, matching `juce::Slider`'s own `jmax(interval, abs(delta))` floor: without it, a `deltaY` small
enough that its scaled and taper-mapped raw delta rounds under the 0.1 dB grid would snap straight
back to the value it started from and a real notch would move nothing.

**Gesture bracketing.** `MixerFaderSlider` has no direct access to the bound `AudioParameterFloat`, so
it calls `juce::Slider`'s own public `onDragStart`/`onDragEnd` `std::function` members — **not the
private `Slider::Listener` `sliderDragStarted`/`sliderDragEnded` that `SliderParameterAttachment`
itself listens for**, since those are only ever invoked from the same internal Pimpl machinery this
class bypasses. `MixerFader::bind()` wires `onDragStart`/`onDragEnd` to the bound parameter's
`beginChangeGesture()`/`endChangeGesture()` directly, the same pattern `MixerFader::nudge()` uses for
the keyboard path. **Every gesture `MixerFaderSlider` starts — a drag, a reset, a wheel step —
collapses to exactly ONE undo step** through the existing `MixerFader::parameterGestureChanged`
bracket, with no second mechanism. A whole drag, however many `mouseDrag` calls it takes, is one step
because `onDragStart`/`onDragEnd` fire exactly once each, from `mouseDown` and `mouseUp`.

## Cmd click and double click reset to 0 dB

Cubase's convention: **Cmd-click (Ctrl-click on Windows — `juce::ModifierKeys::isCommandDown()` is
already the platform-correct check) and double-click both reset the fader to 0 dB, as one undo step.**

Both are implemented directly in `MixerFaderSlider::mouseDown`/`mouseDoubleClick`, **not via
`juce::Slider::setDoubleClickReturnValue`**: a physical double-click's two preceding mouseDown and
mouseUp pairs would otherwise also have to interoperate with whatever that setting's internal state
machine does, and this class already needs its own `mouseDown` override for Cmd-click. Both call the
same `resetToZero()` helper, bracketed by `onDragStart`/`onDragEnd` like any other gesture. **A plain
click with no modifiers, and not a double-click, starts an ordinary drag, unaffected.**

## Why a full custom mouse implementation

`MixerFaderSlider` reimplements `mouseDown`, `mouseDrag`, `mouseUp`, `mouseDoubleClick` and
`mouseWheelMove` from scratch and **never calls the `juce::Slider` base versions**, for two reasons:

- **Driving synthesized `MouseEvent`s through a STOCK `juce::Slider`'s mouseDown and mouseDrag hung
  the test suite in CI.** Its internal mouse handling (`juce::Slider::Pimpl`) reaches into platform
  mouse-capture and cursor code the rest of this codebase's real-mouse tests never touch.
- **None of JUCE's velocity-mode options give "1/8 while Shift is held, anchored to wherever the drag
  currently is, no jump on toggle".**

Reimplementing all five is what lets headless tests drive them directly and deterministically.

## Keyboard nudge is unaffected

`MixerFader::nudge(deltaDb)` operates purely in dB on the bound parameter — it never touches the
slider or the taper at all, so keyboard Up and Down behaviour is independent of everything above. See
[`docs/mixer/panel.md`](panel.md#keyboard-navigation-and-accessibility).

## Test coverage

| File | Covers |
|------|--------|
| `Tests/UI/Mixer/MixerFaderTaperTests.cpp` | `faderDbToFraction` and its inverse hit every one of the 11 taper breakpoints exactly (0 dB at exactly 0.71), are each monotonic across the whole -60 to +12 dB scale, clamp beyond 0 and 1 and beyond -60 and +12, round-trip both directions, and the bottom decade (-60 to -50) occupies less fraction-per-dB than the -5 to 0 dB segment |
| `Tests/UI/Mixer/MixerFaderTests.cpp` | a bound fader's slider THUMB POSITION (`getNormalisableRange().convertTo0to1(value)`) matches the taper for -14.2, 0 and +12 dB while the bound parameter stays exactly linear dB; a regression test pinning that `textFromValueFunction` still reports "-3.0 dB" after a real `bind()`, not `juce::SliderParameterAttachment`'s own parameter-`getText()`-based overwrite; and the `parameterValueChanged` use-after-free guard |
| `Tests/UI/Mixer/MixerFaderDragTests.cpp` | real `mouseDown`, `mouseDrag`, `mouseUp`, `mouseDoubleClick` and `mouseWheelMove` calls on `MixerFaderSlider` with synthesized `MouseEvent`s, never `juce::Slider`'s own mouse handling: a plain drag moves the value along the taper from the press-time anchor; the same pixel drag moves more dB near the bottom of the taper than near 0 dB; a Shift-held drag moves about 1/8 as far as a plain one; toggling Shift mid-drag re-anchors instead of jumping the value, in both directions; a whole drag collapses to exactly ONE undo step; Cmd-click and double-click both reset to 0 dB as one undo step; Shift plus wheel moves a smaller step than a plain wheel notch; many small wheel deltas accumulate reasonably like a trackpad; and every non-zero wheel notch moves at least one grid step |

## Related

- [`docs/mixer/panel.md`](panel.md) — the column the fader sits in, and the keyboard nudge path.
- [`docs/mixer/meters.md`](meters.md) — the meter's own separate taper.
- [`docs/mixer/mixer.md`](mixer.md) — the `gain` parameter's range and the strip it belongs to.
- [`docs/development/test-patterns.md`](../development/test-patterns.md) — the real-mouse-path testing
  convention.
