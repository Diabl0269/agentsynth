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

- [`docs/layout/layout.md`](layout/layout.md) — hub: the soft grid, anti-overlap search, auto-arrange, the `LayoutUtil` API, drag affordance and alignment guides
- [`docs/layout/chrome.md`](layout/chrome.md) — toolbar, status bar, minimum window size, panel collapse and persistence, the welcome overlay, the mod-matrix panel
- [`docs/layout/module-card.md`](layout/module-card.md) — a card's own geometry: width buckets, body layout, header buttons, custom titles, the Audio Output identity treatment, the Wavetable card
- [`docs/layout/module-library.md`](layout/module-library.md) — the library sidebar: rows, search, collapsible sections, scrolling, the help popover, the Shortcuts tab that mirrors it
- [`docs/layout/preset-positions.md`](layout/preset-positions.md) — where factory presets place their modules
- [`docs/layout/selection.md`](layout/selection.md) — multi-select, `SelectionModel`, group drag as one rigid body, the drag-flag reset sites
- [`docs/layout/snippets-clipboard.md`](layout/snippets-clipboard.md) — snippets, copy/paste/duplicate, and the validate-strictly/apply-faithfully trust boundary
- [`docs/layout/macro-cards.md`](layout/macro-cards.md) — macro containers on the canvas: collapse, the hull and its chip, the shared menu, undo and persistence
- [`docs/layout/cables.md`](layout/cables.md) — a cable is not a graph edge: enumeration, hit-testing, hover, the menu, and colour resolution
- [`docs/layout/smart-connections.md`](layout/smart-connections.md) — cables suggested while dragging, Ctrl to insert in series, double-click a port to disconnect
- [`docs/layout/minimap.md`](layout/minimap.md) — the graph overview overlay
- [`docs/layout/rendering.md`](layout/rendering.md) — repaint discipline: the zoom-frozen card cache, the one canvas invalidation seam, gated timers
- [`docs/layout/animation.md`](layout/animation.md) — `AnimationDriver`, `PanelSlide`, what moves, and the time-bounded animation rule with its two exceptions
- [`docs/layout/visualizers.md`](layout/visualizers.md) — in-card signal displays and editors (frequency response, EQ curve, scope, threshold meter, wavetable, curve editor)
- [`docs/layout/theming.md`](layout/theming.md) — the token reference (colours, metrics, typography, treatment), the font limitation, reload, themed widgets
- [`docs/layout/theme-authoring.md`](layout/theme-authoring.md) — the `*.gtheme.json` schema, a worked example, and what each treatment parameter does
- [`docs/layout/icons.md`](layout/icons.md) — the SVG icon enum, the token-to-tint map, the null-fallback contract, adding an icon
- [`docs/layout/colour-overrides.md`](layout/colour-overrides.md) — piano-roll note colours, mixer meter colour stops, and the shared colour picker popup

## Macros & mixer

- [`docs/macros.md`](macros.md) — Macros: the P8-12 presentation-only container and the DECIDED Macro I/O model (P8-14) overview, and why a container node with an inner graph was rejected
- [`docs/macros_ports.md`](macros_ports.md) — Macro I/O port mechanics: node types, port set/ordering, poly/stereo shape, cable rendering across the boundary, bypass/mute, the macro menu
- [`docs/macros_implementation.md`](macros_implementation.md) — Macro I/O AI-authorability decision, the P8-15 implementation tracker, and out-of-scope items
- [`docs/mixer.md`](mixer.md) — Mixer channels (P9-1, decided 2026-09-10): the ChannelStrip node is the channel, macros are the container, solo as a render-time gate, Master/Direct, the track/channel link, track presets
- [`docs/mixer_fader.md`](mixer_fader.md) — The mixer fader's own Cubase-like taper (FRO150): UI-only position mapping vs. the linear-dB parameter, Shift fine-drag, Cmd-click/double-click reset
- [`docs/mixer_implementation.md`](mixer_implementation.md) — Mixer build log: dependency order and the test list for each P9 item (engine/solo gate, channel creation flows, track/channel link, mixer panel, detachable windows, track presets, stem export, and the remaining side tracks)
- [`docs/testing_mixer_meters.md`](testing_mixer_meters.md) — mixer meter & fader test list (FRO146/FRO150/FRO147): peak latch, dB scale zones, clip readout, taper

## Timeline

- [`docs/timeline/timeline.md`](timeline/timeline.md) — hub: the panel shell, its regions, the bottom dock, panel height and the show/hide slide
- [`docs/timeline/view.md`](timeline/view.md) — `TimelineViewState`: beat↔pixel mapping, snap divisions, the lanes grid, wheel/pinch/keyboard zoom and scroll
- [`docs/timeline/ruler.md`](timeline/ruler.md) — the ruler strip: tick density, the two gesture zones, the loop brace, markers
- [`docs/timeline/tracks.md`](timeline/tracks.md) — track header rows: binding and channel chips, M/S/R, focus, drag-to-reorder, the row context menu
- [`docs/timeline/add-track.md`](timeline/add-track.md) — the `"+ Track"` menu and every flow it starts (MIDI, Audio, Instrument, plugins, presets, markers)
- [`docs/timeline/playhead.md`](timeline/playhead.md) — the playhead overlay, its two timers, latency compensation, follow-playhead
- [`docs/timeline/transport.md`](timeline/transport.md) — the transport bar, the recording gate, transport actions, metronome and count-in
- [`docs/timeline/edit-tools.md`](timeline/edit-tools.md) — the Cubase-style tool strip shared by the clip lanes and the piano roll
- [`docs/timeline/clips.md`](timeline/clips.md) — clip lanes: selection, drag/trim, edge auto-scroll, authoring and audio import
- [`docs/timeline/automation.md`](timeline/automation.md) — the automation strip and its curve canvas
- [`docs/timeline/piano-roll.md`](timeline/piano-roll.md) — the per-clip MIDI note editor
- [`docs/timeline/scale-assist.md`](timeline/scale-assist.md) — the Scale Assist panel, the scale engine, pitch-row collapse, random generation
- [`docs/timeline/focus.md`](timeline/focus.md) — which surface Cmd+C/V/D/X/R and Cmd+Shift+A act on

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

## Development: testing, CI & distribution

- [`docs/development/testing.md`](development/testing.md) — running the suite, the build flags, adding tests for a new module, snapshot references
- [`docs/development/test-layers.md`](development/test-layers.md) — what each suite in the test binary covers, grouped by layer
- [`docs/development/test-patterns.md`](development/test-patterns.md) — the conventions every test follows: the real mouse path, headless seams, settings isolation, sanitizers
- [`docs/development/gain-staging.md`](development/gain-staging.md) — the `ModuleGainAudit` sweep: only gain controls may add gain; anything above +6 dB is allow-listed with a reason
- [`docs/development/ai-harnesses.md`](development/ai-harnesses.md) — `AIPatchHarness` and `AIEvalHarness`: measurement, not tests, and the corpus the offline replay suite is built from
- [`docs/development/local-cloud-dev.md`](development/local-cloud-dev.md) — exercising Pro-gated flows against a locally-run backend
- [`docs/development/local-ci.md`](development/local-ci.md) — `scripts/ci-local.sh`, the git hooks, the clang-format pin, worktree dependency reuse, running suites in parallel, dev-signing
- [`docs/development/ci-pipeline.md`](development/ci-pipeline.md) — `ci.yml`'s triggers and jobs, the six required status checks, docs-only PR coverage, the apt-mirror failover
- [`docs/development/ci-caching.md`](development/ci-caching.md) — the ccache and FetchContent caches, the six rules that keep them working, and the health check that gates them
- [`docs/development/ascii-literal-guard.md`](development/ascii-literal-guard.md) — no non-ASCII bytes in a `Source/` string literal, and the JUCE decoding contract behind it
- [`docs/development/file-size-guard.md`](development/file-size-guard.md) — the 1000-line cap, its strict ratchet baseline, and how to split an over-cap file
- [`docs/development/function-size-guard.md`](development/function-size-guard.md) — the 200-line-per-function cap, how a function's size is measured, and its ratchet baseline
- [`docs/development/header-comment-guard.md`](development/header-comment-guard.md) — comment *placement* in headers: why it tracks excess (comments minus code) rather than a raw count, the exact threshold, and the ratchet baseline
- [`docs/development/docs-guard.md`](development/docs-guard.md) — `scripts/check-docs.sh`'s seven checks (naming, links, `docs/...` mentions, `§`-section refs, README map completeness, bare `#anchor` mentions, bare basenames), the naming ratchet, and where it runs
- [`docs/development/pr-title-convention.md`](development/pr-title-convention.md) — PR title format (`type(scope)!: subject`) and why it is checked on the title, not a commit message
- [`docs/development/distribution.md`](development/distribution.md) — version identity, the build-time "What's New" data, and the signing state of a shipped build
- [`docs/development/auto-update.md`](development/auto-update.md) — Sparkle (macOS) and WinSparkle (Windows), EdDSA key generation, and the manual verification recipes
- [`docs/development/releases.md`](development/releases.md) — the post-merge artifact build, appcast publishing, release asset upload reliability, and promoting a build to stable

## Other

- Feature planning artifacts (timeline concept & task tracker) live in a private repo, kept out of this public repo on purpose.
