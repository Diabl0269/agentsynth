# ui-verify

Scripts that drive a running Agent Synth through its in-app test mode: open a project, click, drag, press keys, read a
control's state and save a PNG of any component, without touching the real mouse or keyboard and without any macOS
privacy prompt. The protocol and its limits are in [`docs/development/test-mode.md`](../../docs/development/test-mode.md).

## Run an example

```bash
bash scripts/ci-local.sh                          # builds build-ci-local/.../Agent Synth.app (dev builds only)
python3 Tools/ui-verify/examples/select_card.py   # opens fixtures/macros.agsproj, selects a card, writes out/select_card.png
python3 Tools/ui-verify/examples/parallel.py      # two instances at once, each with its own settings directory
python3 Tools/ui-verify/examples/edge_scroll_drags.py  # each canvas drag held at the window edge: the canvas scrolls, then stops
```

Both take `--app <path to Agent Synth.app>`, `--out <dir>` and `--visible`. Each run starts its own app with `--automation 0`
(a free port) and a fresh temporary settings directory, so it runs next to your own Agent Synth and next to other runs.

## Idle CPU

`idle_cpu.py` measures the % CPU Activity Monitor shows for an idle app (CPU seconds over a fixed window, repeated).
A test-mode launch is **not** that number: it renders through the silent device and skips the real audio device, the
plugin scan and the update check, and it read about half of what a user-style launch burns. `--real` launches like a
user (real audio device, visible window; only the keychain and the settings folder stay private), and `--table` runs
the empty app and `--project` with Animations On and Off in both styles and marks a gap over 25% as a finding:

`python3 Tools/ui-verify/idle_cpu.py --table --project "~/Music/AgentSynth/Load test.agsproj"`

Any CPU claim quotes that table (the user-style column is the Activity Monitor figure). The table also covers a new
project, the device buffer sizes in `--buffers` (default `0,512,128`, 0 = the device's own default; the buffer the device
really opened at is read back from the app's settings file and shown as `opened (asked N)`) and a **playing** row per
launch: after the idle windows the app presses Play itself (`--play-after <seconds>`, a launch flag for measuring) and
the same windows are measured with the transport running (the Mac's output is muted meanwhile and restored after;
`--audible` keeps it). Every row splits the number per
thread from `ps -M` (the audio device thread at real-time priority, the main thread, all others), and with
`--bench-idle-us` / `--bench-playing-us` (the mean block of the engine bench,
[audio-engine.md](../../docs/architecture/audio-engine.md#measuring-render-cost)) the `--project` rows say how many of
their Activity Monitor points the bench explains. Run it unsandboxed (`ps`, `sample`): a full table takes about 30 minutes and
brings a window to the front once per launch.

Single runs: `--project <bundle> --motion --visible` (test mode, a copy of the project), `--real` (user-style),
`--real --play --buffer 128` (idle, then playing, at that buffer). `--sample <file>` writes a `sample` call tree
([rendering.md](../../docs/layout/rendering.md#an-idle-canvas-with-moving-flow-dots-draws-one-picture)), with `--table`
or `--play` one per launch and state named from it, and the JSON carries each one's busy share per thread and the audio
and main threads' top functions (`cpu_threads.py`); in a real launch the audio device thread is its biggest share.

## Hidden by default

A run never puts a window on your screen, shows a Dock icon or takes keyboard focus, so you can keep working while it
runs: the app is launched with `--automation 0` and, unless you ask otherwise, its windows are invisible and
click-through. Screenshots still work, since they render the component rather than read the screen. Pass `--visible` to
`run.py` and the examples (`TestModeApp(visible=True)`, which adds `--automation-visible` and brings the app to the front)
to watch it. Details: [`docs/development/test-mode.md`](../../docs/development/test-mode.md#hidden-windows).

## Writing a script

`agentsynth_testmode.TestModeApp` is a context manager: `app.call("click", id="card:LFO 1")` returns the result or raises
`TestModeError`. Ids come from `list`; `card:<title>` finds a module card.

## Scenarios

`scenarios/*.yaml` replay regressions that came back (a bug header, the steps a user takes, the checks) and `run.py`
runs them, each in its own instance. Needs python3 with PyYAML:

```bash
python3 Tools/ui-verify/run.py Tools/ui-verify/scenarios            # every scenario; non-zero exit when one fails
python3 Tools/ui-verify/run.py Tools/ui-verify/scenarios/poly-switch-chain.yaml --out out/poly
```

Screenshots and a `run.log` land in the output folder (one per scenario when a directory runs), plus an MP4 with
`record: true` and a saved project with `save_project: <name>`. `mark: {check: N, name: slug}` saves the screen frame (main window, menus, pickers, other windows, pointer) as
`proof/<name>.png` and cuts a short clip around it ([Proof marks](../../docs/development/test-mode.md#proof-marks)); a
recorded video is made of the same frames. `stale_pixel_check: true` fails a step that leaves
pixels on screen that a full repaint would not draw, with the pictures in `stale-step-NN/`
([Stale pixels](../../docs/development/test-mode.md#stale-pixels)). The step format and how to add a scenario are in
[`docs/development/test-mode.md`](../../docs/development/test-mode.md#scenarios). A recorded `drag` ending in another window (a drop onto the shape window) is filmed in screen coordinates, and `feedVisual` fills a module's spectrum or scope buffer with a sine, noise or silence so a display that follows sound can be filmed (the launch renders on a silent audio device; `feedVisual` detaches its callback first) ([feedVisual](../../docs/development/test-mode.md#feedvisual)). `bash scripts/tests/ui-verify-run.test.sh`
tests the runner without an app.

A run holds a scenario to what a hand and a real app do ([Realism rules](../../docs/development/test-mode.md#realism-rules)):
a recorded drag trembles and overshoots (`straight_drags: "<reason>"` turns it off), a step whose input takes over 400 ms
to answer fails (`max_step_ms: {ms, reason}`), `feedVisual` cannot sit with a Mute, Bypass or note check, the audio is
re-attached at the end and a click under another window of the app fails (`click_through_windows: "<reason>"`). A scenario with `save_project` reopens the saved project in a fresh app and repeats its last `expect`s there (`skip_reopen_check: "<reason>"`); a user's own project is copied first with `copyFixture: {name, from}`.

A few checks need a real, visible window (focus, CPU, keys the OS delivers). A scenario with `real_window: true` starts the
app visible with motion on and sends its `key` and `type` steps to the app's pid through System Events. It runs only with
`run.py <dir> --real-window` while nobody is at the machine (no input for `--idle-seconds`, default 300, or
`AGENTSYNTH_USER_AWAY=1`); otherwise it is reported SKIPPED with the reason, and the default run skips it
([Real window](../../docs/development/test-mode.md#real-window)).
Scenarios that broke a rule before it existed are in `known-realism-fails.txt`, which only shrinks.

## The fixture

`fixtures/macros.agsproj` holds an open macro, a collapsed macro and a nested macro, each with LFO and ADSR cards, and a
free oscillator. It is written by a disabled test through the app's real save path:

```bash
build-ci-local/Tests/Tests --gtest_also_run_disabled_tests --gtest_filter=UiVerifyFixture.DISABLED_WritesMacrosProject
```

`fixtures/channel-strip-macro.agsproj` holds an Oscillator track's open channel macro (its Channel Strip cabled to the
channel's output port) and a folded macro with no ports; `UiVerifyFixture.DISABLED_WritesChannelStripMacroProject`
regenerates it and `scenarios/channel-strip-cable-on-folded-macro.yaml` drives it.

`fixtures/lane-rows.agsproj` holds a MIDI track playing a Filter whose Cutoff lane carries two LFO modulator rows;
`UiVerifyLaneRowsFixture.DISABLED_WritesLaneRowsProject` regenerates it and `scenarios/lane-row-resize.yaml` drives it.

`fixtures/fro830-test.agsproj` is a copy of a real saved project (two track macros, three folded macros and the
output dock, with no audio); `scenarios/open-macro-pushes-neighbour-right.yaml` drives it.

`fixtures/fro1153.agsproj` is a copy of another real saved project (an open track macro holding a folded nested macro
and a Reverb, a folded track macro below it, and a loose Reverb between them, with no audio);
`scenarios/drag-card-across-gap-lands-where-dropped.yaml` drives it, and `LayoutUnitFootprint` tests open it.

`fixtures/cross-track-lanes.agsproj` is the shared saved project (three audio tracks in channel macros: folded, nested,
open) with a Compressor lane on each track and an LFO on Audio 1's Ratio lane;
`UiVerifyCrossTrackLanesFixture.DISABLED_WritesCrossTrackLanesProject` regenerates it and
`scenarios/mod-row-cross-track-drag.yaml` and `scenarios/mod-row-key-move-across-lanes.yaml` drive it.

`fixtures/macro-card-drops.agsproj` holds an open track macro (a Channel Strip) beside folded macros, and
`scenarios/macro-card-drops.yaml` drops the strip's Left and Right cables on a folded card and on its input jack;
`fixtures/macro-input-drop.agsproj` is a saved project (a folded macro inside the open track macro Oscillator 1, a Reverb
outside it) and `scenarios/macro-input-drop.yaml` drops the Reverb's cable on the folded macro. The saved project is
also read by `MacroPortFlowCardDrop.ASavedProjectsMacroInputTakesACableAcrossABorderThroughAPortLikeAnOutputDoes`.

`UiVerifyFixture.TheCommittedMacrosProjectHoldsAnOpenACollapsedAndANestedMacro` fails if the committed copy stops
loading or loses a macro.
