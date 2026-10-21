# Timeline invariants (Source/Timeline/)

Engine-level timeline rules (`EpochExchange`, publish order, the `timelineChanged` seam, uuid mirroring) are in `Source/CLAUDE.md`. One rule per bullet; detail in the linked docs.

- **Audio clips STREAM, and only the prefetch thread touches a reader**: nothing on the audio path opens, reads or maps a file; a bounce steers `wantedFrame` via `waitUntilPrimed`. → [app-wiring](../../docs/architecture/app-wiring.md#audioclipstreamer-disk-streaming-clip-playback) · [modules](../../docs/modules/modules.md#track-audio-module-timeline-audio-source-hidden)
- **A hosted plugin's lanes bind only through `synth::resolveLaneParameter`**: exact id wins; a stale `paramIndexHint` orphans the lane. → [modulation](../../docs/modules/modulation.md#hosted-plugin-parameters-as-automation-lanes)
- **A lane lives on the track that plays its module**: `synth::resolveTrackOwners` is the one ownership rule (mirrors auto-arrange's claim). → [automation](../../docs/timeline/automation.md#which-track-a-lane-lands-on)
- **A duplicated track copies only what it exclusively plays and re-creates every cable that left the copy**, in one undo step. → [add-track](../../docs/timeline/add-track.md#duplicate-a-track)
- **A track's "module" is its instrument; an open plugin window is read from `HostedPluginWindowManager::toggleEditorFor`**, never a cached flag. → [tracks](../../docs/timeline/tracks.md#show-a-tracks-module)
- **A note or length edit on a linked clip syncs its whole group inside the same mutation** (`syncLinkedClips` at the end of every note mutator), and a link group never has fewer than two MIDI clips. → [linked-clips](../../docs/timeline/linked-clips.md#what-is-shared)
- **A drum machine clip preview runs only while the transport is stopped, and `TimelineMidiSourceModule` ends it**: the UI publishes start/stop through atomics (never a lock), the module drops it when the transport plays, the track is lost or the module is bypassed. → [drum-machine](../../docs/timeline/drum-machine.md#play-clip)
- **Per-drum mute/solo lives on the `Clip` (`mutedPitches`/`soloPitches`) and is applied only in `TimelineSnapshot::buildFrom`**; never write `MidiNote.muted` for it, so the piano roll's own note mutes stay the user's. → [drum-machine](../../docs/timeline/drum-machine.md#mute-and-solo)
- **A modulator's amount lane is never empty and is removed only with its routing** (`writeAmountLane`); identify it by the Attenuverter uuid. → [automation](../../docs/timeline/automation.md#amount-lane)
