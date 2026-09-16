# Plugin card layout — which hosted-plugin parameters show as knobs

Design decided 2026-09-17, same founder session as [`midi_remote.md`](midi_remote.md). A hosted
VST3/AU plugin's module card today shows **no parameters** — only "Open Editor". This doc
decides how a user picks the parameters a plugin card shows as knobs, how that choice is scoped
(this instance vs. every instance of that plugin), how it is saved as presets, and why the
data type it introduces — `CardLayout` — is the seed of the future "edit any module's layout"
feature (§8), which is otherwise **out of scope** here.

**Status:** designed, not built. §10 is the record of what exists.

---

## 1. Today

- `HostedPluginModule` (`Source/Plugin/Hosting/`) owns a `juce::AudioPluginInstance`. The
  instance's parameters are `juce::HostedAudioProcessorParameter`s (a sibling hierarchy to
  `RangedAudioParameter`: no `NormalisableRange`, native domain always 0..1, text via
  `getText`). The module's own `getParameters()` holds only `muted`.
- `HostedPluginModule::getInstanceParameters()` returns `{index, paramId, displayName}` per
  parameter — `paramId` is the plugin's stable id or a synthetic `"legacy:<index>"` — built for
  the automation lane picker. `findInstanceParameter(paramId)` /
  `findInstanceParameterByIndex` / `getInstanceParamIndexFallback` back
  `synth::resolveLaneParameter`'s hosted rules (exact id → index hint rescue → drift orphans).
- `ModuleComponent::createControls()`'s `HostedPluginModule` branch
  (`Source/UI/Graph/ModuleComponent/ModuleComponent.cpp`) builds one "Open Editor" button.
  Per-type card units already exist as the precedent for a bespoke body
  (`ModuleComponentEQCard.cpp`, `ModuleComponentEnvelopeCard.cpp`, `ModuleComponentWavetable.cpp`).
- Plugin identity for persistence is `PluginIdentity {format, name, uid}` (no path), carried in
  the node's extra state with the plugin's opaque state blob; extra state is applied on the
  **trusted path only** and `HostedPlugin` is never authorable by a model
  (`AIStateMapper::kNonAuthorableModuleTypes`).
- There is **no per-plugin-type store** anywhere: `PluginScanService` persists only the scan
  list and blacklist (`UserSettings::kPluginScanListSettingKey`).

---

## 2. Goals

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

## 3. The `CardLayout` type and where a layout comes from

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
**not** plugin-specific — §8 reuses it for built-in modules. A hosted card's live layout is
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

Slots whose `paramId` no longer resolves on the live instance render as an **orphan knob**
(greyed, tooltip "parameter not found in this plugin version") and are dropped when the user next
saves the layout — the same "degrade visibly, repair explicitly" rule as automation lanes.

A layout's slot count is uncapped in the model; the card shows them in the ordinary knob grid
(`layoutKnobGrid`, width buckets from `layout.md`), growing the card's height like any module
with many parameters. An empty layout shows the "Open Editor" button and a **Choose knobs…**
button as the whole body.

---

## 4. Rendering: `ModuleComponentHostedPluginCard.cpp`

A new per-type card unit (the EQ/Envelope precedent), taking the `HostedPluginModule` branch
**out** of `createControls()` rather than growing it (that function is at the function-size
ratchet ceiling — `Source/UI/CLAUDE.md`). Per slot it creates the same widget the generic path
would for that kind (rotary `juce::Slider`, `ToggleButton`, `ComboBox` from the parameter's
`getAllValueStrings()`), labelled with `label` or the parameter's name, and binds it with a new
**`HostedParameterAttachment`** (`Source/UI/Graph/ModuleComponent/HostedParameterAttachment.h`):

- slider range 0..1 normalised; text via `param.getText(value, 0)` / `getValueForText`;
- writes: `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture` on the hosted
  parameter, so the plugin, the automation recorder and the undo gesture listener see a normal
  gesture;
- reads: an `AudioProcessorParameter::Listener` **per bound parameter** (not an instance-wide
  `AudioProcessorListener`) whose `parameterValueChanged` may arrive on any thread and is hopped
  with an `AsyncUpdater` (the `HostedPluginModule::InstanceListener` idiom) before touching the
  slider; re-entrancy guarded so a slider-driven write does not echo.

The card's own `parameterGestureChanged`-based undo capture works unchanged because the
attachment emits gestures on the hosted parameter and the card listens on the parameters it
registered. The unit registers every slot with the MIDI Learn registry
(`midi_remote_ui.md` §1) using the `(nodeUuid, paramId, indexHint)` triple.

Unbind discipline: the attachment holds raw pointers into the instance, so the card unbinds
in `GraphEditor::onBeforeDetachAllModuleComponents` and before a "Replace with…" / delete —
the same seam the mixer's bound controls use (`Source/UI/CLAUDE.md`).

---

## 5. Choosing knobs

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
  start** on the instance (`parameterGestureChanged(index, true)`) is appended to the layout.
  Many plugins never emit gestures, so the fallback is explicit: any **value change** on a
  parameter not already in the layout while the picker is open counts as a touch, debounced so
  an automation-driven or preset-load burst (more than 3 distinct parameters within 200 ms) is
  ignored rather than adding all of them. The picker opens the plugin's editor window when this
  is ticked and it is not already open.
- Changes apply live to the card as they are made (no OK button); closing the popover keeps
  them.

---

## 6. Persistence

- **Per-instance:** extra-state key `"cardLayout"` on the `HostedPlugin` node, saved with the
  project and with snippets; restored trusted-only with the rest of extra state.
- **Per-type default and presets:** JSON files under the settings folder
  (`branding::kSettingsFolderName`), never inside the `PropertiesFile`; the store is an
  app-layer class (`PluginCardLayoutStore`, `Source/Plugin/Hosting/`) injected into the card
  and the picker — Core never reads `ApplicationProperties`. Import/export of a preset is a
  file copy.
- **Versioning:** `version: 1`; a higher version is refused visibly.

---

## 7. Interaction with MIDI Remote and automation

Nothing special — that is the point of binding real hosted parameters rather than proxies:

- **MIDI Learn** on a plugin-card knob creates a project assignment targeting
  `(nodeUuid, paramId, indexHint)`, resolved by `resolveLaneParameter`'s hosted rules.
- **Automate** on a plugin-card knob calls the existing `onAutomateParameterRequested` with the
  hosted parameter, which the lane picker already supports.
- A parameter removed from the layout keeps its lanes and assignments — the layout is
  presentation, never a binding.

---

## 8. Future: editing any module's layout (out of scope here)

The founder also asked for right-click → **Edit layout…** on *any* module. It is parked as its
own epic because a built-in card's controls are created by type-specific code, not from data,
and turning that into a data-driven layout is a larger refactor of `ModuleComponent` than this
feature needs. What carries over when that epic starts:

- **`CardLayout` is the type.** For a built-in module a slot's `paramId` is the module's own
  `paramID`; `kind: auto` derives the widget the same way; `label` overrides the name; slot
  order is the grid order; parameters absent from the layout are hidden.
- **The same precedence** (instance override in node extra state → per-module-type user default
  under `<settings>/ModuleCardLayouts/<ModuleType>/` → the type's code-defined layout).
- **The same picker** with "Apply to: this instance / all <Module> modules" and presets.
- A future **focus bank** in MIDI Remote (`midi_remote.md` §9) follows a card's slot order.

Bespoke cards (EQ, Envelope, Wavetable, Sampler) are the hard part: their bodies are not a knob
grid, so "edit layout" there means at most hide/reorder of the knobs they *do* expose.

---

## 9. Tests

- `Tests/Modules/CardLayoutTests.cpp`: round-trip, `kind: auto` derivation, orphan slot
  behaviour, precedence resolution with all three sources, automatic default rule
  (`isAutomatable`, bypass skipped, first 8).
- `Tests/Plugin/PluginCardLayoutStoreTests.cpp`: default/preset files, listener broadcast,
  version refusal, "All instances" clears overrides.
- `Tests/UI/Graph/ModuleComponent/HostedPluginCardTests.cpp` (uses the existing headless
  `HostedPluginTests` fake instance): slots render as the right widget, empty layout shows the
  two buttons, orphan knob paints grey, `HostedParameterAttachment` round-trips value and text,
  a plugin-side change reaches the slider on the message thread, unbind on detach.
- `Tests/UI/Graph/PluginKnobPickerTests.cpp`: search, tick/untick, reorder, label, scope
  switch, presets, touch-to-add via gesture and via value-change fallback, burst ignored.
- `Tests/Plugin/HostedPluginLaneTests.cpp` gains: MIDI Learn and Automate from a plugin-card
  knob produce the same target triple as the lane picker.
- E2E: add plugin → Choose knobs → tick two → save project → reload → knobs present → set as
  default for all → second instance shows them.

---

## 10. Implementation tracker

Epic FRO122; the parked module-layout epic is FRO123 (its first ticket, FRO129, is the design pass §8 describes).

| # | Item | Depends on | Ticket |
|---|---|---|---|
| 1 | **`CardLayout` type, precedence resolver, automatic default, per-type store + presets, per-instance extra-state key, undo** | — | FRO126 |
| 2 | **`HostedParameterAttachment` + `ModuleComponentHostedPluginCard` unit** (knobs/toggles/choices, empty state, orphan knob, unbind seam) | 1 | FRO128 |
| 3 | **`PluginKnobPicker`**: list, search, reorder, label, scope, presets, touch-to-add | 2 | FRO132 |
| 4 | **Integration + docs**: MIDI Learn registry, Automate, snippets carry the key, E2E, docs pass | 3, MIDI Remote item 4 | FRO137 |

---

## Related

- [`midi_remote.md`](midi_remote.md) · [`midi_remote_ui.md`](midi_remote_ui.md)
- [`modules.md`](modules.md) — hosted plugin channel rules · [`modulation.md`](modulation.md)
  — `resolveLaneParameter`'s hosted rules
- [`layout.md`](layout.md) — knob grid and width buckets
