# MIDI Remote — interaction design, panel and coverage

Companion to [`midi-remote.md`](midi-remote.md) (the model and decisions; read it first). This
doc is the **user-facing design**: what right-click MIDI Learn does on every surface, what the
MIDI Remote panel looks like and how each flow in it works, the settings, the plugin-build
behaviour and the tests. Module-card MIDI Learn (the "Generic module card"/"Bespoke cards"/"Header
buttons" rows below) shipped in FRO130; the mixer column, Master's fader and the transport bar
(FRO133) ship here too, and the mixer column's Solo (FRO253, a [node command target](midi-remote.md#node-command-targets)
rather than a parameter) ships here as well. The MIDI Remote panel itself
(FRO131) shipped: the dock tab, Controllers list, Surface and Inspector (including Solo's own
Surface cell / Inspector row, resolved by node command rather than parameter), and
`GraphEditor::onEditMidiAssignmentRequested`. Detect mode, the "+ Add controller" popover,
Templates, Import/Export and the Inspector's encoder Auto-detect (FRO134) shipped too, along with
the Inspector's editable name, kind and encoding. The mapping assistant (FRO135) shipped after it:
"Assign from the panel" with its pick-target overlay and action picker, orphan Re-link/Recreate, and
the orphan-node display. The Preferences group, the Audio-tab caption and the plugin build's Host
MIDI source (FRO136) round it out. Right-click Learn (and "Automate...") on a hosted plugin card's
own chosen knobs (FRO137) shipped too — see the "Hosted plugin card" row below and
[`plugin-card-layout.md`](plugin-card-layout.md#interaction-with-midi-remote-and-automation). Still
design-only: the Inspector's **Relearn** button (rendered, disabled).

---

## Right-click MIDI Learn — coverage

The requirement is *every* knob, slider and button on *any* module. The generic-card
choke point (`slider->addMouseListener(this)` in `ModuleComponent::createControls`) covers
generic float/int sliders only, so coverage is an explicit list with one acceptance line each.
A surface is "covered" when right-clicking its control shows the MIDI item block of [The learn interaction](#the-learn-interaction) and a
Learn from there binds the right parameter (or, for the mixer column's Solo, a [node command target](midi-remote.md#node-command-targets)).

| Surface | Control(s) | Where the hook goes | Acceptance |
|---|---|---|---|
| Generic module card | rotary sliders (float/int params) | the existing `mouseDown` slider-identity match in `ModuleComponentInteraction.cpp` → `showAutomateMenuForSlider` grows the MIDI items | **shipped**: right-click "Cutoff" on Filter → Learn → CC binds `(uuid, "cutoff")` |
| Generic module card | `ToggleButton` per bool param | `registerMidiLearnable` + a `RightClickSafeButton<juce::ToggleButton>` guard so a right-click never also toggles it | **shipped**: right-click a bool toggle → Learn → note/CC toggles it |
| Generic module card | `ComboBox` per choice param | right-click on a `ComboBox` must **not** open its popup: `ModuleComponent::mouseDown`'s registry lookup runs before the combo's own native popup handling | **shipped**: right-click a choice combo → Learn shows the MIDI block; the popup never opens |
| Bespoke cards | EQ card bands, Envelope card knobs, Wavetable card, Sampler controls | each card's own control creation registers its controls with the same registry the generic path uses (`registerMidiLearnable(component, param)`), so `mouseDown` needs no card-specific branches | **shipped**: every parameter visible on a bespoke card is learnable; a control with no parameter shows no MIDI items |
| Header buttons | Bypass, Mute, Dual I/O | same registry, via `RightClickSafeButton<juce::DrawableButton>` | **shipped**: right-click Bypass → Learn → pad toggles bypass |
| Hosted plugin card | the chosen knobs, toggles and choice combos (`plugin-card-layout.md`) | `ModuleComponent::MidiLearnableRegistry::addHosted` registers each hosted control with the hosted parameter's `(uuid, paramId, indexHint)` triple; `MidiLearnController::arm`/`assignControl` resolve a non-`RangedAudioParameter` paramId through `resolveLaneParameter`'s hosted rules, capturing the same `paramIndexHint` an automation lane would | **shipped** (FRO137): right-click a plugin-card knob → "Automate..." (opens the lane picker on that hosted parameter) then MIDI Learn → CC binds `(uuid, paramId, indexHint)`; a toggle/choice control gets MIDI Learn only. Removing the knob from the card layout never forgets the mapping — see `plugin-card-layout.md`'s own note |
| Mixer column | fader (`MixerFader`), pan, Mute, each send level (`MixerSendList`) | one `mouseDown` in `MixerColumnComponent` over its bound params (they are `ChannelStripModule` params, so ordinary parameter targets); `MixerSendList::onSendKnobBuilt` hands send-row knobs back for the SAME registry | **shipped**: right-click a fader → Learn → CC drives the strip's level |
| Mixer column | Solo | `MixerColumnComponent`'s registry gets an `isSolo` entry (no parameter — `ChannelStripModule::soloed_` is engine state, [`Source/Modules/ChannelStripModule.h`](../../Source/Modules/ChannelStripModule.h)); its menu/badge/armed outline route through `MixerPanelComponent::onSoloMidiLearnRequested`/`onSoloMidiForgetRequested`/`onQuerySoloMidiMapping` to `MidiLearnController::armNodeCommand`/`forgetNodeCommand`/`queryNodeCommandMappings` rather than `GraphEditor`'s parameter-keyed callbacks | **shipped**: right-click S → MIDI Learn 'Solo'... → press a pad/button → it toggles solo (one undo step per press); Forget MIDI clears it. The MIDI Remote panel's Surface cell and Inspector row resolve it too (FRO131, "ModuleName · Solo") |
| Master column | master level (fader only — Master has no pan, and its insert list has no learnable control; no Mute learn yet either, just the fader) | `MixerMasterColumn` registers its own fader the same way, via a `GraphEditor&` threaded through `configure()` | **shipped**: right-click Master's fader → Learn → CC drives Master's level |
| Direct column | none | `MixerDirectColumn` has no fader/pan/M-S of its own (just "Make channel") — nothing to register | — not applicable, not a gap |
| Transport bar | Play/Stop, Record, Loop, Metronome (`TimelineTransportBar::GlyphButton`) | right-click shows Learn with an **action** target ([`midi-remote.md`](midi-remote.md#action-targets)); `GlyphButton` is right-click-safe the same way Mute/Bypass are, and `MidiLearnController::armAction()`/`forgetAction()` write the assignment into the learned device's `ControllerProfile.actions` (global, not the project doc — [`midi-remote.md`](midi-remote.md#undo)) | **shipped**: right-click Play → Learn → pad toggles playback; badge shows on the button |
| Macro card / macro ports | none | no parameters of their own — not learnable; a collapsed macro's member knobs are learnable once expanded | — |

Not covered by design: the piano roll, timeline clip lanes, library, AI panel (nothing there is
a parameter or a mapped action; actions there are reachable from the panel's action picker).

---

## The learn interaction

Right-click on any covered control shows, under the existing "Automate '<Param>'" item, a
separated block:

```text
Automate 'Cutoff'
──────────────
MIDI Learn 'Cutoff'…                 ← not mapped yet
```
```text
Automate 'Cutoff'
──────────────
MIDI: Knob 1 on Launchkey Mini       ← mapped: disabled title row, tells you what drives it
   Edit MIDI assignment…            ← opens the panel with this assignment selected
   MIDI Learn again…                 ← replaces the assignment
   Forget MIDI                       ← removes it (undoable)
```

"Edit MIDI assignment..." shipped in FRO131: `GraphEditor::onEditMidiAssignmentRequested` (and the
mixer column/master/transport-bar equivalents) open the MIDI Remote panel's dock tab with the
assignment's control already selected.

**States while learning (target-first learn):**

1. **Armed.** The control's outline breathes: a thin 1px outline in the theme's accent colour on
   the control's bounds, its alpha easing between ~0.4 and 1.0, time-bounded to the 10 s
   timeout, restarted by activity — never an unconditional repaint. The alpha is recomputed from
   wall time on every paint, but nothing repaints on its own, so each surface must actually ask
   for one: every learnable surface repaints the armed control's own bounds from its EXISTING
   gated per-surface tick while (and only while) something on it is armed — `ModuleComponent`'s
   15 Hz `timerCallback`, `MixerColumnComponent`/`MixerMasterColumn`'s 10 Hz `refreshMeter`, and
   `TimelineTransportBar`'s 10 Hz `updateFromTransport` (FRO256) — never a new timer or an
   AnimationDriver entry. This is explicitly not a glow: Obsidian has glow 0, and this state must
   read the same in every theme. The status bar
   (the existing `StatusBarComponent`, `Source/UI/Chrome/`) shows *"MIDI Learn: move a control on
   your controller for 'Cutoff' — Esc to cancel"*. If no controller profile exists yet and no
   MIDI input is open, the status line adds *"No MIDI device is enabled — open Settings →
   Audio"* and offers the settings link.
2. **Settling.** The first eligible message opens the 300 ms window ([`midi-remote.md`](midi-remote.md#learn-what-does-the-first-message-mean)); the
   pulse turns solid.
3. **Bound.** The assignment is created (project scope for a parameter, global for an action),
   the pulse ends with one short confirm flash, the status bar says *"'Cutoff' ← Knob 1 on
   Launchkey Mini"*, and the control gains the **MIDI badge**: a 6 px dot at the control's
   top-right in the theme's MIDI colour (a new theme token, `midiMapped`, with a default in
   every built-in theme, chosen so it reads as "mapped" and stays visually distinct from the
   accent colour's "selected" meaning; `theming.md`). Hovering the badge tooltips the assignment,
   e.g. *"MIDI: Knob 1 on Launchkey Mini MK3"*.
4. **Cancelled** (Esc, click elsewhere, timeout): pulse ends, status bar clears, nothing changes.

Only one learn is armed at a time; arming another replaces it. A learn never blocks anything —
audio, playback and every other click keep working; it is not a modal mode.

**Learn from the panel side (control-first)** is [Assign from the panel](#assign-from-the-panel-control-first-learn).

---

## The MIDI Remote panel

A third tab on the bottom dock, `BottomDockComponent::Tab::MidiRemote`, next to Timeline and
Mixer (`Source/UI/Mixer/BottomDockComponent.h`) — it reuses the dock's slide, persistence
(`"bottomDockActiveTab"`), detach-to-window (`DetachablePanelHost`) and focus-region machinery.
Toolbar toggle + `ShortcutManager` action `toggleMidiRemotePanel` (category General, default
unbound). Files: `Source/UI/MidiRemote/MidiRemotePanel/` (`MidiRemotePanelComponent` + one unit
per region below), `Source/UI/MidiRemote/ControllerSurface/`, `Source/UI/MidiRemote/Inspector/`.
The panel opens attached in the dock by default; detach-to-window is available but is not the
default. FRO158 (still open, unrelated to this ticket) tracks a separate, unreproduced report that
opening the Mixer tab can leave the dock blank; FRO131 does not depend on it and did not attempt
to reproduce or fix it.

The panel stays live while it's open (FRO263) — it re-pulls the profile/assignment set on every
mutation that changes it, wherever it happens: a canvas Learn/Forget, a module deleted from the canvas (its assignments turn to "(missing module)"), Undo/Redo, an action Learn on
the transport bar, or the panel's own Rename/Delete/Retype/drag-to-reposition. It is not limited to
catching up when the tab is switched into, which is now the fallback for changes made while the tab
was hidden, not the only refresh path. FRO262 extends the same live-refresh seam to a MIDI device
opening or closing after launch (a controller ticked in the Audio tab, or one that reconnects) — the
Controllers list's present/absent state updates immediately rather than only on the next tab switch.

```text
┌ Controllers ──────┬ Surface: Launchkey Mini MK3 ───────────────────────┬ Inspector ─────────────┐
│ ▸ Launchkey Mini  │  [Detect]  [Assign…]  [Templates ▾]  [⋯]           │ Knob 1                 │
│   Host MIDI (plugin only)  ──────────────────────────────────────────── │ Kind    Knob ▾         │
│ ○ nanoKONTROL2    │   (K1)  (K2)  (K3)  (K4)  (K5)  (K6)  (K7)  (K8)    │ Message CC 21 ch 1     │
│   (not on this    │  Cutoff Reso  LFO   —     —     Atk   Dec   Rel     │ Encoding Absolute ▾    │
│    machine)       │                                                      │ ── Assignment ──       │
│                   │   [▶] [■] [●] [↻]     [P1][P2][P3][P4][P5][P6][P7][P8] │ Drives  Filter · Cutoff│
│ + Add controller  │   Play Stop Rec Loop   —   —   —   —   —   —   —   —  │ Scope   Project        │
│                   │                                                      │ Takeover Default (Scale)▾│
│                   │   ▸ activity: a control lights and its ring/fader    │ Range   0% … 100%  [⇅] │
│                   │     moves live while you touch the hardware          │ [Learn target] [Forget]│
└───────────────────┴──────────────────────────────────────────────────────┴────────────────────────┘
```

### Controllers list (left)

One row per profile: name, a live-activity dot, a state glyph — present (device found and
open), **absent** (profile exists, device not connected: greyed, assignments kept), **orphan**
(the project references a profile this machine lacks: [`midi-remote.md`](midi-remote.md#where-does-a-mapping-live--global-or-in-the-project), row shows
*"not on this machine"*; selecting it shows [Orphan controllers](#orphan-controllers) in the inspector's
place instead of a control inspector). Right-click:
Rename, Export…, Delete… (confirms with the count of project assignments it will orphan), **Send
feedback to ▸** (FRO139, [`midi-remote.md`](midi-remote.md#controller-feedback)) — "None" (ticked
when the profile has no output configured) plus one item per available MIDI output device, ticked
against whichever one is currently picked; choosing an item sets that `ControllerProfile`'s
`output`/`hasOutput` and republishes it, so `RemoteEngine`'s drain starts (or stops) echoing mapped
values to it. Hidden in the plugin build, same as "+ Add controller". "+
Add controller" is [Add controller](#add-controller). In the plugin build the list holds exactly "Host MIDI".

### Surface (centre)

The profile's controls drawn on a grid (`col`, `row` from the profile; default cell 56 px,
snapped) using the app's own widgets: a rotary for `knob`/`encoder`, a vertical slider for
`fader`, a square for `pad`, a round button for `button`, a horizontal strip for `wheel`.
Each cell shows the control's name above and its **assignment label** below (parameter:
*"Filter · Cutoff"*, node command (FRO253's Solo): *"Kick · Solo"*, action: *"Play"*, none: *"—"*,
orphaned node: *"(missing module)"* in the warning colour). Widgets are **display-only** — they move with the hardware (activity events,
[`midi-remote.md`](midi-remote.md#the-engine)) and are never dragged to send MIDI. A cell mapped to
a parameter or the Solo node command builds already showing that target's current value
(FRO262) rather than always at rest — an unmapped, orphaned, or action-target cell still shows 0/off,
since there is nothing live to read. Clicking a cell selects it (inspector);
dragging moves it on the grid; Delete removes the control (and its assignments). Both are undoable:
the control from the controller edit history (Cmd+Z with the panel focused), its project assignments
from the project history ([`midi-remote.md`](midi-remote.md#undo)). Repaint is event-driven from the activity ring at ≤ 30 Hz while the tab is
showing, gated exactly like `BottomDockComponent::refreshMeters` — no free-running timer.

### Inspector (right)

For the selected control: name, kind, message spec (editable, with a **Relearn** button that
re-detects the message), encoding (with **Auto-detect…** for encoders: "turn left… now right",
which observes the two value patterns and picks the encoding), button mode. Name (double-click),
kind and encoding are editable (FRO134/FRO264); each edit is a profile edit — global, one step on the
controller edit history ([`midi-remote.md`](midi-remote.md#undo)) —
and is copied onto every assignment that references the control, because the engine reads the
encoding from the assignment. **Relearn** is still a disabled placeholder.

The **encoding dropdown** offers different options depending on the message type and CC number
(see [`midi-remote.md#14-bit-and-nrpn-encodings`](midi-remote.md#14-bit-and-nrpn-encodings)):
- **CC 0..31:** "Absolute (7-bit)", the three relative encodings ("Relative (two's complement)", etc.),
  "Absolute (14-bit, MSB first)", "Absolute (14-bit, LSB first)".
- **CC ≥ 32 or other types (note, pitch bend, etc.):** only the original four entries
  (Absolute 7-bit and the three relative encodings).
- **NRPN:** "NRPN (7-bit, CC 6 only)", "NRPN (14-bit, MSB first)", "NRPN (14-bit, LSB first)".
  A CC control cannot be retyped into an NRPN from the dropdown.

Below the divider, its assignment(s): what it drives (click jumps to the module on the canvas via the existing
locate path), scope tag, takeover, range with an invert toggle, **Learn target**
([Assign from the panel](#assign-from-the-panel-control-first-learn)),
**Forget**. A control may carry one project assignment and one global assignment at most for
now; both are listed when both exist, each as its own block headed by its own scope tag
(**Project** / **Global**), and the block being edited names its scope in its own header, so the
user always knows whether the change they are making is unique to this project or applies
everywhere (the project one wins at runtime while the project is open).

### Assign from the panel (control-first learn)

Select a control → **Assign…** (toolbar) or **Learn target** (inspector) → a two-item menu:

- **Pick a module control** — the *pick-target* overlay: every learnable control
  on every card, mixer column and the transport bar gets a subtle outline; the status bar says
  *"Click the knob, slider or button that Knob 1 should drive - Esc to cancel"*; the next left click
  on a learnable control makes the assignment and ends the overlay. Clicking anything else, a right
  click, or Esc cancels. The dock's **Timeline / Mixer / MIDI Remote tab buttons are the exception**:
  they receive the click (the overlay steps aside over them), so you can switch to the Mixer or Timeline
  to reach its controls mid-pick; the session stays open and re-collects its candidates for the newly
  showing surface. This is the only overlay-style mode in the feature, and it is entered from the panel,
  never as a global key. A mixer detached into its own window is not a child of the main window and is
  not pickable.
- **Choose an action** — a searchable list grouped by `ShortcutCategory` using the Shortcuts
  tab's display names (`ShortcutManager::getActionDescription`), command-dispatched actions only
  (`AppCommands::getCommandForAction` is not `kNoCommand`), plus one more group appended last —
  **Continuous** — with a fixed three rows (Tempo (BPM), Playhead Position, Master Volume; named by
  `synth::continuousTargetDisplayName`, docs/control/midi-remote.md#continuous-targets), filtered by
  the same search box.

Where it lives: the assignment is made by `MidiLearnController::assignControl` (a parameter or Solo →
project scope, one `recordMidiRemoteChange` step; an action or a continuous target, e.g. a transport
button or Tempo (BPM) → global, written into the profile, one controller-history step). It replaces whatever the
target was mapped to and whatever the control drove in the same scope, so a control has at most one
project and one global assignment. The overlay is
`synth::ui::PickTargetOverlay` (`Source/UI/Graph/PickTargetOverlay/`), a transparent layer added to
`MainComponent` — not to the canvas — because the mixer columns and the transport bar are not canvas
children. It draws once (no timer, no animation), resolves the click itself against the candidates every
surface reports through its own `collectPickCandidates()` (each surface keeps its own registry; no card
knows the mode exists), clips each outline by its ancestors so a control under the dock's edge is neither
drawn nor pickable, lets `BottomDockComponent::getTabButtons()` through (`setPickPassThrough`) and re-collects
on `BottomDockComponent::onActiveTabChanged` (`MidiLearnController::refreshPickTarget`), and ends on Esc, on any graph rebuild (`GraphEditor::onBeforeDetachAllModuleComponents`)
or on a click that hits nothing. The action picker is `ActionPickerComponent`
(`Source/UI/MidiRemote/ActionPicker/`).

### Orphan controllers

An orphan controller row (the project names a `profileId` this machine lacks) shows, in the inspector's
place, how many assignments depend on it and two repairs — **Re-link…** (choose a controller on this
machine) and **Recreate** (choose a free MIDI input) — whose rules are in
[`midi-remote.md`](midi-remote.md#where-does-a-mapping-live--global-or-in-the-project). Re-link reports
"N of M assignments linked; K stay orphaned" when some specs have no counterpart, and the orphan row stays
until none are left. Recreate is disabled when no MIDI input is free.

**Orphan node.** A project assignment whose parameter target no longer resolves (its module was deleted, or a
hosted plugin's parameter drifted away) after `RemoteEngine::reconcile` is shown as *"(missing module)"* in
the warning colour on the surface and in the inspector, where **Forget** is its only action (takeover, range and
invert are disabled). Forget works by assignment id, so it does not need the module it points at. It is never
re-bound automatically.

### Detect mode

**Detect** toggles the surface into detection: every message the profile's device sends that
is not yet a control appears as a new cell in touch order (kind guessed: CC → knob, note →
pad, pitch bend → wheel, NRPN → knob; name "CC 21" / "C3" / "NRPN 1024"), pulsing until the next one arrives; an existing control's cell lights
instead. A hint row reads *"Touch each knob, fader and button once. Rename or retype them
afterwards. Turn an encoder left then right to detect its encoding."* Leaving Detect keeps
everything. Detect never consumes messages, never assigns anything, and never touches the
patch's MIDI flow.

How it is built (`Source/UI/MidiRemote/Detect/`, `MidiRemotePanelDetect.cpp`): the panel's single
`RemoteEngine::drainActivity` pass feeds `DetectModeController`, which decides per event on a copy of
the selected profile; the additions are persisted once per drain (`MidiLearnController::updateProfile`)
and the drain's events are replayed onto the rebuilt cells so the control the user just touched
shows its value. The engine mirrors every eligible message from a profile's device onto the activity
ring whether or not a control or assignment exists (`pushActivityOnly`), which is the only thing
Detect needs from it. An event is credited to **every** profile bound to that device (two can share
one input, e.g. an imported copy), so whichever one is selected moves live (FRO272). Details a reader
would not guess:

- A note-off, a program change and a learn candidate never create a cell (the press already did;
  each program number is its own message key). A detected control keeps the channel it arrived on.
- **14-bit CC pairing:** if the two halves of a CC pair (CC `n` and CC `n+32`, either order,
  same channel) arrive within 5 ms, they are merged into ONE control; CC `n` then `n+32`
  becomes `abs14`, and `n+32` then `n` becomes `abs14LsbFirst` (renumbered to `n`). Slower or
  different-channel arrivals stay separate.
- **NRPN detection:** an unmapped NRPN appears as ONE control named "NRPN `<address>`" with
  encoding `abs14` and kind knob.
- The pulse is a breathing accent outline **bounded to 10 s** — the animation rules forbid an
  unbounded animation — repainted only from the panel's existing gated activity tick, per cell. The
  "lit" flash on an existing control is a solid outline for 250 ms.
- Selecting another controller turns Detect off; it belongs to one controller.
- Detect works on an empty profile, and in `HostMode::Hosted` (it reads the host stream).

**Auto-detect…** (Inspector, enabled for a CC knob/encoder) asks the user to turn the control left, then
right, each step ending with a **Next** press (async prompts — a modal loop would stop the activity
tick the samples arrive on). `EncoderAutoDetect` classifies the raw 7-bit values (`RemoteEvent::rawValue`,
so it sees the hardware's bytes whatever encoding the control currently claims): left high and right
low is two's complement when the left values are ≈127 and sign-magnitude when they are ≈65; left low
and right high is binary offset (63 / 65); a falling left sweep followed by a rising right sweep that
matches none of those is absolute. Anything else says it could not tell and changes nothing. A
relative result on a plain knob also retypes it as an encoder. This helper is the only place an
encoding is ever inferred — the runtime never guesses.

### Templates and import/export

**Templates ▾** applies a shipped layout (the generic ones: 8 knobs; 8 faders + 8 buttons; transport
strip; keyboard-with-8-knobs; plus real-hardware templates for popular controllers) to an empty
profile or **merges** it into a non-empty one
(existing message keys win; the additions get fresh ids and are stacked below the existing rows).
The menu groups templates by vendor (**Generic** first, then one section per manufacturer,
alphabetically — `synth::midi::groupControllerTemplatesByVendor`), so picking e.g. Korg →
nanoKONTROL2 draws that device's knobs/sliders/buttons correctly without running Detect. **⋯** has *Import controller…* / *Export controller…* (JSON file,
the profile document of [`midi-remote.md`](midi-remote.md#data-model)). Importing a document whose id is already set up on this
machine prompts **Replace / Cancel** (a replace keeps the project's assignments linked, since they
reference the profile by id). Shipped templates are JSON resources under
`assets/midi-remote-templates/`, embedded through the `Assets` binary-data library and enumerated
by `synth::midi::listControllerTemplates()` (`Source/MidiRemote/ControllerTemplates.h`).

#### Contribute a template

A template file is an ordinary `ControllerProfile` document (`midi-remote.md#data-model`'s JSON
shape — `version`, `id`, `name`, `input`, `controls[]`, `actions: []`), plus two keys the profile
schema itself doesn't know about (`ControllerProfile::fromVar` reads named keys only, so it
tolerates and ignores them):

- `"vendor"` — the manufacturer, e.g. `"Korg"`. Omit it (or leave it `""`) for a generic template;
  it groups under **Generic** in the Templates menu. Set it for a real device and it groups under
  that manufacturer's name instead.
- `"source"` — **required whenever `"vendor"` is set.** The exact manual or MIDI-implementation
  document the CC/note numbers were read from, with a URL and the section/page — e.g. *"Korg
  nanoKONTROL2 MIDI Implementation, https://cdn.korg.com/…, section 4 'Native KORG Mode
  Messages'"*. Every number in a vendor template's `controls[]` must trace back to the vendor's own
  manual or official MIDI-implementation chart, in the device's factory-default mode — never
  memory, a forum post, or a reverse-engineered third-party doc. If no such authoritative document
  is fetchable for a device, it doesn't get a template.

Drop the new file under `assets/midi-remote-templates/` (filename `template-<name>.json`,
hyphen-separated — an underscore collides with the `Assets` library's BinaryData symbol
separator), add it to the `Assets` file list in `cmake/Assets.cmake` (next to the existing
`template-*.json` entries) — the most common way a new template passes locally and fails CI is
forgetting this step — and give its layout `col`/`row`s that mirror the physical device's
arrangement without any two controls sharing a cell.

`Tests/MidiRemote/ControllerTemplatesVendorTests.cpp` guards the contract: every template parses,
template ids are unique across the whole library, control ids are unique within a template, no two
controls' layout cells overlap, every vendor template has a non-empty `source`, and
`groupControllerTemplatesByVendor` puts Generic first then vendors alphabetically. Register a new
test file in `Tests/CMakeLists.txt` the same way (see that file's `MidiRemote/` entries).

---

## Add controller

"+ Add controller" → a popover: **MIDI input device** (the `juce::MidiInput::getAvailableDevices()`
list, devices that already have a profile greyed with the profile's name), **Name** (prefilled
from the device), **Start with**: *Detect controls now* (default) / a template / *Empty*. OK
creates the profile file, opens the device (`AudioEngine::ensureMidiDeviceOpen`, then republishes the engine's sources — the
engine ignores a device it has not been told about), selects it
in the list and, for the default choice, enters Detect. The button is hidden in the plugin build. A controller created implicitly by a
Learn ([`midi-remote.md`](midi-remote.md#learn-what-does-the-first-message-mean)) is exactly this with *Empty* plus the one detected control.

---

## Settings

Preferences tab (`PreferencesSettingsTab`), a "MIDI Remote" group (FRO136). Both are scalar keys in
the shared settings file, documented in `Source/UserSettings.h`:

- **Default takeover**: Jump / Pick-up / Scale (default Scale; key `midiRemoteDefaultTakeover`,
  stored as `jump` / `pickup` / `scale`). Assignments set to *Default* follow it live:
  `MainComponent::applyMidiRemotePreferences` re-reads it on every settings-file change and hands it
  to `RemoteEngine::setDefaultTakeover` — Core never reads settings.
- **Show MIDI badges on mapped controls** (default on; key `midiRemoteShowBadges`). The same
  function pushes it to `synth::ui::midilearn::setMappedBadgesVisible`, which gates
  `paintMidiMappedBadge` — so module cards, mixer columns and the transport bar all honour it without
  knowing about it. The panel's own surface cells use `paintMidiMappedDot` and always show theirs.

The Audio tab's MIDI-input checklist keeps its meaning (which devices feed the *patch*); a
device with a profile is opened by the remote engine regardless ([`midi-remote.md`](midi-remote.md#the-engine)).
JUCE's stock device selector cannot decorate a single device row, so instead of a per-row
"(MIDI Remote)" suffix the tab (`AudioSettingsTab`) carries a one-paragraph caption under the selector
naming every profiled controller, which is what lets the two lists explain each other. No profiled
controllers, no caption. The Audio tab's MIDI-output selector stays hidden and dead either way —
controller feedback (FRO139) is picked per controller from the Controllers list's own right-click
("Send feedback to ▸", above), not from that selector.

---

## Plugin build

In `HostMode::Hosted` ([`midi-remote.md`](midi-remote.md#the-plugin-build-vst3au-inside-a-host)) the
one live controller is the pseudo-controller **Host MIDI**, fed from the MIDI buffer the host hands to
`processHostBlock`:

- The controllers list always has a **Host MIDI** row, even before any profile exists; selecting it
  creates the (empty) Host MIDI profile, so Detect, templates and Learn all work on it.
- Add controller, the device pickers and Detect-by-device are hidden (`ControllersListComponent::setHosted`);
  the Audio tab, and with it its MIDI Remote caption, is not built at all in the plugin.
- Profiles for real devices that exist on the machine are listed as *"standalone only"*, greyed and
  inert: they are viewable, and Detect is not offered on them.
- Assignments made in the standalone app against a real controller do not fire inside a host
  (different source key). The panel says so on the row's tooltip — for a standalone-only row and for
  an orphan row alike — rather than pretending.

---

## Tests

Mirroring the source layout (`Tests/MidiRemote/…`, `Tests/UI/MidiRemote/…`), headless, with a
fake message source (no real `juce::MidiInput`):

- **Model / serialisation** (`Tests/MidiRemote/RemoteModelTests.cpp`): round-trip every type;
  `version` refusal; a profile with duplicate message keys is rejected; `"midiRemote"` is
  stashed and carried by `ProjectBundle` and refused by `validatePatch` untrusted
  (`AIStateMapperTest.MidiRemoteKeyIsRefusedUntrusted`).
- **Engine** (`RemoteEngineDecodeTests.cpp`, `RemoteEngineApplyTests.cpp`,
  `RemoteEngineLearnTests.cpp`, `RemoteEngineReconcileTests.cpp`): abs7 / the three relative
  encodings / button momentary vs toggle; takeover Jump/Pick-up/Scale sequences; gesture
  begin/end timing (`kGestureIdleMs`) produces exactly one undo step and one
  `parameterGestureChanged` pair; consumed vs passed messages reach / don't reach the collector
  and an `ExternalMidiModule`; learn settle picks the majority key and ignores note-off / poly
  aftertouch / clock; auto-profile creation; reconcile orphans a deleted node and never
  rebinds; hosted-plugin target resolves via `resolveLaneParameter` rules; Hosted-mode source
  key; a snapshot swap during a burst never drops into a lock (`ThreadSanitizer` job in CI, the
  `run-asan` label).
- **Actions** (`Tests/App/ShortcutManager/ShortcutManagerTransportActionsTests.cpp`): the
  promoted transport actions exist, have command ids, and invoke the right `TransportService`
  verbs / record intent.
- **UI** (`Tests/UI/MidiRemote/…`, `Tests/UI/Graph/ModuleComponent/ModuleComponentMidiLearnTests.cpp`,
  `Tests/UI/Mixer/MixerColumnMidiLearnTests.cpp`, `Tests/UI/Timeline/TransportBarMidiLearnTests.cpp`):
  the right-click block appears on every surface in [Right-click MIDI Learn — coverage](#right-click-midi-learn--coverage)'s table (one test per row, "test the
  real mouse path" convention), including a hosted plugin card's knob/toggle/choice controls
  (`Tests/UI/Graph/ModuleComponent/HostedPluginCardMidiLearnTests.cpp`,
  `Tests/MidiRemote/MidiLearnControllerHostedParameterTests.cpp`); the badge paints only when mapped; the panel's list/surface/
  inspector render from a profile; Detect adds cells in order (`ControllerSurfaceDetectTests.cpp`); pick-target overlay assigns and
  cancels (`PickTargetOverlayTests.cpp`, `ActionPickerTests.cpp`, `MidiRemotePanelAssignTests.cpp`); orphan
  controller Re-link/Recreate and the orphan node (`OrphanControllerTests.cpp`,
  `Tests/MidiRemote/MidiLearnControllerOrphanTests.cpp`); the controller edit history and its Cmd+Z routing by
  panel focus (`Tests/MidiRemote/ProfileEditHistoryTests.cpp`, `MidiRemoteUndoRoutingTests.cpp`); PNG render of the surface for visual inspection
  (`MIDI_SURFACE_PNG=<path>`, like the ADSR card's).
- **E2E** (`Tests/MidiRemote/MidiRemoteWorkflowE2ETests.cpp`): one workflow through the real seams (messages enter
  `AudioEngine::handleIncomingMidiMessageFromSource`, `RemoteEngine::drain()` applies them on a fake clock). Fake
  device → a real right-click "MIDI Learn 'Cutoff'..." on the Filter card → sweep → the CC settles, the assignment
  and auto-profile exist → the sweep moves the parameter as one gesture pair → an armed Touch lane records the take
  (its own undo step) → one Cmd+Z reverts the whole sweep → save the project, reload into a fresh session, the
  assignment resolves and still drives the parameter → delete the node, the assignment is unresolved and never
  rebinds (even to a new Filter), Forget removes it. Also a transport pad learned onto `transportTogglePlayStop`
  invoking the `togglePlayback` command on press only, and a Hosted-mode learn + sweep fed through
  `processHostBlock` under the `"Host MIDI"` source. Stand-ins for what a headless run cannot reach: the fake device
  key is re-registered with `RemoteEngine::setSources()` after arming (no real `juce::MidiInput` to enumerate), and
  the action is checked at `RemoteActionInvoker::invokeRemoteCommand` (`MainComponent`'s invoker is private).

---

## Related

- [`midi-remote.md`](midi-remote.md) — model and decisions.
- [`plugin-card-layout.md`](plugin-card-layout.md) — the hosted-plugin knobs this maps.
- [`docs/mixer/panel.md`](../mixer/panel.md) — the bottom dock the panel joins.
- [`layout/animation.md`](../layout/animation.md) — the animation rules the pulse and the surface obey.
- [`shortcuts.md`](shortcuts.md) — action ids and display names the action picker reuses.
