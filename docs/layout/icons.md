# Icons

Agent Synth uses SVG `Drawable` icons, **not** an icon or glyph font. That avoids the JUCE 8 and
CoreText runtime font-family swap corruption described in
[theming](theming.md#typography). The colours they are tinted with come from the theme tokens in
[theming](theming.md#colours).

## The icon enum

`synth::theme::Icon` in `Source/UI/Theme/IconLibrary.h` defines 46 icons, `TransportPlay = 0`
through `TogglePanel`, followed by `kCount`:

```
TransportPlay    TransportStop    ActionUndo       ActionRedo
ActionSave       ActionLoad       ActionNew        ActionSettings
ActionAutoArrange ActionFeedback  ToggleAI         ToggleMatrix
ToggleLibrary    ThemeToggle      ModuleBypass     ModuleMute
ModuleDelete     CatSources       CatSequencing    CatEnvelopes
CatFilters       CatModulationFX  CatTimeFX        CatDynamics
CatUtility       WaveformSine     WaveformSaw      WaveformSquare
WaveformTriangle ToggleMinimap    ModuleDualIO
ToolSelect       ToolSplit        ToolGlue         ToolErase
ToolMute         ToolDraw
TrackMidi        TrackAudio       TrackAutomation  FollowPlayhead
CatIO            ActionDetachWindow ToolRange        MixerSources
TogglePanel
```

**Ordinals are append-only.** New entries go immediately before `kCount`, never grouped in beside a
related icon, so every existing enum ordinal — and the `IconLibraryTests.cpp` spot-checks against
them — stays unchanged. `CatIO` (41) and `ActionDetachWindow` (42) were both appended for that
reason rather than being grouped with the other `CatXxx` and `Action*` entries, and `ToolRange` (43)
likewise sits after them rather than beside the other `Tool*` glyphs.

Notes on individual entries:

- **`TransportPlay`** (0) is scaffolding: no button shows it.
- **`TransportStop`** (1) is the status bar's master-mute button glyph.
- **The toolbar set** (`ActionUndo` … `ToggleLibrary`, `ThemeToggle`, `ToggleMinimap`,
  `TogglePanel`) are [multi-role icons](#multi-role-icons) drawn only by the top bar's
  `ToolbarButton`; no other surface uses them. `ActionFeedback` (9) opens Settings pre-selected to
  the Feedback tab.
- The four **waveform icons** (25-28) are rendered in the Oscillator waveform combo via
  `AppLookAndFeel::drawPopupMenuItem` (a 14x14 glyph left of the item text) and `drawComboBox` (the
  selected waveform glyph in the closed combo). `positionComboBoxText` shifts the label right when
  the selected item has an icon.
- **`ToggleMinimap`** (29) is the toolbar toggle for the graph editor's
  [minimap](minimap.md) overlay.
- **`ModuleDualIO`** (30) is the module-header toggle that splits a collapsed `"Audio"` jack into
  separate Left and Right jacks. There is no universal stereo-split glyph; this one is a Y-fork into
  two jacks. The button's tooltip carries the Dual I/O on/off copy. See
  [fx-modules.md](../modules/fx-modules.md#stereo-io-dual-io-toggle).
- **`ToolSelect` through `ToolDraw`** (31-36) are six of the seven glyphs for the timeline
  edit-tool strip (the seventh, `ToolRange`, is appended at 43 — see below)
  (`synth::ui::EditTool` — Select, Split, Glue, Erase, Mute, Draw): a pointer arrow, scissors, a
  glue bottle, an angled eraser block, a crossed-out speaker, and a pencil at about 45 degrees. The
  mute glyph is visually distinct from `ModuleMute`: that one is an outlined speaker with a small
  corner X, this one is a solid-filled speaker with a single strike-through slash.
  `Source/UI/Timeline/ToolCursors.h`'s `makeToolCursor()` renders these same tinted `Drawable`s into
  the custom per-tool mouse cursor shown over the clip lanes and piano roll, rather than shipping a
  second cursor-only asset — that header's doc comment carries the per-tool hotspot table. The
  enum's index order here has no relationship to `EditTool`'s enumerator order; the UI layer looks
  up through a small tool-to-`Icon` mapping, never by casting one enum to the other.
- **`TrackMidi`, `TrackAudio`, `TrackAutomation`** (37-39) are the timeline track header's
  kind-badge glyphs, one per `synth::TrackKind`, drawn in place of the `"MIDI"` / `"AUD"` / `"Auto"`
  text pill when a themed `AppLookAndFeel` and the asset library are both present
  (`TimelineTrackHeaderComponent::kindBadgeIcon`; see
  [tracks](../timeline/tracks.md#kind-badge)). The text pill remains the fallback in a headless
  build, or when the icon asset is missing — the badge is identity chrome either way, never a
  control.
- **`FollowPlayhead`** (40) is the toggle button next to the timeline panel's snap selector that
  page-flips the view to keep the playhead on screen while playing — see
  [playhead](../timeline/playhead.md#follow-playhead). The glyph is the playhead (a down-pointing
  head on a line) with two chevrons chasing it from the left. Its colour carries the toggle state:
  the library tints it `textMuted` and the panel clones a `textPrimary` hover and an `accent` on
  variant, on top of the button's accent wash.
- **`CatIO`** (41) is the speaker glyph for the module library's "I/O" category header (Audio Input
  and Audio Output), and doubles as the Audio Output card's identity glyph in
  `ModuleComponent::paint()` — see
  [module-card](module-card.md#audio-output-card-identity). Both previously fell back to
  `CatUtility`, which gave the graph's actual source and sink no visual identity of their own.
- **`ActionDetachWindow`** (42) is the icon-only open-in-window / dock-back control
  `DetachablePanelHost` uses for both the Timeline and Mixer panels.
- **`ToolRange`** (43) is the Range edit tool's glyph (`EditTool::Range`, key 2): two vertical edge
  bars with a double-headed arrow between them — a span of time, not an object. Strip button and
  cursor use it exactly like the other `Tool*` glyphs above.
- **`MixerSources`** (44) is the mixer column header's sources badge: an arrow running into a bracket,
  "plays into this channel". It is a muted base the badge (`MixerIconButton`) clones hover and on variants
  from (muted, `textPrimary` on hover, accent when on).
- **`TogglePanel`** (45) is the toolbar's Hide/Show panel glyph: a window with its bottom panel
  filled.

## Token to tint map

`AppLookAndFeel::retintIcons()` assigns tint colours from `theme_.colors`. It is called from
`applyTheme()`, so it is part of the single re-skin pass every theme switch triggers.

| Icons | Tint token |
|---|---|
| `ModuleBypass`, `ModuleDualIO` | `textMuted` |
| `ModuleMute` | `warning` |
| `ModuleDelete` | `error` |
| `TransportPlay` | `textMuted` |
| The toolbar set | not tinted here: recoloured per group by `ToolbarButton` ([multi-role icons](#multi-role-icons)) |
| `TransportStop` | `textPrimary` |
| Category icons (`CatSources` … `CatUtility`, `CatIO`) | `textMuted` |
| `WaveformSine`, `WaveformSaw`, `WaveformSquare`, `WaveformTriangle` | `textPrimary` — the same as the combo text colour, so they stay legible across all themes |
| `ToolSelect` … `ToolDraw`, `ToolRange` | `textPrimary` — which tool is ACTIVE is a per-button highlight painted with the `toolActive` token, not a different icon tint; the glyph itself never changes colour |
| `TrackMidi`, `TrackAudio`, `TrackAutomation` | `textMuted` — quiet identity chrome, the same convention as the category icons |
| `FollowPlayhead` | `textPrimary` |
| `ActionDetachWindow`, `MixerSources` | `textMuted` |

**A muted tint can be a base, not the drawn colour.** `MixerIconButton` clones the muted
`MixerSources` base and re-tints it via `Drawable::replaceColour` into the hover (`textPrimary`) and
toggled-on (`accent`) variants it hands to `DrawableButton::setImages()`, and `DetachablePanelHost`
clones its hover variant from `ActionDetachWindow` the same way, so those bases must stay
`textMuted` in `retintIcons()`.

## Multi-role icons

The top bar's glyphs are drawn in four colour roles instead of one white tint. Each SVG paints a
role in a fixed placeholder colour, which `IconLibrary::createRecoloured(id, IconRoleColours)`
(through `AppLookAndFeel::getRoleIcon`) maps to real colours:

| Role | Placeholder | Toolbar colour |
|---|---|---|
| colour | `#FF00FF` (`kRoleHue`) | the button's group colour |
| soft | `#FF00FF` at `fill-opacity=".45"` (`kRoleSoftAlpha`) | the group colour at 45 percent |
| ink | `#00FFFF` (`kRoleInk`) | `iconInk` |
| paper | `#FFFF00` (`kRolePaper`) | `iconPaper` |

A colour that is not one of the four placeholders is left exactly as the SVG wrote it, so an icon
can mix role colours with fixed ones. `ActionSave` is drawn entirely in fixed colours (the floppy's
black, silver and white, with the "Agent / Synth" label as outlines); `ActionFeedback` uses the
colour, soft and paper roles only, and the top bar gives it its own green group.

The recolour is one pass over every fill and stroke that classifies a colour before writing it, so
a role colour that equals another placeholder is never mapped twice, and any extra opacity a shape
has (Save's shutter is paper at 70 percent) carries over. It always starts from the untinted
original, like `setTintColour`. `setTintColour` on such an icon gives a one-colour version (colour
and soft take the tint, ink and paper are cut out), so the white-tint path still draws something
sensible.

A group with `id="mv"` (and `id="mv2"`) is the part that moves on hover. `splitToolbarIconArt`
takes it out of the drawable tree and draws it as its own `Drawable` through the same icon-to-screen
transform, with its motion applied first (moving a child in place with `Component::setTransform`
makes its parent composite re-fit its origin and drift the pivot). The SVG parser bakes group
transforms into the paths, so a nested part (Redo's, inside its mirror group) still draws in icon
coordinates.

## The null-fallback contract

`IconLibrary::getDrawable(id)` returns `nullptr` when the `HAS_FONT_ASSETS` compile flag is absent —
a headless test build without the asset target, for instance. **All callers must null-check:**

```cpp
if (auto d = lf->getIcon(Icon::ModuleBypass))
    bypassButton->setImages(d.get());
// else: button remains blank — acceptable in headless tests
```

`AppLookAndFeel::getIcon()` and `peekIcon()` delegate directly to the `IconLibrary` member and also
return `nullptr` when the library has no asset for the requested id.

## Parallel-array design

`IconLibrary` keeps two `std::array<std::unique_ptr<juce::Drawable>, kCount>`:

- `originals_` — loaded once at construction, never mutated (pure-white SVG source)
- `drawables_` — tinted copies, updated by `setTintColour`

**`setTintColour` always clones from `originals_[]` before tinting**, so the second and third theme
switch produce the correct colour and do not accumulate tints.

## BinaryData symbol naming

JUCE's binary-data name mangler **strips hyphens** and concatenates the remaining tokens. Contrary
to the JUCE documentation's "a-b.svg becomes `BinaryData::a_b_svg`", hyphens are stripped, not
converted to underscores. `IconLibrary.cpp`'s lookup table uses the real symbol names, and a CMake
guard (`file(GLOB)` plus `string(FIND ... "_")`) enforces hyphen-only filenames to prevent
accidental underscore collisions.

| Filename | BinaryData symbol |
|---|---|
| `transport-play.svg` | `BinaryData::transportplay_svg` |
| `action-undo.svg` | `BinaryData::actionundo_svg` |
| `cat-modulation-fx.svg` | `BinaryData::catmodulationfx_svg` |
| `action-auto-arrange.svg` | `BinaryData::actionautoarrange_svg` |
| `action-feedback.svg` | `BinaryData::actionfeedback_svg` |
| `waveform-sine.svg` | `BinaryData::waveformsine_svg` |
| `waveform-saw.svg` | `BinaryData::waveformsaw_svg` |
| `waveform-square.svg` | `BinaryData::waveformsquare_svg` |
| `waveform-triangle.svg` | `BinaryData::waveformtriangle_svg` |
| `cat-io.svg` | `BinaryData::catio_svg` |

## Adding an icon

1. Create `assets/icons/<category>-<name>.svg` — a 24x24 viewBox with `width="24" height="24"`,
   `fill="#FFFFFF"` or `stroke="#FFFFFF" fill="none"` (or, for a toolbar glyph, the
   [role placeholders](#multi-role-icons)), with no gradients, CSS classes, `<use>` or `<defs>`.
2. Add the enum value to `Icon` in `IconLibrary.h`, immediately before `kCount`.
3. Add the `binaryDataForIcon` entry in `IconLibrary.cpp`'s `kTable` array, in the same order as the
   enum. The symbol name is the filename with hyphens stripped plus an `_svg` suffix.
4. Add a tint assignment in `AppLookAndFeel::retintIcons()` (a multi-role toolbar glyph needs none).
5. Rebuild `Assets` to regenerate `BinaryData.h`.

`static_assert(std::size(kTable) == (size_t)Icon::kCount, ...)` guards the count: it fails to
compile if step 3 is omitted.

### The AI button

`ToggleAI` is a spark on a patch cable: a short cable in the group colour (`hueRose`) ending in a jack
plug whose body takes the colour role and whose metal tip keeps its own greys, in
`assets/icons/toggle-ai.svg`. It has no `mv` part. The signal pulse that runs along the cable, the glow
at the plug and the four-point spark are drawn in code (`Source/UI/Chrome/ToolbarButton/ToolbarAiSpark.cpp`)
because they travel along a path and fade; the pulse follows `juce::Path::getPointAlongPath` on the
cable's curve. Pulse and glow use the theme's `hueAmber` (a dark gold on Daylight, so they stay visible
on a light chip); the spark is a fixed pink on dark themes and `hueRose` on light ones. On a lit chip
all three turn `iconInk`.

At rest the spark shows small and nothing else moves. One cycle is 2.8 s: the pulse leaves the cable's
start and reaches the plug at 50 percent (faded in by 6, out between 50 and 58 percent), the glow
swells from 45 to 58 percent and fades by the end, and the spark blooms from 48 to 64 percent (scale
0.15 to 1.12, turning to 30 degrees, with a bounce), settles to 0.95 / 45 degrees at 82 percent and
fades to 0.4 / 70 degrees at the end. It loops while the assistant is working
(`AIChatComponent::onWaitingChanged` -> `ToolbarButton::setBusy`) and while the button is hovered; the
keyframes cross-fade with the rest drawing over 140 ms in and 220 ms out. Under Reduce Motion it never
plays. While busy the button's screen-reader description and help text say "Assistant is working"; its
name and tooltip do not change.

