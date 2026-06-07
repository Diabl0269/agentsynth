# Theming Guide

Gravisynth ships with three built-in themes and a JSON-based theming system that lets you create
and share your own visual styles.

---

## 1. Overview

**Built-in themes** are compiled into the app; they cannot be deleted or modified.

| Theme | Style | Personality |
|---|---|---|
| **Obsidian Studio** | Flat | Dark, restrained — sharp edges, subtle shadows |
| **Neon Lab** | Glass | Translucent surfaces, neon glow, glassmorphism |
| **Warm Console** | Textured | Amber tones, brushed-metal striation overlay |

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

**Required minimum:** `bg0`, `surface`, `accent`, `textPrimary`, `audioWire`, `modWire`.
All other colour tokens are optional and fall back to the Obsidian defaults listed above.

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

### Treatment (`treatment`) — all optional

| Token | Default | Type | Meaning |
|---|---|---|---|
| `style` | `"flat"` | string | Surface style: `"flat"`, `"glass"`, or `"textured"` |
| `glow` | `0.0` | float `[0,1]` | Accent glow strength — neon wire bloom, selection halo |
| `shadow` | `0.6` | float `[0,1]` | Drop-shadow strength under cards |
| `blur` | `0.0` | float `[0,1]` | Glass frost highlight strength (NOT live blur) |
| `texture` | `0.0` | float `[0,1]` | Brushed-metal striation overlay opacity |

---

## 3. JSON schema

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

## 4. Create your own theme

### Step 1: Find the user themes folder

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Gravisynth/Themes` |
| Windows | `%APPDATA%\Gravisynth\Themes` |
| Linux | `~/.config/Gravisynth/Themes` |

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
    "modRingNegative": "#FFFF6E00"
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

In Gravisynth: `Settings → Appearance → Reload Themes`. Your theme appears in the list.
Click it to apply instantly.

If the theme does not appear, check the log for a `[Theme] Skipped ...` error. Common issues:
- Missing required `colors` keys (`bg0`, `surface`, `accent`, `textPrimary`, `audioWire`, `modWire`)
- A colour value that is not valid hex (e.g. a CSS `rgb()` function — use hex instead)
- The file is not valid JSON (missing comma, trailing comma, unquoted key)

---

## 5. Treatment parameters guide

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

Drop-shadow strength under module cards. Uses `juce::DropShadow` with a radius of 10px and a
4px downward offset. `0` = no shadow; `0.65` = Obsidian default.

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

## 6. Contrast guidance

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

## 7. Where files live and how reload works

| Location | Description |
|---|---|
| `themes/obsidian.gtheme.json` | Reference/example JSON (shipped in-repo for docs + round-trip tests). NOT loaded at runtime. |
| `assets/fonts/*.ttf` | Embedded font files (SIL OFL). Only the app target links them; tests use JUCE default fonts. |
| User themes folder (see section 4) | `*.gtheme.json` files placed here are loaded at startup and on "Reload Themes". |

**At startup**: built-in themes are registered first (obsidian/neon/warm), then user themes are
loaded from the folder. If two user themes share the same `id`, the second file overwrites the
first in the list.

**On reload** (pressing "Reload Themes"): previously-loaded user themes are cleared; built-ins
are preserved. Then the folder is re-scanned. If the active theme was a user theme that has since
been deleted, the manager falls back to Obsidian and broadcasts a change.

**Persistence**: the active theme `id` is stored under the key `"themeId"` in the shared
`ApplicationProperties` (the same settings file used for audio device and AI preferences). No
second file is created.

**Theme switch performance**: a theme change triggers exactly one re-skin pass —
`applyTheme()` remaps all JUCE ColourIds, then `sendLookAndFeelChangeMessage()` propagates
`lookAndFeelChanged()` to all child components, then a single `repaint()` is requested.
Because module cards use `setBufferedToImage(true)`, only their cached images are invalidated
and re-rendered once. There is no animation loop or per-tick repaint added.

---

## 8. Phase 2 (deferred)

The following components still use hardcoded colours and are **not yet themed**:

- `FrequencyResponseComponent.h` — filter frequency response curve colours
- `ScopeComponent.h` — oscilloscope scope trace colour
- `AIChatComponent.cpp` — chat bubble palette + debug console

These are self-contained DSP visualisers / chat UI whose colours do not visually clash with the
global theme re-skin. Migrating them requires adding new tokens to `Colors` and extending the
above components to read from `lf.getTheme().colors`. This is planned for a follow-up phase.
