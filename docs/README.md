# Docs map

One topic per doc, split at section boundaries. Every doc below is the mechanism and history behind a rule; the rules themselves live in the per-directory `CLAUDE.md` files, mapped by area in the root [`CLAUDE.md`](../CLAUDE.md). When you change behaviour, update the doc as part of the change, and keep this map current (a new doc gets a line here).

## Architecture & engine

- [`docs/architecture/architecture.md`](architecture/architecture.md) — hub: project structure, signal flow, quality standards, and the index into the topic docs below
- [`docs/architecture/audio-engine.md`](architecture/audio-engine.md) — AudioEngine, TransportService (bounce/export, stem export, metronome, input monitoring, mixer solo gate)
- [`docs/architecture/timeline.md`](architecture/timeline.md) — TimelineDoc, TimelineSnapshot, AutomationKernel/Applier/Recorder, UI reflection
- [`docs/architecture/project-bundle.md`](architecture/project-bundle.md) — ProjectBundle (.agsproj): open/save, recent projects, dirty state, autosave, welcome screen
- [`docs/architecture/module-base.md`](architecture/module-base.md) — ModuleBase (logical-port API, bypass/mute contract, output level stage) + supporting components (LayoutUtil, ModuleComponent, AppUndoManager, LookAndFeel)
- [`docs/architecture/graph-editor.md`](architecture/graph-editor.md) — GraphEditor: per-concern translation units and the three collaborator classes
- [`docs/architecture/app-wiring.md`](architecture/app-wiring.md) — who owns the live TimelineDoc and every hook that keeps it in step; audio recording, latency alignment, AudioClipStreamer, asset management
- [`docs/architecture/plugin-layer.md`](architecture/plugin-layer.md) — VST3/AU host modes, ownership, state format; hosting third-party plugins inside our own graph
## Modules

- [`docs/modules/modules.md`](modules/modules.md) — hub: per-module specs (Oscillator, Filter, VCA, ADSR, LFO, Sequencer, Poly MIDI, Voice Mixer, Math, Hosted Plugin …)
- [`docs/modules/fx-modules.md`](modules/fx-modules.md) — FX specs and the shared Dual I/O / Output Level stages (Distortion, Delay, Reverb, Chorus, Phaser, Compressor, Flanger, Limiter, Gate, Bitcrusher, Parametric EQ, Pitch Shifter, Ring Modulator)
- [`docs/modules/modulation.md`](modules/modulation.md) — routing model, logical-port API, poly-bus wires, attenuverters, visual signal flow
- [`docs/modules/poly-channel-layout.md`](modules/poly-channel-layout.md) — the raw channel table for every poly-capable module
- [`docs/modules/wavetable.md`](modules/wavetable.md) — the Wavetable oscillator: tables, warp, mip pyramid, interpolation, file import
- [`docs/modules/development-guide.md`](modules/development-guide.md) — step-by-step guide to adding a module

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

- [`docs/macros/macros.md`](macros/macros.md) — hub: what a macro is, the macro model, ports as proxy nodes on a flat graph, AI authorability, deliberate limits
- [`docs/macros/ports.md`](macros/ports.md) — Macro I/O port mechanics: node types, port set/ordering, poly/stereo shape, how a port is drawn, cable rendering across the boundary, bypass/mute
- [`docs/macros/auto-ports.md`](macros/auto-ports.md) — the auto-port preference, auto-creating ports when grouping, a modulation cable through an attenuverter, ungroup and direct deletion of a port, ports on a cable drag
- [`docs/macros/configure-io.md`](macros/configure-io.md) — the Configure I/O dialog: adding/renaming/reordering/deleting a port, changing its shape, per-port colour, keyboard handling
- [`docs/macros/menu-and-membership.md`](macros/menu-and-membership.md) — the macro menu's entry points, adding to/removing from a macro, incremental port splicing, Cmd-drag across a hull border (join, leave, transfer; the drag-without-Cmd preference), dropping a library module into a macro, the collapse button
- [`docs/mixer/mixer.md`](mixer/mixer.md) — hub: what a channel is, node types, channels follow audio not tracks, solo as a render-time gate, mono/stereo, bypass/mute, inserts, building a channel, AI authorability
- [`docs/mixer/fader.md`](mixer/fader.md) — the mixer fader's own Cubase-like taper (FRO150): UI-only position mapping vs. the linear-dB parameter, Shift fine-drag, Cmd-click/double-click reset
- [`docs/mixer/meters.md`](mixer/meters.md) — the per-reader peak latch, dB scale and taper, ballistics, colour zones, user-editable meter colours, the clip readout, the track header chip
- [`docs/mixer/panel.md`](mixer/panel.md) — what the mixer panel shows, placement and detachable windows, unbinding before a graph change, the EQ curve thumbnail, keyboard navigation and accessibility
- [`docs/mixer/sends-and-buses.md`](mixer/sends-and-buses.md) — a bus is a Channel Strip, a send is an output leg, channel layout, parameter vs. state, pre/post and mute/bypass, the per-leg audible-solo mask, the send/bus UI, stems
- [`docs/mixer/stem-export.md`](mixer/stem-export.md) — one render pass per strip, stem naming, buses are stems too, what sums back to the mix
- [`docs/mixer/track-presets.md`](mixer/track-presets.md) — what a track preset is, saving and setting a default, creating a track from a preset, what a saved preset carries, loading a preset

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

- [`docs/control/midi-input.md`](control/midi-input.md) · [`docs/control/shortcuts.md`](control/shortcuts.md) — external MIDI routing, keyboard shortcuts
- [`docs/control/midi-remote.md`](control/midi-remote.md) · [`docs/control/midi-remote-ui.md`](control/midi-remote-ui.md) — MIDI Remote: external controller profiles, drawn surfaces, right-click MIDI Learn on every control, message-thread apply with gestures, scope-by-target-type persistence; the panel and coverage table
- [`docs/control/plugin-card-layout.md`](control/plugin-card-layout.md) — hosted plugin cards showing a chosen set of parameters as knobs: `CardLayout`, instance/type/automatic precedence, `HostedParameterAttachment`, the knob picker with presets; and what carries over to editing any module's layout

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
