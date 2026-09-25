# Plugin card layout — which hosted-plugin parameters show as knobs

A hosted VST3/AU plugin's module card today shows **no parameters** — only "Open Editor". This doc
decides how a user picks the parameters a plugin card shows as knobs, how that choice is scoped
(this instance vs. every instance of that plugin), how it is saved as presets, and why the
data type it introduces — `CardLayout` — is the seed of the future "edit any module's layout"
feature (see [Future: editing any module's layout](#future-editing-any-modules-layout-out-of-scope-here)), which is otherwise **out of scope** here.

**Status:** built. The data layer (FRO126): `CardLayout`, the precedence resolver, the automatic
default, `PluginCardLayoutStore`, the per-instance `"cardLayout"` extra-state key and its undo seam.
The card unit and `HostedParameterAttachment` (FRO128), described in
[Card rendering as built](#card-rendering-as-built-fro128). The picker (FRO132), described in
[Choosing knobs as built](#choosing-knobs-as-built-fro132) -- gesture-only touch-to-add; the value-
change fallback is FRO241. The MIDI Learn / Automate right-click on a plugin-card knob is still
designed only.

---

## Today

- `HostedPluginModule` (`Source/Plugin/Hosting/`) owns a `juce::AudioPluginInstance`. The
  instance's parameters are `juce::HostedAudioProcessorParameter`s (a sibling hierarchy to
  `RangedAudioParameter`: no `NormalisableRange`, native domain always 0..1, text via
  `getText`). The module's own `getParameters()` holds only `muted`.
- `HostedPluginModule::getInstanceParameters()` returns `{index, paramId, displayName}` per
  parameter — `paramId` is the plugin's stable id or a synthetic `"legacy:<index>"` — built for
  the automation lane picker. `findInstanceParameter(paramId)` /
  `findInstanceParameterByIndex` / `getInstanceParamIndexFallback` back
  `synth::resolveLaneParameter`'s hosted rules (exact id → index hint rescue → drift orphans).
- Before FRO128, `ModuleComponent::createControls()`'s `HostedPluginModule` branch built one "Open
  Editor" button and nothing else; it now calls the card unit described below. Per-type card units already exist as the precedent for a bespoke body
  (`ModuleComponentEQCard.cpp`, `ModuleComponentEnvelopeCard.cpp`, `ModuleComponentWavetable.cpp`).
- Plugin identity for persistence is `PluginIdentity {format, name, uid}` (no path), carried in
  the node's extra state with the plugin's opaque state blob; extra state is applied on the
  **trusted path only** and `HostedPlugin` is never authorable by a model
  (`AIStateMapper::kNonAuthorableModuleTypes`).
- There is **no per-plugin-type store** anywhere: `PluginScanService` persists only the scan
  list and blacklist (`UserSettings::kPluginScanListSettingKey`).

---

## Goals

1. A plugin card shows a **small, chosen set of its parameters as ordinary knobs** (and toggles /
   choice combos where the parameter is discrete), bound live in both directions.
2. Choosing is **simple**: tick parameters in a searchable list, or **touch them in the plugin's
   own editor** and they appear (Ableton "Configure").
3. The choice can apply to **this instance only** or to **every instance of that plugin**
   (Bitwig device pages / "save as default"), and can be **saved and loaded as named presets**.
4. Those knobs are first-class: MIDI Learn, automation lanes and undo treat them exactly like a
   built-in module's knobs.
5. A sensible default exists before anyone chooses anything.

---

## The `CardLayout` type and where a layout comes from

```text
CardLayout
  version : 1
  slots[] : Slot
Slot
  paramId : string          // the hosted parameter's stable id (or "legacy:<index>")
  indexHint : int           // captured at creation, the same rescue rule as an automation lane
  label   : string | null   // user override; null = the parameter's own name
  kind    : knob | toggle | choice | auto   // auto = derived from the parameter (isDiscrete/isBoolean/getNumSteps)
```

`CardLayout` is a plain value type in `Source/Modules/CardLayout.h` (Core), deliberately
**not** plugin-specific — [Future: editing any module's layout](#future-editing-any-modules-layout-out-of-scope-here) reuses it for built-in modules. A hosted card's live layout is
resolved by precedence, first hit wins:

1. **Per-instance override** — the node's extra state, key `"cardLayout"` (next to the plugin
   identity and state blob). Trusted path only, like everything in extra state; scrubbed by
   `HostedPluginModule::getExtraState` when writing an untrusted-apply-carrying format is never
   a concern because the key is simply absent from provider output.
2. **Per-plugin-type user default** — a file
   `<settings>/PluginCardLayouts/<format>-<uid>/default.json` (uid is the stable plugin
   identifier; the plugin's name is stored inside for the picker's display). Presets are
   sibling files `<name>.json` in the same directory.
3. **Automatic default** — the first **8** parameters of the instance for which
   `isAutomatable()` is true, skipping `getBypassParameter()` and any parameter whose name is
   exactly "Bypass". Never persisted: it is recomputed, so a plugin update that reorders
   parameters just yields a different automatic set rather than a stale file.

The automatic default (rule 3) is **shown** on the card as knobs before the user chooses
anything — there is no separate "unconfigured" state distinct from "showing the automatic set"
(the automatic default is always shown, never a separate blank state). The "empty layout" case (Open Editor + **Choose knobs…** as the
whole card body) therefore only occurs when the plugin has no automatable parameters at all.

A slot whose `paramId` no longer resolves on the live instance is **hidden from the card
entirely**; the picker instead shows a line, *"N parameters missing in this plugin version"*,
listing them, and they are dropped from the layout the next time the user saves it — the same
"degrade visibly, repair explicitly" rule as automation lanes, except the "visibility" half of
that phrase now lives in the picker's missing-params line, not in a greyed knob on the card.

A layout's slot count is uncapped in the model; the card shows them in the ordinary knob grid
(`layoutKnobGrid`, width buckets from [`layout/module-card.md`](../layout/module-card.md#width-buckets)),
growing the card's height like any module with many parameters. An empty layout shows the "Open Editor" button and a **Choose knobs…**
button as the whole body.

---

## Data layer as built (FRO126)

- `Source/Modules/CardLayout.{h,cpp}` (Core): `CardLayout`/`CardSlot`, `toVar`/`fromVar`
  (`ParseStatus::UnsupportedVersion` for a newer file, `Malformed` for anything else, including a
  slot with no `paramId` or an unknown `kind` — refused whole, never partly loaded), and
  `deriveSlotKind` / `effectiveSlotKind`.
- `Source/Plugin/Hosting/HostedPluginCardLayout.{h,cpp}` (AppUI, beside the store):
  `resolveHostedCardLayout(node, store)` returns a `ResolvedCardLayout` — the source
  (`Instance` / `PluginDefault` / `Automatic`), the layout as stored, and each slot bound to its
  live parameter. Binding goes through `resolveLaneParameter` itself, so a slot rescues and orphans
  exactly like an automation lane. An override or stored default that does not parse is skipped, not
  fatal, and left untouched. While no instance is live a slot is *unresolved*, not orphaned (the
  plugin may still be loading); `layoutWithoutOrphans()` is what a save writes.
- `Source/Plugin/Hosting/PluginCardLayoutStore.{h,cpp}` (AppUI): one directory per plugin,
  `<format>-<uid>` (a uid of 0 falls back to `<format>-name-<name>`), holding `default.json` and
  `<preset>.json`. Each file is the layout plus the plugin's identity, so the picker can show a name
  for a plugin that is not loaded. `default` is a reserved preset name, compared case-insensitively.
  The `Listener` fires after `setDefault` / `clearDefault` only, never for presets.
- Per-instance override: `HostedPluginModule::getCardLayoutOverride` / `setCardLayoutOverride`
  (stored as opaque JSON, so the module needs no knowledge of `CardLayout`), in
  `HostedPluginModuleCardLayout.cpp`.
- Undo: `AppUndoManager::recordNodeExtraStateChange` takes before/after **layout-only patches**
  (`HostedPluginModule::makeCardLayoutPatch`), never a full `getExtraState()` — that carries the
  plugin's state blob and would make undo re-load the plugin.

---

## Rendering: `ModuleComponentHostedPluginCard.cpp`

A per-type card unit (the EQ/Envelope precedent), taking the `HostedPluginModule` branch **out** of
`createControls()` rather than growing it (that function is at the function-size ratchet ceiling —
`Source/UI/CLAUDE.md`). Per resolved slot it creates the same widget the generic path would for that
kind (rotary `juce::Slider`, `ToggleButton`, `ComboBox` from the parameter's value strings), labelled
with `label` or the parameter's name, and binds it with a **`HostedParameterAttachment`**
(`Source/UI/Graph/ModuleComponent/HostedParameterAttachment.h`):

- slider range 0..1 normalised; text via `param.getText(value, 1024)` / `getValueForText` (not a
  max length of 0: the VST3 wrapper truncates to it and would return an empty string);
- a combo's items are `getAllValueStrings()` (or, for a slot forced to Choice on a parameter with none,
  its steps when there are 2..64) and index `i` of `N` items is normalised `i / (N - 1)` — the item
  count, never `getNumSteps()`, which is `0x7fffffff` for a parameter that does not override it;
- writes: `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture` on the hosted
  parameter. A drag is one gesture; any other change (typed text, a click, a wheel notch) wraps its own
  begin/end pair around its single set;
- reads: an `AudioProcessorParameter::Listener` **per bound parameter** (not an instance-wide
  `AudioProcessorListener`) whose `parameterValueChanged` may arrive on any thread and only stores the
  value and triggers an `AsyncUpdater` (the `HostedPluginModule::InstanceListener` idiom); the widget is
  updated from `handleAsyncUpdate` with `dontSendNotification`, so a widget-driven write never echoes.
  Automation writes a hosted parameter with a plain `setValue` that no listener hears, so
  `ModuleComponent::reflectParameterValue` also feeds the attachment that owns the parameter.

### Card rendering as built (FRO128)

- **What is drawn.** Every slot whose parameter resolves. An **orphaned** slot, and a slot with no live
  parameter yet (the plugin is still loading), renders nothing; only the picker will mention them. Each
  widget is added to the card's existing `sliders` / `comboBoxes` / `toggles` arrays (with a null entry
  in the index-parallel `sliderParams` / `comboParams`: a hosted parameter is not a
  `RangedAudioParameter`), so the generic layout places them and the card grows like any many-parameter
  module; after a rebuild the card re-measures and asks the canvas to accept the new size
  (`refreshPortLayout`). Component ids are `hostedKnob:<paramId>` / `hostedToggle:` / `hostedChoice:`.
  A Choice slot with fewer than two entries is drawn as a knob.
- **Chrome.** One row at the top of the body: **Open Editor** and **Choose knobs...** (id
  `chooseKnobs`), each half of the narrow band. **Choose knobs...** only fires
  `ModuleComponent::onChooseKnobsRequested`, which the picker will set. With no automatable parameters
  the two buttons are the whole body.
- **Rebuild triggers.** The instance going live (a card is built before an async load publishes, so it
  starts with the buttons only), the per-instance override changing (`HostedPluginModule::onCardLayoutChanged`,
  a single slot the card owns), and `PluginCardLayoutStore::Listener::layoutChangedForPlugin` for this
  module's identity. The store is owned by `MainComponent` (declared before the `GraphEditor`) and
  injected with `GraphEditor::setPluginCardLayoutStore`; a null store is fine (`resolveHostedCardLayout`
  accepts one).
- **Undo.** Hosted parameters are **not** routed through `ModuleComponent::parameterGestureChanged`: it
  keys on an `int` index into the module's own parameter array, where a hosted index 0 would collide with
  `muted`. The attachment forwards every gesture on its parameter (the card's own widget, the plugin's
  editor, a controller) to `ModuleComponent::handleHostedGesture`, which captures the graph at the first
  start and pushes the snapshot at the last end: one begin/end pair, one undo step, and overlapping
  gestures still make one.
- **Not yet wired.** The card registers nothing with the MIDI Learn registry (`registerMidiLearnable`
  takes a `RangedAudioParameter*`) and adds no right-click Automate; hosted widgets are inert to
  right-click. That comes with the picker work.

### Instance lifetime and unbinding

An attachment holds a listener on a parameter that belongs to the plugin instance, so it has to be
removed **before** that instance is freed. `HostedPluginModule` gained a multi-observer for the two edges
of `hasInstance()`, `HostedPluginModule::InstanceObserver` (`hostedInstanceGone` / `hostedInstanceLive`),
fired from the same two sites as `onInstanceChanged` and with the same ordering guarantee: the gone edge
fires while the instance is still alive and before `reapRetired()` can free it, and the module's
destructor fires it too, before it frees the instance. A poll could not win that race. The card is the
observer (`ModuleComponent::HostedCardBinding`): gone unbinds every attachment synchronously and empties
the body (the re-measure is deferred one loop turn, because a node delete fires this edge from the
module's destructor with the node already out of the graph); live rebuilds from `resolveHostedCardLayout`.

`detachFromProcessor` leaves the observers first and unbinds, reaching the module only through a
`WeakReference`; if the module or the bound instance is already gone the attachments are abandoned rather
than detached, so no freed parameter is ever touched. Which paths reach which half:

- undo/redo restore, Load, New Patch, AI apply: `GraphEditor::detachAllModuleComponents` calls every
  card's `detachFromProcessor` before the nodes are freed;
- `deleteSelection`, `requestDeleteModule`, `replaceModule` ("Replace with..."): these fire
  `onBeforeDetachAllModuleComponents` (the mixer's seam) but free the node **before** the card is torn
  down in `updateComponents()` and do not call the card's `detachFromProcessor` first; the module's own
  gone edge, fired from its destructor, is what unbinds the card there.

---

## Choosing knobs

Entry points: **Choose knobs…** on the card body (next to Open Editor) and the same item in the
card's right-click menu (`buildModuleContextMenu`). It opens `PluginKnobPicker`
(`Source/UI/Graph/PluginKnobPicker/`), a popover anchored to the card:

```text
┌ Knobs for "Serum" ──────────────────────────────────────────────┐
│ Apply to:  (• This instance)  ( All Serum instances )           │
│ Preset:  [Default ▾]  [Save as…] [Delete] [Reset to automatic]  │
│ ┌ search ─────────────┐   [ ] Touch in the plugin editor to add │
│ │ filt                │                                          │
│ └─────────────────────┘                                          │
│  ☑ Filter Cutoff        ≡     label: [Cutoff   ]                 │
│  ☑ Filter Resonance     ≡     label: [          ]                │
│  ☐ Filter Drive                                                  │
│  ☐ Filter Type                                                   │
│  …  (all 512 parameters, checked ones first, drag ≡ to reorder) │
└──────────────────────────────────────────────────────────────────┘
```

- **Apply to** decides which store the picker edits: *This instance* writes the node's
  `"cardLayout"` extra state (undoable via `recordParameterChange`-style snapshot of the node's
  extra state — a new `AppUndoManager::recordNodeExtraStateChange`); *All <plugin> instances*
  writes `default.json` for the type and **clears** this instance's override so it follows the
  default. Every open instance of the type without an override re-resolves and rebuilds its
  card (a `PluginCardLayoutStore::Listener` broadcast on the message thread).
- **Preset** lists the type's saved layouts; *Save as…* names the current slot list; loading a
  preset copies it into whichever scope *Apply to* selects; *Reset to automatic* removes the
  chosen scope's layout so precedence falls through.
- **Touch in the plugin editor to add**: while ticked, a parameter that reports a **gesture
  start** on the instance (`parameterGestureChanged(index, true)`) is appended to the layout. For now it
  listens to gestures only. Many plugins never emit gestures; a **value-change fallback**
  (debounced so an automation-driven or preset-load burst — more than 3 distinct parameters
  within 200 ms — is ignored rather than adding all of them) is deferred to a later ticket. The
  picker opens the plugin's editor window when this is ticked and it is not already open.
- Changes apply live to the card as they are made (no OK button); closing the popover keeps
  them.

### Choosing knobs as built (FRO132)

Three units under `Source/UI/Graph/PluginKnobPicker/`, matching the design above:
`PluginKnobPickerComponent` (the popover itself, split by concern into
`PluginKnobPickerComponent.cpp` (chrome/layout), `PluginKnobPickerComponentRows.cpp` (search,
tick/untick, drag-reorder, label, the one `applyCurrentLayout()` write path) and
`PluginKnobPickerComponentScope.cpp` (Apply to / presets / Reset to automatic)),
`PluginKnobPickerRow` (one row's checkbox/name/drag-handle/label controls) and
`PluginKnobPickerTouchCapture` (touch-to-add's gesture listener).

- **Entry points.** `ModuleComponent::showPluginKnobPicker()` (`ModuleComponentHostedPluginCard.cpp`)
  builds the picker and opens it via a `juce::CallOutBox` anchored to the **Choose knobs...** button
  (`createHostedPluginControls()` wires `onChooseKnobsRequested` to it) or the card's own right-click
  menu item (`ModuleComponentInteraction.cpp::buildModuleContextMenu`, offered only when the node is a
  `HostedPluginModule`). The actual `juce::CallOutBox::launchAsynchronously` call sits behind a
  protected virtual, `launchPluginKnobPickerCallOutBox`, so a headless test can drive the real
  button-click / menu-click handler without constructing a real top-level window.
- **The row list.** Checked rows (in the layout's own order) first, then the rest in the instance's
  parameter order, both filtered by the live search text against display name. A checked row shows a
  drag handle (reordering is scoped to the checked group only) and a label field that commits on
  focus-lost/Return; empty text means "the parameter's own name", matching the card's own fallback.
  Every tick, untick, reorder, or label commit calls the same `applyCurrentLayout()`, which writes
  whichever scope "Apply to" currently names and replays through
  `AppUndoManager::recordNodeExtraStateChange` with a layout-only patch (`HostedPluginModule::
  makeCardLayoutPatch`) -- the same undo shape a knob's own drag gesture already uses.
- **Apply to / presets**, built as designed above, with one simplification: the Preset combo lists
  only the plugin's SAVED presets (`PluginCardLayoutStore::listPresets`) -- there is no separate
  "Default" pseudo-entry in the combo, since "Reset to automatic" already covers falling through to
  the plugin's stored default (or the automatic set) and a combo entry for it would just be a second
  path to the same place. Switching "Apply to" immediately re-applies the picker's current working set
  to the newly selected scope (writing the new scope and, for "All instances", clearing this
  instance's own override), rather than waiting for a further edit -- so the scope switch itself is
  what the design doc's "clears this instance's override so it follows the default" sentence means in
  practice.
- **Touch to add is gesture-only, as decided** (no value-change fallback -- FRO241).
  `PluginKnobPickerTouchCapture` registers a `juce::AudioProcessorParameter::Listener` on every
  parameter of the live instance while armed; `parameterGestureChanged` can arrive on ANY thread (the
  plugin's own editor, a controller), so every callback only queues the touched parameter's index and
  calls `triggerAsyncUpdate()` -- `onParameterTouched` is invoked only from `handleAsyncUpdate()`, on
  the message thread, exactly like `HostedParameterAttachment` and `HostedPluginModule::
  InstanceListener` already do for their own hosted-parameter callbacks. Arming also asks the owner to
  open the plugin's editor (the same `onOpenPluginEditorRequested` the card's own "Open Editor" button
  uses, which is idempotent if it is already open).
- **Missing parameters.** A slot loaded from the resolved layout (or a preset) whose `paramId` does
  not match any of the instance's current parameters gets no row and no checkbox; the picker instead
  shows a "N parameters missing in this plugin version: ..." line naming them. The very next apply
  (any tick, reorder, label, scope switch, or preset load) writes only the resolvable slots, so the
  missing ones are dropped from storage at that point -- "dropped on next save" in practice means
  "dropped the moment the user touches the picker again," since every gesture here already applies.
- **Tests:** `Tests/UI/Graph/PluginKnobPicker/PluginKnobPickerTests.cpp`, against the same
  `StubPluginInstance` fake the FRO126/FRO128 tests use.

---

## Persistence

- **Per-instance:** extra-state key `"cardLayout"` on the `HostedPlugin` node, saved with the
  project and with snippets; restored trusted-only with the rest of extra state.
- **Per-type default and presets:** JSON files under the settings folder
  (`branding::kSettingsFolderName`), never inside the `PropertiesFile`; the store is an
  app-layer class (`PluginCardLayoutStore`, `Source/Plugin/Hosting/`) injected into the card
  and the picker — Core never reads `ApplicationProperties`. Import/export of a preset is a
  file copy.
- **Versioning:** `version: 1`; a higher version is refused visibly.

---

## Interaction with MIDI Remote and automation

Nothing special — that is the point of binding real hosted parameters rather than proxies:

- **MIDI Learn** on a plugin-card knob creates a project assignment targeting
  `(nodeUuid, paramId, indexHint)`, resolved by `resolveLaneParameter`'s hosted rules.
- **Automate** on a plugin-card knob calls the existing `onAutomateParameterRequested` with the
  hosted parameter, which the lane picker already supports.
- A parameter removed from the layout keeps its lanes and assignments — the layout is
  presentation, never a binding.

---

## Future: editing any module's layout (out of scope here)

Right-click → **Edit layout…** on *any* module is parked as its
own epic because a built-in card's controls are created by type-specific code, not from data,
and turning that into a data-driven layout is a larger refactor of `ModuleComponent` than this
feature needs. What carries over when that epic starts:

- **`CardLayout` is the type.** For a built-in module a slot's `paramId` is the module's own
  `paramID`; `kind: auto` derives the widget the same way; `label` overrides the name; slot
  order is the grid order; parameters absent from the layout are hidden.
- **The same precedence** (instance override in node extra state → per-module-type user default
  under `<settings>/ModuleCardLayouts/<ModuleType>/` → the type's code-defined layout).
- **The same picker** with "Apply to: this instance / all <Module> modules" and presets.
- A future **focus bank** in MIDI Remote ([`midi-remote.md`](midi-remote.md)) follows a card's slot order.

Bespoke cards (EQ, Envelope, Wavetable, Sampler) are the hard part: their bodies are not a knob
grid, so "edit layout" there means at most hide/reorder of the knobs they *do* expose.

---

## Tests

- `Tests/Modules/CardLayoutTests.cpp`: round-trip, `kind: auto` derivation, orphan slot
  behaviour, precedence resolution with all three sources, automatic default rule
  (`isAutomatable`, bypass skipped, first 8).
- `Tests/Plugin/PluginCardLayoutStoreTests.cpp`: default/preset files, listener broadcast,
  version refusal. Writing a default clears nothing by itself: dropping instance overrides when the
  user picks "All instances" is the picker's job.
- `Tests/Plugin/HostedPluginTests.cpp`: the `"cardLayout"` key round-trips through the trusted
  extra-state path, a layout-only patch never reloads the plugin, and untrusted apply never sets it.
- `Tests/UI/Graph/ModuleComponent/HostedPluginCardTests.cpp` (FRO128; uses the headless
  `HostedPluginTests` fake instance): slots render as the right widget with the right label, an empty
  layout shows the two buttons, an orphan slot renders no widget, the automatic default / per-instance
  override / stored default each rebuild the card, `HostedParameterAttachment` round-trips value and text
  (slider, toggle, combo at several sizes), a plugin-side change reaches the widget only after the message
  loop runs (from another thread and from the message thread), a widget write does not echo, one gesture
  pair is one undo step, the instance-gone edge fires before anything can be freed, and the card unbinds on
  unload, replace, node delete, `detachAllModuleComponents` and a detach after its node was freed. The picker
  listing an orphan as missing belongs to the picker's tests.
- `Tests/UI/Graph/PluginKnobPicker/PluginKnobPickerTests.cpp`: search, tick/untick, reorder,
  label, scope switch (clears the override and broadcasts), presets (save/load/delete, reset to
  automatic), touch-to-add via a real gesture and its off-thread -> message-thread hop, missing
  parameters, and the two real-gesture entry points (the card button and the context-menu item,
  each through a real click handler with a stubbed `juce::CallOutBox`). The value-change fallback
  and its burst-ignored debounce are deferred to a follow-up (FRO241), see
  [Choosing knobs](#choosing-knobs).
- `Tests/Plugin/HostedPluginLaneTests.cpp` gains: MIDI Learn and Automate from a plugin-card
  knob produce the same target triple as the lane picker.
- E2E: add plugin → Choose knobs → tick two → save project → reload → knobs present → set as
  default for all → second instance shows them.

---

## Related

- [`midi-remote.md`](midi-remote.md) · [`midi-remote-ui.md`](midi-remote-ui.md)
- [`modules.md`](../modules/modules.md#hosted-plugin-module-third-party-vst3--au-hidden) — hosted plugin channel rules · [`modulation.md`](../modules/modulation.md)
  — `resolveLaneParameter`'s hosted rules
- [`layout/module-card.md`](../layout/module-card.md) — knob grid and width buckets
