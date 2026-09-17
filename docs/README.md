# Docs map

One topic per doc, split at section boundaries. Every doc below is the mechanism and history behind a rule; the rules themselves live in the per-directory `CLAUDE.md` files and the tripwire index in the root [`CLAUDE.md`](../CLAUDE.md). When you change behaviour, update the doc as part of the change, and keep this map current (a new doc gets a line here).

## Architecture & engine

- [`docs/architecture.md`](docs/architecture.md) — layers, core classes (ModuleBase, AudioEngine, TransportService, TimelineDoc, GraphEditor, UndoManager, LookAndFeel), bypass/mute contract, signal flow, plugin layer (VST3/AU host modes, ownership, state format)
- [`docs/modules.md`](docs/modules.md) — per-module specs + poly channel layouts (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math …)
- [`docs/fx_modules.md`](docs/fx_modules.md) — FX specs (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Pitch Shifter, Parametric EQ, Ring Modulator)
- [`docs/modulation.md`](docs/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/Module_Development_Guide.md`](docs/Module_Development_Guide.md) — step-by-step guide to adding a module

## Layout, canvas & theming

- [`docs/layout.md`](docs/layout.md) — grid/snap/auto-arrange, toolbar & status-bar chrome, width buckets, LayoutUtil API, drag affordance + smart connections
- [`docs/layout_visuals_animation.md`](docs/layout_visuals_animation.md) — visualizer components, UI rendering performance, animation system (UIAnimation.h, AnimationDriver, PanelSlide, micro-interactions), alignment guides
- [`docs/layout_selection_canvas.md`](docs/layout_selection_canvas.md) — multi-select + group drag + snippets/clipboard (§1.5), collapsible library sections, cable interaction, minimap overlay
- [`docs/theming.md`](docs/theming.md) — theme tokens, SVG icons, JSON user themes, LookAndFeel, font limitation

## Macros & mixer

- [`docs/macros.md`](docs/macros.md) — Macros: the P8-12 presentation-only container and the DECIDED Macro I/O model (P8-14) overview, and why a container node with an inner graph was rejected
- [`docs/macros_ports.md`](docs/macros_ports.md) — Macro I/O port mechanics: node types, port set/ordering, poly/stereo shape, cable rendering across the boundary, bypass/mute, the macro menu
- [`docs/macros_implementation.md`](docs/macros_implementation.md) — Macro I/O AI-authorability decision, the P8-15 implementation tracker, and out-of-scope items
- [`docs/mixer.md`](docs/mixer.md) — Mixer channels (P9-1, decided 2026-09-10): the ChannelStrip node is the channel, macros are the container, solo as a render-time gate, Master/Direct, the track/channel link, track presets
- [`docs/mixer_fader.md`](docs/mixer_fader.md) — The mixer fader's own Cubase-like taper (FRO150): UI-only position mapping vs. the linear-dB parameter, Shift fine-drag, Cmd-click/double-click reset
- [`docs/mixer_implementation.md`](docs/mixer_implementation.md) — Mixer build log: dependency order and the test list for each P9 item (engine/solo gate, channel creation flows, track/channel link, mixer panel, detachable windows, track presets, stem export, and the remaining side tracks)
- [`docs/testing_mixer_meters.md`](docs/testing_mixer_meters.md) — mixer meter & fader test list (FRO146/FRO150/FRO147): peak latch, dB scale zones, clip readout, taper

## Timeline

- [`docs/timeline_panel_core.md`](docs/timeline_panel_core.md) — timeline panel overview, ruler/grid/zoom/snap + markers
- [`docs/timeline_panel_tracks.md`](docs/timeline_panel_tracks.md) — track headers/binding chips, Add-Track
- [`docs/timeline_panel_transport.md`](docs/timeline_panel_transport.md) — playhead, transport bar, metronome, edit-tool strip
- [`docs/timeline_panel_clips_automation.md`](docs/timeline_panel_clips_automation.md) — clip lanes, automation strip, keyboard & focus arbitration
- [`docs/timeline_panel_piano_roll.md`](docs/timeline_panel_piano_roll.md) — the piano roll note editor

## MIDI, shortcuts & remote control

- [`docs/midi_input.md`](docs/midi_input.md) · [`docs/shortcuts.md`](docs/shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/midi_remote.md`](docs/midi_remote.md) · [`docs/midi_remote_ui.md`](docs/midi_remote_ui.md) — MIDI Remote (decided 2026-09-17, FRO121): external controller profiles, drawn surfaces, right-click MIDI Learn on every control, message-thread apply with gestures, scope-by-target-type persistence; the panel, coverage table and tracker
- [`docs/plugin_card_layout.md`](docs/plugin_card_layout.md) — hosted plugin cards showing a chosen set of parameters as knobs (decided 2026-09-17, FRO122): `CardLayout`, instance/type/automatic precedence, `HostedParameterAttachment`, the knob picker with presets; and what carries over to editing any module's layout (FRO123)

## AI

- [`docs/AI_Engine.md`](docs/AI_Engine.md) · [`docs/AI_Usage_Guide.md`](docs/AI_Usage_Guide.md) — AI patching subsystem: architecture, communication pattern, patch-diff/feedback UI
- [`docs/AI_Engine_patch_safety.md`](docs/AI_Engine_patch_safety.md) — patch validity/few-shot/untrusted-timeline data/arrangement context/timeline operations/agentic security model
- [`docs/AI_Engine_chat_component.md`](docs/AI_Engine_chat_component.md) — AIChatComponent and its logging rules
- [`docs/AI_Engine_providers_accounts.md`](docs/AI_Engine_providers_accounts.md) — conversation history, provider registry (OllamaProvider/RemoteProvider), account sign-in, device id/trial, quota UI
- [`docs/agents.md`](docs/agents.md) — short overview of the AI providers (local Ollama, remote) and how the assistant is wired in

## Testing, CI & distribution

- [`docs/testing.md`](docs/testing.md) — test layers, build/test commands, CI pipeline, git hooks, coverage
- [`docs/testing_gain_staging.md`](docs/testing_gain_staging.md) — the `ModuleGainAudit` sweep: only gain controls may add gain; anything above +6 dB is allow-listed with a reason
- [`docs/distribution.md`](docs/distribution.md) — version identity, Sparkle auto-update (macOS), EdDSA key generation, CI appcast publishing, WinSparkle status

## Other

- Feature planning artifacts (timeline concept & task tracker) live in a private repo, kept out of this public repo on purpose.
