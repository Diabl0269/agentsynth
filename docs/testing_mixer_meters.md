# Mixer Meter Tests (FRO146)

The meters rework ([`mixer.md`](mixer.md) §5.10's Meters subsection): a per-reader peak latch, a
-60..+3 dBFS two-bar scale with colour zones and ballistics, a per-column clip readout, and the
track header chip's own dB mapping. Split out of [`testing.md`](testing.md) to hold that file's own
line cap; see `Tests/Mixer/ChannelStripTests.cpp`'s entry there for the module-level latch tests
(the missed-overs regression) that stayed alongside the existing `ChannelStripTest`/`MasterModuleTest`
suites. All headless; no `juce::Timer`, no wall-clock read anywhere in the ballistics themselves.

| File | Covers |
|------|--------|
| `Tests/Mixer/PeakMeterLatchTests.cpp` | `PeakMeterLatch`: the max across several `storeBlockPeak()` calls is kept until `takePeak()`; a read resets only that reader's own slot; two `MeterReader` slots are independent (one's read never affects the other's); a NaN or non-positive input is dropped rather than latched; silence reads back 0 |
| `Tests/UI/Mixer/MixerMeterScaleTests.cpp` | `meterLinearToDb`/`meterDbToFraction` boundaries: -60, -18, -6, 0, +3 dBFS and a clamp above +3; a NaN/negative/zero linear input floors to `kMeterMinDb` |
| `Tests/UI/Mixer/MeterColourStopsTests.cpp` | zone selection at the exact boundaries (-18, -6, 0 dBFS) and just below/above each; built from a theme's four tokens, never a code literal; `forEachBand` splits a range into its constituent zone bands at those same boundaries (a bar reaching +4 dB yields four bands stacked low->mid->high->clip, one reaching only -10 dB yields low + a partial mid band and nothing past it); `setStops`/the vector constructor with 1, 2 and 6 stops, and with unsorted, duplicate-`dbFrom` input (sorts, dedups to the first-listed of a tie, never ends up empty) |
| `Tests/UI/Mixer/MixerMeterPaintTests.cpp` | `MixerMeter::paint` actually draws POSITIONAL bands, not one flat colour, for a bar spanning multiple zones -- pixel-sampled off an offscreen render at the low-zone and high-zone heights of the same bar |
| `Tests/UI/Mixer/MixerMeterBallisticsTests.cpp` | attack is instant (a louder input snaps `displayedDb` up in one call); release falls at `kMeterReleaseDbPerSecond` scaled by the elapsed time passed in; the peak-hold line snaps to a new peak instantly, holds for `kMeterPeakHoldSeconds` once nothing louder arrives, then falls at `kMeterPeakHoldFallDbPerSecond` -- every case drives `advanceMeterBallistics` with explicit elapsed-time arguments, never a wall clock |
| `Tests/UI/Mixer/MixerMeterReadoutTests.cpp` | turns the clip colour once a peak exceeds 0 dBFS and stays that colour across later quiet ticks; `reset()` clears both the max and the clip colour; a real mouse click (`mouseUp`, no modifiers) resets the readout; an Alt-modifier click fires `onResetAllRequested` instead of resetting itself |
| `Tests/UI/Mixer/MixerColumnComponentMeterTests.cpp` | a column's readout reaches through `MixerPanelComponent::resetAllMeterReadouts()` -- an Alt-click on ONE column's readout resets every strip column's AND Master's; the mixer dock's "Reset Meters" button (next to "+ Bus") does the same |
| `Tests/UI/Theme/ThemeMeterZoneTests.cpp` | `meterMid`/`meterHigh`/`meterClip` parse from JSON, fall back to `Theme.h`'s defaults when the keys are absent, a malformed value rejects the whole theme, and all four built-ins populate the three tokens distinctly (round-trip through `serialiseTheme`/`parseTheme` is `ThemeTests.cpp`'s existing `JsonRoundTrip` case, extended) |
| `Tests/Mixer/ChannelFlow/ChannelFlowTrackChannelLinkTests.cpp` (updated) | the channel chip's meter now displays a fraction of the dB scale, not the raw linear peak (`TheMeterTickReportsTheChannelsRealLevelThroughTheCheapRead`); the per-reader latch's two independent slots (`Mixer` vs `TrackHeader`) are exercised across the render/solo cases |

**PNG render-to-file inspection.** `MixerColumnComponentMeterTests.cpp`'s
`ClippedMeterRendersToPngForVisualInspection` follows the same convention as
`ModuleComponentLayoutTests.cpp`'s `AdsrCardRendersToPngForVisualInspection` (`docs/testing.md`'s
"Test the real mouse path" neighbourhood): a strip driven hot enough that its clip readout shows
the clip colour and its bars sit in the clip zone, painted offscreen unconditionally (so the
meaningful-content assertions always run), and written to disk only when
`MIXER_METER_CLIP_PNG=<path>` is set in the environment -- `GTEST_SKIP()` otherwise, so CI never
depends on writing a file.

See also [`testing.md`](testing.md) and [`theming.md`](theming.md)'s token table for `meterFill`/
`meterMid`/`meterHigh`/`meterClip`.
