# Snippets and the Clipboard

A snippet is a saved sub-graph; the clipboard is the same thing without a file. Both operate on
whatever [selection](selection.md) holds.

## What a snippet is

`Source/SnippetManager.{h,cpp}`. A snippet is the **same JSON dialect as a preset**
(`AIStateMapper::graphToJSON`) narrowed to a subset of nodes, stored one file per snippet as
`<userAppData>/<settingsFolder>/Snippets/<name>.agsnip`, mirroring
`ThemeManager::getUserThemesFolder()`.

Three rules define what it contains:

| Rule | Reason |
|---|---|
| Only connections with **both** endpoints selected | A snippet must mean the same thing in every patch it lands in; it cannot assume the destination has the node on the other end. Wires to Audio Output are dropped — re-patch the output after dropping. |
| **No Attenuverter nodes**; modulation is stored in the `modulations` array | The same representation the AI patch format uses. `applyJSONToGraph` rebuilds the attenuverter chain on insert. |
| Positions normalised so the selection's top-left is `(0,0)` | `prepareForInsert()` re-offsets by the drop point, so internal layout is preserved wherever it lands. |

Graph I/O nodes (Audio In/Out, MIDI In) are never captured — they are singletons that already exist
in the target patch.

Extraction **filters `graphToJSON` output** rather than re-deriving the per-node JSON shape, so the
params encoding, the MIDI port sentinel and the attenuverter-to-`modulations` folding keep exactly
one owner.

## Id renumbering is mandatory

`prepareForInsert()` renumbers snippet ids to a fresh range starting at `nextFreeIdBase(graph)`, the
maximum existing uid plus 1. This is not cosmetic: `applyJSONToGraph` in **merge mode** treats an
incoming node id that already exists as "update that node", so a colliding id would silently retune
an existing module of the same type instead of adding a copy.

## Validate strictly, apply faithfully

`insertSnippet()` deliberately splits the two:

```cpp
validatePatch(prepared, graph, /*clearExisting=*/false, /*trusted=*/false);   // strict — reject whole
applyJSONToGraph(prepared, graph, /*clearExisting=*/false, /*trusted=*/true,  // faithful — no rescale
                 /*autoConnectNewNodes=*/false);                              // exact — no extra wires
```

- **Strict validation** because a snippet is a file on disk and can be hand-edited, truncated, or
  copied in from elsewhere. A malformed snippet is rejected whole, never applied halfway. This
  *adds* a validation gate the trusted path would otherwise skip — it does not relax
  `validatePatch` (see the invariant in the root `CLAUDE.md`).
- **Trusted apply** because the untrusted apply path carries an AI-output heuristic: when a
  parameter's range extends beyond `[0,1]` but the incoming value sits inside it, the value is
  treated as normalised and rescaled. Snippet values come from `graphToJSON` already denormalised,
  so that heuristic would corrupt legitimate small values — an LFO `rateHz` of 0.5 Hz over a
  0.01-20 range would land at roughly 5 Hz. Guarded by
  `SnippetInsert.PreservesParameterValuesThatLookNormalised`.
- **`autoConnectNewNodes=false`**, and this one is easy to miss. Merge mode otherwise runs a
  convenience pass that wires every newly created audio node with no outgoing wire straight to Audio
  Output, and every new MIDI-accepting node to an existing MIDI source. That is right for an AI
  merge patch — a model that adds an Oscillator means it to be audible — and **wrong** for a
  snippet, where the absent wires are as deliberate as the present ones. Without the opt-out,
  dropping a snippet into the patch it came from splices the copy's leaf modules into the live
  output. Guarded by `SnippetInsert.DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` and
  `...DoesNotAttachInsertedModulesToAnExistingMidiSource`; the AI-side behaviour it preserves is
  locked by `AIStateMapperTest.MergeAutoConnectsNewAudioNodesToOutputByDefault`.

A snippet is therefore **exactly** its internal wiring: what was selected, nothing the surrounding
patch happened to be connected to, and nothing added for convenience on the way back in.

## Wiring

`GraphEditor` owns no file dialogs and `ModuleLibraryComponent` owns no filesystem access;
`MainComponent` brokers between them via `GraphEditor::onSaveSnippetRequested` /
`GraphEditor::snippetProvider` and `ModuleLibraryComponent::onSnippetDeleteRequested`.

Snippet drags reuse the **existing** DragAndDrop channel between sidebar and canvas. The payload is
prefixed (`snippet:<name>`, `SnippetManager::kPayloadPrefix`) so `itemDropped` can tell a group drop
from a plain module drop. `itemDragEnter` sizes the landing ghost from the snippet's own bounding
box rather than the single-module estimate table.

Insert is one undoable change (`recordStructuralChange`), and the newly landed group is left
selected — it is what the user will want to move next.

## Copy, paste and duplicate

`Source/UI/Graph/ModuleClipboard.h`. Cmd+C / Cmd+V / Cmd+D, plus the same three actions on the
module and canvas context menus.

**They are snippets that never reach disk.** `copySelection()` calls the same
`SnippetManager::extractSnippet` the Save-as-Snippet path calls and parks the result in a
`ModuleClipboard`; paste and duplicate hand it to the same `insertSnippet`. Nothing about the wiring
rules is re-derived, which is the point — the three snippet rules give a paste its behaviour for
free:

| Snippet rule | What it means for a paste |
|---|---|
| Only connections with **both** endpoints selected | The copies wire to **each other**, never back to the originals. A wire that ran into the group from outside is dropped rather than duplicated onto the copy. |
| Modulation stored as intent | An LFO-to-Filter routing between two copied modules comes back as a rebuilt attenuverter chain, not as a second wire onto the original attenuverter. |
| Origin-relative positions plus a fresh id range | The group keeps its internal layout, and merge mode cannot mistake a copy's id for an existing node and silently retune it. |

`autoConnectNewNodes=false` carries over too, so a pasted module never wires itself to Audio Output.
Guarded by `ClipboardPaste.RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals`,
`...DropsConnectionsThatLeftTheCopiedSelection`,
`...DoesNotSpliceTheCopiesIntoTheSurroundingPatch` and
`...RebuildsModulationChainsBetweenTheCopies`.

## Non-parameter state

`extractSnippet` / `prepareForInsert` / `insertSnippet` take an `includeExtraState` flag, **off by
default**. A `.agsnip` is a hand-editable file that inserts on the *trusted* apply path, and
`applyExtraStateToProcessor` reads `state` as a filename for `SamplerModule` — so a snippet file
must not be able to carry one. The clipboard has no such exposure: its payload comes straight from
the live graph and never leaves the process, so it opts in and a duplicated Sampler keeps its
sample. (`SnippetExtraState.*`,
`ClipboardCopy.CarriesNonParameterModuleStateSoADuplicatedSamplerKeepsItsSample`.)

**A track preset is the second `includeExtraState=true` caller, and the one that DOES leave the
process.** `TrackPresetManager::extractTrackPreset` forces it on — a track preset must carry the
Channel Strip's shape, gain and pan, or a Mono strip would silently reload as the Stereo default —
even though, unlike the clipboard, the resulting file is written to disk and can be handed to
`insertTrackPreset` on a different project or a different day. This is safe for the same reason a
`.agsnip` file being untrusted is safe: `insertTrackPreset` is `SnippetManager::insertSnippet` with
`trustedPayload=false`, so `validatePatch` still gates the load. The one piece of Channel Strip
state that would be actively dangerous on import — `"solo"`, which would silence the whole mix
render-wide if it came back `true` — is scrubbed from the captured state at save time, in
`TrackPresetManager.cpp`, instead of relying on the untrusted load path to catch it. The root
`CLAUDE.md` carries this as a tripwire.

## Placement

`SnippetManager::selectionOrigin()` returns the top-left corner extraction normalises against — the
clipboard's anchor. Both paste and duplicate offset from it by `ModuleClipboard::kOffsetStep`
(40 px, 5 grid units, so an offset copy stays on-grid):

- **Cmd+V** cascades: each successive paste steps one more offset down-right, so repeated pastes fan
  out instead of stacking on one pixel and looking like nothing happened.
- **"Paste Here"** (canvas right-click) drops at the click point and re-anchors the cascade there.
- **Cmd+D** offsets one step from the *current* selection and leaves the clipboard untouched —
  Cmd+D must not cost the user what they had copied. Since a duplicate leaves the copies selected,
  repeating it walks a chain across the canvas rather than piling up on one spot.

Neither runs the group through `findFreeSlot`: a fixed offset is predictable (the
Figma/Illustrator convention), the copy is visibly its own card, and it arrives selected and ready
to drag. Both are one undoable change for the whole group, on the same `recordStructuralChange` path
as a snippet drop.

**The clipboard is in-app and in-memory only.** It is deliberately not the system clipboard: Cmd+C
on the canvas must not silently destroy whatever text the user had copied, and text fields keep
their own copy and paste because JUCE's `TextEditor` consumes Cmd+C and Cmd+V before the key reaches
`MainComponent::keyPressed`.
