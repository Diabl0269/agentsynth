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
mode, Add controller, Templates, import/export and the encoder Auto-detect (FRO134); the
control-first "Assign from the panel" popover, orphan Re-link/Recreate and the Inspector's Relearn
are still design-only.
This doc, and [`midi-remote-ui.md`](midi-remote-ui.md), describe the feature as designed; where
current behaviour differs from the design, the surrounding text says so explicitly.

---

## What exists today

External MIDI today is a **note path only** ([`midi-input.md`](midi-input.md)):

- `AudioEngine::handleIncomingMidiMessage` (MIDI driver thread,
  `Source/AudioEngine/AudioEngineMidi.cpp`) is the single convergence point for every opened
  `juce::MidiInput`. It forwards each message to the engine's own `MidiMessageCollector`
  (the graph-input stream) and to any `ExternalMidiModule` whose **display name equals the
  device name** (`setMidiDeviceName` → `setModuleName`; renaming the module breaks the binding).
- Devices are opened by name via `AudioEngine::ensureMidiDeviceOpen(name)`; refused in
  `HostMode::Hosted` — the host owns MIDI and hands it in through `processHostBlock`.
- There is no persistent "enabled MIDI devices" list beyond what the stock
  `juce::AudioDeviceSelectorComponent` (Settings → Audio) ticks in `AudioDeviceManager`.
- **Nothing interprets CC, pitch-bend, aftertouch or program change.** No `MidiLearn`,
  `controllerNumber`, `isController` anywhere in `Source/`. This is a from-scratch feature.
- **No MIDI output.** The Audio tab's MIDI-output selector is a dead control: nothing reads
  `deviceManager.getDefaultMidiOutput()`.
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
  **message-thread only**. `ShortcutManager` is a real action registry (74 named actions), but
  only *command-dispatched* actions carry a `juce::CommandID` (`AppCommands::getCommandForAction`);
  `togglePlayback`, `undo`, `redo` do, while `timelineToggleLoop` and friends are
  *surface-resolved* (the panel's own `keyPressed` matches them). Record and metronome have no
  action at all — the transport bar reports intent through `onRecordToggled` and `MainComponent`
  decides.
- The module card's generic controls come from one function, `ModuleComponent::createControls()`
  (`Source/UI/Graph/ModuleComponent/ModuleComponent.cpp`): a rotary `juce::Slider` per
  float/int parameter (each registered with `slider->addMouseListener(this)` — the
  "right-click-any-knob" hook), a `ComboBox` per choice, a `ToggleButton` per bool (no mouse
  listener today), parallel `sliders`/`sliderParams` arrays. Right-click on a knob today shows one
  item, **"Automate '<Param>'"** (`showAutomateMenuForSlider`,
  `ModuleComponentInteraction.cpp`). A hosted plugin card shows **no parameters at all** — only
  "Open Editor".

---

## Goals and non-goals

**Goals (v1):**

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

**Non-goals (v1)** — each a planned extension tracked separately, not an accident: 14-bit CC / NRPN,
feedback to the controller (LED rings, motor faders, MIDI out), MCU/HUI protocol surfaces,
MPE per-note expression, a "focused module" bank that follows selection, a device template
library beyond a few generic ones, OSC.

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
specs). Mappings never silently die because a settings folder is elsewhere. This is the same
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
channels (MPE is out of scope for v1; this rule just stops MPE traffic from binding garbage). A learn
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
picker, no Detect-by-device) and shows Host MIDI's surface only. Whether the host actually
forwards a controller's CCs to a plugin is the host's business (most do for instrument tracks).

### Action targets

*Options:* invoke `ShortcutManager` actions generically, or only the command-dispatched subset.

**Decision:** an action target invokes a **`juce::CommandID`** through
`ApplicationCommandManager::invokeDirectly` on the message thread — i.e. only
**command-dispatched** actions are targets. The transport verbs users actually want on hardware
buttons — **Play, Stop, Play/Stop toggle, Record, Loop toggle, Metronome toggle, Return to
start** — are today either surface-resolved (loop) or not actions at all (record, metronome,
stop, return-to-start). They get **promoted to command-dispatched actions** first (a
prerequisite task in the tracker; it also gives them keyboard shortcuts, which they lack). The
panel's action picker lists actions by `ShortcutCategory` with the same display names as the
Keyboard Shortcuts settings tab, so the two lists can never disagree.

A button target's `buttonMode` (momentary / toggle) decides whether note-off / CC 0 fires
anything (momentary: nothing; toggle: the action fires on every press only). BPM and playhead
position as *continuous* action targets are a planned extension, tracked separately.

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
press toggles solo, like a mouse click; hold-to-solo is not v1. Applying reaches the app layer
through a new `RemoteActionInvoker::invokeNodeCommand(nodeId, command)` (Core knows neither
`ChannelStripModule` nor `AudioEngine::setChannelStripSoloed`), which performs the SAME undo-bracketed
call the mixer column's own click does — one undo step per press.

---

## Data model

All types live in Core under `Source/MidiRemote/` (headless-testable, no `ApplicationProperties`
— the stores are injected from the app layer, see [Persistence](#persistence-and-the-trust-boundary)). Names match the shipped types.

```text
ControllerProfile                         // GLOBAL — one per physical controller
  id            : uuid string
  name          : "Launchkey Mini MK3"
  input         : { identifier, name }    // juce::MidiDeviceInfo; identifier matches first, name is the fallback
  output        : { identifier, name } | null   // reserved for v2 feedback; never read in v1
  passMapped    : bool (default false)    // see Are mapped messages consumed
  controls[]    : Control
  actions[]     : Assignment              // GLOBAL assignments: target.kind == action only
  version       : 1

Control
  id            : uuid string
  name          : "Knob 1"
  kind          : knob | fader | button | pad | encoder | wheel
  message       : MessageSpec
  encoding      : abs7 | relTwos | relBinOffset | relSignMag      // abs14 is v2
  buttonMode    : momentary | toggle       // buttons/pads only
  layout        : { col, row }             // grid cell on the drawn surface

MessageSpec     // the KEY the engine matches on
  type          : cc | note | pitchBend | channelPressure | programChange
  channel       : 1..16 | 0 (= any)
  number        : 0..127                   // cc number / note number; ignored for pitchBend/channelPressure

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

Project "midiRemote" (reserved top-level key in project.json)
  version       : 1
  assignments[] : Assignment               // target.kind == parameter or nodeCommand; never action
  controllers[] : { profileId, name }      // for the orphan-controller display, see Where does a mapping live
```

Rules:

- A `MessageSpec` is the engine's lookup key; two controls on one profile may not share a key
  (the panel refuses; Detect merges into the existing control instead).
- A parameter assignment resolves through `synth::resolveLaneParameter` and nothing else — the
  hosted-plugin rules (exact id, index hint rescue, drift → orphan) come for free.
- Deleting a node orphans its assignments (they stay in the project, flagged; the control does
  nothing). Deleting the assignment is explicit. Duplicating / pasting a module does **not**
  copy its assignments in v1.
- A **relative** encoding delivers a signed delta; the engine applies `delta × sensitivity` to
  the *current* normalised value (sensitivity per assignment, default 1/127 per detent). Takeover
  is skipped. Auto-detect of the encoding is a Detect-mode helper ("turn it left, now right"),
  never a runtime guess.
- `range` maps the hardware's 0..1 onto `[min, max]` of the parameter's normalised range;
  `min > max` inverts. Buttons on a float parameter toggle between `min` and `max`.

---

## The engine

`synth::midi::RemoteEngine` (Core, `Source/MidiRemote/RemoteEngine*.cpp` split by concern):

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
  every control surface, tests and the implementation tracker.
- [`plugin-card-layout.md`](plugin-card-layout.md) — which hosted-plugin parameters show as
  knobs (and the future "edit any module's layout").
- [`midi-input.md`](midi-input.md) — the existing note path this feature sits in front of.
- [`modules/modulation.md`](../modules/modulation.md) — sample-accurate control is CV, not MIDI Remote.
- [`shortcuts.md`](shortcuts.md) — the action registry action targets invoke.
