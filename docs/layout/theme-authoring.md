# Writing a Theme

The JSON file format for a user theme, and what each treatment parameter does. The tokens
themselves, and their defaults, are [theming](theming.md).

## JSON schema

File extension: `*.gtheme.json`. Encoding: UTF-8.

| Key | Type | Required | Notes |
|---|---|---|---|
| `schemaVersion` | integer | no | Must be 1 or lower if present; omit it, or set it to `1` |
| `name` | string | **yes** | Display name shown in the Appearance tab |
| `id` | string | no | Stable lookup id (a slug); defaults to a slugified version of the filename |
| `colors` | object | **yes** | See the token table; the minimum keys are required |
| `metrics` | object | no | All keys optional |
| `typography` | object | no | All keys optional |
| `treatment` | object | no | All keys optional |

Colour values accept three forms: `"#AARRGGBB"` (alpha-first, 8 hex digits — `"#FF00D1FF"` is
fully-opaque cyan), `"#RRGGBB"` (implied fully-opaque alpha), and `"#RGB"` (3-digit shorthand, each
digit doubled — `"#0AF"` becomes `"#FF00AAFF"`). Lowercase and uppercase hex digits are both
accepted.

**Error handling is per-file and all-or-nothing:**

- A file that fails JSON parsing, or whose root is not an object, is skipped with one log line.
- A missing required key, or a malformed colour or style value, is skipped with one log line.
- Unknown optional keys are silently ignored, for forward compatibility.
- Treatment floats outside `[0,1]` are clamped, not an error.
- A `schemaVersion` greater than 1 is skipped with a "newer schema" log message.

## Find the user themes folder

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Agent Synth/Themes` |
| Windows | `%APPDATA%\Agent Synth\Themes` |
| Linux | `~/.config/Agent Synth/Themes` |

**Open Themes Folder** in `Settings -> Appearance` opens it in the file manager, creating it if it
does not exist.

## Copy the reference theme and edit it

Copy `themes/obsidian.gtheme.json` from the repository, or from the installed app's resources, into
the user themes folder and rename the file — for example `my-theme.gtheme.json`. Then change at
minimum `"name"` and, optionally, `"id"`:

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

Then `Settings -> Appearance -> Reload Themes`, and the theme appears in the list; clicking it
applies it instantly.

If the theme does not appear, check the log for a `[Theme] Skipped ...` error. The common causes
are a missing required `colors` key (`bg0`, `surface`, `accent`, `textPrimary`, `audioWire`,
`modWire`), a colour value that is not valid hex (a CSS `rgb()` function, for instance — use hex),
or invalid JSON such as a missing or trailing comma or an unquoted key.

**A theme meant for the canvas should also set every wire token and the full `cableCategory`
array** — an unset one silently inherits Obsidian's dark-tuned value, which is exactly how a light
theme ends up with an illegible MIDI wire. See
[cables](cables.md#every-built-in-theme-must-set-every-wire-token).

## Treatment parameters

### style

| Value | Visual effect |
|---|---|
| `"flat"` | Obsidian: flat fill plus a soft drop shadow. Clean and efficient. |
| `"glass"` | Neon: translucent surface fill, a top-edge highlight and an optional glow bloom — the graph canvas shows through the cards. Set the `surface` alpha below `FF` (for example `#991E1238`) to let light through. |
| `"textured"` | Warm: gradient fill plus a subtle vertical striation overlay, a brushed-metal feel. Keep `texture` above 0 to see the effect. |

### glow (float, 0-1)

The accent glow bloom applied to selection halo borders on selected module cards, the neon wire
bloom (an extra wide translucent stroke behind connection wires), and jack glow halos. `0` (Obsidian
and Warm) means no bloom; `0.85` (Neon) gives a vivid neon glow.

### shadow (float, 0-1)

Drop-shadow strength under module cards. `0` is no shadow, `0.6` is the Obsidian default.

**It is implemented as a stack of three translucent, downward-offset, expanding rounded rectangles,
not `juce::DropShadow`.** `juce::DropShadow` re-rasterizes a per-paint gaussian blur every time a
buffered card is re-rendered at a new zoom scale, which was the dominant cost behind zoom lag. The
layered-fill approximation is visually close at a fraction of the cost — plain fills, no blur — so
zooming stays smooth.

### blur (float, 0-1)

Glass frost highlight strength: the opacity of the top-edge highlight gradient applied to cards in
`glass` style. It does **not** perform a real backdrop blur, which is not available in JUCE and
would be too expensive. `0.6` (Neon) gives a strong frosted-glass highlight.

### texture (float, 0-1)

Brushed-metal striation overlay opacity, visible only in `textured` style. It controls how visible
the fine vertical hairlines drawn over the card body are. `0` is flat; `0.55` (Warm) is clearly
visible striation.
