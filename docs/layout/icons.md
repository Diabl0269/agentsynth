# Icons

Agent Synth uses SVG `Drawable` icons, **not** an icon or glyph font. That avoids the JUCE 8 and
CoreText runtime font-family swap corruption described in
[theming](theming.md#typography). The colours they are tinted with come from the theme tokens in
[theming](theming.md#colours).

## The icon enum

`synth::theme::Icon` in `Source/UI/Theme/IconLibrary.h` defines 43 icons, `TransportPlay = 0`
through `ActionDetachWindow`, followed by `kCount`:

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
CatIO            ActionDetachWindow
```

**Ordinals are append-only.** New entries go immediately before `kCount`, never grouped in beside a
related icon, so every existing enum ordinal — and the `IconLibraryTests.cpp` spot-checks against
them — stays unchanged. `CatIO` (41) and `ActionDetachWindow` (42) were both appended for that
reason rather than being grouped with the other `CatXxx` and `Action*` entries.

Notes on individual entries:

- **`TransportPlay`** (0) has no dedicated glyph of its own. It is reused as the ToggleTimeline
  toolbar button's icon.
- **`TransportStop`** (1) is the status bar's master-mute button glyph.
- **`ActionFeedback`** (9) is the toolbar button that opens Settings pre-selected to the Feedback
  tab, a speech-bubble glyph.
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
- **`ToolSelect` through `ToolDraw`** (31-36) are the six glyphs for the timeline edit-tool strip
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
  kind-badge glyphs, one per `synth::TrackKind`, drawn in place of the `"MIDI"` / `"AUD"` / `"AUTO"`
  text pill when a themed `AppLookAndFeel` and the asset library are both present
  (`TimelineTrackHeaderComponent::kindBadgeIcon`; see
  [tracks](../timeline/tracks.md#kind-badge)). The text pill remains the fallback in a headless
  build, or when the icon asset is missing — the badge is identity chrome either way, never a
  control.
- **`FollowPlayhead`** (40) is the toolbar-style toggle button next to the timeline panel's snap
  selector that page-flips the view to keep the playhead on screen while playing — see
  [playhead](../timeline/playhead.md#follow-playhead).
- **`CatIO`** (41) is the speaker glyph for the module library's "I/O" category header (Audio Input
  and Audio Output), and doubles as the Audio Output card's identity glyph in
  `ModuleComponent::paint()` — see
  [module-card](module-card.md#audio-output-card-identity). Both previously fell back to
  `CatUtility`, which gave the graph's actual source and sink no visual identity of their own.
- **`ActionDetachWindow`** (42) is the icon-only open-in-window / dock-back control
  `DetachablePanelHost` uses for both the Timeline and Mixer panels.

## Token to tint map

`AppLookAndFeel::retintIcons()` assigns tint colours from `theme_.colors`. It is called from
`applyTheme()`, so it is part of the single re-skin pass every theme switch triggers.

| Icons | Tint token |
|---|---|
| `ModuleBypass`, `ModuleDualIO` | `textMuted` |
| `ModuleMute` | `warning` |
| `ModuleDelete` | `error` |
| Toolbar actions and panel toggles (`ActionNew` … `ActionFeedback`, `ToggleAI`, `ToggleMatrix`, `ToggleLibrary`, `ThemeToggle`, `ToggleMinimap`), plus `TransportPlay` | `textMuted` |
| `TransportStop` | `textPrimary` |
| Category icons (`CatSources` … `CatUtility`, `CatIO`) | `textMuted` |
| `WaveformSine`, `WaveformSaw`, `WaveformSquare`, `WaveformTriangle` | `textPrimary` — the same as the combo text colour, so they stay legible across all themes |
| `ToolSelect` … `ToolDraw` | `textPrimary` — which tool is ACTIVE is a per-button highlight painted with the `toolActive` token, not a different icon tint; the glyph itself never changes colour |
| `TrackMidi`, `TrackAudio`, `TrackAutomation` | `textMuted` — quiet identity chrome, the same convention as the category icons |
| `FollowPlayhead` | `textPrimary` |
| `ActionDetachWindow` | `textMuted` |

**The muted tint on the toolbar set is a base, not the drawn colour.**
`MainComponent::applyToolbarIcons()` clones that base and re-tints it via `Drawable::replaceColour`
into the hover (`textPrimary`) and toggled-on (`accent`) variants it hands to
`DrawableButton::setImages()`. **Do not tint those icons `textPrimary` in `retintIcons()`:**
`applyToolbarIcons()`'s `replaceColour(textMuted, ...)` calls assume this exact starting colour.
`DetachablePanelHost` clones its own hover variant from `ActionDetachWindow` the same way.

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

1. Create `assets/icons/<category>-<name>.svg` — a 24x24 viewBox, `fill="#FFFFFF"` or
   `stroke="#FFFFFF" fill="none"`, with no gradients, CSS, `<use>` or `<defs>`.
2. Add the enum value to `Icon` in `IconLibrary.h`, immediately before `kCount`.
3. Add the `binaryDataForIcon` entry in `IconLibrary.cpp`'s `kTable` array, in the same order as the
   enum. The symbol name is the filename with hyphens stripped plus an `_svg` suffix.
4. Add a tint assignment in `AppLookAndFeel::retintIcons()`.
5. Rebuild `Assets` to regenerate `BinaryData.h`.

`static_assert(std::size(kTable) == (size_t)Icon::kCount, ...)` guards the count: it fails to
compile if step 3 is omitted.
