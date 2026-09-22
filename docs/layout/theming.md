# Theming

Agent Synth ships four built-in themes and a JSON theming system for user-authored ones. This doc is
the token reference and the mechanism; writing a theme file is
[theme-authoring](theme-authoring.md), the SVG icon set is [icons](icons.md), and the sparse user
colour overrides layered on top of the tokens are [colour-overrides](colour-overrides.md) and
[cables](cables.md#user-overrides).

**Built-in themes** are compiled into the app and cannot be deleted or modified:

| Theme | Style | Personality |
|---|---|---|
| **Obsidian Studio** | Flat | Dark, restrained — sharp edges, subtle shadows |
| **Neon Lab** | Glass | Translucent surfaces, neon glow, glassmorphism |
| **Warm Console** | Textured | Amber tones, brushed-metal striation overlay |
| **Daylight Studio** | Flat | Light, clean — indigo accent, subtle drop shadow (`id: "daylight"`, `isDark: false`) |

`synth::theme::builtInThemes()` (`Source/UI/Theme/BuiltInThemes.cpp`) returns them in that order.

**User themes** live in the user themes folder as `*.gtheme.json` files, loaded at launch and
whenever **Reload Themes** is pressed in the Appearance tab.

`Settings -> Appearance` switches theme: clicking any row applies it instantly, and the selection
persists across launches.

## Colours

Tokens are semantic names for every colour, metric and font the UI uses. Values are strings in
`"#AARRGGBB"` (alpha-first hex) format, for example `"#FF00D1FF"` for fully-opaque cyan.
`"#RRGGBB"` (implied full alpha) and `"#RGB"` (3-digit shorthand, each digit doubled, full alpha)
are also accepted.

| Token | Default (Obsidian) | Meaning |
|---|---|---|
| `bg0` | `#FF0B0D10` | Deepest background — window and panel fill |
| `bg1` | `#FF13161B` | Canvas / graph editor background |
| `surface` | `#FF1B1F26` | Module card / panel fill |
| `surfaceHi` | `#FF232833` | Raised surface — card top gradient stop, header band |
| `border` | `#FF2A2F38` | Hairline borders |
| `accent` | `#FF00D1FF` | Primary accent — selection glow, value arc, active states |
| `accent2` | `#FF00D1FF` | Secondary accent (Neon: magenta against cyan) |
| `audioWire` | `#FFE8EDF2` | Audio signal connection wires |
| `midiWire` | `#FFB48EF5` | MIDI note/event wires |
| `modWire` | `#FF00D1FF` | Modulation CV wires (DirectCV / attenuverter chains) |
| `pitchWire` | `#FFAAD4FF` | Poly pitch fan wires (port role Pitch) |
| `gateWire` | `#FFFFA500` | Poly gate fan wires (port role Gate) |
| `polyBusWire` | `#FF00E5FF` | Collapsed poly ModCV bus wires (`RoutingKind::PolyBus`) |
| `textPrimary` | `#FFEAEEF3` | Primary UI text |
| `textMuted` | `#FF8A93A0` | Secondary / label text |
| `textDisabled` | `#FF5C6470` | Disabled / bypassed text |
| `success` | `#FF46C66B` | Activity LED / OK indicator |
| `warning` | `#FFE0A33D` | Warning / mute-pending state |
| `error` | `#FFE5484D` | Error / muted state |
| `knobBody` | `#FF13161B` | Knob body gradient inner stop |
| `knobPointer` | `#FFEAEEF3` | Knob pointer line |
| `meterFill` | `#FF00D1FF` | Meter LOW zone fill, below -18 dBFS — kept under this name for back-compat with themes saved before the zone model. Consumers: `synth::ui::MixerMeter` and `ChannelChipComponent` |
| `meterMid` | `#FFFFD43B` | Meter MID zone fill, -18 to -6 dBFS (`Source/UI/Mixer/MeterColourStops.h`) |
| `meterHigh` | `#FFFF922B` | Meter HIGH zone fill, -6 to 0 dBFS |
| `meterClip` | `#FFFF4D4F` | Meter CLIP zone fill, above 0 dBFS — also the clip readout's "clipped" text colour (`MixerMeterReadout`) |
| `modRingPositive` | `#FF00E5FF` | Modulation ring, positive modulation |
| `modRingNegative` | `#FFFF6E00` | Modulation ring, negative modulation |
| `toolActive` | `#FF00D1FF` | Timeline edit-tool strip — active-tool button highlight |
| `midiMapped` | `#FFB48EF5` | MIDI Learn "mapped" badge on a control with an assignment ([`midi-remote-ui.md`](../control/midi-remote-ui.md#the-learn-interaction)) |
| `noteFill` | `#FFB48EF5` | Piano-roll note body, unselected — see [colour-overrides](colour-overrides.md#note-colours) |
| `noteBorder` | `#FF4A3B75` | Piano-roll note outline, unselected — deliberately a distinct literal, not a `.darker()` derivation of `noteFill` |
| `noteSelected` | `#FF00D1FF` | Piano-roll note border/highlight when selected |
| `noteOutOfScale` | `#FFFF6B57` | Piano-roll note fill when Scale Assist flags it outside the active scale — a warning colour, winning over any per-pitch-class override or `noteFill` |
| `pianoKeyWhite` | `#FFEDEFF3` | Piano-roll keys column — white-key fill |
| `pianoKeyBlack` | `#FF15171C` | Piano-roll keys column — black-key fill |
| `trackMuteOn` | `#FFFFA033` | Timeline track header — `M` button, active state |
| `trackSoloOn` | `#FFFFD23D` | Timeline track header — `S` button, active state |
| `trackArmOn` | `#FFE5484D` | Timeline track header — `R` (arm/record) button, active state |

**Required minimum:** `bg0`, `surface`, `accent`, `textPrimary`, `audioWire`, `modWire`. Every other
colour token is optional and falls back to the Obsidian defaults above. `ThemeLoader::parseTheme`
treats each as an independent optional key (`parseColourKey(..., required=false,
defaults.<token>)`), so a theme JSON written before a token existed keeps loading unchanged and
simply renders that surface in Obsidian's colours until the theme author opts in.

**Why `toolActive`, `accent2` and `noteSelected` repeat `accent`'s literal rather than deriving from
it.** No token in this table dynamically re-reads another token's *live* value at construction, so
each is a static default; a theme that changes `accent` and wants them to follow must set them too.

### cableCategory

A nested object under `colors`, used when cable colouring is set to **By source module** (see
[cables](cables.md#colour-resolution)). Wholly optional, and optional per key — an absent entry
keeps the Obsidian default, so a theme can recolour just the categories it cares about.

| Key | Obsidian default | Modules |
|---|---|---|
| `sources` | `#FFFFB454` | Oscillator, Noise, LFO |
| `sequencing` | `#FFC792EA` | Sequencer, Poly Sequencer, MidiKeyboard, Poly MIDI, External MIDI |
| `envelopes` | `#FF7FD962` | ADSR, VCA |
| `filters` | `#FF4FC1FF` | Filter |
| `modfx` | `#FFFF7AB2` | Chorus, Phaser, Flanger, Distortion, Bitcrusher, Ring Modulator |
| `timefx` | `#FF56D4C0` | Delay, Reverb |
| `dynamics` | `#FFF07178` | Compressor, Limiter |
| `utility` | `#FFA0A8B4` | Voice Mixer, Attenuverter |

```json
"colors": {
  "cableCategory": { "filters": "#FF4FC1FF", "timefx": "#FF56D4C0" }
}
```

**These key names are stable identifiers.** They also appear in the user's settings file as part of
a cable-colour override key. They are defined once in `kCableCategoryIds` (`Theme.h`) and read from
there by both `ThemeLoader` and `CableColour.h`, so the two cannot drift. Never rename a shipped id;
change the display label instead.

## Metrics

User-overridable metrics, all optional:

| Token | Default | Meaning |
|---|---|---|
| `cornerRadius` | `10.0` | Module card / panel corner radius (px) |
| `windowRadius` | `14.0` | Top-level window corner radius (cosmetic; a native title bar is in use) |
| `pillRadius` | `8.0` | Button / pill corner radius (px) |
| `padding` | `14` | Card body internal padding (px) |
| `spacingUnit` | `6` | Base grid spacing unit (px) |
| `knobTrackWidth` | `4.0` | Rotary track and value arc stroke width (px) |
| `knobRingWidth` | `3.5` | Outer modulation ring stroke width (px) |
| `borderWidth` | `1.0` | Hairline border stroke width (px) |
| `wireCoreWidth` | `2.5` | Connection wire core stroke width (px) |
| `wireCasingWidth` | `5.0` | Connection wire dark underlay stroke width (px) |

**The rest of `Metrics` is code-only.** These fields govern structural chrome and visual-effect
constants and are **not parsed from user JSON** — `ThemeLoader` silently ignores them, as
unknown-key forward compatibility. Their values come from the C++ struct defaults in
`Source/UI/Theme/Theme.h` only. This is the one authoritative table; the consuming docs link here
rather than repeating it.

| Token | Default | Meaning |
|---|---|---|
| `gridSize` | `8` | Snap quantum, and the alignment-guide activation radius (px) |
| `guideAlpha` | `0.7` | Alignment guide line opacity (0.0-1.0) |
| `guideLineWidth` | `1.5` | Alignment guide stroke width (px) |
| `cornerRadiusSmall` | `4.0` | Pill / small element corner radius (px) |
| `toolbarHeight` | `44` | Toolbar strip height (px) — sized so an 18 px icon and an 11 px label both fit; at 36 `DrawableButton`'s built-in `min(16, 25% of height)` split starved the label to 7-9 px |
| `statusBarHeight` | `24` | Status bar strip height (px) |
| `controlPadding` | `4` | Inset around toolbar buttons (px) |
| `minWindowWidth` | `480` | Narrow-mode breakpoint, and the minimum window width (px) |
| `minWindowHeight` | `400` | Minimum window height reference (px) |
| `sidebarCollapsedWidth` | `0` | Library panel width when hidden (px) |
| `librarySidebarWidth` | `200` | Library panel width when visible (px) |
| `aiPanelWidth` | `300` | AI panel width when visible (px) |
| `iconSize` | `16` | Icon render size in library and status-bar contexts (px) |
| `timelinePanelHeight` | `220` | Timeline panel **default** height and minimum drag height (px) — the live height is the user's, persisted under the `timelinePanelHeight` setting key (see [timeline](../timeline/timeline.md#panel-height)) |
| `timelineTrackHeaderWidth` | `190` | Timeline track-header column width (px) — wide enough that the M/S/R/A toggle row does not crush the name label when a track's automation button is visible |
| `timelineTransportBarHeight` | `34` | Timeline transport-bar strip height (px) — sized so `TimelineTransportBar`'s 26 px glyph buttons are not clamped back down by the strip |
| `timelineRulerHeight` | `30` | Timeline ruler strip height, top of the lanes region (px) — the strip carries TWO tiled rows, the bar/beat numbers (17 px) and the marker band (13 px); at 24 the marker flag was squeezed to 9 px |
| `timelineTrackRowHeight` | `56` | The row height BOTH the track-header column and the clip-lane area lay their rows out at — the single source keeping header rows and clip rows aligned. `TimelineTrackHeaderComponent::kRowHeight` is only the headless literal fallback and is kept equal to this default. |
| `timelineAutomationStripHeight` | `72` | The automation strip docked at the BOTTOM of the panel's lanes region (px) — the clip-lane area and the piano roll shrink by exactly this much while the strip is open |

The chrome these govern is [chrome](chrome.md); the alignment-guide constants are
[layout](layout.md#alignment-guides).

## Typography

All optional:

| Token | Default | Meaning |
|---|---|---|
| `uiFamily` | `"Inter"` | Sans-serif family name for UI labels and text |
| `monoFamily` | `"JetBrains Mono"` | Monospace family for value readouts |
| `h1` | `18.0` | Large heading / window title font size (pt) |
| `h2` | `13.0` | Card title font size (rendered uppercase and tracked) |
| `label` | `10.5` | Knob name / port label / section heading font size |
| `value` | `10.0` | Mono value readout font size |
| `micro` | `8.5` | Smallest caption font size |

Six font families are embedded (SIL OFL): **Inter**, **Manrope**, **IBM Plex Sans** for UI, and
**JetBrains Mono**, **Space Mono**, **IBM Plex Mono** for mono. A user theme may request any of them
by name; any other name falls back to the JUCE system sans-serif.

**Themes never swap font families.** All four built-in themes deliberately use Inter plus JetBrains
Mono. Swapping the embedded typeface *family* at runtime — picking a theme whose font differs from
the currently-loaded one — corrupts text rendering globally on JUCE 8 with macOS CoreText:
already-rendered glyph runs get mis-indexed against the new typeface. Themes therefore differ by
**colour, treatment and glow**, not font.

A user theme may still set a different `uiFamily` / `monoFamily`; that font renders correctly when
the theme is the one **active at app launch**, but live-switching *to* a different-font theme
garbles text until the app is relaunched. Keep `uiFamily` / `monoFamily` at the defaults unless the
theme is meant to ship as a launch default.

**Typeface pre-creation is what keeps a same-font switch clean.** `AppLookAndFeel`'s constructor
pre-creates embedded typefaces for all six families and populates the per-instance `typefaceCache`
before any text is rendered. Creating an embedded typeface for the first time via
`juce::Typeface::createSystemTypefaceFor` *after* the app has already rendered with another font is
what triggers the corruption; pre-loading means `getTypefaceForFont` only ever returns cached
instances, so a live theme switch never triggers a runtime typeface creation.

## Treatment

All optional. The `treatment` object controls the surface rendering style of module cards and wires;
each parameter's visual effect is described in
[theme-authoring](theme-authoring.md#treatment-parameters).

| Token | Default | Type | Meaning |
|---|---|---|---|
| `style` | `"flat"` | string | Surface style: `"flat"`, `"glass"` or `"textured"` |
| `glow` | `0.0` | float `[0,1]` | Accent glow strength — neon wire bloom, selection halo |
| `shadow` | `0.6` | float `[0,1]` | Drop-shadow strength under cards |
| `blur` | `0.0` | float `[0,1]` | Glass frost highlight strength (NOT a live blur) |
| `texture` | `0.0` | float `[0,1]` | Brushed-metal striation overlay opacity |

## Contrast

`textPrimary` on `bg0` must meet **WCAG 2.x Level AA**, a contrast ratio of at least 4.5. The
built-in themes all target 7 or above (AAA).

```
lin(c) = c <= 0.03928 ? c/12.92 : ((c+0.055)/1.055)^2.4   // c is a channel in [0,1]
L = 0.2126*lin(R) + 0.7152*lin(G) + 0.0722*lin(B)
ratio = (max(L1,L2) + 0.05) / (min(L1,L2) + 0.05)
```

`Tests/UI/Theme/ThemeTests.cpp` enforces `ratio >= 4.5` for every built-in automatically.

## Where files live, and how reload works

| Location | Description |
|---|---|
| `themes/obsidian.gtheme.json` | Reference/example JSON, shipped in-repo for docs and round-trip tests. NOT loaded at runtime. |
| `assets/fonts/*.ttf` | Embedded font files (SIL OFL). Only the app target links them; tests use JUCE default fonts. |
| The user themes folder | `*.gtheme.json` files placed here are loaded at startup and on Reload Themes — paths in [theme-authoring](theme-authoring.md#find-the-user-themes-folder) |

**At startup** the built-in themes are registered first (obsidian, neon, warm, daylight), then user
themes are loaded from the folder. If two user themes share the same `id`, the second file overwrites
the first in the list.

**On reload**, pressing Reload Themes clears previously-loaded user themes, preserves the built-ins,
and re-scans the folder. If the active theme was a user theme that has since been deleted, the
manager falls back to Obsidian and broadcasts a change.

**Persistence**: the active theme `id` is stored under the key `"themeId"` in the shared
`ApplicationProperties` — the same settings file used for audio device and AI preferences. No second
file is created.

**A theme change triggers exactly one re-skin pass**: `applyTheme()` remaps all JUCE ColourIds,
`sendLookAndFeelChangeMessage()` propagates `lookAndFeelChanged()` to all child components, then a
single `repaint()` is requested. Because module cards are buffered through
`synth::ui::ZoomFrozenCachedImage` (see [rendering](rendering.md)), only their cached images are
invalidated and re-rendered once. No animation loop or per-tick repaint is added.

## Themed widgets

These stock-widget overrides are implemented in `AppLookAndFeel`:

- **ComboBox** — `drawComboBox` (pressed/disabled/focused states, drawn chevron arrow),
  `drawComboBoxTextWhenNothingSelected` (muted placeholder text).
- **PopupMenu** — `drawPopupMenuItem` (separator hairline, highlight fill, drawn tick checkmark,
  submenu chevron, disabled dim). It also paints a 14x14 waveform glyph left of the item text for
  waveform combos; `drawComboBox` renders the selected waveform glyph in the closed combo, and
  `positionComboBoxText` shifts the label right when the selected item carries a glyph.
- **ScrollBar** — `getDefaultScrollbarWidth()` returns 6 px; `drawScrollbar` (slim track and thumb
  with hover and press states); `drawScrollbarButton` (triangle arrows, for Windows and Linux
  parity).
- **TabbedButtonBar** — `drawTabbedButtonBarBackground` (bg0 fill plus a hairline at the
  content-facing edge per orientation); `drawTabButton` (active tab: rounded top corners plus an
  accent indicator; hover: surface tint; inactive: border hairline). `tabOutlineColourId` is mapped
  in `applyTheme()`.
- **ListBox** — `backgroundColourId`, `textColourId` and `outlineColourId` mapped in `applyTheme()`.
- **TooltipWindow** — a single `juce::TooltipWindow` is owned by `MainComponent` (member
  `tooltipWindow{ this }`). The tooltip `ColourIds` and the `drawTooltip` override live in
  `AppLookAndFeel`; this instance is what causes them to take effect. Feature code calls
  `setTooltip()` on individual controls. **No second `TooltipWindow` should be created anywhere
  else.**

**MidiKeyboardComponent is themed by the card, not by `applyTheme()`.** `juce_audio_utils` is not
linked into `Core` — it would bloat the headless test binary — so `AppLookAndFeel::applyTheme()`
cannot map `MidiKeyboardComponent` ColourIds. The on-screen keyboard is themed instead by
`ModuleComponent::applyKeyboardThemeColours()`, at card construction and again from
`lookAndFeelChanged()` on every theme switch, using the same `bg1` / `surfaceHi` / `border` /
`accent` / `textPrimary` tokens the piano-roll key column uses.

The assistant panel's chat bubbles and debug console still pick their colours from hardcoded
literals rather than tokens, so they do not re-skin with the rest of the app.

## Implementation notes

**`Theme.h`** (`Source/UI/Theme/Theme.h`) has **no JUCE GUI dependencies** beyond `juce::Colour` and
`juce::String`, pulled in via `juce_graphics`, which makes it fully headless-testable without
linking the UI modules. It also defines the `ThemeStyle` enum (`Flat`, `Glass`, `Textured`) used by
`Treatment::style`.

**`ThemeLoader`** (`Source/UI/Theme/ThemeLoader.h`) exposes these public helpers beyond the main
`parseTheme` / `themeToJson` pair:

| Method | Description |
|---|---|
| `parseHexColour(const juce::String&)` | Parses `"#RGB"` / `"#RRGGBB"` / `"#AARRGGBB"` into `std::optional<juce::Colour>`; `nullopt` on malformed input |
| `parseStyle(const juce::String&)` | Maps `"flat"` / `"glass"` / `"textured"` (case-insensitive) into `std::optional<ThemeStyle>` |
| `styleToString(ThemeStyle)` | The reverse: a canonical lowercase string for serialization |
| `getLastError()` | The reason from the most recent `parseTheme` failure on the calling thread, empty on success — used by callers to emit the single "Skipped ..." log line |

**Knob sweep arc constants.** `AppLookAndFeel` declares two `static constexpr float` constants
defining the 270-degree rotary sweep shared by knob drawing (`drawRotarySlider`) and modulation ring
drawing (`drawModulationRing`):

```cpp
static constexpr float kRotaryStart = -juce::MathConstants<float>::pi * 0.75f; // about -135 degrees
static constexpr float kRotaryEnd   =  juce::MathConstants<float>::pi * 0.75f; // about +135 degrees
```

**Both helpers read these constants directly** instead of using the `rotaryStartAngle` /
`rotaryEndAngle` arguments JUCE passes, so the ring and the knob arc are always co-aligned
regardless of what the `Slider` component was configured with.
