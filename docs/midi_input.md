# MIDI Input Support

Agent Synth now supports external MIDI input devices, allowing you to connect hardware controllers for more expressive playability, including polyphonic support.

## Usage
1. Open the application.
2. Go to **Settings** (Cmd+,).
3. In the **Audio** tab, you will find the audio device settings.
4. The **MIDI Input** dropdown allows you to select and enable your connected MIDI hardware controllers.
5. Once selected, MIDI messages from the hardware will be automatically routed to the active audio processor graph.

## Implementation Details
- `AudioEngine` now implements `juce::MidiInputCallback` to listen to external MIDI messages.
- Incoming MIDI messages are collected via `juce::MidiMessageCollector` and injected into the audio processing block.
- All available system MIDI input devices are automatically detected and started on application initialization.
- The `AudioDeviceSelectorComponent` in the Settings window has been updated to display MIDI input options, providing users with the ability to manage connections.

## External MIDI path: sample-accurate stamping

`AudioEngine::handleIncomingMidiMessage` runs on the MIDI thread and forwards every `juce::MidiInput` message to two places: `AudioEngine`'s own `midiMessageCollector` (feeds the graph's audio-thread MIDI stream, drained in `audioDeviceIOCallbackWithContext`) and any `ExternalMidiModule` node whose device name matches the source, via `pushMidiMessage()`. `juce::MidiInput` messages carry a wall-clock timestamp in seconds (the `Time::getMillisecondCounterHiRes()*0.001` convention).

`ExternalMidiModule` now owns its own `juce::MidiMessageCollector` (mirroring `AudioEngine`'s):
- `prepareToPlay()` calls `collector.reset(sampleRate)`; the constructor also calls `reset(44100.0)` so a `processBlock()` call that lands before `prepareToPlay()` can't hit the collector's un-reset jassert.
- `pushMidiMessage()` forwards straight to `collector.addMessageToQueue()`.
- `processBlock()` drains via `collector.removeNextBlockOfMessages()` into a member scratch `juce::MidiBuffer` (no per-block allocation), then applies the existing channel filter — preserving each event's `samplePosition` — before replacing the graph-supplied MIDI buffer, exactly as before.
- When bypassed, `processBlock()` also drains-and-discards the collector's queue every block, so messages pushed while the module sits bypassed can't accumulate unboundedly and don't resurface once bypass is lifted.

This replaces the old hand-rolled `incomingMessages` buffer + `CriticalSection`, which stamped **every** incoming message at sample 0 of the next block regardless of when it actually arrived — intra-block timing was destroyed and timeline notes collapsed to block starts. The collector instead converts each message's wall-clock timestamp into a same-block sample offset from the elapsed real time between drains, the same mechanism `AudioEngine`'s own `midiMessageCollector` already uses for the graph-input path.

Messages timestamped `0` (untimestamped — e.g. hand-constructed messages from tests or other synthetic sources) still land at sample 0 of the next block: `MidiMessageCollector` computes a deeply negative sample offset for them relative to its wall-clock reference point and clamps it to `0` on drain, preserving the old behaviour for callers that never call `setTimeStamp()`.

**Caveat on "future" timestamps:** `MidiMessageCollector::removeNextBlockOfMessages()` unconditionally drains and clears its *entire* queue on every call — it does not hold back events whose timestamp lies beyond the current block. A message queued long before it's actually due gets flushed (position-clamped into the current block) on the very next drain, regardless of how far "in the future" its timestamp claims to be. This matches real usage: a `juce::MidiInput` callback delivers a message when it physically arrives, timestamped at (approximately) that same moment — it never hands you a message seconds ahead of time. Sample-accurate placement therefore depends on `pushMidiMessage()` being called close to a message's timestamp, as a real MIDI thread does.

## MIDI timestamp handling audit

A survey of every MIDI producer/consumer in the codebase and how each handles a MIDI event's sample position within a block:

| Module | Handling | Notes |
|---|---|---|
| `MidiKeyboardModule` | Sample-accurate | Preserves `metadata.samplePosition` when it re-adds octave-shifted notes to the output buffer. |
| `PolyMidiModule` | Sample-accurate | Chunk-renders per event: uses `msg.getTimeStamp()` (populated by `MidiMessageMetadata::getMessage()` from `samplePosition`) as a render-chunk boundary, so gate/pitch changes land on the exact sample. |
| `AudioEngine` (`midiMessageCollector`, graph-input path) | Sample-accurate | `juce::MidiMessageCollector` converts MIDI-thread wall-clock timestamps into sample offsets within the audio block. |
| `ExternalMidiModule` | Sample-accurate (fixed by TL1-6) | Now backed by its own `juce::MidiMessageCollector`; previously stamped everything at sample 0 (the bug this task fixes). |
| `ADSRModule` (mono MIDI branch) | Block-granularity | Applies `noteOn()`/`noteOff()` for every event in the block before `applyEnvelopeToBuffer()` runs across the whole block — event offsets are read but not acted on. |
| `OscillatorModule` | Block-granularity | Overwrites `lastMidiNote` from the last note-on seen in the block; the resulting pitch is applied uniformly across the whole block. |
| `SamplerModule` | Block-granularity | Gate/note state (`midiGateOpen`, `midiNote`) is latched from the MIDI loop before the per-sample render loop runs, ignoring each event's `samplePosition`. |
| `LFOModule` (retrig) | Block-granularity | A note-on anywhere in the block resets `phase` to 0 before the per-sample generation loop — retrig is quantized to the block start. |
| `WavetableOscillatorModule` | Block-granularity | Same shape as `OscillatorModule`/`LFOModule`: note-on sets a block-level `pendingRetrigger` flag/pitch, consumed at the start of the block's processing. |
| `SequencerModule` / `PolySequencerModule` | Hardcoded offsets | Emit note-offs at sample `0` and note-ons at sample `min(1, numSamples-1)` instead of the true beat-crossing sample, once `samplesUntilNextBeat` signals a crossing occurred somewhere in the block. |

Block-granularity is an accepted trade-off for hand-played/CV-triggered input today — the poly path (gate-CV, sample-accurate via `PolyMidiModule`) is the timeline's actual route, so per-module fixes for the mono/legacy paths above are deferred to their own tasks rather than bundled here.

`SequencerModule` / `PolySequencerModule`'s hardcoded offsets are left as-is deliberately: existing presets are golden-tested byte-identical against those exact sample positions, and computing the true beat-crossing sample is revisited under the transport-sync task TL1-8.
