# Docs map

One topic per doc, split at section boundaries. Every doc below is the mechanism and history behind a rule; the rules themselves live in the per-directory `CLAUDE.md` files and the tripwire index in the root [`CLAUDE.md`](../CLAUDE.md). When you change behaviour, update the doc as part of the change, and keep this map current (a new doc gets a line here).

## Architecture & engine

- [`docs/architecture.md`](architecture.md) — hub: project structure, signal flow, quality standards, and the index into the topic docs below (split under FRO163, one doc had sat at the 1000-line cap)
- [`docs/architecture_audio_engine.md`](architecture_audio_engine.md) — AudioEngine, TransportService (bounce/export, stem export, metronome, input monitoring, mixer solo gate)
- [`docs/architecture_timeline.md`](architecture_timeline.md) — TimelineDoc, TimelineSnapshot, AutomationKernel/Applier/Recorder, UI reflection
- [`docs/architecture_project_bundle.md`](architecture_project_bundle.md) — ProjectBundle (.agsproj): open/save, recent projects, dirty state, autosave, welcome screen
- [`docs/architecture_module_base.md`](architecture_module_base.md) — ModuleBase (logical-port API, bypass/mute contract, output level stage) + supporting components (LayoutUtil, ModuleComponent, AppUndoManager, LookAndFeel)
- [`docs/architecture_graph_editor.md`](architecture_graph_editor.md) — GraphEditor: per-concern translation units and the three collaborator classes
- [`docs/architecture_app_wiring.md`](architecture_app_wiring.md) — who owns the live TimelineDoc and every hook that keeps it in step; audio recording, latency alignment, AudioClipStreamer, asset management
- [`docs/architecture_plugin_layer.md`](architecture_plugin_layer.md) — VST3/AU host modes, ownership, state format; hosting third-party plugins inside our own graph
- [`docs/modules.md`](modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math …)
- [`docs/fx_modules.md`](fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter, Parametric EQ, Ring Modulator)
- [`docs/modulation.md`](modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/Module_Development_Guide.md`](Module_Development_Guide.md) — step-by-step guide to adding a module

## Layout, canvas & theming

- [`docs/layout.md`](layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, LayoutUtil API, drag affordance + smart connections
- [`docs/layout_visuals_animation.md`](layout_visuals_animation.md) — visualizer components, UI rendering performance, animation system (UIAnimation.h, AnimationDriver, PanelSlide, micro-interactions), alignment guides
- [`docs/layout_selection_canvas.md`](layout_selection_canvas.md) — multi-select + group drag + snippets/clipboard (§1.5), collapsible library sections, cable interaction, minimap overlay
- [`docs/theming.md`](theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation

## Macros & mixer

- [`docs/macros.md`](macros.md) — Macros: the P8-12 presentation-only container and the DECIDED Macro I/O model (P8-14) overview, and why a container node with an inner graph was rejected
- [`docs/macros_ports.md`](macros_ports.md) — Macro I/O port mechanics: node types, port set/ordering, poly/stereo shape, cable rendering across the boundary, bypass/mute, the macro menu
- [`docs/macros_implementation.md`](macros_implementation.md) — Macro I/O AI-authorability decision, the P8-15 implementation tracker, and out-of-scope items
- [`docs/mixer.md`](mixer.md) — Mixer channels (P9-1, decided 2026-09-10): the ChannelStrip node is the channel, macros are the container, solo as a render-time gate, Master/Direct, the track/channel link, track presets
- [`docs/mixer_fader.md`](mixer_fader.md) — The mixer fader's own Cubase-like taper (FRO150): UI-only position mapping vs. the linear-dB parameter, Shift fine-drag, Cmd-click/double-click reset
- [`docs/mixer_implementation.md`](mixer_implementation.md) — Mixer build log: dependency order and the test list for each P9 item (engine/solo gate, channel creation flows, track/channel link, mixer panel, detachable windows, track presets, stem export, and the remaining side tracks)
- [`docs/testing_mixer_meters.md`](testing_mixer_meters.md) — mixer meter & fader test list (FRO146/FRO150/FRO147): peak latch, dB scale zones, clip readout, taper

## Timeline

- [`docs/timeline_panel_core.md`](timeline_panel_core.md) — timeline panel overview, ruler/grid/zoom/snap + markers
- [`docs/timeline_panel_tracks.md`](timeline_panel_tracks.md) — track headers/binding chips, Add-Track
- [`docs/timeline_panel_transport.md`](timeline_panel_transport.md) — playhead, transport bar, metronome, edit-tool strip
- [`docs/timeline_panel_clips_automation.md`](timeline_panel_clips_automation.md) — clip lanes, automation strip, keyboard & focus arbitration
- [`docs/timeline_panel_piano_roll.md`](timeline_panel_piano_roll.md) — the piano roll note editor

## MIDI, shortcuts & remote control

- [`docs/midi_input.md`](midi_input.md) · [`docs/shortcuts.md`](shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/midi_remote.md`](midi_remote.md) · [`docs/midi_remote_ui.md`](midi_remote_ui.md) — MIDI Remote (decided 2026-09-17, FRO121): external controller profiles, drawn surfaces, right-click MIDI Learn on every control, message-thread apply with gestures, scope-by-target-type persistence; the panel, coverage table and tracker
- [`docs/plugin_card_layout.md`](plugin_card_layout.md) — hosted plugin cards showing a chosen set of parameters as knobs (decided 2026-09-17, FRO122): `CardLayout`, instance/type/automatic precedence, `HostedParameterAttachment`, the knob picker with presets; and what carries over to editing any module's layout (FRO123)

## AI

- [`docs/ai/ai.md`](ai/ai.md) — overview: the two providers and what the assistant may author
- [`docs/ai/usage-guide.md`](ai/usage-guide.md) — user-facing guide: prompting, timeline changes, the Patch/Arrange selector, troubleshooting
- [`docs/ai/engine.md`](ai/engine.md) — AIIntegrationService/AIStateMapper architecture, request flow, the AI patch undo contract
- [`docs/ai/patch-format.md`](ai/patch-format.md) — the JSON patch dialect: nodes/connections/params, `state`/`uuid`/`displayName`, reserved keys and forward compatibility
- [`docs/ai/patch-safety.md`](ai/patch-safety.md) — `validatePatch`, the non-authorable module set, constrained decoding/retry/repair, worked examples, the harnesses
- [`docs/ai/patch-preview.md`](ai/patch-preview.md) — the patch card's diff preview and `PatchDiff`'s snapshot-based diff
- [`docs/ai/timeline-safety.md`](ai/timeline-safety.md) — `validateTimeline`, the two-door model, the checks, and the agentic security model
- [`docs/ai/timeline-ops.md`](ai/timeline-ops.md) — the `timelineOps` envelope, `placeMidiClip`, trust posture, the chat seam
- [`docs/ai/arrangement-context.md`](ai/arrangement-context.md) — the read-only arrangement summary sent to the model
- [`docs/ai/chat-component.md`](ai/chat-component.md) — AIChatComponent: bubbles, timeout, logging rules, the model-discovery and auth-token ordering contracts
- [`docs/ai/providers.md`](ai/providers.md) — the provider registry, which provider a launch gets, hosted-mode disclosure
- [`docs/ai/ollama-provider.md`](ai/ollama-provider.md) — OllamaProvider: fail-fast, the worker-thread contract, request cancellation
- [`docs/ai/remote-provider.md`](ai/remote-provider.md) — RemoteProvider: wire contract, error-kind mapping, capability requests and arrange mode
- [`docs/ai/structured-output.md`](ai/structured-output.md) — schema generation, the vendored envelope codegen, grammar-compiler pitfalls
- [`docs/ai/accounts.md`](ai/accounts.md) — sign-in surface, rotation-before-use, device id and trial, quota UI and PlanBadge
- [`docs/ai/history.md`](ai/history.md) — server-side and local conversation history, retention, the history panel
- [`docs/ai/feedback.md`](ai/feedback.md) — patch thumbs and their sync, general feedback, opt-in prompt collection

## Testing, CI & distribution

- [`docs/testing.md`](testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/testing-header-comments.md`](testing-header-comments.md) — the header-comment-placement guard (FRO183): why it tracks excess (comments minus code) rather than a raw comment count, the exact threshold, and the ratchet baseline
- [`docs/pr-title-convention.md`](pr-title-convention.md) — PR title format (`type(scope)!: subject`), why it's checked on the title not a commit message, and the not-yet-required rollout plan (FRO182)
- [`docs/docs-guard.md`](docs-guard.md) — `scripts/check-docs.sh`'s five checks (naming, links, `docs/...` mentions, `§`-section refs, README map completeness), the naming ratchet, and where it runs
- [`docs/testing_gain_staging.md`](testing_gain_staging.md) — the `ModuleGainAudit` sweep: only gain controls may add gain; anything above +6 dB is allow-listed with a reason
- [`docs/distribution.md`](distribution.md) — version identity, Sparkle auto-update (macOS), EdDSA key generation, CI appcast publishing, WinSparkle status

## Other

- Feature planning artifacts (timeline concept & task tracker) live in a private repo, kept out of this public repo on purpose.
