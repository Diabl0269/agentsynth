# Theming Guide

Agent Synth ships with four built-in themes and a JSON-based theming system that lets you create
and share your own visual styles.

---

## 1. Overview

**Built-in themes** are compiled into the app; they cannot be deleted or modified.

| Theme | Style | Personality |
|---|---|---|
| **Obsidian Studio** | Flat | Dark, restrained — sharp edges, subtle shadows |
| **Neon Lab** | Glass | Translucent surfaces, neon glow, glassmorphism |
| **Warm Console** | Textured | Amber tones, brushed-metal striation overlay |
| **Daylight Studio** | Flat | Light, clean — indigo accent, subtle drop shadow (`id: "daylight"`, `isDark: false`) |

**User themes** live in the user themes folder as `*.gtheme.json` files. They are loaded at
launch and whenever you press **Reload Themes** in the Appearance tab.

### How to switch

`Settings → Appearance` (keyboard shortcut: the same as **Open Settings** → navigate to the
Appearance tab). Click any row to apply the theme instantly. The selection persists across
launches.

---

## 2. Token reference

Tokens are semantic names for every colour, metric, and font used by the UI. Copy-paste these
into your JSON theme to override them.

### Colours (`colors`)

All values are strings in `"#AARRGGBB"` (alpha-first hex) format, e.g. `"#FF00D1FF"` for
fully-opaque cyan. You may also use `"#RRGGBB"` (implied full alpha) or `"#RGB"` (3-digit
shorthand, each digit doubled, full alpha).

| Token | Default (Obsidian) | Meaning |
|---|---|---|
| `bg0` | `#FF0B0D10` | Deepest background — window / panel fill |
| `bg1` | `#FF13161B` | Canvas / graph editor background |
| `surface` | `#FF1B1F26` | Module card / panel fill |
| `surfaceHi` | `#FF232833` | Raised surface — card top gradient stop, header band |
| `border` | `#FF2A2F38` | Hairline borders |
| `accent` | `#FF00D1FF` | Primary accent — selection glow, value arc, active states |
| `accent2` | `#FF00D1FF` | Secondary accent (Neon: magenta vs cyan) |
| `audioWire` | `#FFE8EDF2` | Audio signal connection wires |
| `midiWire` | `#FFB48EF5` | MIDI note/event wires |
| `modWire` | `#FF00D1FF` | Modulation CV wires (DirectCV / attenuverter chains) |
| `pitchWire` | `#FFAAD4FF` | Poly pitch fan wires (port role = Pitch) |
| `gateWire` | `#FFFFA500` | Poly gate fan wires (port role = Gate) |
| `polyBusWire` | `#FF00E5FF` | Collapsed poly ModCV bus wires (RoutingKind::PolyBus) |
| `textPrimary` | `#FFEAEEF3` | Primary UI text |
| `textMuted` | `#FF8A93A0` | Secondary / label text |
| `textDisabled` | `#FF5C6470` | Disabled / bypassed text |
| `success` | `#FF46C66B` | Activity LED / OK indicator |
| `warning` | `#FFE0A33D` | Warning / mute-pending state |
| `error` | `#FFE5484D` | Error / muted state |
| `knobBody` | `#FF13161B` | Knob body gradient inner stop |
| `knobPointer` | `#FFEAEEF3` | Knob pointer line |
| `meterFill` | `#FF00D1FF` | Output meter fill (top of gradient) |
| `modRingPositive` | `#FF00E5FF` | Modulation ring, positive modulation |
| `modRingNegative` | `#FFFF6E00` | Modulation ring, negative modulation |
| `toolActive` | `#FF00D1FF` | Timeline edit-tool strip — active-tool button highlight. Defaults to the same literal as `accent`'s Obsidian default (no token in this table dynamically re-reads another token's *live* value at construction — `accent2` is the closest precedent and it likewise just repeats `accent`'s literal — so this is a static default, not a derived one) |

**Required minimum:** `bg0`, `surface`, `accent`, `textPrimary`, `audioWire`, `modWire`.
All other colour tokens are optional and fall back to the Obsidian defaults listed above.

#### `cableCategory` — cable colours by module category

A nested object under `colors`, used when cable colouring is set to **By source module**
(see [§11 Cable colours](#11-cable-colours)). Wholly optional, and optional per key — an absent entry
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

These key names are **stable identifiers** — they also appear in the user's settings file as part
of a cable-colour override key. They are defined once in `kCableCategoryIds` (`Theme.h`) and read
from there by both `ThemeLoader` and `CableColour.h`, so the two cannot drift. Never rename a
shipped id; change the display label instead.

### Metrics (`metrics`) — all optional

| Token | Default | Meaning |
|---|---|---|
| `cornerRadius` | `10.0` | Module card / panel corner radius (px) |
| `windowRadius` | `14.0` | Top-level window corner radius (cosmetic) |
| `pillRadius` | `8.0` | Button / pill corner radius (px) |
| `padding` | `14` | Card body internal padding (px) |
| `spacingUnit` | `6` | Base grid spacing unit (px) |
| `knobTrackWidth` | `4.0` | Rotary track + value arc stroke width (px) |
| `knobRingWidth` | `3.5` | Outer modulation ring stroke width (px) |
| `borderWidth` | `1.0` | Hairline border stroke width (px) |
| `wireCoreWidth` | `2.5` | Connection wire core stroke width (px) |
| `wireCasingWidth` | `5.0` | Connection wire dark underlay stroke width (px) |

The following `Metrics` fields are **code-only layout constants** — they control the app's structural chrome dimensions and are **not parsed from user JSON**. A user theme that includes these keys will have them silently ignored. Their values are fixed in the C++ struct defaults.

| Token | Default | Meaning |
|---|---|---|
| `toolbarHeight` | `36` | Toolbar strip height (px) — code-only |
| `statusBarHeight` | `24` | Status bar strip height (px) — code-only |
| `controlPadding` | `4` | Inset around toolbar buttons (px) — code-only |
| `minWindowWidth` | `480` | Narrow-mode breakpoint / minimum window width (px) — code-only |
| `minWindowHeight` | `400` | Minimum window height reference (px) — code-only |
| `sidebarCollapsedWidth` | `0` | Library panel width when hidden (px) — code-only |
| `librarySidebarWidth` | `200` | Library panel width when visible (px) — code-only |
| `aiPanelWidth` | `300` | AI panel width when visible (px) — code-only |
| `iconSize` | `16` | Icon render size in library / status bar contexts (px) — code-only |
| `timelinePanelHeight` | `220` | Timeline panel **default** height and minimum drag height (px) — the live height is the user's, persisted under the `timelinePanelHeight` setting key (see [`layout.md` §16](layout.md)) — code-only (TL5-1) |
| `timelineTrackHeaderWidth` | `160` | Timeline track-header column width (px) — code-only (TL5-1) |
| `timelineTransportBarHeight` | `28` | Timeline transport-bar strip height (px) — code-only (TL5-1) |
| `timelineRulerHeight` | `24` | Timeline ruler strip height, top of the lanes region (px) — code-only (TL5-2) |
| `timelineTrackRowHeight` | `56` | Row height shared by the track-header column and the clip-lane area (px) — code-only (TL5-7) |

The following `Metrics` fields are **UI rendering constants** — they control the appearance of UI visual effects and are also **not parsed from user JSON**. A user theme that includes these keys will have them silently ignored.

| Token | Default | Meaning |
|---|---|---|
| `gridSize` | `8` | Snap quantum / alignment guide activation radius (px) — code-only |
| `guideAlpha` | `0.7` | Alignment guide line opacity (0.0–1.0) — code-only |
| `guideLineWidth` | `1.5` | Alignment guide stroke width (px) — code-only |
| `cornerRadiusSmall` | `4.0` | Pill / small element corner radius (px) — code-only |

### Typography (`typography`) — all optional

| Token | Default | Meaning |
|---|---|---|
| `uiFamily` | `"Inter"` | Sans-serif family name for UI labels and text |
| `monoFamily` | `"JetBrains Mono"` | Monospace family for value readouts |
| `h1` | `18.0` | Large heading / window title font size (pt) |
| `h2` | `13.0` | Card title font size (rendered uppercase + tracked) |
| `label` | `10.5` | Knob name / port label / section heading font size |
| `value` | `10.0` | Mono value readout font size |
| `micro` | `8.5` | Smallest caption font size |

Six font families are embedded (SIL OFL): **Inter**, **Manrope**, **IBM Plex Sans** (UI) and
**JetBrains Mono**, **Space Mono**, **IBM Plex Mono** (mono). A user theme may request any of
them by name.

> ⚠️ **Runtime font-switching limitation.** All three *built-in* themes deliberately use
> **Inter + JetBrains Mono**. Swapping the embedded typeface *family* at runtime (i.e. picking a
> theme whose font differs from the currently-loaded one) corrupts text rendering globally on
> JUCE 8 + macOS/CoreText — already-rendered glyph runs get mis-indexed against the new
> typeface. The themes therefore differ by **colour, treatment, and glow**, not font. A user
> theme may still set a different `uiFamily`/`monoFamily`; that font renders correctly when the
> theme is the one **active at app launch**, but live-switching *to* a different-font theme will
> garble text until the app is relaunched. Keep `uiFamily`/`monoFamily` as the defaults unless
> you intend to ship the theme as a launch default.
Any other name falls back to the JUCE system sans-serif.

> **Implementation note — typeface pre-creation.** `AppLookAndFeel`'s constructor
> pre-creates embedded typefaces for all six built-in font families (Inter, JetBrains Mono,
> Manrope, Space Mono, IBM Plex Sans, IBM Plex Mono) and populates the per-instance
> `typefaceCache` before any text is rendered. Creating an embedded typeface for the first time
> via `juce::Typeface::createSystemTypefaceFor` *after* the app has already rendered with another
> font is what triggers the JUCE 8 + CoreText global text corruption. Pre-loading all typefaces
> at startup means `getTypefaceForFont` only ever returns cached instances — a live theme switch
> never triggers a runtime typeface creation, so font switching stays clean.

### Treatment (`treatment`) — all optional

| Token | Default | Type | Meaning |
|---|---|---|---|
| `style` | `"flat"` | string | Surface style: `"flat"`, `"glass"`, or `"textured"` |
| `glow` | `0.0` | float `[0,1]` | Accent glow strength — neon wire bloom, selection halo |
| `shadow` | `0.6` | float `[0,1]` | Drop-shadow strength under cards |
| `blur` | `0.0` | float `[0,1]` | Glass frost highlight strength (NOT live blur) |
| `texture` | `0.0` | float `[0,1]` | Brushed-metal striation overlay opacity |

---

## 3. Icon Tinting

Agent Synth uses SVG `Drawable` icons — **not** an icon or glyph font. This avoids the JUCE 8 / CoreText runtime font-family swap corruption described in the typography section above.

### Icon enum

36 icons are defined in `synth::theme::Icon` (in `Source/UI/Theme/IconLibrary.h`):

```
TransportPlay    TransportStop    ActionUndo       ActionRedo
ActionSave       ActionLoad       ActionNew        ActionSettings
ActionAutoArrange ToggleAI        ToggleMatrix     ToggleLibrary
ThemeToggle      ModuleBypass     ModuleMute       ModuleDelete
CatSources       CatSequencing    CatEnvelopes     CatFilters
CatModulationFX  CatTimeFX        CatDynamics      CatUtility
WaveformSine     WaveformSaw      WaveformSquare   WaveformTriangle
ToggleMinimap    ModuleDualIO
ToolSelect       ToolSplit        ToolGlue         ToolErase
ToolMute         ToolDraw
```

**`Icon::TransportPlay` is scaffolding** — the SVG asset is present and the enum value exists, but no `DrawableButton` is wired to it. It is tinted to `textMuted` and reserved for a future transport affordance.

The four **waveform icons** (`WaveformSine`, `WaveformSaw`, `WaveformSquare`, `WaveformTriangle`, indices 24–27) are rendered in the Oscillator waveform combo via `AppLookAndFeel::drawPopupMenuItem` (14×14 glyph left of the item text) and `drawComboBox` (selected waveform glyph in the closed combo). `positionComboBoxText` shifts the label right when the selected item has an icon.

**`Icon::ToggleMinimap`** (index 28) is the toolbar toggle for the Graph Editor minimap overlay — see [`layout.md` §15](layout.md#15-minimap-overlay-issue-159).

**`Icon::ModuleDualIO`** (index 29) is the module-header toggle that splits a collapsed `"Audio"` jack into separate Left/Right jacks. There is no universal stereo-split glyph; this one is a Y-fork into two jacks. The button's tooltip carries the Dual I/O on/off copy. See [`fx_modules.md` § Stereo I/O](fx_modules.md#stereo-io-dual-io-toggle).

**`Icon::ToolSelect` … `Icon::ToolDraw`** (indices 30–35) are the six glyphs for the timeline edit-tool strip (`synth::ui::EditTool` — Select, Split, Glue, Erase, Mute, Draw; see [`Source/UI/EditTool.h`](../Source/UI/EditTool.h)): a pointer arrow, scissors, a glue bottle, an angled eraser block, a crossed-out speaker (visually distinct from `ModuleMute` — that one is an outlined speaker with a small corner X; this one is a solid-filled speaker with a single strike-through slash), and a pencil at ~45°. `Source/UI/ToolCursors.h`'s `makeToolCursor()` renders these same tinted Drawables into the custom per-tool mouse cursor shown over the clip lanes / piano roll, rather than shipping a second cursor-only asset — see that header's doc comment for the per-tool hotspot table.

### Token → tint map

`AppLookAndFeel::retintIcons()` assigns tint colours from `theme_.colors`:

| Icons | Tint token |
|---|---|
| `ModuleBypass` | `textMuted` |
| `ModuleMute` | `warning` |
| `ModuleDelete` | `error` |
| `ModuleDualIO` | `textMuted` |
| `TransportPlay` | `textMuted` (scaffolding; no DrawableButton consumer) |
| All toolbar actions + transport stop + toggles | `textPrimary` |
| `ToolSelect` … `ToolDraw` (edit-tool strip) | `textPrimary` — active-tool highlight is a separate `toolActive`-coloured button state, not a different icon tint |
| Category icons (`CatSources` … `CatUtility`) | `textMuted` |
| `WaveformSine`, `WaveformSaw`, `WaveformSquare`, `WaveformTriangle` | `textPrimary` (consumer: Oscillator waveform combo via `drawPopupMenuItem`/`drawComboBox`) |

`retintIcons()` is called from `applyTheme()` — it is part of the single re-skin pass triggered by every theme switch.

### Null-fallback contract

`IconLibrary::getDrawable(id)` returns `nullptr` when the `HAS_FONT_ASSETS` compile flag is absent (e.g. headless test builds without the asset target). All callers must null-check:

```cpp
if (auto d = lf->getIcon(Icon::ModuleBypass))
    bypassButton->setImages(d.get());
// else: button remains blank — acceptable in headless tests
```

`AppLookAndFeel::getIcon()` and `peekIcon()` delegate directly to the `IconLibrary` member and also return `nullptr` when the library has no asset for the requested id.

### Parallel-array design

`IconLibrary` keeps two `std::array<std::unique_ptr<juce::Drawable>, kCount>`:
- `originals_` — loaded once at construction, never mutated (pure-white SVG source)
- `drawables_` — tinted copies (updated by `setTintColour`)

`setTintColour` always clones from `originals_[]` before tinting, so the 2nd and 3rd theme switch produce the correct colour and do not accumulate tints.

### BinaryData symbol naming

JUCE's binary-data name mangler **strips hyphens** and concatenates the remaining tokens. The mapping for icon filenames is therefore:

| Filename | BinaryData symbol |
|---|---|
| `transport-play.svg` | `BinaryData::transportplay_svg` |
| `action-undo.svg` | `BinaryData::actionundo_svg` |
| `cat-modulation-fx.svg` | `BinaryData::catmodulationfx_svg` |
| `action-auto-arrange.svg` | `BinaryData::actionautoarrange_svg` |
| `waveform-sine.svg` | `BinaryData::waveformsine_svg` |
| `waveform-saw.svg` | `BinaryData::waveformsaw_svg` |
| `waveform-square.svg` | `BinaryData::waveformsquare_svg` |
| `waveform-triangle.svg` | `BinaryData::waveformtriangle_svg` |

Note: the spec text says "a-b.svg → `BinaryData::a_b_svg`" — this is incorrect. Hyphens are stripped, not converted to underscores. The `IconLibrary.cpp` lookup table uses the real symbol names. A CMake guard (`file(GLOB)` + `string(FIND ... "_")`) enforces hyphen-only filenames to prevent accidental underscore collisions.

### How to add a new icon

1. Create `assets/icons/<category>-<name>.svg` — 24×24 viewBox, `fill="#FFFFFF"` or `stroke="#FFFFFF" fill="none"`, no gradients / CSS / `<use>` / `<defs>`.
2. Add the enum value to `Icon` in `IconLibrary.h` (before `kCount`).
3. Add the `binaryDataForIcon` entry in `IconLibrary.cpp`'s `kTable` array (same order as enum). Symbol name = strip hyphens + `_svg` suffix.
4. Add a tint assignment in `AppLookAndFeel::retintIcons()`.
5. The `static_assert(std::size(kTable) == (size_t)Icon::kCount, ...)` guards the count — it will fail to compile if step 3 is omitted.
6. Rebuild `Assets` to regenerate `BinaryData.h`.

---

## 4. JSON Schema

File extension: `*.gtheme.json`. Encoding: UTF-8.

### Top-level keys

| Key | Type | Required | Notes |
|---|---|---|---|
| `schemaVersion` | integer | no | Must be `≤ 1` if present; omit or set to `1` |
| `name` | string | **yes** | Display name shown in the Appearance tab |
| `id` | string | no | Stable lookup id (slug); defaults to a slugified version of the filename |
| `colors` | object | **yes** | See token table above; minimum keys required |
| `metrics` | object | no | All keys optional |
| `typography` | object | no | All keys optional |
| `treatment` | object | no | All keys optional |

### Colour format

- `"#AARRGGBB"` — alpha-first, 8 hex digits (e.g. `"#FF00D1FF"` = fully-opaque cyan)
- `"#RRGGBB"` — implied fully-opaque alpha
- `"#RGB"` — 3-digit shorthand, each digit doubled (e.g. `"#0AF"` → `"#FF00AAFF"`)

Lowercase or uppercase hex digits are both accepted.

### Error handling

- Files that fail JSON parsing, or whose root is not an object → skipped, one log line.
- A missing required key, or a malformed colour / style value → skipped, one log line.
- Unknown optional keys → silently ignored (forward-compatible).
- Treatment floats outside `[0,1]` → clamped (not an error).
- `schemaVersion` greater than `1` → skipped with a "newer schema" log message.

---

## 5. Create your own theme

### Step 1: Find the user themes folder

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Agent Synth/Themes` |
| Windows | `%APPDATA%\Agent Synth\Themes` |
| Linux | `~/.config/Agent Synth/Themes` |

You can also click **Open Themes Folder** in `Settings → Appearance` and the folder opens in your
file manager (it is created automatically if it does not exist).

### Step 2: Copy the reference theme

Copy `themes/obsidian.gtheme.json` from the repository (or from the installed app's resources)
into your user themes folder, then rename the file — for example `my-theme.gtheme.json`.

### Step 3: Edit name, id, and colours

Open the file in any text editor. At minimum change `"name"` and (optionally) `"id"`:

```json
{
  "schemaVersion": 1,
  "name": "My Theme",
  "id": "my-theme",
  "colors": {
    "bg0": "#FF0B0D10",
    "bg1": "#FF13161B",
    "surface": "#FF1B1F26",
    "surfaceHi": "#FF232833",
    "border": "#FF2A2F38",
    "accent": "#FF7B61FF",
    "accent2": "#FF7B61FF",
    "audioWire": "#FFE8EDF2",
    "midiWire": "#FFB48EF5",
    "modWire": "#FF7B61FF",
    "pitchWire": "#FFAAD4FF",
    "gateWire": "#FFFFA500",
    "polyBusWire": "#FF7B61FF",
    "textPrimary": "#FFEAEEF3",
    "textMuted": "#FF8A93A0",
    "textDisabled": "#FF5C6470",
    "success": "#FF46C66B",
    "warning": "#FFE0A33D",
    "error": "#FFE5484D",
    "knobBody": "#FF13161B",
    "knobPointer": "#FFEAEEF3",
    "meterFill": "#FF7B61FF",
    "modRingPositive": "#FF7B61FF",
    "modRingNegative": "#FFFF6E00",
    "cableCategory": {
      "sources": "#FFFFB454",
      "sequencing": "#FFC792EA",
      "envelopes": "#FF7FD962",
      "filters": "#FF4FC1FF",
      "modfx": "#FFFF7AB2",
      "timefx": "#FF56D4C0",
      "dynamics": "#FFF07178",
      "utility": "#FFA0A8B4"
    }
  },
  "metrics": {
    "cornerRadius": 10.0, "windowRadius": 14.0, "pillRadius": 8.0,
    "padding": 14, "spacingUnit": 6,
    "knobTrackWidth": 4.0, "knobRingWidth": 3.5, "borderWidth": 1.0,
    "wireCoreWidth": 2.5, "wireCasingWidth": 5.0
  },
  "typography": {
    "uiFamily": "Inter", "monoFamily": "JetBrains Mono",
    "h1": 18.0, "h2": 13.0, "label": 10.5, "value": 10.0, "micro": 8.5
  },
  "treatment": {
    "style": "flat",
    "glow": 0.0, "shadow": 0.6, "blur": 0.0, "texture": 0.0
  }
}
```

### Step 4: Reload and select

In Agent Synth: `Settings → Appearance → Reload Themes`. Your theme appears in the list.
Click it to apply instantly.

If the theme does not appear, check the log for a `[Theme] Skipped ...` error. Common issues:
- Missing required `colors` keys (`bg0`, `surface`, `accent`, `textPrimary`, `audioWire`, `modWire`)
- A colour value that is not valid hex (e.g. a CSS `rgb()` function — use hex instead)
- The file is not valid JSON (missing comma, trailing comma, unquoted key)

---

## 6. Treatment parameters guide

The `treatment` object controls the surface rendering style of module cards and wires.

### `style`

| Value | Visual effect |
|---|---|
| `"flat"` | **Obsidian**: flat fill + soft drop shadow. Clean and efficient. |
| `"glass"` | **Neon**: translucent surface fill + top-edge highlight + optional glow bloom. Glassmorphism look — the graph canvas shows through the cards. Set `surface` alpha < `FF` (e.g. `#991E1238`) to let light through. |
| `"textured"` | **Warm**: gradient fill + subtle vertical striation overlay (brushed metal feel). Keep `texture > 0` to see the effect. |

### `glow` (float, 0–1)

Controls the accent glow bloom applied to:
- Selection halo borders on selected module cards
- Neon wire bloom (an extra wide translucent stroke behind connection wires)
- Jack glow halos

Set to `0` (Obsidian/Warm) for no bloom. Set to `0.85` (Neon) for vivid neon glow.

### `shadow` (float, 0–1)

Drop-shadow strength under module cards. Implemented as a stack of three translucent,
downward-offset, expanding rounded rectangles — **not** `juce::DropShadow`. `juce::DropShadow`
re-rasterizes a per-paint gaussian blur every time a buffered card is re-rendered at a new zoom
scale, which was the dominant cost behind zoom lag. The layered-fill approximation is visually
close and a fraction of the cost (plain fills, no blur), so zooming stays smooth.
`0` = no shadow; `0.6` = Obsidian default.

### `blur` (float, 0–1)

Glass "frost" highlight strength. Controls the opacity of the top-edge highlight gradient
applied to cards in `glass` style. Does NOT perform a real backdrop blur (which is not
available in JUCE and would be too expensive). A value of `0.6` (Neon) gives a strong frosted-
glass highlight.

### `texture` (float, 0–1)

Brushed-metal striation overlay opacity multiplier. Only visible in `textured` style. Controls
how visible the fine vertical hairlines drawn over the card body are. `0` = flat,
`0.55` (Warm) = clearly visible striation texture.

---

## 7. Contrast guidance

For readability, ensure `textPrimary` on `bg0` meets **WCAG 2.x Level AA** (contrast ratio ≥ 4.5).
The built-in themes all target ≥ 7 (AAA).

WCAG contrast ratio formula:

```
lin(c) = c <= 0.03928 ? c/12.92 : ((c+0.055)/1.055)^2.4   // c is channel in [0,1]
L = 0.2126×lin(R) + 0.7152×lin(G) + 0.0722×lin(B)
ratio = (max(L1,L2) + 0.05) / (min(L1,L2) + 0.05)
```

The ThemeTests suite (`Tests/ThemeTests.cpp`, case 15) enforces `ratio ≥ 4.5` for all built-ins
automatically.

---

## 8. Where files live and how reload works

| Location | Description |
|---|---|
| `themes/obsidian.gtheme.json` | Reference/example JSON (shipped in-repo for docs + round-trip tests). NOT loaded at runtime. |
| `assets/fonts/*.ttf` | Embedded font files (SIL OFL). Only the app target links them; tests use JUCE default fonts. |
| User themes folder (see section 4) | `*.gtheme.json` files placed here are loaded at startup and on "Reload Themes". |

**At startup**: built-in themes are registered first (obsidian/neon/warm/daylight), then user
themes are loaded from the folder. If two user themes share the same `id`, the second file
overwrites the first in the list.

**On reload** (pressing "Reload Themes"): previously-loaded user themes are cleared; built-ins
are preserved. Then the folder is re-scanned. If the active theme was a user theme that has since
been deleted, the manager falls back to Obsidian and broadcasts a change.

**Persistence**: the active theme `id` is stored under the key `"themeId"` in the shared
`ApplicationProperties` (the same settings file used for audio device and AI preferences). No
second file is created.

**Theme switch performance**: a theme change triggers exactly one re-skin pass —
`applyTheme()` remaps all JUCE ColourIds, then `sendLookAndFeelChangeMessage()` propagates
`lookAndFeelChanged()` to all child components, then a single `repaint()` is requested.
Because module cards are buffered via `synth::ui::ZoomFrozenCachedImage` (see
[`docs/layout.md` §10](layout.md#10-ui-rendering-performance)), only their cached images are
invalidated and re-rendered once. There is no animation loop or per-tick repaint added.

---

## 9. Status of themed widgets

### Phase 3 completions

The following stock-widget overrides are now fully implemented in `AppLookAndFeel`:

- **ComboBox** — `drawComboBox` (pressed/disabled/focused states, drawn chevron arrow), `drawComboBoxTextWhenNothingSelected` (muted placeholder text)
- **PopupMenu** — `drawPopupMenuItem` (separator hairline, highlight fill, drawn tick checkmark, submenu chevron, disabled dim)
- **ScrollBar** — `getDefaultScrollbarWidth()` returns 6 px; `drawScrollbar` (slim track + thumb with hover/press states); `drawScrollbarButton` (triangle arrows for Win/Linux parity)
- **TabbedButtonBar** — `drawTabbedButtonBarBackground` (bg0 fill + hairline at content-facing edge per orientation); `drawTabButton` (active tab: rounded top corners + accent indicator; hover: surface tint; inactive: border hairline)
- **ListBox** — ColourId mappings (`backgroundColourId`, `textColourId`, `outlineColourId`) added in `applyTheme()`
- **TabbedButtonBar** — `tabOutlineColourId` added in `applyTheme()`

> **MidiKeyboardComponent ColourIds:** `juce_audio_utils` is not linked into `Core` (it would bloat the headless test binary), so `AppLookAndFeel::applyTheme()` cannot map `MidiKeyboardComponent` ColourIds. The on-screen keyboard is themed instead by `ModuleComponent::applyKeyboardThemeColours()` — at card construction and again from `lookAndFeelChanged()` on every theme switch — using the same tokens (`bg1` / `surfaceHi` / `border` / `accent` / `textPrimary`) the piano-roll key column uses.

### Phase 5 completions

- **TooltipWindow** — a single `juce::TooltipWindow` is now owned by `MainComponent` (member `tooltipWindow{ this }`). The tooltip `ColourIds` and `drawTooltip` override were already present in `AppLookAndFeel`; this instance causes them to take effect. Feature code calls `setTooltip()` on individual controls; no second `TooltipWindow` should be created elsewhere.

### Phase 4 completions

- **Waveform glyphs** (`WaveformSine`, `WaveformSaw`, `WaveformSquare`, `WaveformTriangle`) — SVG assets added in `assets/icons/waveform-{sine,saw,square,triangle}.svg`; `Icon` enum values 22–25 registered in `IconLibrary`; tinted to `textPrimary` by `retintIcons()`; consumed by `ModuleComponent` waveform combos (Oscillator). `AppLookAndFeel::drawPopupMenuItem` now paints the 14×14 waveform glyph left of the item text; `drawComboBox` renders the selected waveform glyph in the closed combo; `positionComboBoxText` shifts the label right when the selected item carries a glyph.

### Still deferred

- **`Icon::TransportPlay`** — SVG asset and enum value are present (scaffolding), but no `DrawableButton` is wired to it. Reserved for a future transport affordance.
- **`AIChatComponent.cpp`** — chat bubble palette + debug console (hardcoded)

---

## 10. Developer implementation notes

### `Theme.h` data model

`Theme.h` (`Source/UI/Theme/Theme.h`) has **no JUCE GUI dependencies** beyond `juce::Colour` and
`juce::String` (pulled in via `juce_graphics`). This makes it fully headless-testable without
linking the UI modules. The file also defines the `ThemeStyle` enum (`Flat`, `Glass`,
`Textured`) used by `Treatment::style`.

### `ThemeLoader` public API

`ThemeLoader` (`Source/UI/Theme/ThemeLoader.h`) exposes the following public helpers beyond the
main `parseTheme` / `themeToJson` pair:

| Method | Description |
|---|---|
| `parseHexColour(const juce::String&)` | Parse `"#RGB"` / `"#RRGGBB"` / `"#AARRGGBB"` → `std::optional<juce::Colour>`. Returns `nullopt` on malformed input. |
| `parseStyle(const juce::String&)` | Map `"flat"` / `"glass"` / `"textured"` (case-insensitive) → `std::optional<ThemeStyle>`. |
| `styleToString(ThemeStyle)` | Reverse: `ThemeStyle` → canonical lowercase string for serialization. |
| `getLastError()` | Returns the error reason from the most recent `parseTheme` failure on the calling thread (empty string on success). Used by callers to emit the single "Skipped …" log line. |

### Knob sweep arc constants

`AppLookAndFeel` declares two `static constexpr float` constants that define the 270°
rotary sweep arc shared by knob drawing (`drawRotarySlider`) and modulation ring drawing
(`drawModulationRing`):

```cpp
static constexpr float kRotaryStart = -juce::MathConstants<float>::pi * 0.75f; // ≈ –135°
static constexpr float kRotaryEnd   =  juce::MathConstants<float>::pi * 0.75f; // ≈ +135°
```

Both helpers read these constants directly instead of using the `rotaryStartAngle` /
`rotaryEndAngle` arguments passed by JUCE, ensuring the ring and the knob arc are always
co-aligned regardless of what the `Slider` component was configured with.

### Toolbar icon gating

`applyToolbarIcons()` clones `Drawable` instances from `AppLookAndFeel` and assigns them
to each toolbar `DrawableButton` — this is non-trivial Drawable clone work. To avoid repeating
it on every resize frame, `MainComponent::resized()` gates the call: `applyToolbarIcons()` is
only called when the toolbar transitions **into** narrow mode (detected by comparing the new
`isNarrowMode()` result against the previous value). Normal resize events that do not cross the
480 px threshold skip the Drawable clone work entirely.

---

## 11. Cable colours

Wires on the patch canvas are coloured through a single resolver,
`synth::ui::resolveCableColour()` in `Source/UI/CableColour.h`. Nothing in the paint path picks a
wire colour directly — `GraphEditor::colourForCable()` is the only caller, and the Appearance
settings swatches resolve through the same function, so a swatch can never show a colour the
canvas does not actually use.

### Modes

| Mode | Persisted id | Behaviour |
|---|---|---|
| **By signal type** (default) | `signalType` | Colour encodes what the cable carries: Audio, MIDI, Mod CV, Poly Bus, Pitch, Gate. Preserves signal semantics — you can tell a gate fan from a pitch fan at a glance. |
| **By source module** | `sourceCategory` | Colour follows the module the cable leaves from, grouped into the eight `cableCategory` buckets above. |

Colouring by source is deliberately **per category, not per module type**: 22 module types would
mean 22 colour pickers (which nobody configures) and a newly added module would render uncoloured
until someone updated a table. With categories, a new module inherits its group's colour for free.

The mode is a canvas-wide setting under **Settings → Appearance → Cables**, persisted as
`cableColour.mode`.

### Every built-in theme must set every wire token

Every built-in theme (`Source/UI/Theme/BuiltInThemes.cpp`, and by extension any future
theme-construction helper) MUST explicitly set all six wire tokens (`audioWire`, `midiWire`,
`modWire`, `pitchWire`, `gateWire`, `polyBusWire`) and the full 8-entry `cableCategory` array. A
field the constructor doesn't set silently falls back to `Theme.h`'s Obsidian (dark) defaults —
exactly the bug that shipped in `makeDaylight()`: it set every wire token except `midiWire` and
never set `cableCategory`, so both fell back to Obsidian's dark-tuned values (a lavender MIDI wire
that washes out on Daylight's near-white background). Guarded by
`Tests/CableColourTests.cpp`'s `EveryBuiltInThemeWireIsLegibleAgainstItsOwnCanvas` (WCAG contrast
floor against both `bg1` and `surface`, every built-in theme) and
`DaylightMidiAndCategoryColoursAreFixedNotInherited` (pinned regression for this specific bug).

### Wire activity/hover treatment is theme-polarity aware

The core stroke's activity/hover treatment lives in `synth::ui::wireCoreColour`
(`Source/UI/CableColour.h`), called from `AppLookAndFeel::drawConnectionWire` — never inline a
brightness ramp at a paint site. Dark themes keep the long-standing idle-dim law (50% brightness
at idle, token colour at full activity; hover = `brighter(0.3)`). Light themes draw an idle wire
at its exact token colour — the dark-theme dim law darkened every hue toward black on a light
canvas (indigo read navy, violet read near-black), destroying the palette's identity — and mark
activity by darkening a touch (hover = `darker(0.3)`). Pinned by
`LightThemeIdleWireKeepsTokenColourIdentity`, `DarkThemeIdleWireStillDimsTowardCanvas` and
`HoverEmphasisFollowsThemePolarity` in `Tests/CableColourTests.cpp`.

### User overrides

Theme tokens supply the defaults; the settings panel writes a **sparse override layer** on top —
the same relationship VS Code has between a colour theme and `workbench.colorCustomizations`.

- An **unset** override means "follow the theme", so a theme switch moves any colour the user has
  not explicitly pinned.
- Clicking a swatch pins it (`cableColour.signal.<id>` / `cableColour.category.<id>`, stored as
  `#AARRGGBB`). Pinned swatches are drawn with a brighter ring.
- Right-clicking a swatch, or **Reset Cable Colours**, *removes* the key rather than writing the
  theme's current value in — that is what lets it follow the theme again.

Overrides are stored **globally, not per theme**. If someone picks green cables they want green
cables, not green-until-the-theme-changes; Reset covers the case where a pinned colour clashes
with a newly selected theme.

`MainComponent::initialiseCommon()` restores the mode and overrides at launch. This matters:
`AppearanceSettingsTab` is built lazily when the Settings window opens, so leaving the restore to
the tab would mean the canvas ignored the user's saved colours until they went looking for them.

### Bypassed cables

`resolveCableColour()` applies `kBypassedCableAlpha` (0.3) to a bypassed modulation cable, after
mode and overrides. `resolveCableBaseColour()` is the same resolution *without* the bypass alpha —
that is what the settings swatches render, so a pinned colour is shown at full strength.
