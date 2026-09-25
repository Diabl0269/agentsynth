# Mixer Meters

**Meters only, like Cubase's MixConsole — no limiter.** The peak latch that gets levels off the audio
thread, the dB scale and its taper, ballistics, colour zones, the clip readout, and the track header
chip. The panel that polls them is [`docs/mixer/panel.md`](panel.md); the fader's own, separate taper
is [`docs/mixer/fader.md`](fader.md).

Everything here is headless-testable: no `juce::Timer` and no wall-clock read anywhere in the
ballistics themselves.

---

## A per reader peak latch

**One latch per leg, keyed by `MeterReader` (`Mixer`, `TrackHeader`), not one shared float.**
`Source/Mixer/PeakMeterLatch.h`: the audio thread's `storeBlockPeak()` is a **lock-free CAS-max loop
into EVERY reader's own slot**, and each reader's `takePeak()` is a read-and-reset of **only its own
slot** — so the mixer column and a track header's channel chip poll independently and never steal
each other's peaks.

**Why per reader.** `ChannelStripModule`/`MasterModule` used to store exactly one peak float per leg,
overwritten every block, so a hot block landing between two 10 Hz UI polls was silently gone by the
next poll.

**`getMeterPeak(leg)` does not exist; call `takeMeterPeak(MeterReader, leg)`.**
`TrackChannelLinkController::getChannelInfo()` deliberately does **NOT** read the `TrackHeader` slot —
that would race the 15 Hz tick (`getChannelMeterPeak()`), which is that slot's one consumer — so its
`ChannelInfo::meterPeak` field stays at its default.

A NaN or non-positive input is dropped rather than latched, and silence reads back 0.

## The dB scale and its taper

**Two bars, L and R, per column** — strips, buses, Direct if metered, and Master — over
**-60 to +3 dBFS** (`Source/UI/Mixer/MixerMeterScale.h`). Tick marks at +3, 0, -6, -12, -18, -24,
-30, -40, -50 and -60 dBFS (Cubase's own channel-meter marks plus our own +3 dB headroom cap), with
the 0 dB tick drawn visibly stronger. Tick NUMBERS appear only where the column has room
(`MixerMeter::paint`'s own width check); the dashes themselves always draw.

**The mapping is Cubase's own taper, NOT linear in dB.** `meterDbToFraction` and its inverse
`meterFractionToDb` are a monotonic **piecewise-linear interpolation through ten breakpoints**
(`detail::kMeterTaperBreakpoints` — the same ten dB values as the tick table, each paired with its own
0 to 1 position) rather than a straight `(db - min) / (max - min)`. The positions are measured off
Cubase's MixConsole meter — 0 dB at 92% of the height, each 6 dB down to -24 about 12.5%, and -50 to
-60 about 7% — so a channel sitting near 0 dB, the common case, reads with real resolution.

**Every caller goes through that ONE mapping** — ticks, the bar fill (`MeterColourStops::forEachBand`'s
band edges), the peak-hold line, and `ChannelChipComponent`'s horizontal fill — so a taper change is
one file. `meterLinearToDb` floors NaN, negative and zero input to `kMeterMinDb`.

**The fader has its own separate, unrelated mapping** (`MixerFaderTaper.h`, 0 dB at thumb position
0.71) and the two are never shared: Cubase's own fader and meter read differently. See
[`docs/mixer/fader.md`](fader.md).

## Ballistics

**Instant attack; release about 20 dB/s; a peak-hold line per bar holds 1.5 s then falls at about
20 dB/s** (`Source/UI/Mixer/MixerMeterBallistics.h`'s `advanceMeterBallistics`). The peak-hold line
snaps to a new peak instantly.

**Rate-independent, and driven by the SAME 10 Hz poll as everything else in this area — no new
timer.** `MixerPanelComponent::refreshMeters()` measures real elapsed time itself
(`juce::Time::getMillisecondCounterHiRes()`, **clamped to avoid a multi-second elapsed-time jump right
after the tab was hidden**) and threads it down, so the ballistics are exercised by tests with
explicit elapsed times rather than a wall clock. **Repaint stays gated on the drawn state actually
moving.**

## When the mixer counts as visible

**"Visible" means showing ANYWHERE, not just docked.** `MainComponent::timerCallback()`'s gate
([`docs/layout/rendering.md`](../layout/rendering.md)) is
`mixerDock.isMixerShowing() || mixerPlacement_.isOwnPanelShowing()`:

- `isMixerShowing()` covers both docked-on-the-Mixer-tab-with-the-dock-open **and** detached into its
  own `DetachedPanelWindow` — a detached window is a separate top-level `Component`, so the dock's own
  `isVisible()` says nothing about it;
- `isOwnPanelShowing()` covers the "Own panel" placement.

**A detach or redock toggle reparents the SAME `MixerPanelComponent`, never rebuilds it, and
deliberately skips `rebuild()`** (`MixerDockComponent::applyTabVisibility(false)` from that one
caller). Unlike a real tab switch, nothing about which graph nodes the mixer shows has changed, and
rebuilding would silently reset every column's latched clip-readout state to "-inf" on every detach
and redock.

## Colour zones

**POSITIONAL bands, not one whole-bar colour.** Hard band edges, `Source/UI/Mixer/MeterColourStops.h`:
below -18 dBFS = low (the `meterFill` token, kept as the low zone's colour for theme back-compat),
-18 to -6 = mid (`meterMid`), -6 to 0 = high (`meterHigh`), above 0 = clip (`meterClip`) — see
[`docs/layout/theming.md#colours`](../layout/theming.md#colours)'s token table.

`MeterColourStops::forEachBand(fromDb, toDb, callback)` is what drives it: **a bar reaching +4 dB
paints low, mid, high and clip STACKED bottom to top**, each band only as tall as its own dB span,
and a bar reaching only -10 dB paints low plus part of mid and stops there — **never a single colour
for the whole filled bar**. Both `MixerMeter` (vertical bars) and `ChannelChipComponent` (horizontal,
same banding left to right) call it with `fromDb = kMeterMinDb` and `toDb` as the bar's own displayed
dB. **The peak-hold line stays a single colour, for its OWN zone** — it is a 1 px cap, not a filled
span.

`MeterColourStops` holds its stops as a **SORTED, arbitrary-length `std::vector<MeterColourStop>`**
rather than a fixed four, precisely so the stop set can be user-edited with no painter change.
`setStops()` sorts by `dbFrom`, drops exact-`dbFrom` duplicates, and **always leaves at least one
stop**: the model's floor, so any dB value below the lowest stop's own `dbFrom` still resolves to that
stop's colour — its `dbFrom` is treated as -inf, never a hard edge a quieter value could fall through.
`fromTheme()` builds the default four-stop model above, and `kMaxStops` is 8.

## User editable meter colours

Settings > Appearance carries a **"Meter Colours"** section
(`Source/UI/Settings/MeterColourStopsEditor`) that adds, drags, recolours and removes stops. The
result is **ONE GLOBAL `meterColourStops` override cached on `synth::theme::AppLookAndFeel` and read
by every meter painter** — `MixerMeter::paint` and `ChannelChipComponent::paintButton` both read the
same `AppLookAndFeel::getMeterColourStops()` cache, so setting an override changes both and clearing
it reverts both to the active theme's own tokens. See
[`docs/layout/colour-overrides.md#meter-colours`](../layout/colour-overrides.md#meter-colours).

**Persistence is strict: ANY one malformed token fails the WHOLE key rather than partially
applying.** `serializeMeterColourStops`/`parseMeterColourStops` reject an empty string, a missing
separator, a garbage or out-of-range dB field, a short or non-hex colour field, and a slot count of 0
or over `kMaxStops`. **This guards against `getFloatValue()`'s own "garbage parses as 0" trap.** An
out-of-order or duplicate-`dbFrom` but otherwise well-formed value still normalises through the
vector constructor rather than failing.

**An override SURVIVES a later `applyTheme()`** — a theme switch must not silently clear a pinned
override — and clearing it falls back to the current theme. `clear` removes the key rather than
writing the theme's current stops in.

**The editor's own gesture rules.** A swatch press is undecided between click and drag until it
resolves (`kSwatchDragThresholdPx`): a click, or a sub-threshold jitter, opens the colour picker on
mouse-up and never moves the stop, while a press-and-drag from the SAME swatch moves the handle like
any row-body drag and opens no picker at all. A row-body drag snaps to 0.5 dB, fires a live
uncommitted change per frame plus one committed change on mouse-up, and **clamps at each neighbour
rather than crossing it**. **The FLOOR stop never moves**, by drag or by nudge — it only recolours.
Clicking empty space adds a stop at the snapped dB, refused once `kMaxStops` exist or when the click
would collide with an existing stop's own `dbFrom`. Delete and Backspace remove the selected stop,
**never the floor and never the last remaining one**. Up and Down nudge 0.5 dB, or 3 dB with Shift,
clamped the same way and consumed-but-a-no-op on the floor.

## The clip readout

Cubase's "Meter Peak Level" field: `MixerMeterReadout`, one per metered column, sitting above its
meter and fader. It shows the highest peak since the last reset ("-3.2", "+4.1", "-inf") and **turns
the clip colour once any peak exceeds 0 dBFS and STAYS that colour until reset**, across later quiet
ticks.

A plain click resets that column; Option or Alt-click resets every column
(`onResetAllRequested`, fanned out by `MixerPanelComponent::resetAllMeterReadouts()`); the mixer panel
header's "Reset Meters" button, next to "+ Bus", calls the same fan-out. `reset()` clears both the max
and the clip colour.

## The Master meter and Master inserts

**With no Master inserts nothing changes:** the column reads `MasterModule::takeMeterPeak(Mixer, leg)`, the
level at Master's own fader output. **With one or more inserts it reads the level LEAVING the master
chain instead**, as every DAW's master meter does — so a limiter's ceiling shows on the meter and the
clip readout, not the hotter signal that went into it. The source is
`AudioEngine::takeOutputMeterPeak(MeterReader, leg)`: a `std::array<PeakMeterLatch, 2>` the audio thread
stores straight after `mainProcessorGraph.processBlock()` (`AudioEngineRenderPass.cpp`) — after the
whole graph, before the metronome click and the master-mute zero-fill — the one point both host modes
funnel through. It is a `MeterReader::Mixer` consumer like the column itself, so the same per-reader,
read-and-reset rules apply; leg 1 falls back to leg 0 for a mono buffer. The column switches on
`MixerMasterColumn::outputPeakProvider` (wired by `MixerPanelComponent` to that engine call) whenever its
snapshot has an insert; unset, it falls back to Master's own latch.

**Gain reduction stays inside the limiter module** — there is no separate gain-reduction meter on
the Master column.

## The track header chip

`ChannelChipComponent` reads its own `TrackHeader` latch slot and applies the same dB mapping and
colour zones, so it turns the clip colour on an over. **No numeric readout there, just the coloured
bar.**

## Test coverage

| File | Covers |
|------|--------|
| `Tests/Mixer/PeakMeterLatchTests.cpp` | `PeakMeterLatch`: the max across several `storeBlockPeak()` calls is kept until `takePeak()`; a read resets only that reader's own slot; two `MeterReader` slots are independent; a NaN or non-positive input is dropped rather than latched; silence reads back 0 |
| `Tests/UI/Mixer/MixerMeterScaleTests.cpp` | `meterLinearToDb` boundaries and its NaN, negative and zero floors; `meterDbToFraction` and its inverse hit every one of the ten taper breakpoints exactly, are each monotonic across the whole scale, clamp beyond 0 and 1 and beyond -60 and +3, round-trip both directions, and 0 to -12 dB reads at several times the fraction-per-dB rate of the -50 to -60 tail |
| `Tests/UI/Mixer/MeterColourStopsTests.cpp` | zone selection at the exact boundaries (-18, -6, 0 dBFS) and just below and above each; built from a theme's four tokens, never a code literal; `forEachBand` splits a range into its constituent zone bands at those same boundaries; `setStops` and the vector constructor with 1, 2 and 6 stops, and with unsorted or duplicate-`dbFrom` input (sorts, dedups to the first-listed of a tie, never ends up empty) |
| `Tests/UI/Mixer/MixerMeterPaintTests.cpp` | `MixerMeter::paint` actually draws POSITIONAL bands, not one flat colour, for a bar spanning multiple zones — pixel-sampled off an offscreen render at the low-zone and high-zone heights of the same bar |
| `Tests/UI/Mixer/MixerMeterBallisticsTests.cpp` | attack is instant; release falls at `kMeterReleaseDbPerSecond` scaled by the elapsed time passed in; the peak-hold line snaps to a new peak instantly, holds for `kMeterPeakHoldSeconds`, then falls at `kMeterPeakHoldFallDbPerSecond` — every case drives `advanceMeterBallistics` with explicit elapsed-time arguments, never a wall clock |
| `Tests/UI/Mixer/MixerMeterReadoutTests.cpp` | turns the clip colour once a peak exceeds 0 dBFS and stays that colour across later quiet ticks; `reset()` clears both the max and the clip colour; a real `mouseUp` with no modifiers resets the readout; an Alt-modifier click fires `onResetAllRequested` instead |
| `Tests/UI/Mixer/MixerColumnComponentMeterTests.cpp` | a column's readout reaches through `MixerPanelComponent::resetAllMeterReadouts()` — an Alt-click on ONE column's readout resets every strip column's and Master's; the dock's "Reset Meters" button does the same |
| `Tests/UI/Theme/ThemeMeterZoneTests.cpp` | `meterMid`, `meterHigh` and `meterClip` parse from JSON, fall back to `Theme.h`'s defaults when the keys are absent, a malformed value rejects the whole theme, and all four built-ins populate the three tokens distinctly |
| `Tests/Mixer/ChannelFlow/ChannelFlowTrackChannelLinkTests.cpp` | the channel chip's meter displays a fraction of the dB scale, not the raw linear peak; the per-reader latch's two independent slots are exercised across the render and solo cases |
| `Tests/Engine/OutputMeterTapTests.cpp` | `AudioEngine::takeOutputMeterPeak`: each leg latches the graph output's own peak (a negative excursion counts) and reads back once, another `MeterReader`'s slot is untouched, a silent graph latches nothing, a mono buffer's right leg follows its left |
| `Tests/UI/Mixer/MixerMasterColumnInsertTests.cpp` | the Master column's meter source: an empty chain reads Master's own latch and never calls the engine provider, a chain with an insert reads the provider instead (and falls back to Master's latch with none wired); a Limiter added through the column's own list on a hot signal shows the limited level while Master's pre-insert latch still sees the full peak |
| `Tests/UI/Mixer/MixerDockMeterGatingTests.cpp` | `MixerDockComponent::isMixerShowing()` at its three states; `MainComponent::timerCallback()` actually calls `refreshMeters()` when the mixer is detached with the dock hidden and does NOT when the mixer is showing nowhere; detaching then redocking the SAME column leaves its clip readout's latched state exactly as it was, not reset to "-inf" |
| `Tests/UI/Mixer/MeterColourStopsPersistenceTests.cpp` | round-trip at 1, a handful, and the maximum number of stops; strict malformed-token rejection with ANY one bad token failing the whole key; an out-of-order or duplicate but well-formed value still normalising; `load`, `write`, `save` and `clear` against a real `juce::PropertiesFile`; `AppLookAndFeel`'s own cache, including an override surviving a later `applyTheme()` and clearing falling back to the current theme |
| `Tests/UI/Settings/MeterColourStopsEditorTests.cpp` | the editor driven with synthesized `juce::MouseEvent`s: handle selection writing nothing; the click-versus-drag resolution on a swatch, including the floor's own; a row-body drag's 0.5 dB snap, its live-plus-committed change pair and its clamp at each neighbour; the floor never moving; adding on an empty-space click and its refusals; remove never taking the floor or the last stop; the arrow nudge and its Shift step; `setStops()` from the owner replacing the working set and clearing selection |
| `Tests/UI/Mixer/MeterColourStopsLiveApplyTests.cpp` | pixel-sampled proof that `MixerMeter::paint` and `ChannelChipComponent::paintButton` read the SAME `AppLookAndFeel` cache — an override changes what each paints, and clearing it reverts both |
| `Tests/App/MainComponent/MeterColourStopsSettingsIntegrationTests.cpp` | the live-apply wire on a real `MainComponent`: writing the key the way the Appearance tab does, pumping the message loop for the settings file's async `ChangeBroadcaster`, and asserting `changeListenerCallback` pushed it into the one `AppLookAndFeel` instance every meter painter reads; clearing the key reverts it |

**PNG render-to-file inspection.** `MixerColumnComponentMeterTests.cpp`'s
`ClippedMeterRendersToPngForVisualInspection` follows the same convention as
`ModuleComponentLayoutTests.cpp`'s `AdsrCardRendersToPngForVisualInspection`
([`docs/development/test-patterns.md`](../development/test-patterns.md)): a strip driven hot enough
that its clip readout shows the clip colour and its bars sit in the clip zone, painted offscreen
unconditionally so the meaningful-content assertions always run, and written to disk only when
`MIXER_METER_CLIP_PNG=<path>` is set in the environment — `GTEST_SKIP()` otherwise, so CI never
depends on writing a file. `MeterColourStopsEditorTests.cpp`'s `RendersToPngForVisualInspection`
follows the identical convention for the Appearance section itself
(`METER_COLOUR_EDITOR_PNG=<path>`).

## Related

- [`docs/mixer/panel.md`](panel.md) — the panel and the 10 Hz poll that drives these meters.
- [`docs/mixer/fader.md`](fader.md) — the fader's own, separate taper.
- [`docs/mixer/mixer.md`](mixer.md) — the channel chip and the track and channel link.
- [`docs/layout/theming.md#colours`](../layout/theming.md#colours) — the meter colour tokens.
- [`docs/layout/colour-overrides.md#meter-colours`](../layout/colour-overrides.md#meter-colours) — the
  `meterColourStops` user override.
- [`docs/development/test-layers.md`](../development/test-layers.md) — the shared test catalogue this
  area's tables were split out of.
