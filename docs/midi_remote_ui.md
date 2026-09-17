# MIDI Remote — interaction design, panel, coverage and tracker

Companion to [`midi_remote.md`](midi_remote.md) (the model and decisions; read it first). This
doc is the **user-facing design**: what right-click MIDI Learn does on every surface, what the
MIDI Remote panel looks like and how each flow in it works, the settings, the plugin-build
behaviour, the tests, and the implementation tracker (§8), which is the record of what actually
exists.

---

## 1. Right-click MIDI Learn — coverage

The founder's requirement is *every* knob, slider and button on *any* module. The generic-card
choke point (`slider->addMouseListener(this)` in `ModuleComponent::createControls`) covers
generic float/int sliders only, so coverage is an explicit list with one acceptance line each.
A surface is "covered" when right-clicking its control shows the MIDI item block of §2 and a
Learn from there binds the right parameter.

| Surface | Control(s) | Where the hook goes | Acceptance |
|---|---|---|---|
| Generic module card | rotary sliders (float/int params) | the existing `mouseDown` slider-identity match in `ModuleComponentInteraction.cpp` → `showAutomateMenuForSlider` grows the MIDI items | right-click "Cutoff" on Filter → Learn → CC binds `(uuid, "cutoff")` |
| Generic module card | `ToggleButton` per bool param | add the same `addMouseListener(this)` for toggles; `mouseDown` matches against `toggles`/`buttonAttachments` (a parallel `toggleParams` array is needed — today no param pointer is kept) | right-click a bool toggle → Learn → note/CC toggles it |
| Generic module card | `ComboBox` per choice param | right-click on a `ComboBox` must **not** open its popup: intercept `mouseDown` with `isPopupMenu()` before the combo sees it (a `juce::ComboBox` subclass or the parent listener consuming the event) | right-click a choice combo → Learn → CC 0..127 scales across choices; button steps to next |
| Bespoke cards | EQ card bands, Envelope card knobs, Wavetable card, Sampler controls | each card's own control creation registers its controls with the same registry the generic path uses (`registerMidiLearnable(component, param)`), so `mouseDown` needs no card-specific branches | every parameter visible on a bespoke card is learnable; a control with no parameter shows no MIDI items |
| Header buttons | Bypass, Mute, Dual I/O | same registry (`bypassAttachment` etc. already know the params) | right-click Bypass → Learn → pad toggles bypass |
| Hosted plugin card | the chosen knobs (`plugin_card_layout.md`) | the card unit registers each knob with the hosted parameter's `(uuid, paramId, indexHint)` triple | Learn on a plugin knob binds through `resolveLaneParameter`'s hosted rules |
| Mixer column | fader (`MixerFader`), pan, Mute, Solo, each send level (`MixerSendList`) | one `mouseDown` in `MixerColumnComponent` over its bound params (they are `ChannelStripModule` params, so ordinary parameter targets) | right-click a fader → Learn → CC drives the strip's level |
| Master / Direct column | master level | same | learnable |
| Transport bar | Play/Stop, Record, Loop, Metronome (`TimelineTransportBar::GlyphButton`) | right-click shows Learn with an **action** target (`midi_remote.md` §4.9) | right-click Play → Learn → pad toggles playback; badge shows on the button |
| Macro card / macro ports | none | no parameters of their own — not learnable; a collapsed macro's member knobs are learnable once expanded | — |

Not covered by design: the piano roll, timeline clip lanes, library, AI panel (nothing there is
a parameter or a mapped action; actions there are reachable from the panel's action picker).

---

## 2. The learn interaction

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

**States while learning (target-first learn):**

1. **Armed.** The control's outline breathes: a thin 1px outline in the theme's accent colour on
   the control's bounds, its alpha easing between ~0.4 and 1.0, time-bounded to the 10 s
   timeout, restarted by activity — never an unconditional repaint. This is explicitly not a
   glow: Obsidian has glow 0, and this state must read the same in every theme. The status bar
   (the existing `StatusBarComponent`, `Source/UI/Chrome/`) shows *"MIDI Learn: move a control on
   your controller for 'Cutoff' — Esc to cancel"*. If no controller profile exists yet and no
   MIDI input is open, the status line adds *"No MIDI device is enabled — open Settings →
   Audio"* and offers the settings link.
2. **Settling.** The first eligible message opens the 300 ms window (`midi_remote.md` §4.5); the
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

**Learn from the panel side (control-first)** is §3.4.

---

## 3. The MIDI Remote panel

A third tab on the bottom dock, `MixerDockComponent::Tab::MidiRemote`, next to Timeline and
Mixer (`Source/UI/Mixer/MixerDockComponent.h`) — it reuses the dock's slide, persistence
(`"bottomDockActiveTab"`), detach-to-window (`DetachablePanelHost`) and focus-region machinery.
Toolbar toggle + `ShortcutManager` action `toggleMidiRemotePanel` (category General, default
unbound). Files: `Source/UI/MidiRemote/MidiRemotePanel/` (`MidiRemotePanelComponent` + one unit
per region below), `Source/UI/MidiRemote/ControllerSurface/`, `Source/UI/MidiRemote/Inspector/`.
The panel opens attached in the dock by default; detach-to-window is available but is not the
default (founder review). A known dock bug — opening the Mixer tab can leave the dock blank — is
tracked separately and must be fixed before this third tab lands.

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

### 3.1 Controllers list (left)

One row per profile: name, a live-activity dot, a state glyph — present (device found and
open), **absent** (profile exists, device not connected: greyed, assignments kept), **orphan**
(the project references a profile this machine lacks: `midi_remote.md` §4.1, row shows
*"not on this machine"* and the inspector offers **Re-link** / **Recreate**). Right-click:
Rename, Export…, Delete… (confirms with the count of project assignments it will orphan). "+
Add controller" is §4. In the plugin build the list holds exactly "Host MIDI".

### 3.2 Surface (centre)

The profile's controls drawn on a grid (`col`, `row` from the profile; default cell 56 px,
snapped) using the app's own widgets: a rotary for `knob`/`encoder`, a vertical slider for
`fader`, a square for `pad`, a round button for `button`, a horizontal strip for `wheel`.
Each cell shows the control's name above and its **assignment label** below (parameter:
*"Filter · Cutoff"*, action: *"Play"*, none: *"—"*, orphaned node: *"(missing module)"* in the
warning colour). Widgets are **display-only** — they move with the hardware (activity events,
`midi_remote.md` §6) and are never dragged to send MIDI. Clicking a cell selects it (inspector);
dragging moves it on the grid; Delete removes the control (and its assignments, undoable for the
project half). Repaint is event-driven from the activity ring at ≤ 30 Hz while the tab is
showing, gated exactly like `MixerDockComponent::refreshMeters` — no free-running timer.

### 3.3 Inspector (right)

For the selected control: name, kind, message spec (editable, with a **Relearn** button that
re-detects the message), encoding (with **Auto-detect…** for encoders: "turn left… now right",
which observes the two value patterns and picks the encoding), button mode. Below the divider,
its assignment(s): what it drives (click jumps to the module on the canvas via the existing
locate path), scope tag, takeover, range with an invert toggle, **Learn target** (§3.4),
**Forget**. A control may carry one project assignment and one global assignment at most in
v1; both are listed when both exist, each as its own block headed by its own scope tag
(**Project** / **Global**), and the block being edited names its scope in its own header, so the
user always knows whether the change they are making is unique to this project or applies
everywhere (the project one wins at runtime while the project is open).

### 3.4 Assign from the panel (control-first learn)

Select a control → **Assign…** (or **Learn target** in the inspector) → a small popover with
two choices:

- **Pick a module control** — the canvas enters a *pick-target* overlay: every learnable control
  on every card, mixer column and the transport bar gets a subtle outline; the status bar says
  *"Click the knob, slider or button that Knob 1 should drive — Esc to cancel"*; the next click
  on a learnable control makes the assignment and ends the overlay. Kept in v1 (founder review
  2026-09-17). Clicking anything else cancels. This is the only overlay-style mode in the
  feature, and it is entered from the panel, never as a global key.
- **Choose an action** — a searchable list grouped by `ShortcutCategory` using the Shortcuts
  tab's display names (`ShortcutManager::getDisplayName`), command-dispatched actions only.

### 3.5 Detect mode

**Detect** toggles the surface into detection: every message the profile's device sends that
is not yet a control appears as a new cell in touch order (kind guessed: CC → knob, note →
pad; name "CC 21" / "C3"), pulsing until the next one arrives; an existing control's cell lights
instead. A hint row reads *"Touch each knob, fader and button once. Rename or retype them
afterwards. Turn an encoder left then right to detect its encoding."* Leaving Detect keeps
everything. Detect never consumes messages, never assigns anything, and never touches the
patch's MIDI flow.

### 3.6 Templates and import/export

**Templates ▾** applies a shipped generic layout (8 knobs; 8 faders + 8 buttons; transport
strip; keyboard-with-8-knobs) to an empty profile or **merges** it into a non-empty one
(existing message keys win). **⋯** has *Import controller…* / *Export controller…* (JSON file,
the profile document of `midi_remote.md` §5, name conflicts prompt). Shipped templates are
resources under `Resources/MidiRemote/Templates/`.

---

## 4. Add controller

"+ Add controller" → a popover: **MIDI input device** (the `juce::MidiInput::getAvailableDevices()`
list, devices that already have a profile greyed with the profile's name), **Name** (prefilled
from the device), **Start with**: *Detect controls now* (default) / a template / *Empty*. OK
creates the profile file, opens the device (`AudioEngine::ensureMidiDeviceOpen`), selects it
in the list and, for the default choice, enters Detect. A controller created implicitly by a
Learn (`midi_remote.md` §4.5) is exactly this with *Empty* plus the one detected control.

---

## 5. Settings

Preferences tab (`PreferencesSettingsTab`), a new "MIDI Remote" group:

- **Default takeover**: Jump / Pick-up / Scale (default Scale). Assignments set to *Default*
  follow it live.
- **Show MIDI badges on mapped controls** (default on).

The Audio tab's MIDI-input checklist keeps its meaning (which devices feed the *patch*); a
device with a profile is opened by the remote engine regardless (`midi_remote.md` §6), and the
Audio tab shows a small "(MIDI Remote)" suffix on such devices so the two lists explain each
other. The dead MIDI-output selector stays hidden until v2 feedback needs it.

---

## 6. Plugin build

In `HostMode::Hosted` (`midi_remote.md` §4.8): the controllers list shows only **Host MIDI**;
Add controller, device pickers, Detect-by-device and the Audio-tab suffix are hidden; Detect
mode still works (it reads the host stream); profiles for real devices that exist on the
machine are listed as *"standalone only"* but inert. Assignments made in the standalone app
against a real controller do not fire inside a host (different source key) — the panel says so
on the orphan row rather than pretending.

---

## 7. Tests

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
  the right-click block appears on every surface in §1's table (one test per row, "test the
  real mouse path" convention); the badge paints only when mapped; the panel's list/surface/
  inspector render from a profile; Detect adds cells in order; pick-target overlay assigns and
  cancels; orphan controller Re-link/Recreate; PNG render of the surface for visual inspection
  (`MIDI_SURFACE_PNG=<path>`, like the ADSR card's).
- **E2E** (`Tests/E2E/E2EMidiRemoteWorkflow.cpp`): fake device → Learn on Filter cutoff → sweep
  → parameter follows, automation Touch records it, undo reverts one step → save project →
  reload → assignment resolves → delete node → orphan → Forget.

---

## 8. Implementation tracker

Ordered by dependency. Epic FRO121. An item is ticked only
when its PR has merged and the docs above describe what shipped.

| # | Item | Depends on | Ticket |
|---|---|---|---|
| 1 | **Model + persistence**: `Source/MidiRemote/` types, JSON round-trip, profile file store, `"midiRemote"` project key (stash/carry, refused untrusted, `MidiRemoteNotAllowed`), undo `recordMidiRemoteChange` | — | FRO124 |
| 2 | **Transport verbs → command-dispatched actions** (play, stop, play/stop toggle, record, loop, metronome, return to start) | — | FRO125 |
| 3 | **RemoteEngine**: `RemoteMessageSink` seam in `AudioEngine`, snapshot table, decode, FIFO, drain with gestures + takeover, action invoke, reconcile, Hosted source | 1, 2 | FRO127 |
| 4 | **MIDI Learn on the module card**: menu block, arm/settle/bind, auto-profile, badge, `registerMidiLearnable` registry covering sliders, toggles, combos, bespoke cards, header buttons | 3 | FRO130 |
| 5 | **Learn coverage: mixer, master, transport bar** | 4 | FRO133 |
| 6 | **MIDI Remote panel**: dock tab, controllers list, surface with live activity, inspector | 3 | FRO131 |
| 7 | **Detect, Add controller, templates, import/export, encoder auto-detect** | 6 | FRO134 |
| 8 | **Mapping assistant**: pick-target overlay, action picker, orphan controller Re-link/Recreate, orphan node display | 6, 4 | FRO135 |
| 9 | **Preferences + Audio-tab suffix + plugin-build behaviour** | 6 | FRO136 |
| 10 | **E2E test, docs pass, tripwires** (`Source/CLAUDE.md` threading line, `Source/UI/CLAUDE.md` registry line, Docs map, `midi_input.md` pointer) | 4–9 | FRO138 |
| v2 | Feedback to controller · 14-bit/NRPN · focus bank · mapping pages · template library | 10 | FRO139 · FRO140 · FRO141 · FRO142 · FRO143 |

---

## Related

- [`midi_remote.md`](midi_remote.md) — model and decisions.
- [`plugin_card_layout.md`](plugin_card_layout.md) — the hosted-plugin knobs this maps.
- [`mixer.md`](mixer.md) §5.9 — the bottom dock the panel joins.
- [`layout_visuals_animation.md`](layout_visuals_animation.md) — the animation rules the pulse
  and the surface obey.
- [`shortcuts.md`](shortcuts.md) — action ids and display names the action picker reuses.
