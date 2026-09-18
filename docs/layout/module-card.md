# Module Card Geometry

`ModuleComponent` is the card one module draws as. It outgrew one file and is split by concern
under `Source/UI/Graph/ModuleComponent/`, the same `<Class>/<Class><Concern>.cpp` pattern as
`Source/UI/Graph/GraphEditor/` and `Source/MainComponent/` — the unit-by-unit table is in
[architecture_module_base.md](../architecture_module_base.md#modulecomponent).

Where a card LANDS on the canvas is [layout](layout.md); what it draws inside itself is here.

## Width buckets

Card widths are standardised into three named buckets, defined in `LayoutUtil.h`:

| Bucket | Constant | Width | Modules |
|---|---|---|---|
| `Narrow` | `kNarrowWidth` | 40 px | AttenuverterModule |
| `Single` | `kSingleWidth` | 280 px | Oscillator, Filter, VCA, ADSR, LFO, FX modules, VoiceMixer, PolyMidi, all others |
| `Double` | `kDoubleWidth` | 560 px | Sequencer, PolySequencer, MidiKeyboard |

`kDoubleWidth == 2 * kSingleWidth`, so a Double module occupies exactly two standard column slots.
Attenuverter modules are excluded from the visible card grid entirely — they draw no
`ModuleComponent` and are skipped by auto-arrange.

## Body layout

Every module without a bespoke layout (the exceptions are Sequencer, PolySequencer, MidiKeyboard,
ADSR and Attenuverter) is laid out by one function, `layoutDefaultContent(bool apply)`. It runs
twice per size change: once with `apply = false` to measure the height, once with `apply = true`
from `resized()` to position the children.

**Why one function runs twice instead of two functions.** The measured height and the real positions
cannot drift apart when they come from the same code. They previously did: two hand-maintained
copies of the geometry disagreed, and body content was drawn on top of the lowest port labels.

| Constant | Value | Meaning |
|---|---|---|
| `kKnobColumns` | 3 | knobs per row |
| `kContentMargin` | 12 | left/right gutter for body content |
| `kNarrowContentWidth` | 200 | combos / toggles / the Sampler load row, centred |
| `kLabelHeight` | 18 | label above a knob or combo |
| `kRowHeight` | 24 | combo box, toggle, button |
| `kKnobHeight` | 58 | rotary plus its text box |
| `kWaveformHeight` | 72 | Sampler waveform overview |
| `kPortLabelClearance` | 15 | gap below the lowest jack before body content starts |

Generic bool-parameter toggles, and the `freqResponse` / `spectrum` / `scope` show-hide toggles, lay
out at full `contentX` / `contentW`, flush-left with the knob grid, rather than the centred
`narrowX` / `narrowW` band — a toggle's label reads better flush-left than centred in a narrow
column.

**Body content always starts below every jack.** `getContentTopY()` derives that y from
`getPortCenter()` — the same function that anchors wires — rather than recomputing the port
geometry, so content can never land on a port label. Because the body sits below the ports it does
not need the narrow gutters that used to keep it clear of them, which is what makes three knobs per
row fit inside the 280 px card.

The header-to-first-port gap is 9 px (base header offset constant 38 in both `getContentTopY()` and
`getPortCenter()`), giving the MIDI In/Out row breathing room under the header hairline.

Measured heights. `GraphEditor::estimateModuleSize()` mirrors these for the library drag ghost, and
`ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents` constructs every library-offered
type and fails if this table drifts from what `layoutDefaultContent()` actually produces:

| Module | Height (px) | Module | Height (px) |
|---|---|---|---|
| Oscillator | 553 | Sample & Hold | 571 |
| Filter | 463 | Comparator | 205 |
| LFO | 361 | Sampler | 665 |
| VCA | 273 | Chorus / Phaser / Flanger | 297 |
| ADSR | 359 | Bitcrusher | 343 |
| Poly MIDI | 205 | Pitch Shifter | 487 |
| Distortion | 343 | Compressor | 257 |
| Ring Modulator | 411 | Limiter | 181 |
| Delay | 257 | Voice Mixer | 321 |
| Reverb | 257 | External MIDI | 146 |
| Noise | 301 | Rec Tap / Track Audio / Hosted Plugin | 131 |
| Envelope Follower | 315 | Math | 259 |

Outside that function: Sequencer and PolySequencer 406, MidiKeyboard 150, Macros (tracks its
`Knobs` count), Attenuverter (square, `kNarrowWidth`), Wavetable 554, Parametric EQ 592, and
AudioInput / AudioOutput on a 100 px floor.

## Modules that resize at runtime

Widths are static; one module's **height** is not. The Macro bank (`MacroControlModule`) grows and
shrinks with its `Knobs` parameter. Its geometry lives in `LayoutUtil.h` so the component layout,
the output-jack hit test and `estimateModuleSize` all read the same numbers:

```cpp
kMacroHeaderH  = 94   // title bar + Knobs / Bipolar row
kMacroRowH     = 44   // one macro knob and its output jack
kMacroBottomPad = 12

macroBankHeight(count) == kMacroHeaderH + count * kMacroRowH + kMacroBottomPad
macroRowCentreY(index) == kMacroHeaderH + index * kMacroRowH + kMacroRowH / 2
```

**Growth is anchored at the top-left and pushes neighbours, never itself.** Moving the module the
user is currently interacting with would teleport it out from under the cursor, so
`GraphEditor::handleModuleResized` feeds every module box to
`LayoutUtil::resolveOverlapsAfterResize` and applies the displacements it returns. Shrinking returns
an empty result set: nothing is pulled back up, the canvas just gains space.

## Header buttons

The header area holds `DrawableButton` instances, not `TextButton`s, positioned in `resized()`
(`ModuleComponentPaint.cpp`):

| Button | Bounds | Action |
|---|---|---|
| `deleteButton` | `(w-26, 2, 22, 20)` | Calls `owner.requestDeleteModule(nodeId)` — tooltip "Delete module" |
| `bypassButton` | `(w-50, 2, 22, 20)` | Toggles bypass state — tooltip "Bypass" |
| `muteButton` | `(w-74, 2, 22, 20)` | Toggles mute state — tooltip "Mute" |
| `dualIOButton` | `(w-98, 2, 22, 20)` | Stereo-capable modules only — every type in `AIStateMapper::dualIOCapableModuleTypes()` (the FX, Voice Mixer and Ring Modulator outputs, and the split-block voice modules). Splits or merges the stereo jack pair; the tooltip names Dual I/O. |

`requestDeleteModule(NodeID)` is the canonical delete entry point; `deleteButton.onClick` delegates
to it.

`applyHeaderButtonIcons()` retints the header buttons from the active `AppLookAndFeel`. It is
null-guarded: when the look-and-feel cast fails (headless tests) the function returns early and the
buttons stay imageless but functional. `lookAndFeelChanged()` calls it, so icons update on a theme
switch.

The header title text is drawn by `AppLookAndFeel::drawModulePanel()` with an asymmetric inset,
`header.withTrimmedLeft(22.0f)`, so the activity LED (`fillEllipse(6, 8, 8, 8)`, x from 6 to 14)
never overlaps the title regardless of whether the LED is lit.

## Custom card titles

Double-clicking a card's **header band** (`ModuleComponent::kHeaderHeight`, 24 px) opens an inline
`juce::TextEditor` over the title. Return or a click away commits, Escape cancels, and the editor is
seeded with whatever the header currently shows, so a first rename starts from `Chorus 2` rather
than an empty box. It is a **child component**, so there is no window seam to stub out for a
display-less test run, and it is themed for free — `AppLookAndFeel` already overrides
`fillTextEditorBackground` and `drawTextEditorOutline`.

**The custom title is NOT the processor's name.** `ModuleBase::getName()` is the auto-numbered
`"Chorus 2"`, and `AudioEngine::updateModuleNames()` recomputes every one of those **wholesale on
every graph change**: it strips a trailing number off each name and renumbers per base type. A
custom title written there would be clobbered by the next node the user adds, and worse, `"My
Chorus 2"` would have its `2` stripped and be renumbered into `"My Chorus 3"`. So the custom title
lives in the node property **`displayName`**, and the numbered processor name stays the fallback:
`GraphEditor::getModuleTitle()` returns the custom one when set, else `processor->getName()`. Card
paint goes through `ModuleComponent::cardTitle()` and never reads the processor name directly.

Consequences worth knowing:

- Renaming one instance does not disturb any other instance's number, and a later instance still
  auto-numbers correctly — a renamed `Chorus 1` does not free up the name, because the count is per
  base type over all nodes. Pinned by `AutoNumberingStillAppliesAlongsideCustomTitles`.
- Blank or whitespace-only reverts to the numbered default rather than showing an empty header.
- Typing the numbered name back in stores **blank**, so the card keeps following the numbering
  instead of freezing today's number as a literal custom title.
- `displayName` is message-thread-only and display-only, so it is deliberately **not** mirrored into
  the processor the way `uuid` is. Nothing on the audio thread reads a card title, so there is no
  lock-free read to make sound and a mirror would only add a second copy to keep in sync.

**Gesture ordering.** The double-click is intercepted in `mouseDown` on `getNumberOfClicks() >= 2`,
*before* `dragStartPosition` and `bodyDragActive` are touched — the same interception the port
double-click uses. The first click of the pair has already armed and disarmed a body drag; letting
the second arm another would leave `bodyDragActive` set under an open editor, so the next stray
`mouseDrag` would walk the card out from under the cursor. Pinned by
`ModuleTitleRenameDoesNotArmABodyDrag`.

**Undo** goes through `recordStructuralChange`, which is also what makes the title survive an
undo/redo of any *other* structural change: the snapshot is `graphToJSON`, so the title has to be
serialized for undo to restore it at all — see
[patch-format](../ai/patch-format.md#per-node-displayname).

## Audio Output card identity

Audio Output is a bare `juce::AudioGraphIOProcessor`, not a `ModuleBase`, so it otherwise renders
through the exact same generic path as every other card — its `getType()` falls back to
`ModuleType::Oscillator` (see `detail::getType` in `ModuleComponentInternal.h`). Two small, purely
additive blocks in `ModuleComponent::paint()` give it its own identity, both gated on a local
`isAudioOutputIONode(juce::AudioProcessor*)` helper: a `dynamic_cast` to `AudioGraphIOProcessor`
plus an `IODeviceType == audioOutputNode` check — the same type-not-name idiom `isTerminalAudioSink`
in `GraphEditorInternal.h` uses for cable routing, so a `ModuleBase` named "Audio Output" cannot
impersonate the sink.

**Identity glyph.** The `synth::theme::Icon::CatIO` speaker glyph sits in the activity LED's slot.
Audio Output is never a `ModuleBase`, so it never has a `VisualBuffer` and `cachedRMS` never leaves
`0.0f` for this card — that slot is otherwise always dark. It is sized and positioned by
`ModuleComponent::outputCardIconBoundsForTest()`, public purely so a test can assert the exact
geometry `paint()` draws, rather than by a fixed box:

- **Size** — a square equal to the title's cap-height, computed as `titleFont.getAscent() * 0.72f`
  for the title's own font (`theme.type.h2`, bold). JUCE has no direct cap-height query, and 0.72x
  ascent is the standard sans-serif approximation, close to Inter's real ratio. This keeps the glyph
  proportional to the title — roughly cap-height, about 9-10 px at the default 13 pt title size —
  instead of the header's full 24 px band, which read a touch large and heavy next to the
  letter-spaced caps.
- **Vertical position** — centred on the title's cap-height optical centre, derived from the SAME
  centred-text-box math `drawText` uses to place the title (`textBoxTop = headerTop + (headerHeight
  - titleFont.getHeight()) / 2`, `baseline = textBoxTop + titleFont.getAscent()`, `capCentreY =
  baseline - capHeight / 2`) rather than the header band's raw geometric middle. Centring on the
  full ascent-plus-descent box the title is drawn in reads slightly low against cap-height-only
  glyphs, because an all-caps title never touches the descender clearance that box reserves.
- **Horizontal position** — the right edge is pinned to `x = 14`, the activity LED's own right edge
  (`fillEllipse(6, 8, 8, 8)`), so the gap to the title's left inset at `x = 22` stays the
  established 8 px rhythm regardless of the glyph's resulting width.
- **Colour** — re-tinted per paint from the library's baked-in `textMuted` to whichever colour token
  the title is using this frame: `accent` when selected, `textDisabled` when bypassed, `textPrimary`
  otherwise, mirroring `drawModulePanel`'s title-colour logic exactly. It uses
  `Drawable::replaceColour` on an owned clone from `AppLookAndFeel::getIcon()`, never the shared
  `peekIcon()` view other cards and the library sidebar also read, so the glyph and the title always
  read as one lockup instead of a fixed muted tint beside a colour-shifting title.

**Destination line.** One muted line (`theme.type.micro`, `colors.textMuted`) under the header, for
example `"MacBook Pro Speakers · 48 kHz · 2ch"`. It is sourced **message-thread-only** from
`AudioEngine`'s device state and pushed in through a small chain rather than read directly:
`MainComponent::computeOutputDeviceInfoText()` branches on `AudioEngine::isHosted()` — Hosted mode
has no device manager, so it returns the fixed string `"Host audio"` instead of touching one, and a
Standalone build with no device open yet returns an empty string — and is installed as a
`std::function<juce::String()>` via `GraphEditor::setOutputDeviceInfoProvider`;
`GraphEditor::refreshOutputDeviceInfo()` calls it and pushes the result into the Audio Output
`ModuleComponent` via `setOutputDeviceInfoText()`. An empty string means "draw no line", not an
empty one.

There is no timer: `refreshOutputDeviceInfo()` is called once right after the provider is installed,
so the card is populated at startup, and again from `AudioEngine::onDeviceStateChanged`, alongside
the existing `refreshIoModulesAfterDeviceChange()` call that already re-points Audio Input's jacks
on a device change. `setOutputDeviceInfoText()` repaints — the normal `ZoomFrozenCachedImage`
invalidation seam, see [rendering](rendering.md) — only when the text actually changed, and is a
no-op on every module that is not the terminal audio sink, so a caller never needs to check
`isAudioOutputIONode()` first.

The library sidebar's "I/O" category header (Audio Input / Audio Output) uses the same `CatIO` icon
via `ModuleLibraryComponent::categoryIconForHeader` — see [icons](icons.md).

## Wavetable card

The Wavetable card is **double-width**: 15 knobs, 8 combos and a 16-jack port stack. Laid out flat
that came to 560x869 — technically correct and genuinely unusable, a wall of identical knobs with no
hierarchy. Three mechanisms bring it to 560x554 and give it a reading order.

**Two-column jack gutter, both columns on the LEFT.** `getInputPortColumns()` returns 2 for a card
with more than 10 visible input jacks at double width; `getPortCenter()` then lays the inputs out
**column-major** — jack 0 top-left, running down then over — at `kPortColumnStride` (100 px) apart.
Sixteen jacks in one column set a roughly 390 px floor on the card height before a single control is
placed.

**Inputs stay on the left, outputs on the right — that convention is not negotiable for a saving in
height.** It is what makes signal flow read left to right across a patch. Spilling the overflow down
the right edge was tried and reverted: it reads as an output, and inputs and outputs are drawn
identically, so the side is the only cue there is. The real objection to an interior column — the
module covers the lower half of a cable being dragged towards it — is answered by not aiming at the
gutter at all; release the cable **on the destination knob** instead, see
[modulation.md](../modulation.md).

Everything that touches jack geometry — wire drawing, hit-testing (`getPortForPoint`), painting —
reads `getPortCenter`, so this is the only place that changes. One consequence:
`getContentTopY()` takes the **maximum** y over all inputs rather than the last one's, because with
more than one column an odd jack count leaves the second column a row short, so the last jack is not
the lowest.

**Tabbed control body.** The 23 controls are grouped into five pages — Tune / Unison / Phase / Sub /
File — by `kWavetablePages` in `ModuleComponentWavetable.cpp`, which maps parameter *display names*
to pages. Three controls sit outside the strip: `Position` and `Warp` / `Warp Amt` are **pinned**
above it (they are what you actually perform with), and `Table` is laid out in the chrome band
beside the display it selects.

- **Jacks are never tabbed.** All 16 CV inputs stay on the card at all times, so a cable can never
  point at a hidden port. Only knobs and combos page.
- **Anything that paints from a knob's bounds must check visibility.** A hidden knob keeps the
  bounds it had when its page was last laid out. Modulation rings go through
  `getModRingSliderIndex()` and drop targeting through `getModTargetPortForPoint()`; both return
  "none" for a knob whose page is hidden, so a ring cannot paint over empty card and a hidden knob
  cannot swallow a cable drop.
- The card is sized to the **tallest** page, not the active one. A card that grew and shrank would
  shove its neighbours around the canvas on every tab click.
- Page knob rows are **centred** and page combos run three across — most pages carry fewer than the
  6 available knob columns, and left-aligning them stranded half the card's width.
- `createWavetableTabs()` must run **after** `createControls()` (it groups the controls that call
  creates) and must end by calling `updateLayout()` — `createControls()` has already sized the card,
  so without a second pass the card keeps its flat-grid height and the tabbed layout never applies.

**Chrome beside the ports.** The display, `Table` combo, button row and caption sit **beside** the
jack stack, starting just under the header — the same reclaim the Parametric EQ card makes for its
response curve (see [visualizers](visualizers.md)). The body still starts below the lowest jack.

The final size is pinned by `EstimatedModuleSizesMatchTheRealComponents`;
`WavetableCardSplitsItsJackGutterIntoTwoColumns` and
`WavetableTabsSwitchContentWithoutResizingTheCard` guard the two mechanisms above, including that
every knob is reachable from some page — a control on no page is unusable.

`ModuleComponent` also builds two file affordances for Wavetable modules: a **"Load Wavetable..."**
`TextButton` opening an async `juce::FileChooser` starting in the module's browser folder, which on
success calls `loadWavetableFile()` and switches the module's `table` choice to `Loaded File`; and a
**folder browser row**, `[Folder...] [<] [>] [caption]`, where `Folder...` opens an async directory
chooser and calls `setWavetableFolder()`, `<` and `>` step the folder cursor, and the caption shows
the loaded file name and `index/total`. Both completion lambdas hold a `Component::SafePointer` and
re-derive the module via `dynamic_cast`, since the card can be destroyed while the dialog is open.
The card also implements `FileDragAndDropTarget` for audio files, routing drops through the same
import path as the Load button.
