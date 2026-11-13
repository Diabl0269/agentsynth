# Custom module design: a folded macro with a face

Design, not built. A **custom module** is a macro whose folded card shows a **face**: a card layout the
user designs, whose controls point at parameters of the modules inside. There is no new node type and no
new library kind: any macro may get a face, a faced macro saves and pastes like any macro, and the sound
comes from its ordinary member modules. This doc records the decisions, the proposed data model, the
open choices (each with a recommended default marked **pending founder pick**) and the risks.

Background: [macros.md](macros.md) (the macro model and its deliberate limits),
[module-card-layout.md](../layout/module-card-layout.md) (the `CardLayout` v2 model, `CardBody` and the
on-card editor this reuses), [snippets-clipboard.md](../layout/snippets-clipboard.md) (snippet capture and
the id remap), [modulation.md](../modules/modulation.md#the-poly-toggle) (the Poly toggle's voice graph).

---

## Decisions

- **A custom module is a folded macro with a face; nothing else.** Why not a container node is settled
  in [macros.md](macros.md#macro-ports-are-proxy-nodes-on-a-flat-graph): fixed channel counts, uuid
  addressing and one latency domain. The face is presentation only, like the macro's name and colour.
- **The face lives on the macro record, not on a node.** A macro has no node to carry a `cardLayout`
  property, and the face must travel with the macro through project save, snippets and the clipboard,
  which already carry the macro record.
- **The face reuses `CardLayout` v2.** Every `ParamItem` on a face sets `node` (reserved in v2 for this)
  to the member that owns the parameter. The reader, writer, widget fallbacks, free positions, ranges,
  labels and sections are the existing ones.
- **Lifting two deliberate limits.** This work lifts "a macro is not a saveable library item" (a faced
  macro is saved as a snippet) and "no macro-level parameter exposure" (the face exposes member
  parameters). `MacroControlModule` stays a modulation knob bank: on a face it is just another member
  whose `macroN` knobs the user can place and rename ("your own knobs").
- **Undo.** Every face write goes through `AppUndoManager::recordGraphAndMacroChange`, one step per
  write, with the same rules as the on-card editor: a drop, a settled nudge, a panel field, an add or
  remove, a width switch; Cancel restores the opening face as one more step when it differs; a write
  that changes nothing records nothing. Cmd+Z while the editor is open re-syncs the overlay to the
  restored face. No new undo action type.
- **Copy, paste and snippet insert keep face controls on the right modules.** In a snippet or clipboard
  payload, `node` is written as the snippet node id through the same `idForUuid` map `members` and
  `ports` use (`SnippetManager.cpp`), renumbered through the same `idMap` on insert, and resolved to the
  copy's fresh uuid in the same loop that resolves members (`uuidForSnippetId`). An item whose node did
  not survive (not captured, failed to create) is dropped, exactly like a port whose node vanished. A
  macro is only captured when fully contained, so a partial selection carries no face.
- **Membership changes prune the face.** Removing a member (delete, take out, ungroup of a nested
  child into the parent is fine since descendants still count) drops that member's items in the same
  undo step. Ungrouping the macro itself drops the face.
- **Hosted plugins.** A face may show built-in module parameters only in the first version; a hosted
  plugin member's controls are listed in Add control as unavailable. Plugin parameter ids are not stable
  across plugin versions and their parameter sets are unknown to the validator.

---

## Proposed data model

```text
Macro
  ... existing fields (id, name, colour, collapsed, parentId, bounds, members, ports)
  face : MacroFace | absent          // absent = today's folded card
MacroFace
  width  : "single" | "double"       // 280 or 560 px, the two existing width buckets
  layout : CardLayout                // v2; written v2 always (usesV2Features is true: every item has node)
```

Rules on `layout` when it is a face:

- Every `ParamItem.node` is set: in a project, the uuid of a **descendant** member
  (`MacroSet::descendantMembers`) that is not a port node (`memberIsPort`); in a snippet or clipboard
  payload, that member's snippet node id.
- `hidden` is always empty and there is no **More** row. A face is "listed = shown": the v2 rule "absent
  = More" would put every parameter of every member (hundreds) in More. A parameter not on the face
  keeps its value, automation, MIDI Learn and modulation inside the macro.
- `Condition` gains an optional `node` (the condition's parameter's owner); on a face it is required.
- `ViewItem` gains an optional `node` the same way. Views on a face (an ADSR's envelope, a filter
  response) are a later step; the first version accepts param items only.
- `basedOn` is unused.

JSON: `"face": { "width": "single", "layout": { "version": 2, "sections": [ ... ] } }` inside the
macro's entry in the project's `"macros"` list and in a snippet's `macros` array. Written only when
set, so every faceless macro saves byte-identical. An older build ignores the key, loads the macro as a
plain macro, and drops the face if it saves the project again (stated in the release note, nothing to
guard: `Macro::fromVar` must keep ignoring unknown keys, which a test pins).

---

## The face validator

`validateMacroFace(face, members)` runs on every untrusted path: a snippet file insert, an imported
module file, and the AI path. It never relaxes `validatePatch`; it is a separate gate for a separate key.

- The layout parses with the existing v2 reader (version, known widget/view/presentation names, number
  ranges), and `width` is one of the two values.
- Bounds: at most 16 sections and 96 items; labels and section titles at most 40 characters; section
  ids at most 32. Free positions keep the reader's 0..4000 bound.
- Every `node` names a member of this macro (or a descendant) that is a built-in module and not a port
  node; every `paramId` exists on that node's module; every condition's `node`/`param` names a choice or
  bool parameter of a member.
- A failure on a file drops the face only (the modules and macro still load; the user is told "The
  module's face could not be read, so it shows as a plain macro"). A failure on the AI path refuses the
  whole request, so the generation side retries.

---

## Rendering and editing

- **The faced card.** `MacroCardComponent` draws the face instead of its preview boxes when `face` is
  set and the macro is collapsed. Jacks move from the side strips to module-card jack rows at the top
  (inputs left, outputs right, body below every jack, `getContentTopY`'s rule), so a single-width face
  keeps three knobs per row (**pending founder pick**, Q3). Its height is measured from the layout, as
  module cards are, and makes room through the existing macro make-room path.
- **`CardBody` needs a parameter source.** Today it binds every widget to one `ModuleBase`. A face
  needs each item resolved through `(node, paramId)` to a member's processor and parameter: attachments,
  MIDI Learn, automation markers, modulation rings and conditions all follow from the resolved
  parameter. Proposed: a small `CardBodyParamSource` interface with a single-module implementation (today)
  and a macro-face one. Knob-bound CV jacks do not exist on a face (its jacks are the macro's ports).
- **Cable dropped on a face knob.** Recommended: it creates an inlet port wired to that parameter's
  CV input through the existing auto-port path ([auto-ports.md](auto-ports.md)); a parameter with no CV
  input refuses the drop (**pending founder pick**; not on the board, small).
- **Editor.** "Edit Layout..." joins the macro menu (`GraphEditor::buildMacroMenu`) and opens the
  existing on-card editor over the faced card with a new `MacroFaceLayoutSource` (a
  `CardLayoutEditorSource`). Differences from a module card: the edit bar shows a Single | Double
  switch, Cancel and Done (no Preset, no Apply to: a face belongs to one macro); the Add control panel
  lists every member's parameters grouped by module, plus "New knob of your own", which raises the
  member Macros bank's `macroCount` (adding a Macros member first if none) and places the new knob.
- **First open.** Recommended (**pending founder pick**, Q1): a suggested face. Order: the Macros
  bank's visible knobs, then each member in signal order with its code default's `knobLarge` items, its
  first `segmented` item and an ADSR's stage faders, capped at 12 controls. The alternative is an empty
  face.
- **Width.** Recommended (**pending founder pick**, Q2): the user picks Single or Double; starts
  Single. The existing note "the custom module is the only card whose width a layout may choose" stands.
- **Poly.** Recommended (**pending founder pick**, Q4): the face footer shows one Poly pill when any
  member is poly-capable. State: on (all poly-capable members poly), off (none), mixed (some). A click
  sets the new value on the voice graph of every poly-capable member as one undo step (seed
  `PolyChainController` with all of them, unioning their voice graphs; the track question is asked once).
  The pill never toggles itself, as today.
- **Fold motion.** Folding a faced macro flies each member into the bounding box of its own controls on
  the face, or the card centre if it has none (`macro_preview::boxes` gains a face mode).

---

## Library, saving and sharing

- **Saving.** A faced macro is saved with the existing macro-menu "Save as Snippet..." (name prefilled
  from the macro) as a `.agsnip`. Recommended (**pending founder pick**, Q5): it stays in the Snippets
  section, with a card glyph on rows whose snippet holds exactly one top-level faced macro and nothing
  else; that row's drag ghost is the folded card's size, and it lands collapsed. Alternatives: a patch
  preset (rejected on the board: a preset replaces the whole patch) or a separate "My modules" listing
  of the same files.
- **Copies are independent** (**pending founder pick**, Q6): no link from a placed copy back to the
  library file.
- **Non-parameter state.** A `.agsnip` written to disk drops extra state today (`includeExtraState`
  false), so a Sampler's recording and a Wavetable's loaded file are not saved. Recommended (**pending
  founder pick**, Q7): keep that rule for the first version and say so in the save prompt; bundling
  audio with a saved module comes with sound packs. State that is safe and self-contained (an LFO's drawn
  wave, a custom filter's shapes) should be checked per module type: if it lives in extra state it is
  lost too, which is a known gap to close by moving it to parameters or a validated state key.
- **Sharing.** Recommended (**pending founder pick**, Q8): Export... on a snippet row and Import... on
  the Snippets header copy a `.agsnip` out of and into the snippets folder. Import runs the full
  untrusted insert path (`validatePatch` strict, then `validateMacroFace`) before the row appears.

---

## AI authoring

Recommended (**pending founder pick**, Q9): the AI can build a whole custom module, face included.

- **No `"macros"` key from the model**, ever ([macros.md](macros.md#ai-authorability)). A new root key
  (working name `"module"`) is a request the app carries out: `{ name, members: [patch node ids],
  face: { width, rows: [ { title, items: [ { node, param, widget?, label? } ] } ] } }`.
- **Separately gated**, the timeline's two-door shape: `validatePatch` still refuses `"macros"`, and the
  new key gets its own validator (members are nodes this same patch creates; every `node`/`param` pair
  exists; the face bounds above). Refused requests go back through the existing retry.
- **Reduced face form.** The model sends titled rows of controls only: no positions, ranges,
  conditions or views. The app turns rows into grid sections and lays them out, so the model never
  authors geometry.
- **Applying.** After the patch phase creates the nodes, the app groups them with the existing grouping
  path (auto ports splice the crossing cables), collapses the macro and sets the face, all in the patch's
  one undo step. The plan card words it ("Adds a custom module with 6 modules and 4 controls on its
  face").
- **Measuring.** The schema grows; measure the pass rate with `Tools/AIPatchHarness` before and after,
  and advertise the key only on the request types that need it if it costs pass rate elsewhere.

---

## Open choices

| # | Choice | Recommended default |
|---|---|---|
| Q1 | How a new face starts | Suggested face (pending founder pick) |
| Q2 | Face width | User picks Single or Double (pending founder pick) |
| Q3 | Jacks on a face | Module-style rows at the top (pending founder pick) |
| Q4 | Poly with disagreeing members | One pill with a mixed state (pending founder pick) |
| Q5 | Where a saved one lives | Snippets section, card glyph (pending founder pick) |
| Q6 | Linked copies | No, copies are independent (pending founder pick) |
| Q7 | Sampler recordings | Not saved in the first version (pending founder pick) |
| Q8 | Sharing | Export and Import a file (pending founder pick) |
| Q9 | AI | Builds a whole custom module (pending founder pick) |
| - | Cable dropped on a face knob | Auto-creates an inlet port (pending founder pick) |
| - | Views on a face | A later step (pending founder pick) |

---

## Risks

- **`CardBody` generalisation** is the largest piece: it is written around one module (conditions, dim
  rules, the Poly pill, modulation rings, the size estimate). Doing it behind a parameter-source interface
  keeps the single-module path byte-identical (the card-body goldens pin it).
- **Stale references.** Any membership change that forgets to prune leaves an item pointing at a node
  outside the macro. Mitigation: prune in `MacroSet` itself whenever members change, and have the reader
  drop items whose node is not a descendant (a test per membership path).
- **Older builds drop the face** on re-save. Acceptable; noted in the release note.
- **Lost extra state** in saved modules (recordings, possibly drawn LFO waves) can surprise users; the
  save prompt says what is left out.
- **AI pass rate.** A larger schema can lower the pass rate of ordinary patches; measured, and the key
  can be offered only when asked for.
- **Face height.** A double-width face with many controls can be very tall; the make-room path handles
  it, but the suggested face caps at 12 controls.

---

## Tests (when built)

- `Tests/Macros/MacroFaceTests.cpp`: face round trip through `Macro::toVar`/`fromVar`; absent face is
  byte-identical; an unknown key is ignored by an older-shaped reader; membership changes prune items.
- `Tests/Macros/MacroFaceValidatorTests.cpp`: unknown node, port node, unknown paramId, over-long label,
  too many items and a bad width each refuse; a valid face passes.
- `Tests/SnippetManager*`: a faced macro's face survives copy/paste and snippet insert with `node`
  remapped to the copies; an item whose node was not captured is dropped; a partial selection carries no
  face; a hand-edited `.agsnip` with a bad face loads its modules without the face.
- `Tests/UI/Graph/...`: one undo step per face write and for Cancel; the Poly pill's mixed state and its
  one-step click; the faced card's measured height matches the built card.
- AI: the module request is refused for every validator rule and applied in one undo step otherwise.

## Accessibility (when built)

Every face control follows the card rules ([module-card-layout.md](../layout/module-card-layout.md#accessibility)),
with its owning module in the accessible description ("Wobble, knob, Macros knob 1"). The width switch is
one Tab stop (Left/Right), titled "Face width" with a tooltip. The Poly pill reads its mixed state ("Poly,
mixed, 2 of 3 modules") and its tooltip names what a click does. Add control rows read "<name>, <module>,
N of M". The app's accessibility coverage check passes with no new exemption.

## Docs updates (when built)

`macros.md` (the two lifted limits), `macro-cards.md` (the faced card), `module-card-layout.md` (face
rules on `ParamItem.node`, `Condition.node`), `snippets-clipboard.md` (the face remap),
`../ai/patch-format.md` (the module request key), and this doc moves from design to "as built".
