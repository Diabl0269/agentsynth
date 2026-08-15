# Architecture Overview

Agent Synth is built on a modular graph architecture using JUCE's `AudioProcessorGraph` (implicitly managed by `AudioEngine`).

---

## Project Structure

The build produces four CMake targets:

| Target | Kind | Contents |
|---|---|---|
| `Core` | Static library | All audio modules, `AudioEngine`, `PresetManager`, `AppUndoManager`, theme system, `LayoutUtil`. Headless-testable — no audio device or GUI window required. |
| `AppUI` | Static library | `MainComponent` plus everything under `Source/UI` not already in `Core` (`GraphEditor`, `ModuleComponent`, `AIChatComponent`, `SettingsWindow`, `ModMatrixComponent`, etc.). Links `Core` PUBLIC. Shared by `AgentSynth` and `AgentSynthPlugin` so the app and the plugin build **one** copy of the editor UI from **one** source list — a plugin whose UI drifts from the app's is the exact failure this library exists to prevent. Deliberately not folded into `Core`: `Core` is the headless-testable layer, and the `Tests` target still compiles these sources itself with `JUCE_MODAL_LOOPS_PERMITTED=1`, which `AppUI` is not built with. |
| `AgentSynth` | JUCE GUI app | Links `AppUI` + `Core`. Adds only `Source/Main.cpp` — the standalone `JUCEApplication` entry point (and the generated `JuceHeader.h`), which has no meaning inside a plugin and so stays out of `AppUI`. |
| `AgentSynthPlugin` | Audio plugin — VST3 on every platform, + AU on macOS | Links `AppUI` + `Core`. Wraps the same `AudioEngine`/`MainComponent` in a `juce::AudioProcessor` (`Source/Plugin/`). Built when the `ENABLE_PLUGIN` CMake option is on (default `ON`). Standalone format is deliberately excluded from its `FORMATS` list — JUCE would emit a second "Agent Synth.app" that collides with the `AgentSynth` target's own release artifact. See [Plugin Layer](#plugin-layer) below. |

`Assets` is a separate binary-data target that embeds font files and SVG icons; it is linked into `Core` (and therefore `AppUI`, `AgentSynth`, and `AgentSynthPlugin`) so every target resolves `BinaryData` symbols.

---

## Core Components

### 1. AudioEngine

`Source/AudioEngine.h/.cpp`

Manages audio device I/O (via `juce::AudioIODeviceCallback`), the `juce::AudioProcessorGraph`, and modulation-matrix routing. Key responsibilities:

- **Audio callback** — `audioDeviceIOCallbackWithContext` drives `mainProcessorGraph.processBlock`, then applies master-mute zero-fill **after** `processBlock` so sequencers, LFOs, and envelopes keep advancing even while muted. `masterMuted_` is `std::atomic<bool>`.
- **Module lifecycle** — dynamic addition/removal of modules and connections in the graph.
- **Preset load/save** — delegates to `PresetManager`.
- **Modulation-matrix view** — `getModulationRoutings()` returns a `std::vector<ModulationRouting>` derived on demand from the live graph. Never serialized. `RoutingKind` values: `AttenuverterChain`, `DirectCV`, `PolyBus`. `getActiveModRoutings()` and `getModulationDisplayInfo()` are thin adapters over it.
- **Voice count / mute API** — `getDisplayVoiceCount()`, `setMasterMute(bool)`, `isMasterMuted()`.
- **Host modes** — `HostMode::Standalone` (default) vs `HostMode::Hosted` (used by the audio-plugin wrapper). Both funnel through one private `renderNextBlock` so master-mute semantics can't drift between them. See [Plugin Layer](#plugin-layer) below.
- **Transport** — the engine owns a `synth::TransportService` (`getTransport()`) and installs it as the graph's playhead once, in the constructor (`mainProcessorGraph.setPlayHead(&transport)`); JUCE re-applies it to every node on every render pass, so nodes re-created by a preset load or an undo restore need no re-injection. `transport.tick(numSamples)` runs as the first thing in `renderNextBlock` — before the graph — so it happens exactly once per callback in both host modes, and `transport.prepare(...)` runs before the graph's `prepareToPlay` in both `audioDeviceAboutToStart` and `prepareForHost`. `getGraphLatencySamples()` **reports** the graph's aggregate latency (JUCE's graph already compensates parallel paths internally); the engine adds no compensation of its own.
- **Timeline snapshots** — the engine also owns a `synth::TimelineSnapshotExchange` (`getTimelineSnapshots()`) and opens exactly one block on it per callback, immediately after the tick, so the snapshot reclamation epoch advances in lockstep with the clock. `shutdown()` calls `reclaimAllUnsafe()` last, once nothing can read it. See [TimelineSnapshot](#4-timelinesnapshot-the-audio-threads-view-of-the-timeline).
- **MIDI recording (TL3-3)** — `synth::MidiRecorder` (`Source/Timeline/MidiRecorder.h/.cpp`) records external MIDI into a new timeline clip as one undo step. `AudioEngine::setMidiCaptureSink(MidiRecorder*)` stores a `std::atomic<MidiRecorder*>` (null by default); `renderNextBlock` calls `recorder->captureBlock(midiMessages, transport.getCurrentBlockInfo())` inside the `SYNTH_ENABLE_TIMELINE` block, right after the snapshot is opened. This is the **single** capture point, and deliberately so: `handleIncomingMidiMessage` forwards every external MIDI message to two places — a direct `pushMidiMessage()` into any bound `ExternalMidiModule`, and `midiMessageCollector`, which is what `midiMessages` here actually is (collector-drained in standalone mode, delivered directly by the host in hosted mode). Recording from the `ExternalMidiModule` push-path copies too would double-record any note whose source also has an ExternalMidi node bound to it — those copies stay internal to that module's own `processBlock` and never reach this buffer, so capturing only here is what makes "recorded once, from the single collector-merged stream" true. `captureBlock` is lock-free and allocation-free: it pushes a POD `{beat, pitch, velocity, channel, isNoteOn}` per NoteOn/NoteOff into a fixed 4096-slot `juce::AbstractFifo` ring (dropping and setting an atomic overrun flag if full, never blocking); `stopAndCommit` (message thread) drains the ring, pairs NoteOn/NoteOff by `(pitch, channel)`, and commits a single new "Take" clip via `AppUndoManager::recordTimelineChange`. See [`docs/midi_input.md`](midi_input.md) for the two-paths detail this taps into.
- Requires `#include <bit>` for `std::popcount` (C++20).

### 2. TransportService (the one clock)

`Source/Transport/TransportService.h/.cpp`, `Source/Transport/TempoMap.h`, `Source/Transport/BlockTimeInfo.h`

The timeline's transport spine (TL1): play state, sample position, BPM, time signature, and loop bounds (in beats), headless-testable with no device dependency.

- **Beats are canonical.** All musical-time conversion goes through the two-method `TempoMap` seam (`beatFromSample`/`sampleFromBeat`), implemented by `ConstantTempoMap` for v1. A BPM change preserves the beat position and re-derives the sample position; so does a sample-rate change (`prepare`).
- **Command FIFO, never a lock.** The message thread posts POD commands (`play`/`stop`/`locateBeat`/`setLoop`/`setBpm`/`setTimeSignature`) through a lock-free SPSC `juce::AbstractFifo`, drained in posting order at the top of `tick()`. Posts are non-blocking and return `false` when the FIFO is full (dropped, caller may re-post). Commands take effect at sample 0 of the next block.
- **`tick(numSamples)` once per callback** computes an immutable POD `BlockTimeInfo` (block start/end ppq, loop-wrap sample offset, bpm, time sig, play/loop state) describing the block *before* the graph renders it, then advances the position, wrapping at the loop end (a wrap landing exactly on the block boundary reports `loopWrapSample == -1`; tiny loops wrap by modulo). Zero allocation on the audio path.
- **It is a `juce::AudioPlayHead`.** The engine installs it with `graph.setPlayHead(...)`; `juce::AudioProcessorGraph` re-applies the playhead to every node processor on every render pass, so every module — however created (fresh drop, preset load, undo restore) — reads the transport via the standard `getPlayHead()->getPosition()` API with no per-node injection bookkeeping. Sample-accurate consumers on the audio thread use `getCurrentBlockInfo()` instead. `AudioEngine` is the owner that wires this up: it calls `setPlayHead(&transport)` in its constructor, `transport.prepare(...)` ahead of the graph's `prepareToPlay`, and `transport.tick(...)` as the first statement of `renderNextBlock` — the single site both Standalone and Hosted modes clock the graph from.
- **Cross-thread reads** (UI playhead, status bar) go through `getPositionSnapshot()`, a seqlock over individual atomics — consistent multi-field reads, no locks, no tearing. The snapshot reflects the start of the current block (what's audible now).
- **The playhead also carries this block's `TimelineSnapshot` (TL3-1).** `setCurrentTimelineSnapshot` / `getCurrentTimelineSnapshot` park a *borrowed* pointer on the transport; `AudioEngine::renderNextBlock` sets it from `timelineSnapshots.beginAudioBlock()` immediately after `tick(...)`. The playhead is the only thing every module already has a handle to, so this is how a module reaches the timeline without per-node injection: downcast `getPlayHead()` to `synth::TransportService*` and read `getCurrentBlockInfo()` and the snapshot together, off one object. Two rules, both load-bearing: the pointer is **written and read on the audio thread only** (hence a plain pointer, no atomic), and it is **valid for the current block only** — caching it in a module member and reading it next block is a use-after-free, since the exchange frees a superseded snapshot two callbacks after retiring it. It is legitimately null (nothing published yet, a timeline-OFF build, a bare transport in a unit test), so every reader null-checks.
- **Revertibility flag.** `SYNTH_ENABLE_TIMELINE` (CMake option, default ON) compiles out only the *integration points* — the playhead install, the engine's per-block tick, and the sequencer sync dispatch; the Core Transport classes stay compiled and inert. A flag-OFF build must reproduce today's app and pass the full suite, asserted by the `Build and Test (Timeline OFF)` CI job on every PR. The runtime companion is `AudioEngine::setTransportEnabled` (persisted as the `timelineEnabled` setting, default on), which freezes/resumes the transport without a rebuild.
- **Offline rendering** — `synth::OfflineTransportDriver` (`Source/Transport/OfflineTransportDriver.h/.cpp`) clocks an `AudioEngine`'s graph at a fixed sample rate / block size with **no audio device**: it drives the engine through `prepareForHost` / `processHostBlock` (so `HostMode::Hosted` is required — a Standalone engine clocks itself) and offers `renderBlocks(n)` and `renderToBeat(beat)`, the latter rendering whole blocks until the just-rendered block's `endPpq` reaches the target, so it may overshoot by up to one block and returns an empty buffer rather than spinning if the target isn't ahead of a *playing* transport. This is the headless harness every engine-level timeline test uses; the per-block observer (`BlockCallback`, handed the rendered block plus its `BlockTimeInfo`) and the single `renderOneBlock` seam behind both entry points are what the user-facing bounce/export is later built on — export streams blocks to disk from the same loop instead of accumulating them. Allocation happens once per render call on the message thread; the per-block path allocates nothing.

### 3. TimelineDoc (the timeline document model)

`Source/Timeline/TimelineDoc.h/.cpp`

The message-thread model behind the timeline (TL2-1): tracks → clips → notes, plus automation lanes. Mutable, serialisable, headless — no GUI, editor or audio dependency. Everything downstream (the audio-thread snapshot, undo, `.agsproj` save) hangs off it.

- **Beats are canonical, and tempo isn't here.** Every time in the document is a beat position; BPM and time signature stay on `TransportService`. A tempo map moves into the doc only when real (non-constant) tempo maps arrive.
- **One mutation choke point.** Every public mutator validates its arguments first, then funnels the actual edit through the private `applyMutation()`, which bumps `getRevision()` and fires `Listener::timelineChanged` exactly once. Because validation happens *before* that seam, a call that changes nothing — a rejected note, `addLane` finding an existing lane, a setter given the value already stored — neither bumps the revision nor notifies. Snapshot republication (TL2-2), undo capture (TL2-5) and save-dirty marking (TL2-4) all attach here rather than diffing the model.
- **Ids are handles, not indices.** `TrackId`/`ClipId`/`LaneId`/`NoteId` are distinct tagged types (`value == 0` is the invalid sentinel), assigned monotonically, never reused within a doc's lifetime, and stable across a save/load — the next-id counters are serialised too, and on load they're floored at one past the highest id in the file so a hand-edited counter can't reissue a live id.
- **Sorted invariants are the model's contract**, not a convenience: clips within a track by `(startBeat, id)`, notes within a clip by `(startBeat, pitch, id)`, breakpoints by beat. That's what lets TL2-2 build its snapshot by flattening instead of sorting. Note positions are clip-relative, so moving a clip never rewrites its notes. Overlapping clips are legal in the model; whether the UI draws them is a policy decision above.
- **The note-editing and clip-operations API (TL2-3)** follows the same validate-then-`applyMutation` contract: `removeNote`/`moveNote`/`resizeNote`/`setNoteVelocity` reject bad input and no-op silently (no revision bump) when the requested value is already what's there; `quantiseNotes` snaps every note in a clip toward a beat grid, blended by strength, as a single mutation (a call that moves nothing is a no-op). `splitClip` keeps the original id on the left part and mints a new id for the right; a note straddling the split point is itself split into a truncated left note (same id) and a new right note re-based to beat 0 — the only place a note's id changes ownership mid-edit. `joinClips` rejects clips on different tracks and REJECTS overlapping clips (kept deliberately simple: unambiguous note re-basing beats supporting the merge case), extending `a` to cover `b`'s span and re-basing/merging `b`'s notes before removing it. `duplicateClip` deep-copies a clip's notes with fresh ids, placed immediately after the source.
- **A lane's identity is `(nodeUuid, paramId)`, doc-wide.** It binds to the graph node's `"uuid"` property — never its integer `NodeID`, which merge-mode graph apply is free to renumber. `addLane` for a pair that already exists anywhere in the document returns the existing lane's id and mutates nothing. Point values are stored **denormalised**, alongside a `RangeSnapshot` of the parameter's range captured at lane creation: if the range changes in a later build the mismatch is detectable instead of silently reinterpreting every point. Values are clamped into that snapshot on insert.
- **Hard caps, enforced by rejection** (`kMaxTracks` 256, `kMaxClipsPerTrack` 4096, `kMaxNotesPerClip` 16384, `kMaxLanesPerTrack` 512, `kMaxBreakpointsPerLane` 16384) — the model is bounded before `validateTimeline` (TL8-1) ever sees untrusted input. Exceeding a cap is refused, never clamped or truncated.
- **`toVar()`/`fromVar()` is the dialect TL2-4 embeds under the reserved top-level `"timeline"` key** (the one `AIStateMapper::validatePatch` refuses from provider output). `fromVar` is all-or-nothing: any malformed field — wrong type, out-of-range value, duplicate id, reserved track kind, a cap exceeded, a `"version"` that isn't 1 — returns `false` and leaves the doc byte-identical, same revision, no notification. On success it replaces the whole doc and fires once. A missing field takes its default; a *present* one must be well-typed — except a note's `"id"`, which is required (the dialect never shipped without one), so both a missing and a duplicate note id fail the whole load. Ordering is repaired rather than trusted (the loader re-sorts), because no reader downstream should have to defend against a file with a broken sort invariant.
- **Orphaned bindings (TL2-6).** A `Track`'s `bindingUuid` and an `AutomationLane`'s `nodeUuid` are the doc's only references to graph nodes, and both are keyed on the node's stable `"uuid"` property — never its integer `NodeID`, which merge-mode graph apply routinely renumbers (see §5 below). After *any* graph change — a deleted module, a rejected restore, an AI patch that dropped a node — such a binding can stop resolving. The rule: an unresolvable binding becomes **orphaned — retained, flagged, re-bindable — and is never auto-deleted**. Auto-deleting would let one AI patch apply silently destroy an hour of hand-authored automation; a merge that adds or renumbers nodes elsewhere in the graph must not orphan bindings that still resolve fine.
  - `Track::orphaned` / `AutomationLane::orphaned` are **runtime-only**: not written by `toVar`, and always `false` immediately after `fromVar` — there is no graph at load time to check a binding against, so the flag is *derived* state, not part of the document's persistent identity.
  - **Unbound vs. orphaned are distinct states.** An *empty* `bindingUuid`/`nodeUuid` is merely **unbound** — it was never pointed at anything, and playing/automating nowhere is legal. **Orphaned** means the binding *was* set and no longer resolves. Only a non-empty binding can be orphaned.
  - `TimelineDoc::reconcileBindings(uuidResolves)` recomputes every flag against a caller-supplied `uuid -> bool` callback, so the doc itself stays graph-agnostic and headless. It routes through the single `applyMutation` choke point **only if at least one flag actually changed** — one revision bump and one `Listener::timelineChanged` call however many bindings flipped in the pass; a reconcile that changes nothing is a true no-op (no bump, no notification, returns `false`). This is explicitly **not** a user edit and must never be wrapped in `AppUndoManager::recordTimelineChange`/`recordCombinedChange` — it derives current truth from the graph, it doesn't record an undoable intent.
  - `synth::TimelineReconciler::reconcile(doc, graph)` (`Source/Timeline/TimelineReconciler.h/.cpp`) is the graph-aware bridge: it collects the `"uuid"` property of every live node and calls `reconcileBindings` with straightforward set membership. Message thread only. **Call sites:** today, only `ProjectBundle::load` (see §5) — once the app owns a live `TimelineDoc` alongside its graph, it must also run after every graph-restoring choke point in `AppUndoManager`'s restore path (`applyJSONToGraph`, `applySnapshotPreservingNodes`, and any direct node delete outside the undo manager) — anywhere a node can appear or disappear is a place a binding can start or stop resolving.
  - **Re-binding.** `TimelineDoc::rebindLane(id, newNodeUuid)` is the one-click re-bind gesture's model half: it retargets a lane's `nodeUuid`, rejects a target that would collide with another lane's `(nodeUuid, paramId)` identity (the same doc-wide invariant `addLane` enforces) or an empty uuid or an unknown `LaneId`, and clears `orphaned` **optimistically** — the next reconcile re-derives whether it actually resolves. Unlike `reconcileBindings`, this **is** a normal user mutation (revision bump, notification, undoable). `setTrackBinding` clears the track's `orphaned` flag the same way whenever the uuid it stores actually changes.
- **MIDI file interchange (TL3-4).** `synth::MidiClipFile` (`Source/Timeline/MidiClipFile.h/.cpp`) imports and exports Standard MIDI Files (SMF) against `TimelineDoc` clips — the escape hatch while the piano roll stays minimal, and per TL8-5 the intended future AI-authored-note-data surface (a `.mid` blob carries no file paths and no plugin identifiers, unlike almost anything else a model could hand back). **PPQ time format only** — a file using SMPTE time format is rejected outright with a message mentioning "SMPTE"; ticks convert to beats by dividing by the file's own ticks-per-quarter-note. **Tempo is deliberately not read**: tempo/time-signature/every other meta event are ignored on import and never written on export, since `TimelineDoc` has no tempo map (tempo lives on `TransportService`, see above) — only track-name meta events are read, to name imported tracks. **Note pairing is FIFO per `(pitch, channel)`**, identical to `MidiRecorder` (TL3-3): the earliest still-open note-on for a key is closed by the next note-off for that key, and a note-on with velocity 0 is treated as a note-off (the SMF convention `juce::MidiMessage::isNoteOn()`/`isNoteOff()` already implement via their default arguments). A dangling note-on, or any paired note resolving to a non-positive length, is floored to `kMinNoteLengthBeats` (1/32 beat) rather than dropped. **Bounds-checking-strict**: a track whose note count would exceed `TimelineDoc::kMaxNotesPerClip` rejects the WHOLE import rather than truncating — this is a future untrusted-input surface. `importIntoTrack` is all-or-nothing against the doc's `kMaxClipsPerTrack` cap and performs its own `addClip`/`addNote` calls with no batching; the caller wraps the whole call in `AppUndoManager::recordTimelineChange` for undo. Export writes SMF type 1 at `kExportPpq` (960) ticks-per-quarter-note, one tempo-less track per clip, notes at clip-relative beats exactly as stored (no truncation to the clip's own length).

### 4. TimelineSnapshot (the audio thread's view of the timeline)

`Source/Timeline/TimelineSnapshot.h/.cpp`, `Source/Timeline/TimelineSnapshotExchange.h/.cpp`

TL2-2: the immutable, flattened projection of `TimelineDoc` the audio thread reads, plus the exchange that publishes it. The doc is a tree of `juce::String`s and nested vectors the message thread mutates in place — none of which is audio-safe — so the message thread flattens it once per change and hands the result over.

- **Flatten on publish, never sort.** `TimelineSnapshot::buildFrom(doc)` (message thread) produces four contiguous arrays of trivially-copyable PODs — `tracks`, `notes`, `lanes`, `points` — plus per-track/per-lane index ranges (`firstNote`/`numNotes`, `firstLane`/`numLanes`, `firstPoint`/`numPoints`). Because the doc keeps clips sorted by `(startBeat, id)` and notes within a clip by `(startBeat, pitch)`, each clip contributes an already-sorted run and a track's clips fold together with a **stable merge of sorted runs** (`std::inplace_merge`), never a full sort. Clips may overlap, which is why concatenation is not enough. `revision` carries the doc revision it was built from.
- **Beats become absolute, notes are clipped to their clip.** The doc stores note starts clip-relative (so moving a clip never rewrites its notes); the snapshot stores `clipStart + noteStart` and an absolute `endBeat`, because the audio thread compares against the transport's ppq and must not have to find the owning clip. A note starting **at or after** its clip's end is **dropped**; one overhanging the end is **truncated** so `endBeat` == clip end.
- **Fixed char buffers, never `juce::String`.** `bindingUuid` / `nodeUuid` / `paramId` are copied into `char[64]`, truncated at 63 bytes and always NUL-terminated (uuids are 36 chars, so truncation is theoretical). The audio thread `strcmp`s these; a `juce::String` copy is refcounted and therefore not audio-safe. `selfCheck()` re-verifies every index range and sort order — used by tests and the stress harness, not on the real audio path.
- **One atomic pointer, no `shared_ptr`, no locks.** `TimelineSnapshotExchange::beginAudioBlock()` is the whole read path: two atomic operations and a branch, zero allocation, `noexcept`. It is called **once per audio callback, before the graph**, and the reference it returns is valid only until the next call — never cached across blocks. It is **never null**: an exchange with nothing published returns a static empty snapshot, eagerly constructed by the exchange's constructor so the audio thread never runs a function-local static's guarded initialisation. `atomic<shared_ptr>` is rejected on purpose: it is lock-based or a per-read refcount RMW, and it would make the audio thread run `free()` whenever it dropped the last reference.
- **Epoch reclamation.** `beginAudioBlock()` bumps `audioEpoch` **before** loading the pointer. `publish(std::unique_ptr<TimelineSnapshot>)` swaps the new snapshot in, pushes the displaced one onto a message-thread-only retire list tagged with the epoch observed **at retire time**, then frees every retiree whose tag is `<= audioEpoch - 2`. The argument: a callback still holding the retiree must have bumped the epoch before its load, and its load must precede the publisher's swap (it didn't see the new pointer), so the tag is at least that callback's own epoch value; two epochs later, two further callbacks have *started*, and starting one is a promise the previous block's reference was dropped. All four operations (bump, load, swap, retire-time read) are `seq_cst` — a release bump plus an acquire load does not stop the load being hoisted above the bump, and nothing weaker gives the two threads the single total order the argument needs. This runs once per block, so the cost is noise (same trade as `TransportService`'s position snapshot).
- **Idle audio, and shutdown.** With the device stopped the epoch never advances, so retirees accumulate — bounded by the number of publishes made while idle, and drained by the first `reap()` after audio restarts. `reap()` is public so a host that publishes rarely can drain on a timer; while audio runs, `publish()`'s own reap keeps steady-state occupancy at a handful of snapshots. `reclaimAllUnsafe()` frees **everything, including the current snapshot**, ignoring the epoch rule — legal **only** once the audio thread is stopped and cannot call `beginAudioBlock()`. The destructor calls it, and `AudioEngine::shutdown()` calls it last, after the device callback is removed and the graph is cleared.
- **Engine wiring.** `AudioEngine` owns the exchange (`getTimelineSnapshots()`), and `renderNextBlock` opens exactly one block on it per callback, right after `transport.tick(...)`, inside the `SYNTH_ENABLE_TIMELINE` guard. Exactly one `beginAudioBlock()` per callback *is* the epoch-reclamation contract — never call it a second time in the same block, and never skip it. Since TL3-1 the returned reference is parked on the transport (`TransportService::setCurrentTimelineSnapshot`, see §2) so modules can reach it through the playhead they already hold. Unlike the tick it is deliberately *not* gated on `setTransportEnabled`, since freezing the transport is a musical decision and must not stall reclamation.
- **`anySoloed`** is computed once in `buildFrom` over the **MIDI** tracks. Solo is a document-wide predicate — "is anything soloed?" is what decides whether a *non*-soloed track is silent — so consumers read the flag instead of rescanning every track every block. Audio tracks are excluded: nothing renders them yet, and a soloed one must not silence the MIDI tracks.
- **Testing.** `Tests/TimelineSnapshotTests.cpp` pins the flatten policy and the reclamation protocol, including a `TimelineSnapshot::liveInstanceCount()` counter (always compiled in) that proves snapshots are actually freed. `StressPublishAcquire` runs a reader thread against a publisher for ~1.2 s; on its own it only proves nothing crashed, but under the label-gated `run-asan` CI job it becomes a hard use-after-free detector — a premature free racing a concurrent read is exactly what ASAN reports.

#### AutomationKernel (evaluating a lane on the audio thread)

`Source/Timeline/AutomationKernel.h` — header-only, `noexcept`, allocation- and lock-free. TL4-1: `synth::AutomationKernel::evaluate(points, numPoints, beat, fallbackValue, cursor)` turns one lane's contiguous breakpoint run (`snapshot.points.data() + lane.firstPoint`, `lane.numPoints`) into its value at a beat, in the lane's own denormalised units. TL4-2's applier is the consumer — it walks a snapshot's lanes, calls this once per lane per block and pushes the result at the bound parameter.

- **Endpoints clamp, never extrapolate.** An empty run returns `fallbackValue` (callers pass the lane's range default); a beat before the first point returns the first value, at or after the last returns the last. A lane is flat outside its own span, and a NaN beat takes the "before the first point" branch rather than producing a NaN.
- **Shape comes from the segment's LEFT point** — `BreakpointCurve`'s own definition. **Hold** returns the left value for the whole segment; **Linear** is `a + (b - a) * x^gamma` with `gamma = exp2(2 * tension)` (tension −1 → 0.25, fast start; 0 → 1, a plain lerp; +1 → 4, slow start), tension re-clamped to [-1, 1] so a hand-built run can't reach `std::pow` with a wild exponent. Endpoints stay exact for every tension, and the value exactly *at* a breakpoint's beat is always that breakpoint's own value. **Any other curve number — Bezier (2), or one from a future build — evaluates as Linear**, deliberately: an old build opening a newer file plays a sane approximation instead of a silent or stepped lane.
- **The cursor is the caller's, and it self-checks.** `AutomationCursor` caches the active segment plus its precomputed `gamma`, so a playing transport costs O(1) per call (one `std::pow` inside a shaped segment, a plain lerp when tension is 0). A beat *behind* the cached segment, a cold cursor, or a forward jump of more than `kMaxLinearAdvance` segments pays exactly one binary search — the worst case is O(log n) whatever the caller does with the beat. The cursor also stores the `points` pointer it cached against and re-searches on a mismatch, so a cursor held across a **snapshot swap** degrades to one search instead of reading a stale array; callers should still reset it (`cursor = {}`) on swap, but forgetting is then a lost cache rather than a wrong parameter value.
- **Defends against runs the doc can't produce.** Duplicate beats resolve to the rightmost point (so a zero-length segment is never the active one), a gap too small to reciprocate finitely is treated as degenerate, and `x` is clamped into [0, 1] before `std::pow` — a NaN escaping here would land straight in a live audio parameter. `Tests/AutomationKernelTests.cpp` pins all of it, including a 100k-beat sweep whose every warm-cursor result must match a cold binary-search evaluation **bit for bit**, and a perf tripwire (1M evaluations over a 4k-point run) that fires if the cursor ever degrades into a per-call scan.

### 5. ProjectBundle (.agsproj)

`Source/ProjectBundle.h/.cpp`

TL2-4: the on-disk project format that pairs a patch with a `TimelineDoc`. A bundle is a directory named `*.agsproj` containing `project.json` plus two reserved, empty-on-creation asset subdirectories, `Audio/` and `Peaks/` (TL6 — nothing reads or writes into them yet). `project.json` is everything `AIStateMapper::graphToJSON` writes (nodes, connections, modulations, schemaVersion, any keys `PatchDocument` had stashed from a prior load) with one addition: a top-level `"timeline"` key holding `TimelineDoc::toVar()`.

- **`"timeline"` is reserved everywhere else, meaningful only here.** A plain `.json` preset that happens to carry one only ever stashes it inertly via `PatchDocument` (see §3 above); `AIStateMapper::validatePatch` refuses it outright from provider-authored output (`PatchValidationError::TimelineNotAllowed`) so a model can never author automation. `ProjectBundle` is the one place a `"timeline"` key is actually interpreted.
- **Fixed, all-or-nothing load order**, mirroring `SnippetManager::insertSnippet`'s trusted/untrusted pairing:
  1. Parse `project.json` — unparseable or non-object is rejected.
  2. **Detach `"timeline"` from the root before anything else.** This has to happen *before* the gate in step 3: the untrusted validator refuses any patch carrying a `"timeline"` key, and a bundle's `"timeline"` is this format's own dialect, not provider output smuggled in — pulling it out first means it's validated on its own terms in step 4 instead of tripping a check meant for a different threat. The remaining root is "the patch".
  3. **Gate:** `AIStateMapper::validatePatch(patch, graph, clearExisting=true, trusted=false)`. `project.json` is a file on disk, hand-editable exactly like a preset or a snippet — a malformed patch is rejected whole, never partially applied.
  4. Validate the timeline into a **local** `TimelineDoc` via `fromVar` (already all-or-nothing). A missing `"timeline"` key is not an error — it means a plain patch is being opened as, or upgraded into, a bundle, and the timeline starts empty. A present-but-malformed one is rejected whole.
  5. Only once *both* validations pass does anything mutate: `applyJSONToGraph(patch, graph, clearExisting=true, trusted=true)` — trusted, so node `"uuid"`s are honoured and parameter values aren't rescaled by the untrusted-input heuristic, the same reasoning as the snippet insert path; `patchDocument.loadFromVar(patch)` on the timeline-stripped root, so `"timeline"` is never double-stored in the stash; then the live `timeline` is brought to the validated state (`fromVar` on the same var already proven valid against the local doc, or `clear()` when the key was absent).
  6. **Binding pass (TL2-6) — never delete.** `TimelineReconciler::reconcile(timeline, graph)` recomputes every track's `bindingUuid` and lane's `nodeUuid` against the freshly-loaded graph's live node uuids (see §3's "Orphaned bindings" for the full policy). A binding that no longer resolves is retained and flagged `orphaned`, never deleted; one that resolves is (re-)confirmed. Running this at the end of `load` means a freshly opened project shows correct orphan state immediately, before the user touches anything.
  - On any failure before step 5, `graph`, `timeline` and `patchDocument` are left exactly as they were.
- **Save order matters too.** `graphToJSON(graph)` → `patchDocument.toVar(...)` (re-merging whatever unknown top-level keys were stashed on this bundle's last load) → **then** `"timeline"` is set to `timeline.toVar()` **last**, so a live timeline always overwrites a stale one sitting in the stash (e.g. a bundle that started life as a plain `.json` carrying an old `"timeline"`).
- **Asset policy, reserved for TL6.** Any future asset reference is a path *relative to the bundle root* (`Audio/foo.wav`) — an absolute or escaping path must be rejected, the same restriction as `ModuleBase::setExtraState`'s trusted-only file paths. No enforcement code exists yet because nothing references an asset by path yet.
- **Plain `.json` save/load is unaffected.** `GraphEditor::savePreset`/`loadPreset` are untouched by this class — a `.agsproj` is a parallel format, not a replacement.

### 6. ModuleBase

`Source/Modules/ModuleBase.h`

Abstract base class for every audio processing unit. Extends `juce::AudioProcessor`.

#### ModuleType enum

Every concrete module implements `virtual ModuleType getModuleType() const = 0`. Current values:

```
Oscillator, Filter, VCA, ADSR, LFO, Sequencer, PolySequencer,
MidiKeyboard, PolyMidi, ExternalMidi, Attenuverter,
Delay, Distortion, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter,
ParametricEQ, VoiceMixer, Bitcrusher, PitchShifter, Noise, Math, Sampler, Wavetable,
MacroControl, SampleHold, EnvelopeFollower, TimelineMidiSource
```

`ModuleType` is consumed by `LayoutUtil::getModuleWidthBucket` to classify modules into width buckets (Narrow / Single / Double) and by `ModuleComponent` for type-safe UI layout switching.

#### Node uuid mirror (`setNodeUuid` / `getNodeUuid`)

A graph node's `"uuid"` property (see `AIStateMapper::graphToJSON`) is the app's long-lived node identity — timeline track bindings and automation lanes key on it. It lives in a `juce::NamedValueSet` of `juce::String`s, neither of which the audio thread may touch, so `ModuleBase` **mirrors** it into a fixed `char[64]` the moment it is assigned. `getNodeUuid()` is audio-safe (acquire-load a flag, return the buffer or `""`, never null, no allocation), which is what lets `TimelineMidiSourceModule` `strcmp` itself against a `TimelineSnapshot::TrackInfo::bindingUuid`.

- **Writers are exactly the three places `AIStateMapper` writes the property**: `adoptUuidIfTrusted` (trusted apply), `graphToJSON`'s lazy generation, and `applySnapshotPreservingNodes`. Each calls the shared `mirrorUuidIntoProcessor` helper immediately after `node->properties.set("uuid", ...)`. Add a fourth writer of the property and it must mirror too, or a Track In node silently stops matching its track.
- **The invariant the lock-free read relies on:** the uuid only ever transitions **empty → value**, and only while the node is not yet audio-visible (all three writers run on a node the caller just created, or under the graph callback lock). It is never rewritten to a different value and never cleared, so an audio-thread reader sees either `""` (and does nothing) or the final value — there is no torn intermediate state to defend against.

#### ModulationTarget / ModulationCategory

- `ModulationTarget { juce::String name; int channelIndex; }` — describes a modulatable parameter and the audio-buffer channel that carries its CV signal. Returned by `virtual getModulationTargets()`.
- `ModulationCategory` enum: `Envelope, LFO, Oscillator, Sequencer, Filter, FX, Other`. Returned by `virtual getModulationCategory()`.

#### VisualBuffer

Modules may opt in to a thread-safe `VisualBuffer` (circular buffer of `std::atomic<float>`) for scope visualization. Enabled/disabled via `enableVisualBuffer(bool)` and accessed via `getVisualBuffer()`.

#### Logical-Port API

Maps raw audio-buffer channel indices to the visible jack slots shown in the UI.

| Symbol | Description |
|---|---|
| `PortRole` | Enum: `Audio, ModCV, Pitch, Gate, Midi, Other` |
| `LogicalPort` | `{ int visibleJackIndex; PortRole role; bool isPolyGroupHead; int polyVoiceSpan; }` |
| `mapInputChannel(int rawChannel)` | Virtual — returns `LogicalPort` for a raw input channel |
| `mapOutputChannel(int rawChannel)` | Virtual — returns `LogicalPort` for a raw output channel |
| `getVisibleInputPortCount()` / `getVisibleOutputPortCount()` | How many jacks the UI renders |
| `JackTarget` / `getJackTargets(jack, isInput)` | Inverse of `mapInput/OutputChannel` — every poly-group head anchored to a visible jack; drives connection creation in `GraphEditor` |

`GraphEditor` uses this API to anchor wire endpoints to the correct visible jack regardless of how many raw channels are fanned out underneath, and to resolve which raw channels a dragged cable or poly toggle should wire (`GraphEditor::resolvePolyLink`, `rewireForPolyChange`). See [docs/modulation.md](modulation.md#creating-poly-connections).

#### isAutoPromotableModTarget

`virtual bool isAutoPromotableModTarget(int dstChannel) const`

Guards poly-mode CV inputs from being auto-wrapped in an `AttenuverterModule` by the AI/routing layer. Default: returns `true` iff `dstChannel` is in `getModulationTargets()`. Modules override this to exclude poly-voice pitch/gate channels which should not receive an attenuverter.

#### Extra (non-parameter) State

`virtual juce::var getExtraState() const` / `virtual void setExtraState(const juce::var&)`

For state that has to survive a graph rebuild but is not expressible as a `juce::AudioProcessorParameter` — today only `SamplerModule`'s loaded file path. `AIStateMapper::graphToJSON` writes whatever `getExtraState()` returns as the node's `"state"` property, and `applyJSONToGraph` feeds it back through `setExtraState()`. Return a **void** `var` when there is nothing to persist, so modules that do not use the hook add no JSON.

This matters because preset load — and any undo that has to fall back to a full rebuild — goes through `graphToJSON` → `applyJSONToGraph`, which rebuilds processors from scratch: anything not in that JSON is silently lost. (An ordinary undo restores by diffing and keeps the processor, so it re-applies `"state"` only when it actually changed — see [AppUndoManager](#appundomanager).)

`setExtraState` is only ever called on the **trusted** path (our own snapshots and presets). A module may legitimately read this as a filename, so honouring it for untrusted model output would turn a patch suggestion into an arbitrary file read.

#### Port Labels

`virtual juce::String getInputPortLabel(int channelIndex) const` / `getOutputPortLabel(int channelIndex)` — overridden per-module to provide descriptive jack names (e.g. "CV In", "Audio L") shown in the UI.

#### Bypass/Mute Contract

Every `processBlock` override **must** honour both flags using **two separate branches**:

```cpp
if (isBypassed()) {
    // Dry pass-through — do NOT touch audio channels (ch0, ch1).
    // Clear CV channels (index >= 2) so mod CV does not leak as audio.
    return;
}
if (isMuted()) {
    buffer.clear();
    return;
}
// ... normal DSP ...
```

**Never** collapse to `if (isBypassed() || isMuted()) buffer.clear()` — that silences on bypass instead of passing the dry signal through.

**Exception:** modules with no dry audio path clear their output on bypass, because there is nothing to pass through. Two shapes qualify:

- **Pure sources** — no audio *input* (e.g. `OscillatorModule`, `PolyMidiModule`).
- **Audio-in / CV-out taps** — no audio *output* (e.g. `EnvelopeFollowerModule`, whose ch0 output is Env CV). Passing the dry signal through would push audio-rate samples into a CV destination, which is worse than emitting no modulation.

Both still use two separate branches, never a fused `if (isBypassed() || isMuted())`, so the intent stays explicit.

#### Output Level Stage

`ModuleBase` offers an opt-in output-level parameter for modules whose output is audio — `addOutputLevelParameter()` in the ctor, `prepareOutputLevel(sampleRate)` in `prepareToPlay`, `applyOutputLevel(buffer, numAudioChannels)` at the end of the **normal** `processBlock` path. It is a no-op on modules that never opt in.

Two constraints follow from the contract above: `applyOutputLevel` must sit **after** both early returns (a bypassed module passes dry audio through at full level; a muted one is already cleared), and `numAudioChannels` must exclude CV channels. Full rules, including why this is opt-in rather than universal and why it must be the last parameter added, live in [`fx_modules.md § Output Level`](fx_modules.md#output-level-shared-stage).

Related: look parameters up with `findParameterByID(processor, "paramID")` rather than `getParameters()[n]`. Parameter order is not part of a module's contract, and positional lookups silently repoint when a parameter is added.

### 7. GraphEditor

`Source/UI/GraphEditor.h/.cpp`

The visual patching interface. Lives in the `AgentSynth` app target.

- **Zoom/pan** — `zoomLevel` + `panOffset`; `mouseWheelMove` / `mouseDrag` on the canvas.
- **Wire drawing** — poly-bus wires (collapsed N-voice `DirectCV` connections) rendered with an "xN" badge; wire endpoints anchored to visible jacks via `ModuleBase`'s logical-port API.
- **Drag-to-connect** — `beginConnectionDrag` / `dragConnection` / `endConnectionDrag`; resolves the dragged jacks through the logical-port API (`resolvePolyLink`) and creates one connection per voice when both ends front an equally-wide poly fan, so a single drag between two poly jacks wires the whole fan at once. `disconnectPort` removes every raw channel a jack owns, including all voices of a fan.
- **Poly toggle rewire** — `rewireForPolyChange` re-anchors a module's existing cables to its new channel layout when its `poly` parameter changes (mono <-> fan), driven by `ModuleComponent`'s `"poly"` parameter listener.
- **Module drag** — `finalizeModuleDrag` snaps the released module to the 8 px grid and resolves overlaps via spiral search. A live drag-preview system (`beginDragPreview` / `updateDragPreview` / `endDragPreview`) shows a themed grid-dot overlay plus a snapped landing ghost during drags.
- **Library drops** — `resolvePlacement` + `finalizeModuleDrag` run on the real component after `updateComponents()` so the final position anti-overlaps using true pixel dimensions.
- **Auto-arrange** — `autoArrange()` (triggered by Cmd+L or the toolbar button) topologically layers modules by signal-flow depth in a single undo step. See `docs/layout.md` for the full layout model.
- **Delete** — `requestDeleteModule(NodeID)` is the canonical deletion entry point; `ModuleComponent::deleteButton.onClick` delegates here.

See [`docs/layout.md`](layout.md) for the grid model, anti-overlap algorithm, and `autoArrange` constants.

---

## Plugin Layer

`Source/Plugin/` wraps the same `AudioEngine` and `MainComponent` the standalone app runs in a `juce::AudioProcessor` (`AgentSynthAudioProcessor` / `AgentSynthPluginEditor`), producing the VST3/AU targets described in Project Structure above.

### Host modes (`AudioEngine::HostMode`)

- **`Standalone`** — `AudioEngine` owns a `juce::AudioDeviceManager`, opens the default output device and every available MIDI input, and is clocked by its own `audioDeviceIOCallbackWithContext`.
- **`Hosted`** — the only mode `AgentSynthAudioProcessor` uses. `initialise()` opens **no** audio device and **no** MIDI input — it only builds the initial patch. The host drives the graph instead, through a mirror of the device-callback trio: `prepareForHost(sampleRate, blockSize, numIn, numOut)` (from `prepareToPlay`), `processHostBlock(buffer, midi)` (from `processBlock`), `releaseFromHost()` (from `releaseResources`). Opening a device from inside a plugin would fight the host for the hardware; opening MIDI input directly would double-trigger notes the host already forwards.
- Both modes funnel through one private `AudioEngine::renderNextBlock`, which calls `mainProcessorGraph.processBlock` and then zero-fills on master-mute — the single choke point that keeps mute semantics from drifting between the two paths.

### Who owns what

- **AudioEngine** — owned by `MainComponent` (`ownedAudioEngine`) on the standalone path; owned by `AgentSynthAudioProcessor` and *injected* into `MainComponent` by reference on the plugin path (`MainComponent(tm, lf, AudioEngine&, ...)`). `MainComponent::initialiseCommon` skips `audioEngine.initialise()`/`shutdown()` whenever it doesn't own the engine, so opening/closing the plugin editor never re-initialises or tears down the running graph — only the processor's constructor/destructor call `initialise()`/`shutdown()`.
- **ThemeManager + AppLookAndFeel** — owned by `AgentSynthAudioProcessor`, not by the editor. A host may create and destroy `AgentSynthPluginEditor` many times over one plugin instance's life (every time its window is closed and reopened), and the LookAndFeel must outlive every `Component` that references it. `PluginProcessor.h` declares `themeManager`/`lookAndFeel` **before** `engine` specifically so they are destroyed *after* it and after any editor (which the base `AudioProcessor` tears down first) — the same shutdown-order guard `Main.cpp` uses for the standalone `AppApplication`.
- **LookAndFeel scope** — `AgentSynthPluginEditor` calls `setLookAndFeel(&processor.getLookAndFeel())` on itself and clears it in its destructor. It never calls `Desktop::setDefaultLookAndFeel`, which is process-global inside the host and would re-skin the host's own windows and every sibling plugin.
- **Settings window** — `MainComponent` passes `showAudioTab = !audioEngine.isHosted()` to `SettingsWindow`, which omits the Audio device tab in the plugin: the host owns the device, so an `AudioDeviceSelectorComponent` there would be inert at best, and dangerous if the user touched it.

### Plugin state format

`AgentSynthAudioProcessor::getStateInformation` / `setStateInformation` serialize a small JSON envelope — `stateVersion`, `patch`, `editorWidth`, `editorHeight`. The `patch` value is exactly `AIStateMapper::graphToJSON`'s output, the same shape a `.json` preset file uses, so plugin session state and preset files stay interchangeable; `setStateInformation` also accepts a bare patch object with no envelope, so a raw preset dropped straight into a host's state slot still loads.

Restore always calls `applyJSONToGraph(..., trusted=false)`: a host session file travels between machines and users, so it goes through the full `validatePatch` boundary (see [`docs/AI_Engine.md`](AI_Engine.md)) and is rejected whole — never partially applied — if it doesn't check out. Around the load, an open editor detaches its module components first (`prepareForGraphReplacement`) and rebuilds them after (`refreshAfterGraphReplacement`) — the same detach-before-clear ordering `GraphEditor::loadPreset` and `MainComponent::aiPatchAboutToApply` use to avoid a `ScopeComponent` timer firing against a freed `VisualBuffer`.

---

## Supporting Components

### LayoutUtil

`Source/UI/LayoutUtil.h/.cpp`

Stateless grid-layout helpers (`snap`, `intersectsAny`, `findFreeSlot`, `computeAutoArrange`). No JUCE GUI dependencies — fully headless-testable. See [`docs/layout.md`](layout.md) for the full API reference.

### ModuleComponent

`Source/UI/ModuleComponent.h/.cpp`

Auto-generates parameter UI from `ModuleBase` metadata using type-safe `ModuleType` switching. Modulation rings read `AudioEngine::getModulationRoutings()`. Uses `setBufferedToImage(true)` and gates its 15 Hz timer repaint so the `GraphEditor`'s 30 Hz connection animation composites cached module images without re-running JUCE text layout every frame.

### AttenuverterModule

`Source/Modules/AttenuverterModule.h`

Intermediary inserted between a modulation source and its destination to scale CV signals. Exposes `lastOutputPeak` / `lastModValue` atomics for UI metering. Constructor default `Amount = 0.0`; set to `1.0` by `addModRouting`, left at `0.0` by `addEmptyModRouting`. See [`docs/modulation.md`](modulation.md) for the full modulation routing model.

### AppUndoManager

`Source/AppUndoManager.h/.cpp`

Snapshot-based undo/redo wrapping `juce::UndoManager`. Structural graph changes (add/remove module, connect/disconnect) are captured as JSON before/after snapshots via `SnapshotAction`. Parameter and position changes have dedicated action types. Safe detach/reattach lifecycle — `setGraphEditor(nullptr)` before graph teardown.

#### Restoring a snapshot is a diff, not a rebuild

`SnapshotAction` restores through `AIStateMapper::applySnapshotPreservingNodes`, which compares the target snapshot against the live graph and touches only what differs. It does **not** replay the snapshot through `applyJSONToGraph`, which reaches the same end state by destroying and re-creating every node.

- **Identity is the per-node `uuid`**, not the integer `id` (merge mode renumbers ids). A live node whose uuid appears in the snapshot is **kept** and updated in place — parameters via the trusted param path, position from `"position"`, and `"state"` re-applied through `setExtraState` **only when it changed** (it reloads a sample/wavetable off disk, so an unconditional re-apply would hit the filesystem on every Cmd+Z). A live node whose uuid is absent from the snapshot is removed; a snapshot node with no live match is created, re-adopting both its uuid and its original id when that id is free.
- **A parameter-only undo performs zero topology operations.** No node and no connection is added or removed, so JUCE never rebuilds its render sequence and the audio callback never blocks. Mixed restores batch their topology ops (`UpdateKind::none`) and rebuild exactly once at the end. No graph callback lock is taken at any point.
- **Module runtime state survives.** Sequencer position, envelope stage and sounding voices belong to the processor instance, which is no longer thrown away. This is also what makes hosted plugins and timeline-driven MIDI sources viable: neither can afford to be re-instantiated on every Cmd+Z.
- **Any doubt falls back to the old full rebuild.** `applySnapshotPreservingNodes` plans the whole restore before mutating anything and returns `false` — graph untouched — on a live or snapshot node with no uuid, a duplicate uuid or id, a uuid whose module type no longer matches, an unknown type, a connection naming an undefined node, or a merge delta (`"remove"` / `"removeModulations"`) rather than a full snapshot. `SnapshotAction` then runs `applyJSONToGraph(..., clearExisting=true, trusted=true)`, which is always correct. Correctness beats preservation.
- **UI teardown is now conditional.** The action's `preRestore` hook (`GraphEditor::detachAllModuleComponents`, and the AI service's `aiPatchAboutToApply`) exists to detach UI from processors that are about to be freed, so it fires only when a node is actually being removed — or on the fallback, which frees everything. When nothing is freed there is nothing to detach, and `updateComponents()` (the `postRestore` hook) reconciles additively, so a parameter-only undo no longer destroys and re-creates every `ModuleComponent` either.

#### Timeline undo

TL2-5: `TimelineDoc` edits (tracks, clips, notes, automation lanes) go through a separate `TimelineSnapshotAction`, not the graph's `SnapshotAction` — folding timeline JSON into every graph snapshot would inflate the size of every existing undo step (a parameter tweak, a module drag) that never touches the timeline at all. Unlike `SnapshotAction`'s diffing restore, `TimelineSnapshotAction::perform()`/`undo()` call `TimelineDoc::fromVar` directly: `fromVar` is already the doc's own all-or-nothing load path, so there is nothing to diff against, and a `var` this action holds always came from this doc's own `toVar()`, so `fromVar` failing would mean the doc's round-trip contract itself broke (`jassert`ed, not silently swallowed). Both action types push onto the **same** `juce::UndoManager`, so the app has one undo stack and Cmd+Z stays chronological whichever domain each step came from.

- `recordTimelineChange(doc, mutation)` snapshots `toVar()` before and after the mutation; if the two serialisations are identical (the doc rejected the edit, or it was a genuine no-op) nothing is pushed and it returns `false` — a no-op must not create an undo step.
- `recordCombinedChange(graph, doc, mutation)` is for edits that touch both domains in one gesture — the canonical case is deleting a module a timeline lane is bound to. It opens ONE transaction, captures graph + timeline "before", runs the single mutation, then pushes a graph `SnapshotAction` and/or a `TimelineSnapshotAction` — only for whichever domain(s) actually changed — inside that same transaction, so one `undo()`/`redo()` reverts or re-applies both together, never half the edit. It reuses the exact same pre/post-restore lambda plumbing (`detachAllModuleComponents` / `updateComponents`) `recordStructuralChange` gives the graph half, factored into a private `createGraphSnapshotAction` helper rather than duplicated.
- **Lifetime rule, extended:** exactly like `SnapshotAction` holding the graph, a pushed `TimelineSnapshotAction` holds a reference to the `TimelineDoc` — the doc must outlive the `AppUndoManager`, or `clearUndoHistory()` must run before the doc is destroyed.

### AppLookAndFeel + ThemeManager

Central `LookAndFeel_V4` subclass and theme registry. Owns all stock-widget re-skins, treatment draw helpers, and the SVG `IconLibrary`. See [`docs/theming.md`](theming.md) for the full token reference, JSON schema, and how-to-add guide.

---

## Signal Flow

Modules communicate via two main signal types:

- **Audio Channels**: Stereo (usually) audio buffers containing PCM data.
- **CV (Control Voltage)**: Handled as control signals within the audio buffer (e.g., ADSR output feeding into VCA input 1).

---

## Quality Standards

All modules follow specific DSP requirements:

- **Smoothing**: All gain/cutoff parameters use linear smoothing to avoid clicks.
- **Antialiasing**: Oscillators use PolyBLEP for sharp waveforms.
- **Oversampling**: Nonlinear effects support configurable oversampling (e.g., Distortion offers Off/2x/4x modes).
