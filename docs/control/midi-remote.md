# MIDI Remote — external controllers, surfaces and MIDI Learn

This doc holds the **model and the decisions**: what exists, what the feature has to do, the
questions that had more than one sensible answer, and the answer picked for each with its
reason. The user-facing interaction (right-click MIDI Learn, the MIDI Remote panel, the
controller surface, the settings) lives in [`midi-remote-ui.md`](midi-remote-ui.md). The sibling
feature that lets a hosted plugin card show a chosen set of its parameters as knobs — the knobs
MIDI Remote then maps like any other — is [`plugin-card-layout.md`](plugin-card-layout.md).

**Status:** the model, persistence and `RemoteEngine` are built and wired into `AudioEngine`.
Right-click MIDI Learn is shipped and creates real assignments on every covered surface, including
the mixer column's Solo (FRO253's `nodeCommand` target). The MIDI Remote panel (FRO131) is shipped
— dock tab, Controllers list, Surface, Inspector, and "Edit MIDI assignment..." — as are Detect
mode, Add controller, Templates, import/export and the encoder Auto-detect (FRO134); and the
mapping assistant (FRO135) — the control-first "Assign from the panel" flow (pick-target overlay,
action picker), orphan-controller Re-link/Recreate and the orphan-node display; the Preferences
group and the plugin build's Host MIDI source (FRO136) round it out, with an end-to-end workflow test
(FRO138). Two things are built only in part: the Inspector's Relearn (rendered, disabled) and
[hosted-plugin knobs](plugin-card-layout.md) — the engine already resolves a hosted parameter
target, but the plugin card shows no knobs yet, so there is nothing on it to right-click.
This doc, and [`midi-remote-ui.md`](midi-remote-ui.md), describe what has shipped so far; where current
behaviour differs from the design, the surrounding text says so explicitly.

---

## The baseline this was built on

The survey below is the state of the app **before** MIDI Remote — kept because the decisions that
follow answer to it. Where a bullet has since changed, it says so. External MIDI at that point was
a **note path only** ([`midi-input.md`](midi-input.md)):

- `AudioEngine::handleIncomingMidiMessage` (MIDI driver thread,
  `Source/AudioEngine/AudioEngineMidi.cpp`) is the single convergence point for every opened
  `juce::MidiInput`. It forwards each message to the engine's own `MidiMessageCollector`
  (the graph-input stream) and to any `ExternalMidiModule` whose **display name equals the
  device name** (`setMidiDeviceName` → `setModuleName`; renaming the module breaks the binding).
- Devices are opened by name via `AudioEngine::ensureMidiDeviceOpen(name)`; refused in
  `HostMode::Hosted` — the host owns MIDI and hands it in through `processHostBlock`.
- There is no persistent "enabled MIDI devices" list beyond what the stock
  `juce::AudioDeviceSelectorComponent` (Settings → Audio) ticks in `AudioDeviceManager`.
- **Nothing interpreted CC, pitch-bend, aftertouch or program change.** No `MidiLearn`,
  `controllerNumber`, `isController` anywhere in `Source/` — a from-scratch feature.
  `RemoteEngine` is now that interpreter; an unmapped message still takes exactly this path.
- **The Audio tab's MIDI-output selector is still dead** — nothing reads
  `deviceManager.getDefaultMidiOutput()`. Controller feedback (FRO139, below) does not use it: it
  sends through each `ControllerProfile`'s own `output` device instead.
- Parameters are plain `juce::AudioParameterFloat/Int/Bool/Choice` on a `ModuleBase`
  (no APVTS). A parameter is addressed persistently as **(node uuid, `paramID`)** —
  `ModuleBase::getNodeUuid()` + `findParameterByID`, never by index — and every lane / binding
  in the app resolves through `synth::resolveLaneParameter`
  (`Source/Timeline/AutomationBinding.h`), which also knows how to address a **hosted plugin's**
  parameters (`HostedAudioProcessorParameter`, exact-id match first, `paramIndexHint` as a
  rescue, version drift → orphan, never a silent rebind).
- Two precedents for writing a parameter from outside the UI: `synth::AutomationApplier`
  (audio thread, `setValue` only — never `setValueNotifyingHost` — with UI reflection over the
  lock-free `AutomationUiFeed` ring), and every mouse gesture
  (`juce::SliderParameterAttachment`, message thread, `beginChangeGesture` /
  `setValueNotifyingHost` / `endChangeGesture`, one undo snapshot per gesture via
  `AppUndoManager::captureBeforeState` / `pushSnapshotFromCapture`).
- `synth::TransportService` producers (`play/stop/locateBeat/setLoop/setBpm`) are
  **message-thread only**. `ShortcutManager` is a real action registry (dozens of named actions), but
  only *command-dispatched* actions carry a `juce::CommandID` (`AppCommands::getCommandForAction`);
  `togglePlayback`, `undo`, `redo` do, while `timelineToggleLoop` and friends are
  *surface-resolved* (the panel's own `keyPressed` matches them). Record and metronome have no
  action at all — the transport bar reports intent through `onRecordToggled` and `MainComponent`
  decides.
- The module card's generic controls come from one function, `ModuleComponent::createControls()`
  (`Source/UI/Graph/ModuleComponent/ModuleComponent.cpp`): a rotary `juce::Slider` per
  float/int parameter (each registered with `slider->addMouseListener(this)` — the
  "right-click-any-knob" hook), a `ComboBox` per choice, a `ToggleButton` per bool (no mouse
  listener at the time), parallel `sliders`/`sliderParams` arrays. Right-click on a knob showed one
  item, **"Automate '<Param>'"** (`showAutomateMenuForSlider`,
  `ModuleComponentInteraction.cpp`); the MIDI Learn block now sits under it, registered through
  `registerMidiLearnable`. A hosted plugin card shows **no parameters at all** — only
  "Open Editor" — and still does until [`plugin-card-layout.md`](plugin-card-layout.md)'s card lands.

---

## Goals and non-goals

**Goals:**

1. **Right-click any control → MIDI Learn.** Every knob, slider, toggle and combo on every
   module card, the mixer strips (fader, pan, mute, solo, sends), the master, and the transport
   bar's buttons. Move a hardware control; it is bound. No panel needs to be open first.
2. **Zero-setup path.** The first Learn on a controller the app has never seen creates that
   controller's profile and adds the touched control to its surface automatically. Configuring a
   controller up front is optional, never required.
3. **One place to see and manage it all** — a MIDI Remote panel that shows each controller as a
   drawn surface (its knobs/faders/buttons/pads in the app's own look, moving live as you touch
   the hardware), each control labelled with what it drives, with an inspector to change any
   assignment, and a mapping assistant to assign from the panel side.
4. **Transport and commands** are mappable targets alongside parameters.
5. **Hosted plugin knobs** ([`plugin-card-layout.md`](plugin-card-layout.md)) are mapped
   exactly like built-in ones.
6. **Behaves like a mouse.** A hardware knob turn is indistinguishable from a mouse drag to the
   rest of the app: automation Touch/Latch records it, undo groups it per gesture, the plugin
   host is notified, the card's knob follows.
7. **The plugin build degrades honestly** (see [The plugin build](#the-plugin-build-vst3au-inside-a-host)): no device management inside a host, but
   mappings on the host-supplied MIDI stream still work.

**Non-goals (for now)** — each a planned extension tracked separately, not an accident:
MCU/HUI protocol surfaces, MPE per-note expression, a "focused module" bank that follows selection,
a device template library beyond a few generic ones, OSC. (Feedback to the controller shipped —
see [Controller feedback](#controller-feedback); 14-bit CC and NRPN encodings shipped — see
[14-bit and NRPN encodings](#14-bit-and-nrpn-encodings).)

---

## What the survey of other DAWs contributes

Studied: Cubase MIDI Remote, Ableton Live MIDI Map, Bitwig, Logic Controller Assignments,
Reaper, Studio One Control Link, FL Studio, Reason Remote, VCV Rack MIDI-Map, Renoise. What
each contributes to this design, and nothing else is borrowed:

| From | Borrowed | Why it fits a modular synth |
|---|---|---|
| **VCV Rack MIDI-Map** | The mental model: a hardware control is *patched* 1:1 to a module parameter and saved with the patch. | Our users already think in cables; a mapping is a cable from a physical knob. |
| **Ableton** | The right-click-the-target learn idiom and the three takeover words **Jump / Pick-up / Scale** (Ableton: None / Pick-up / Value Scaling). | Most field-tested vocabulary; no need to invent terms. |
| **Cubase MIDI Remote** | Separating the **surface** (what the hardware is) from the **mapping** (what it drives); a drawn surface; a mapping assistant you drive from the surface side. | One surface serves every project. The drawn surface is the "visualise them in that area" ask. |
| **Studio One Control Link** | The explicit **global vs. project** split. | Transport/commands must survive across projects; parameter mappings must travel with the project that owns the nodes. |
| **Reaper** | An explicit **relative-encoder encoding** selector, never a silent guess. | Three incompatible relative encodings exist; guessing wrong spins the wrong way. |
| **Bitwig Remote Controls pages** | Per-device-type editable ~8-knob pages. | This is the plugin-card feature, `plugin-card-layout.md`; also the shape a future focus-bank would use. |

Rejected outright: Ableton's whole-app blue overlay as the *only* learn entry (a modal mode is
one more thing to explain; right-click on the thing you want is what our users already do for
Automate), vendor scripting as the primary configuration path (Bitwig/Reason/Cubase scripts —
nobody writes JavaScript to map a knob), and an MCU-first design (a fixed protocol is a
separate integration, tracked as its own future extension).

---

## Decisions

Each subsection is one question, the options considered, the decision, and the reason.

### Where does a mapping live — global or in the project?

*Options:* (A) everything global per controller (Cubase); (B) everything in the project
(Ableton); (C) the user chooses a scope per mapping (Studio One); (D) the **target type
decides**.

**Decision: D.** An assignment whose target is a **parameter** (node uuid + paramID) lives in
the **project**, because node uuids only exist in that project. An assignment whose target is an
**action** (transport, undo, panel toggles — anything in the `ShortcutManager` registry) lives in
the **controller profile**, globally, because it means the same thing in every project. The
**surface** (what the hardware is: its controls, their messages, their encodings, its layout) is
always global — one profile per physical controller.

*Why not C:* asking is the one thing every survey user complained about; nobody wants a
"where should I save this?" prompt while performing. The type rule gives the right answer
every time without a question, and the panel shows a small **Project** / **Global** tag on each
assignment so the rule is visible rather than hidden.

*Consequence — a project references a global profile that may not exist on this machine.* Each
project assignment therefore carries a **denormalised copy of the control's message spec**
(type, channel, number, encoding, button mode, and the control's display name) next to the
`profileId`/`controlId` reference. When the project opens on a machine without that profile, the
assignments still resolve to *messages*; the panel shows the controller as an **orphan
controller** ("Launchkey Mini — not on this machine") and offers **Re-link** (pick a present
profile; controls match by message spec) or **Recreate** (mint a profile from the carried
specs). Re-link matches each assignment's denormalised spec to a control of the chosen profile by
exact `MessageSpec` (type, channel, number); the assignments with no match stay orphaned and the panel
says how many. Recreate mints a profile under a fresh id, bound to a MIDI input the user picks (Host MIDI in
the plugin build), named from `controllers[].name`, with one control per distinct spec and the
assignments repointed at it. Both are one undo step for the project half (Recreate's new profile file is
a profile edit and stays). Mappings never silently die because a settings folder is elsewhere. This is the same
shape as the timeline's rule that a binding is never re-established automatically
([`timeline/tracks.md`](../timeline/tracks.md#a-binding-is-never-re-established-automatically)): degrade visibly, repair explicitly.

### How does a hardware value reach a parameter?

*Options:* (A) on the MIDI thread / audio thread, `param->setValue()` only, UI reflected over a
ring — the `AutomationApplier` shape; (B) hop to the **message thread** and drive the parameter
exactly as a mouse drag does: `beginChangeGesture` on the first message, `setValueNotifyingHost`
per value, `endChangeGesture` after a short idle.

**Decision: B.** The MIDI thread classifies and decodes the message and pushes a small POD event
onto its source's lock-free SPSC FIFO (see [Threading](#threading-the-mapping-table-crosses-threads)); a message-thread drain applies it.

*The drain is a plain 60 Hz `juce::Timer` and nothing wakes it from the MIDI path.* An
`AsyncUpdater` kick was the original plan and is wrong: `triggerAsyncUpdate` takes a
`CriticalSection` and allocates inside the system message queue, which in the plugin build would
be a lock and an allocation on the audio thread. The timer instead simply runs whenever the
published table is non-empty or a learn is armed — both message-thread facts, needing no
cross-thread signal — and stops when neither holds. The worst-case latency is unchanged at one
frame.

*Why:* goal 6. Option A is right for playback automation because automation must *not* be
recorded, undone or host-notified — it is a transient effect. A hardware knob is the opposite: it
is the user editing. With B, `AutomationRecorder` hears it (Touch/Latch record from hardware for
free), `AppUndoManager` gets one snapshot per gesture through the same
`parameterGestureChanged` listener every card already uses (`ModuleComponentInteraction.cpp`,
`MixerFader`), the VST3/AU host sees a proper gesture, and every `SliderParameterAttachment`
follows without a reflection path. Latency is one frame (≤ 16 ms), which is invisible on a knob
and irrelevant to audio: **sample-accurate control of a parameter is what CV cables and the
modulation matrix are for** ([`docs/modules/modulation.md`](../modules/modulation.md)), and this feature never competes with them.

*Gesture end:* `endChangeGesture` fires **250 ms** after the last message for that assignment
(a constant, `kGestureIdleMs`), so a slow sweep is one undo step and one automation touch, not
hundreds. A button press is begin+set+end in one drain.

*Interaction with the automation-record claim:* `AutomationRecorder::isClaimed(param)` /
`ScopedProgrammaticApply` treat the parameter as "hand on the knob" for the gesture's duration,
identically to a mouse. Nothing new to design; the point of B is that there is nothing new.

### Are mapped messages consumed, or also forwarded to the graph?

**Decision: consumed by default**, per-profile toggle ("Also pass mapped messages to the
patch", default off). A message that matches a control with an assignment is handled by the
remote engine and **not** pushed into the engine's `MidiMessageCollector` nor into
`ExternalMidiModule`s. Everything else (notes on an unmapped channel, unmapped CCs) flows exactly
as today.

*Why:* a knob bound to filter cutoff must not also land in a MIDI clip being recorded, and a pad
bound to "Play" must not also trigger a note in the patch. *Stated consequence, so nobody files
it as a recording bug:* mapped CCs are absent from `MidiRecorder`'s take and from any
`ExternalMidiModule` on that device while the assignment exists. The toggle exists for the rare
"I want both" case. Unassigned controls on a profile are **not** consumed — detecting a control
onto a surface never changes what the patch hears until you assign it.

*Second stated consequence, for the same reason:* an assignment whose **target no longer
resolves** (its node was deleted, or its hosted plugin has no instance) is still consumed. The
assignment exists, so the control still belongs to MIDI Remote and the message is swallowed
rather than reaching the patch. The alternative — falling through to the graph once a target
dies — is worse: deleting a node would silently turn a mapped knob into a CC source that starts
landing in recording takes, i.e. a control's behaviour would flip based on whether some
unrelated node happens to exist. Consuming keeps "this control is mine" stable, and the panel's
orphan display is where the user finds out and re-points it. Only a control with *no assignment
at all* falls through.

### Threading: the mapping table crosses threads

The MIDI thread needs the consume filter and the learn-armed state inside
`handleIncomingMidiMessage`; the message thread edits assignments and profiles.

**Decision — a tripwire:** the engine never takes a lock on the MIDI path. The live mapping
table is an **immutable snapshot** (`RemoteMappingSnapshot`: flat sorted arrays of
`{packed message key → slot}` built on the message thread) published by **atomic pointer swap**.
The learn-armed state is a single `std::atomic<std::uint32_t>` token (0 = disarmed). The only
thing the MIDI thread writes is its source's lock-free FIFO. Anyone reaching for a
`CriticalSection` here is doing it wrong.

**There is more than one reader, and that decides the mechanism** (settled while building the
engine). Standalone opens one `juce::MidiInput` per profiled device and each may
deliver on its own driver thread; hosted delivers on the audio thread. So:

- Not `EpochExchange` (`Source/Timeline/EpochExchange.h`, and note it is in `Timeline/`, not
  `AudioEngine/`). It is single-consumer by construction — one epoch counter bumped once per
  audio block by the one audio thread. The MIDI path has no block boundary to bump on, and its
  "safe two epochs later" rule has no meaning with N readers.
- Not `std::atomic<std::shared_ptr<const Snapshot>>`. It is not lock-free in any shipping
  standard library — libstdc++ and MSVC use a spinlock, libc++ a mutex pool. In the plugin build
  that is a lock on the *audio* thread, i.e. the exact thing this section forbids.
- **Instead:** `std::atomic<const Snapshot*>` + a `std::atomic<int>` reader count + a
  message-thread retire list, freed on the drain tick only when the count reads zero. A reader
  holding a retired snapshot must have loaded the pointer before the swap, and its increment
  precedes its load; so if the count reads zero its decrement has already happened, and a reader
  arriving after the check cannot obtain the retired pointer at all. The proof is written out in
  `Source/MidiRemote/RemoteEngine/RemoteMappingSnapshot.h`.
- **One FIFO per source, not one shared FIFO.** `juce::AbstractFifo` (what `AutomationUiFeed`
  uses) is single-producer, so a shared ring would be torn by two devices moving at once. Each
  source owns a pre-allocated lane; a lane index is handed out on the message thread the first
  time a source key is seen and is never recycled, so a lane can never gain a second concurrent
  producer.

### Learn: what does the first message mean?

A learn is armed on a **target** (right-click a control → "MIDI Learn 'Cutoff'"; or from the
panel side, a **control** is armed and the target is picked next — [`midi-remote-ui.md`](midi-remote-ui.md#the-learn-interaction)).

*Resolution rule:* the engine opens a **300 ms settle window** at the first eligible message and
binds the **message key with the most messages** in that window (a knob sweep produces many
CCs; a stray touch-strip blip produces one). Eligible: CC, note-on, pitch-bend, channel
pressure, program change. **Ignored while learning:** note-off, per-note (poly) aftertouch,
clock/active-sensing/sysex, and any message on a channel the profile marks as MPE member
channels (MPE is out of scope for now; this rule just stops MPE traffic from binding garbage). A learn
on a *button-like* target (bool param, action) prefers note-on / CC 0-or-127 patterns and sets
`buttonMode` from the observed behaviour (a CC that returns to 0 on release → momentary).

*Auto-profile:* if the message came from a device with no profile, a profile is **created** named
after the device, and if the message key is not a known control on that profile, a control is
**added** (kind guessed: CC → knob, note → button; encoding absolute-7; layout: next free grid
cell; name "CC 21" / "Note C3"). The user renames and retypes later in the panel, or never.
This is what makes goal 2 true. Implemented in `Source/MidiRemote/MidiRemoteLearnBinder.h`
(the pure bind-the-result function) and `Source/MidiRemote/MidiLearnController.h` (the app-layer
arm/cancel/undo glue a module-card right-click drives, docs/control/midi-remote-ui.md#right-click-midi-learn--coverage).

*Cancel:* Esc, clicking anywhere, or 10 s without an eligible message. Only one learn can be
armed at a time; arming another replaces it.

The armed state's visual is defined in [`midi-remote-ui.md`](midi-remote-ui.md#the-learn-interaction) (breathing outline, no glow).

### Takeover

Applies only to **absolute continuous** encodings (a 7-bit CC knob/fader; not relative
encoders, not buttons). Per-assignment setting, three values borrowed verbatim from Ableton:

- **Jump** — the parameter jumps to the hardware value immediately.
- **Pick-up** — nothing changes until the hardware crosses the current value, then it tracks.
- **Scale** — the parameter moves toward the hardware value proportionally, converging without a
  jump (Ableton "Value Scaling").

**Default: Scale.** Pick-up is the classic answer but produces the "stuck fader" support
ticket (the user moves a fader and nothing happens); Scale never sticks and never jumps. The
default is a Preferences setting ([`midi-remote-ui.md`](midi-remote-ui.md#settings)); each assignment can override it.

### The surface is detected, not drawn

**Decision:** the primary way a surface comes to exist is by *touching the hardware* — in the
panel's **Detect** mode or implicitly during a Learn (see [Learn](#learn-what-does-the-first-message-mean)) — and the panel lays detected
controls out on a grid in touch order. The user can then drag to rearrange, rename, change
kind (knob / fader / button / pad / encoder / wheel) and encoding. A **template** (a few
generic ones shipped: "8 knobs", "8 faders + 8 buttons", "Transport", "Generic keyboard with 8
knobs") is an optional starting point, and a profile can be exported/imported as JSON so a
community template library can grow without code. Nobody has to draw their controller to map a
knob — Cubase's surface editor is the power tool, not the entrance.

### The plugin build (VST3/AU inside a host)

`HostMode::Hosted` never opens MIDI devices (`architecture.md`); that stays true. In the
hosted build the remote engine has exactly **one source, a pseudo-controller named "Host MIDI"**
fed from the MIDI buffer the host passes to `processHostBlock`. Learn, assignments, takeover and
consume all work on that stream; the panel hides device management (no Add controller, no device
picker, no Detect-by-device) and offers Host MIDI's surface as the only live one (profiles for real
devices are listed as *standalone only* and are inert — [`midi-remote-ui.md`](midi-remote-ui.md#plugin-build)). Whether the host actually
forwards a controller's CCs to a plugin is the host's business (most do for instrument tracks).

### Action targets

*Options:* invoke `ShortcutManager` actions generically, or only the command-dispatched subset.

**Decision:** an action target invokes a **`juce::CommandID`** through
`ApplicationCommandManager::invokeDirectly` on the message thread — i.e. only
**command-dispatched** actions are targets. The transport verbs users actually want on hardware
buttons — **Play, Stop, Play/Stop toggle, Record, Loop toggle, Metronome toggle, Return to
start** — plus the cursor moves (**Move Cursor Back/Forward by a Beat or a Bar**) and the loop
jumps (**Jump to Loop Start/End**) and the selection steps (**Select Next/Previous Module** on the canvas, **Select Next/Previous Track** on the timeline) — are today either surface-resolved (loop) or not actions at all (record, metronome,
stop, return-to-start). They were **promoted to command-dispatched actions** first (which also
gave them keyboard shortcuts, which they lacked). The
panel's action picker lists actions by `ShortcutCategory` with the same display names as the
Keyboard Shortcuts settings tab, so the two lists can never disagree.

A button target's `buttonMode` (momentary / toggle) decides whether note-off / CC 0 fires
anything (momentary: nothing; toggle: the action fires on every press only). BPM and playhead
position are NOT action targets — they are continuous targets (below), so a knob/fader/encoder can
drive them with direction and takeover instead of firing on every press.

Action targets are press-only, so a relative encoder assigned to an action fires it on every
detent regardless of direction; direction-aware jogging is what a continuous playhead target
(below) is for.

### Node command targets

*Options:* model the mixer column's Solo button as a fake boolean parameter on
`ChannelStripModule`, or give `Target` a third kind that names a graph node and a command rather
than a parameter.

**Decision:** a third `Target` kind, **`nodeCommand`** — `{ nodeUuid, command }`, `command` today
only `toggleSolo`. Solo is deliberately not a `juce::RangedAudioParameter` (`ChannelStripModule::soloed_`
is trusted engine state the render-time solo gate reads, `mixer.md#solo-is-a-render-time-gate`), so
it has nothing for a `Target::Parameter` to point at, and it is not a `ShortcutManager` action
either — actions are a fixed, per-app id set, and a per-*node* id would grow that registry once per
strip a project happens to have. A `nodeCommand` assignment is **project-scoped**, exactly like a
parameter assignment (it names a node uuid that only means something within this project) —
`MidiRemoteProjectDoc::assignments`, never a `ControllerProfile`'s global `actions`. It resolves
against the graph and orphans on a missing node exactly like a parameter target. It fires
**press-only**, in both momentary and toggle button modes, exactly like an action target — a pad
press toggles solo, like a mouse click; hold-to-solo is not built. Applying reaches the app layer
through a new `RemoteActionInvoker::invokeNodeCommand(nodeId, command)` (Core knows neither
`ChannelStripModule` nor `AudioEngine::setChannelStripSoloed`), which performs the SAME undo-bracketed
call the mixer column's own click does — one undo step per press.

### Continuous targets

*Options:* reserve a handful of fixed `ShortcutManager` action ids for "BPM up/down"-style presses
(the way [Action targets](#action-targets) already promotes transport verbs to commands); or give
`Target` a fourth kind that a knob/fader/encoder drives continuously, with a value and a takeover,
the same way a parameter target already works.

**Decision:** a fourth `Target` kind, **`continuous`** — `{ kind: bpm | playhead | masterVolume }`.
An action target is fundamentally *press-only* (`docs/control/midi-remote.md#action-targets`'s own
"fires on every detent regardless of direction"): reserving action ids for tempo/playhead would
still leave a jog wheel unable to report which way it turned, which is the whole point of a jog
wheel. A continuous target is scope **GLOBAL**, exactly like an action (`ControllerProfile::actions`,
never a project's `MidiRemoteProjectDoc`) — "the current project's tempo" is a contradiction; there
is one transport and one master fader per *machine session*, not per project, so it means the same
thing everywhere an action id does. A control drives at most one global target: assigning a
continuous kind replaces any existing assignment in that profile's actions on the same control
(action or continuous) or with the same continuous kind — and assigning an action likewise replaces
a continuous one on that control.

**masterVolume reuses the parameter path outright.** It resolves to the SAME
`juce::AudioProcessorParameter*` the mixer's own master fader binds (a new injected
`ContinuousParameterLookup`, since Core must not include `MasterModule.h` — the app layer finds the
graph's Master node and its `"gain"` parameter), so `applyToParameter`'s takeover/gesture math and
`RemoteEngineFeedback.cpp`'s echo both apply unchanged; a continuous target only fires its own
`applyToContinuous` for the two kinds with no `juce::AudioProcessorParameter` to point at:

| kind | native units | absolute window | relative | takeover |
|---|---|---|---|---|
| `bpm` | BPM | 60..187, fixed (`kRemoteBpmWindowMin/Max`) — 1 BPM per 7-bit step | ±1 BPM per detent | honoured (default Scale, same as a parameter) |
| `playhead` | beats | the loop region when looping, else 0..the arrangement end rounded UP to a whole bar (minimum 8 bars) | ±1 beat per detent, clamped ≥ 0 | **ignored — always Jump** (the playhead moves on its own; there is nothing to converge from) |
| `masterVolume` | — (parameter path) | the assignment's own `[range.min, range.max]`, same as any parameter | bypasses takeover, same as any parameter | honoured (parameter path) |

An assignment's `range` still narrows the hardware's 0..1 before it is mapped into whichever window
applies — the same `[rangeMin, rangeMax]` role a parameter assignment's range already plays.

**Reaching the transport:** `RemoteActionInvoker` (the same Core-to-app-layer seam `nodeCommand`
uses) gains three message-thread methods — `getContinuousValue`/`setContinuousValue` (native units)
and `getContinuousWindow` (playhead's window; bpm's is the Core constants above, so it never asks).
The app layer's implementation drives `synth::TransportService::setBpm` for bpm (tracking its own
last posted BPM the same unconsumed-request way, so several detents in one drain add up), and for playhead
reuses FRO271's own tracked-request state (`Source/Transport/TransportNudge.h`,
`locateTransportTracked`/the same accumulation `nudgeTransportCursor` relies on) rather than a
second "where is the cursor really going" bookkeeping — a fast jog wheel produces several relative
events inside one drain tick exactly like a fast keyboard repeat does, and both need to accumulate
against the still-pending request, not the stale audio-thread snapshot.

**The plugin build is inert for bpm/playhead** (`setContinuousValue` a no-op, `getContinuousWindow`
returns false) — `HostMode::Hosted` never owns the transport, same rule
[The plugin build](#the-plugin-build-vst3au-inside-a-host) already states for device management.
masterVolume keeps working there: it is the parameter path, and Master is an ordinary graph node
whether or not the app owns the audio device.

**No feedback for bpm/playhead.** [Controller feedback](#controller-feedback)'s loop only ever
echoes a *parameter* slot's value; masterVolume qualifies (it has one) and is echoed exactly like
any other mapped parameter, but bpm/playhead have no `juce::AudioProcessorParameter` to read back
from, so nothing lights an LED ring for them today.

### 14-bit and NRPN encodings

Two additions let 14-bit controllers map without stair-steps: a message type (`nrpn`) and two paired encodings (`abs14`, `abs14LsbFirst`).

**Message types:**
- **`cc` (existing)** — unchanged; CC 0..127 with one of the absolute or relative encodings.
- **`nrpn`** — NRPN (Non-Registered Parameter Number) addressing 0..16383. The engine maps
  the message key `(sourceKey, type=nrpn, channel, number=14bitAddress)` such that NRPN addresses
  never collide with CC numbers — `nrpn 21` is distinct from `cc 21`.

**Encodings for CC pairs (abs14 variants):**
- **`abs14`** (JSON `"abs14"`) — for controllers that send MSB first. CC `n` is MSB (0..31),
  CC `n+32` is LSB; the value is committed when the LSB arrives, using the last MSB seen. A second
  MSB arrival (without an intervening LSB) is remembered; a lone LSB re-commits with the last MSB;
  a lone LSB with no MSB ever seen produces nothing. State is held per source AND per MIDI channel.
- **`abs14LsbFirst`** (JSON `"abs14LsbFirst"`) — for controllers that send LSB first. CC `n`
  is MSB, CC `n+32` is LSB; the value is committed when the MSB arrives, using the last LSB seen.
  Same per-source/per-channel pairing rule as `abs14`.

Only a CC number 0..31 or an NRPN may use a paired encoding. An NRPN carries only `abs7` (data
via CC 6 alone) or one of the paired encodings (`abs14` / `abs14LsbFirst` via CC 6 and CC 38).
Loading a profile that violates these rules rejects the whole profile like any other malformed
field.

**Engine behaviour (MIDI path):**
- **CC pair lookup:** the mapping table registers BOTH CC `n` and CC `n+32` to the same paired slot.
  An explicit user assignment on CC `n+32` keeps that message and prevents `n+32` from binding
  implicitly to the paired control.
- **NRPN state machine:** CC 99 (NRPN MSB address) and CC 98 (NRPN LSB address) arm an address per
  channel (a fresh 99/98 pair resets the buffered data halves — address changes mid-stream never mix
  values). CC 101 and CC 100 (RPN select, the MIDI "cancel NRPN" message) disarm the channel.
  CC 6 = data MSB, CC 38 = data LSB under an armed address. The four address CCs (99, 98, 101, 100)
  are never consumed (always pass to the patch) and never appear as `RemoteEvent`s.
- **Data CCs under NRPN:** CC 6 and CC 38 with an armed address become one `nrpn` message; they
  are consumed only when that address is mapped AND "also pass mapped messages" is off. An explicit
  mapping on the raw CC number (e.g. a plain knob on CC 6) wins over NRPN reading, and CC 6/38 with
  no armed address stay plain CCs. Unmapped NRPN and unmapped CC pairs still light the activity
  surface and feed Detect.

**Feedback:** a paired parameter slot echoes both CCs (MSB then LSB for `abs14`, LSB then MSB for
`abs14LsbFirst`); NRPN slots are not echoed.

**Detect mode:** if the two halves of a CC pair (CC `n` and CC `n+32`, either order, same channel)
arrive within 5 ms, they become ONE control — CC `n` then `n+32` ⇒ `abs14` control at number `n`;
CC `n+32` then `n` ⇒ `abs14LsbFirst` control at number `n` (the control is renumbered to `n`).
Slower or different-channel messages stay separate. `RemoteEvent` carries a 16-bit millisecond
timestamp for this pairing window. An unmapped NRPN shows up as ONE control named "NRPN `<address>`",
encoding `abs14`, kind knob.

**Inspector & Learn:**
- **Encoding dropdown:** for a CC 0..31, the dropdown offers "Absolute (7-bit)", the three relative
  encodings, "Absolute (14-bit, MSB first)", "Absolute (14-bit, LSB first)". For a CC ≥ 32 or any
  other message type, only the original four entries are shown. For an NRPN control: "NRPN (7-bit,
  CC 6 only)", "NRPN (14-bit, MSB first)", "NRPN (14-bit, LSB first)". A CC control cannot be
  retyped into an NRPN from the dropdown.
- **Right-click Learn:** an NRPN learns as type `nrpn` with the detected address and encoding `abs14`
 . A Learn on the LSB of an existing paired CC control
  binds to that control and does not change its encoding. A plain CC Learn never infers 14-bit by
  itself (use Detect or the inspector to set it).

**Known limits:**
- NRPN data-increment/decrement (CC 96/97) and relative NRPN are not supported.
- A controller that sends only the MSB of a 14-bit pair (no LSB) does not move a paired mapping —
  use `abs7` for it.
- A separate control already mapped on CC `n+32` keeps that message: the paired control on CC `n` then
  never receives its LSB, so give the two different numbers.

---

### Controller feedback

**Decision (FRO139):** `RemoteEngine::drain()` sends every mapped parameter's current value back
out to its controller, so LED rings / motor faders / pad lights follow a mouse edit, automation
playback, undo, or a project load, not just a hardware turn.

- **Polling in the drain, not a parameter listener.** The drain already runs on the message thread
  at `kDrainHz` for the apply path, and a mapped parameter can change from four places: a mouse
  drag, automation playback (`synth::AutomationApplier` moves it with a bare `setValue()`, which
  notifies no `juce::AudioProcessorParameter::Listener` at all), undo/redo, and project load.
  Reading `param->getValue()` once per assignment in the same drain pass costs nothing extra and
  needs no per-slot listener to add/remove across every reconcile. It also can never re-enter
  `apply`: this only ever calls `RemoteFeedbackSink::sendFeedback()`, never
  `beginChangeGesture`/`setValueNotifyingHost`.
- **The cooldown.** `kFeedbackCooldownMs` (== `kGestureIdleMs`, 250 ms) treats "a hardware event
  arrived recently" as "the user is still touching this control" and holds off sending an echo — a
  motor fader or LED ring that receives its own just-sent value back while still moving visibly
  hunts. Sending the final value once the window closes is what re-syncs a pick-up/scale takeover's
  own picture of where the parameter really is, and what a motor fader needs to snap to on release.
- **Encoding** is the exact inverse of the assignment's own range mapping (`docs` above, "hardware
  value reach a parameter"): the parameter's normalised value is mapped back through
  `[rangeMin, rangeMax]` to 0..1, then encoded per `MessageSpec.type` and encoding:
  `cc` with abs7 → 7-bit; `cc` with abs14/abs14LsbFirst → both CCs (MSB then LSB, or LSB then MSB per encoding);
  `nrpn` → not echoed (echoing one means re-sending its address CCs first); `pitchBend` → 14-bit; `note` sends note-on velocity 127/0 for
  a bool parameter or a button-like control, else a scaled velocity. A relative-encoded control still gets the
  same absolute CC echo — an LED ring reads position, not delta. Channel 0 (the profile's "any channel" for the
  incoming lookup) becomes channel 1 for feedback, since a message must go out on one real channel.
- **Only a parameter target (or a masterVolume continuous target, which resolves to a real
  parameter — see [Continuous targets](#continuous-targets)) with an output-configured profile
  sends anything** — an action, `nodeCommand`, or bpm/playhead continuous target has no value to
  echo, and a profile with `hasOutput == false` (the default) is silent. The output device is picked
  per controller, from the Controllers list's own
  right-click (see [midi-remote-ui.md](midi-remote-ui.md#controllers-list-left)), never the dead
  Audio-tab MIDI-output selector.
- **The plugin build never sends feedback.** `MainComponent` only calls
  `RemoteEngine::setFeedbackSink()` outside `HostMode::Hosted` — a hosted plugin has no MIDI output
  of its own to send through, so `RemoteEngine`'s `feedbackSink_` stays null there and the whole
  pass is a no-op.
- Implemented in `Source/MidiRemote/RemoteEngine/RemoteEngineFeedback.cpp` (Core; the interface,
  `synth::midi::RemoteFeedbackSink`, is Core too, same split as `RemoteMessageSink`) and
  `Source/MidiRemote/MidiRemoteFeedbackOutputs.{h,cpp}` (app layer: opens and caches a real
  `juce::MidiOutput` per device, remembering rather than retrying a failed open every drain tick).

---

## Data model

All types live in Core under `Source/MidiRemote/` (headless-testable, no `ApplicationProperties`
— the stores are injected from the app layer, see [Persistence](#persistence-and-the-trust-boundary)). Names match the shipped types.

```text
ControllerProfile                         // GLOBAL — one per physical controller
  id            : uuid string
  name          : "Launchkey Mini MK3"
  input         : { identifier, name }    // juce::MidiDeviceInfo; identifier matches first, name is the fallback
  output        : { identifier, name } | null   // controller feedback's destination -- see Controller feedback
  passMapped    : bool (default false)    // see Are mapped messages consumed
  controls[]    : Control
  actions[]     : Assignment              // GLOBAL assignments: target.kind == action or continuous only
  version       : 1

Control
  id            : uuid string
  name          : "Knob 1"
  kind          : knob | fader | button | pad | encoder | wheel
  message       : MessageSpec
  encoding      : abs7 | abs14 | abs14LsbFirst | relTwos | relBinOffset | relSignMag
  buttonMode    : momentary | toggle       // buttons/pads only
  layout        : { col, row }             // grid cell on the drawn surface

MessageSpec     // the KEY the engine matches on
  type          : cc | note | pitchBend | channelPressure | programChange | nrpn
  channel       : 1..16 | 0 (= any)
  number        : 0..127 (cc/note), 0..16383 (nrpn)  // cc number / note number / nrpn address; ignored for pitchBend/channelPressure

Assignment
  id            : uuid string
  control       : { profileId, controlId }
  spec          : MessageSpec + encoding + buttonMode + control name   // DENORMALISED copy, see Where does a mapping live
  target        : Target
  takeover      : jump | pickup | scale | default   // "default" = the Preferences value
  range         : { min: 0.0, max: 1.0 }   // normalised; invert = min > max
  enabled       : bool

Target (exactly one)
  parameter     : { nodeUuid, paramId, paramIndexHint }   // same triple as an automation lane
  action        : { actionId }                            // ShortcutManager action id
  nodeCommand   : { nodeUuid, command }                   // command: toggleSolo -- see Node command targets
  continuous    : { kind }                                // kind: bpm | playhead | masterVolume -- see Continuous targets

Project "midiRemote" (reserved top-level key in project.json)
  version       : 1
  assignments[] : Assignment               // target.kind == parameter or nodeCommand; never action or continuous
  controllers[] : { profileId, name }      // for the orphan-controller display, see Where does a mapping live
```

Rules:

- A `MessageSpec` is the engine's lookup key; two controls on one profile may not share a key
  (the panel refuses; Detect merges into the existing control instead).
- A parameter assignment resolves through `synth::resolveLaneParameter` and nothing else — the
  hosted-plugin rules (exact id, index hint rescue, drift → orphan) come for free.
- Deleting a node orphans its assignments (they stay in the project, flagged; the control does
  nothing). Deleting the assignment is explicit. Duplicating / pasting a module does **not**
  copy its assignments yet. Engine-side a deleted node simply leaves the slot's parameter
  unresolved (`Slot::orphaned` is the separate flag for hosted-plugin drift and node commands);
  the panel shows either state as "(missing module)".
- A **relative** encoding delivers a signed delta; the engine applies `delta × sensitivity` to
  the *current* normalised value (sensitivity per assignment, default 1/127 per detent). Takeover
  is skipped. Auto-detect of the encoding is a Detect-mode helper ("turn it left, now right"),
  never a runtime guess.
- `range` maps the hardware's 0..1 onto `[min, max]` of the parameter's normalised range;
  `min > max` inverts. Buttons on a float parameter toggle between `min` and `max`.

---

## The engine

`synth::midi::RemoteEngine` (Core, `Source/MidiRemote/RemoteEngine/RemoteEngine*.cpp` split by concern):

```text
MIDI thread (or the host's audio thread in Hosted mode)
  RemoteEngine::handleMessage(sourceKey, const MidiMessage&)   // called from AudioEngine::handleIncomingMidiMessage
    1. learn-armed?  → push RemoteEvent{kind=learnCandidate, spec}; return consumed=false
                        (the 300 ms settle histogram is built on the MESSAGE thread during drain:
                         see Threading -- the FIFO is the only thing this thread writes, and it means it)
    2. snapshot = table.load(acquire); look up (sourceKey, MessageSpec) → assignment slot
    3. no slot → return consumed=false (message flows to the graph as today)
    4. decode: abs7 → 0..1 | relative → delta | button → pressed/released
    5. push RemoteEvent{slotIndex, kind, value} onto THIS SOURCE's SPSC FIFO (see Threading);
       return consumed = !profile.passMapped

message thread
  RemoteEngine::drain()   // 60 Hz timer, running while the table is non-empty or a learn is armed
    per event: resolve slot → live target (cached from the last reconcile)
      parameter: takeover(value, current) → if first in gesture: beginChangeGesture; setValueNotifyingHost; arm idle timer
      action   : commandManager.invokeDirectly(commandId, asynchronously=false) on press (per buttonMode)
    expire gestures idle ≥ kGestureIdleMs → endChangeGesture

  RemoteEngine::reconcile(graph)        // MainComponent's existing reconcile funnel after any graph change
    rebuild slot→target cache via resolveLaneParameter; orphan what no longer resolves; republish snapshot

  RemoteEngine::setAssignments(...) / setProfiles(...)   // rebuild + atomic publish of the snapshot
  RemoteEngine::armLearn(LearnRequest) / cancelLearn(); onLearned callback (message thread)
```

`AudioEngine` gains one seam: a `RemoteMessageSink*` (an interface `RemoteEngine`
implements) consulted in `handleIncomingMidiMessage` **before** the collector push and the
`ExternalMidiModule` fan-out; if the sink reports consumed, the message goes nowhere else. The
source key is the `juce::MidiInput`'s device identifier (standalone) or the constant
`"host"` (Hosted). Hosted mode calls the same sink per message from `processHostBlock`'s input
buffer — on the audio thread, which the sink is already built for (no locks, no allocation,
see [Threading](#threading-the-mapping-table-crosses-threads)).

Device opening: the standalone engine opens every input device that has a profile
(`ensureMidiDeviceOpen`) at startup, in addition to the Audio tab's ticked devices — a profiled
controller must never need a second checkbox to work. Ticking a device in the Audio tab (or a
controller reconnecting) after that startup priming also opens it live: `AudioEngine::changeListenerCallback`
reconciles the open set against the Audio tab's checkboxes on every device-manager change
broadcast (FRO262 — see [`docs/architecture/audio-engine.md`](../architecture/audio-engine.md#audioengine)),
and `MidiLearnController::refreshSources()` republishes the resulting source list to
`RemoteEngine::setSources()` so an armed Learn sees the new source without waiting for its own
next arm. Unticking a device does **not** close it live — only a physical disconnect does; see
`AudioEngine::reconcileMidiInputs()`'s own comment (`Source/AudioEngine/AudioEngineMidi.cpp`) for
why treating "unticked" as a close signal would regress every user who has never opened the Audio
tab at all.

A device is opened **at most once**: `AudioEngine::openMidiInput` skips an identifier that is already
open. That matters because the profile priming above runs *before* `initialiseDevices()`'s
open-every-available-input loop, so a profiled controller is reached by both — a second
`juce::MidiInput` on the same endpoint delivers every message twice and doubled each hardware
gesture (a jog wheel at double speed, a toggle that flipped straight back; FRO279).
`Tests/Engine/MidiInputDeliveryTests.cpp` drives a real virtual OS MIDI source through that launch
order (it skips where the OS offers no virtual devices).

Live activity for the panel ([`midi-remote-ui.md`](midi-remote-ui.md#the-midi-remote-panel)) rides the same FIFO: every event carries its
decoded value; the panel drains a separate mirror ring at its own rate, and an unassigned control
still produces an activity-only event so Detect mode and the surface's "it lit up" feedback work
without an assignment.

---

## Persistence and the trust boundary

- **Profiles** are JSON files, one per controller, under the settings folder:
  `<settings>/MidiRemote/Controllers/<profileId>.json` (`branding::kSettingsFolderName`, the
  same folder as the `PropertiesFile`, but *not inside* it — a profile is a shareable document).
  Export / import is a file copy with a name check. Generic templates ship as resources (`assets/midi-remote-templates/*.json`, embedded via the `Assets` library).
- **Project assignments** are the reserved top-level key **`"midiRemote"`** in `project.json`,
  written by `ProjectBundle` **last**, exactly like `"timeline"` and `"macros"`: detached before
  validation and reattached after; **refused on the untrusted path** by
  `AIStateMapper::validatePatch` with a new `PatchValidationError::MidiRemoteNotAllowed`
  (a provider-authored mapping could never resolve to real hardware and is one more channel
  for smuggling state). `"midiRemote"` joins the reserved-keys list in `ai/patch-format.md` /
  `layout/macro-cards.md`. A plain preset (`GraphEditor::savePreset`) carries no
  assignments.
- **Core layering:** Core never touches `juce::ApplicationProperties`
  (`UserSettings.h`, `Source/CLAUDE.md`). `RemoteEngine` receives profiles and assignments as
  values; the file stores (`ControllerProfileStore`, project load/save) live in the app layer
  (`Source/MidiRemote/` for the pure serialisation + a thin `MainComponent`-owned store), and the
  three `PropertiesFile` owners (`Main.cpp`, `MainComponent`, `AgentSynthAudioProcessor`) stay
  in agreement because nothing new goes into the `PropertiesFile` except two scalar preferences
  (default takeover, panel visibility).
- **Versioning:** both documents carry `version: 1`; a loader refuses a higher version with a
  visible message and never partially applies.

---

## Undo

- **Parameter values** driven from hardware: one undo step per gesture, produced by the existing
  gesture listeners — nothing new (see [How does a hardware value reach a parameter](#how-does-a-hardware-value-reach-a-parameter)).
  A sweep over a lane armed for automation Touch records a take, which is its own undo step, so it
  costs two — exactly what a mouse drag over the same lane does.
- **Project assignments** (create via Learn, edit, delete, re-link): undoable through a new
  `AppUndoManager::recordMidiRemoteChange(before, after)` snapshotting the `"midiRemote"`
  document, the same before/after-JSON shape as `recordTimelineChange`. A Learn that also
  auto-creates a profile records only the project half; the profile stays.
- **Profile edits** (rename, retype, rearrange, templates, delete controller): global settings,
  **not undoable**, same as keyboard-shortcut rebinds. The panel confirms destructive ones
  (delete controller with N assignments in this project).

---

## Related

- [`midi-remote-ui.md`](midi-remote-ui.md) — the interaction design, the panel, coverage of
  every control surface, and the tests.
- [`plugin-card-layout.md`](plugin-card-layout.md) — which hosted-plugin parameters show as
  knobs (and the future "edit any module's layout").
- [`midi-input.md`](midi-input.md) — the existing note path this feature sits in front of.
- [`modules/modulation.md`](../modules/modulation.md) — sample-accurate control is CV, not MIDI Remote.
- [`shortcuts.md`](shortcuts.md) — the action registry action targets invoke.
