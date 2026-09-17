# Mixer Meter & Fader Tests (FRO146/FRO150)

The meters rework ([`mixer.md`](mixer.md) §5.10's Meters subsection): a per-reader peak latch, a
-60..+3 dBFS two-bar scale with colour zones and ballistics, a per-column clip readout, and the
track header chip's own dB mapping. Split out of [`testing.md`](testing.md) to hold that file's own
line cap; see `Tests/Mixer/ChannelStripTests.cpp`'s entry there for the module-level latch tests
(the missed-overs regression) that stayed alongside the existing `ChannelStripTest`/`MasterModuleTest`
suites. All headless; no `juce::Timer`, no wall-clock read anywhere in the ballistics themselves.

FRO150 (the fader's own taper, [`mixer_fader.md`](mixer_fader.md)) added the two fader tables below
for the same reason -- new coverage that would have pushed `testing.md` past its own cap.

| File | Covers |
|------|--------|
| `Tests/Mixer/PeakMeterLatchTests.cpp` | `PeakMeterLatch`: the max across several `storeBlockPeak()` calls is kept until `takePeak()`; a read resets only that reader's own slot; two `MeterReader` slots are independent (one's read never affects the other's); a NaN or non-positive input is dropped rather than latched; silence reads back 0 |
| `Tests/UI/Mixer/MixerMeterScaleTests.cpp` | `meterLinearToDb` boundaries and NaN/negative/zero floors to `kMeterMinDb`; `meterDbToFraction`/its inverse `meterFractionToDb` hit every one of the ten taper breakpoints (+3, 0, -6, -12, -18, -24, -30, -40, -50, -60) exactly, are each monotonic across the whole scale, clamp beyond 0/1 and beyond -60/+3, round-trip both directions (`db -> fraction -> db` and back), and 0..-12 dB reads at several times the fraction-per-dB rate of the -50..-60 tail (the taper's whole point) |
| `Tests/UI/Mixer/MeterColourStopsTests.cpp` | zone selection at the exact boundaries (-18, -6, 0 dBFS) and just below/above each; built from a theme's four tokens, never a code literal; `forEachBand` splits a range into its constituent zone bands at those same boundaries (a bar reaching +4 dB yields four bands stacked low->mid->high->clip, one reaching only -10 dB yields low + a partial mid band and nothing past it); `setStops`/the vector constructor with 1, 2 and 6 stops, and with unsorted, duplicate-`dbFrom` input (sorts, dedups to the first-listed of a tie, never ends up empty) |
| `Tests/UI/Mixer/MixerMeterPaintTests.cpp` | `MixerMeter::paint` actually draws POSITIONAL bands, not one flat colour, for a bar spanning multiple zones -- pixel-sampled off an offscreen render at the low-zone and high-zone heights of the same bar |
| `Tests/UI/Mixer/MixerMeterBallisticsTests.cpp` | attack is instant (a louder input snaps `displayedDb` up in one call); release falls at `kMeterReleaseDbPerSecond` scaled by the elapsed time passed in; the peak-hold line snaps to a new peak instantly, holds for `kMeterPeakHoldSeconds` once nothing louder arrives, then falls at `kMeterPeakHoldFallDbPerSecond` -- every case drives `advanceMeterBallistics` with explicit elapsed-time arguments, never a wall clock |
| `Tests/UI/Mixer/MixerMeterReadoutTests.cpp` | turns the clip colour once a peak exceeds 0 dBFS and stays that colour across later quiet ticks; `reset()` clears both the max and the clip colour; a real mouse click (`mouseUp`, no modifiers) resets the readout; an Alt-modifier click fires `onResetAllRequested` instead of resetting itself |
| `Tests/UI/Mixer/MixerColumnComponentMeterTests.cpp` | a column's readout reaches through `MixerPanelComponent::resetAllMeterReadouts()` -- an Alt-click on ONE column's readout resets every strip column's AND Master's; the mixer dock's "Reset Meters" button (next to "+ Bus") does the same |
| `Tests/UI/Theme/ThemeMeterZoneTests.cpp` | `meterMid`/`meterHigh`/`meterClip` parse from JSON, fall back to `Theme.h`'s defaults when the keys are absent, a malformed value rejects the whole theme, and all four built-ins populate the three tokens distinctly (round-trip through `serialiseTheme`/`parseTheme` is `ThemeTests.cpp`'s existing `JsonRoundTrip` case, extended) |
| `Tests/Mixer/ChannelFlow/ChannelFlowTrackChannelLinkTests.cpp` (updated) | the channel chip's meter now displays a fraction of the dB scale, not the raw linear peak (`TheMeterTickReportsTheChannelsRealLevelThroughTheCheapRead`); the per-reader latch's two independent slots (`Mixer` vs `TrackHeader`) are exercised across the render/solo cases |
| `Tests/UI/Mixer/MixerDockMeterGatingTests.cpp` | `MixerDockComponent::isMixerShowing()` at its three states (docked+active+open = true, neither docked-active nor detached = false, detached while the docked tab is on Timeline AND the dock is closed = still true); `MainComponent::timerCallback()` actually calls `refreshMeters()` (a `getRefreshMetersCallCountForTest()` counter) when the mixer is detached with the dock hidden, and does NOT when the mixer is showing nowhere at all; detaching then redocking the SAME column (never rebuilt) leaves its clip readout's latched "STAYS red until reset" state exactly as it was, not reset to "-inf" |

## Fader tests (FRO150)

| File | Covers |
|------|--------|
| `Tests/UI/Mixer/MixerFaderTaperTests.cpp` | `faderDbToFraction`/its inverse `faderFractionToDb` (`MixerFaderTaper.h`) hit every one of the 11 taper breakpoints exactly (0 dB at exactly 0.71), are each monotonic across the whole -60..+12 dB scale, clamp beyond 0/1 and beyond -60/+12, round-trip both directions, and the bottom decade (-60..-50) occupies less fraction-per-dB than the -5..0 dB segment (more dB per pixel near the bottom -- the taper's whole point) |
| `Tests/UI/Mixer/MixerFaderTests.cpp` (extended) | a bound fader's slider THUMB POSITION (`getNormalisableRange().convertTo0to1(value)`) matches the taper for -14.2/0/+12 dB while the bound parameter stays exactly linear dB (`BoundSliderPositionMatchesTaperWhileParameterStaysLinearDb`); a regression test pinning that `textFromValueFunction` still reports "-3.0 dB" after a real `bind()`, not `juce::SliderParameterAttachment`'s own param-`getText()`-based overwrite (`TextFromValueFunctionStillReportsDbAfterBind`) |
| `Tests/UI/Mixer/MixerFaderDragTests.cpp` | real `mouseDown`/`mouseDrag`/`mouseUp`/`mouseDoubleClick`/`mouseWheelMove` calls on `MixerFaderSlider` (never `juce::Slider`'s own mouse handling -- see `mixer_fader.md`'s design note) with synthesized `MouseEvent`s: a plain drag moves the value along the taper from the press-time anchor; the same pixel drag moves more dB near the bottom of the taper than near 0 dB; a Shift-held drag moves ~1/8 as far as a plain one; toggling Shift mid-drag re-anchors instead of jumping the value, in both directions; a whole drag (several `mouseDrag` calls) collapses to exactly ONE undo step; Cmd-click and double-click both reset to 0 dB as one undo step; Shift+wheel moves a smaller step than a plain wheel notch |

## Meter colour tests (FRO147)

**FRO147 -- user-editable meter colours (docs/mixer.md's Meters subsection / docs/theming.md's
meter-colours section): a Settings > Appearance "Meter Colours" section, one GLOBAL
`meterColourStops` override cached on `synth::theme::AppLookAndFeel` and read by every meter
painter.**

| File | Covers |
|------|--------|
| `Tests/UI/Mixer/MeterColourStopsPersistenceTests.cpp` | `serializeMeterColourStops`/`parseMeterColourStops` round-trip at 1, a handful, and the maximum 8 stops; strict malformed-token rejection (empty string, a missing `:` separator, a garbage or out-of-range dB field, a short/non-hex colour field, ANY one bad token failing the WHOLE key rather than a partial apply -- `getFloatValue()`'s own "garbage parses as 0" trap is what this guards against); an out-of-order/duplicate-`dbFrom` but otherwise well-formed value still normalises through the vector constructor rather than failing; `load`/`write`/`save`/`clear` against a real `juce::PropertiesFile` (absent key -> `nullopt`, malformed stored value -> `nullopt`, `clear` removes the key rather than writing the theme's current stops in); `AppLookAndFeel`'s own cache -- no override follows the active theme, a set override becomes the effective stops regardless of theme, an override SURVIVES a later `applyTheme()` (a theme switch must not silently clear a pinned override), and clearing it falls back to the current theme |
| `Tests/UI/Settings/MeterColourStopsEditorTests.cpp` | `Source/UI/Settings/MeterColourStopsEditor` driven with synthesized `juce::MouseEvent`s (the real-mouse-path convention above): clicking a handle selects it without writing anything; clicking a swatch fires `onColourPickerRequested` instead of arming a drag; dragging snaps to 0.5 dB and fires a live (uncommitted) change per frame plus one committed change on mouse-up; a drag clamps at each neighbour rather than crossing it; the FLOOR stop (index 0) never moves via drag or nudge, only recolours; clicking empty space adds a stop at the snapped dB and selects it, refused once 8 stops already exist or the click would collide with an existing stop's own `dbFrom`; `removeSelectedStop()`/Delete/Backspace remove the selected stop, never the floor, never the last remaining one; Up/Down nudge 0.5 dB (Shift = 3 dB), clamped the same way, consumed-but-a-no-op on the floor; `setStops()` from the owner (Reset to Theme, or a theme switch with no override pinned) replaces the working set and clears selection |
| `Tests/UI/Mixer/MeterColourStopsLiveApplyTests.cpp` | pixel-sampled proof that `MixerMeter::paint` and `ChannelChipComponent::paintButton` both read the SAME `AppLookAndFeel::getMeterColourStops()` cache -- setting an override changes what each one paints, and clearing it (`std::nullopt`) reverts both to the active theme's own tokens |
| `Tests/App/MainComponent/MeterColourStopsSettingsIntegrationTests.cpp` | the actual live-apply WIRE, on a real `MainComponent`: writing `meterColourStops` the same way `AppearanceSettingsTab::applyMeterColourStopsChange()` does, pumping the message loop for the settings file's own async `ChangeBroadcaster`, and asserting `MainComponent::changeListenerCallback`'s settings branch pushed it into `getLookAndFeelForTest()` -- the one `AppLookAndFeel` instance every meter painter reads; clearing the key reverts it to the active theme |

**PNG render-to-file inspection.** `MixerColumnComponentMeterTests.cpp`'s
`ClippedMeterRendersToPngForVisualInspection` follows the same convention as
`ModuleComponentLayoutTests.cpp`'s `AdsrCardRendersToPngForVisualInspection` (`docs/testing.md`'s
"Test the real mouse path" neighbourhood): a strip driven hot enough that its clip readout shows
the clip colour and its bars sit in the clip zone, painted offscreen unconditionally (so the
meaningful-content assertions always run), and written to disk only when
`MIXER_METER_CLIP_PNG=<path>` is set in the environment -- `GTEST_SKIP()` otherwise, so CI never
depends on writing a file. `MeterColourStopsEditorTests.cpp`'s
`RendersToPngForVisualInspection` follows the identical convention for the Settings > Appearance
"Meter Colours" section itself (`METER_COLOUR_EDITOR_PNG=<path>`).

See also [`testing.md`](testing.md) and [`theming.md`](theming.md)'s token table for `meterFill`/
`meterMid`/`meterHigh`/`meterClip`, and its meter-colours section for the `meterColourStops` user
override.
