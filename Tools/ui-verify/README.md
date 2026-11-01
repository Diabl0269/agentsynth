# ui-verify

Scripts that drive a running Agent Synth through its in-app test mode: open a project, click, drag, press keys, read a
control's state and save a PNG of any component, without touching the real mouse or keyboard and without any macOS
privacy prompt. The protocol and its limits are in [`docs/development/test-mode.md`](../../docs/development/test-mode.md).

## Run an example

```bash
bash scripts/ci-local.sh                          # builds build-ci-local/.../Agent Synth.app (dev builds only)
python3 Tools/ui-verify/examples/select_card.py   # opens fixtures/macros.agsproj, selects a card, writes out/select_card.png
python3 Tools/ui-verify/examples/parallel.py      # two instances at once, each with its own settings directory
```

Both take `--app <path to Agent Synth.app>`, `--out <dir>` and `--visible`. Each run starts its own app with `--automation 0`
(a free port) and a fresh temporary settings directory, so it runs next to your own Agent Synth and next to other runs.

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
`record: true` and a saved project with `save_project: <name>`. The step format and how to add a scenario are in
[`docs/development/test-mode.md`](../../docs/development/test-mode.md#scenarios). `bash scripts/tests/ui-verify-run.test.sh`
tests the runner without an app.

## The fixture

`fixtures/macros.agsproj` holds an open macro, a collapsed macro and a nested macro, each with LFO and ADSR cards, and a
free oscillator. It is written by a disabled test through the app's real save path:

```bash
build-ci-local/Tests/Tests --gtest_also_run_disabled_tests --gtest_filter=UiVerifyFixture.DISABLED_WritesMacrosProject
```

`fixtures/channel-strip-macro.agsproj` holds an Oscillator track's open channel macro (its Channel Strip cabled to the
channel's output port) and a folded macro with no ports; `UiVerifyFixture.DISABLED_WritesChannelStripMacroProject`
regenerates it and `scenarios/channel-strip-cable-on-folded-macro.yaml` drives it.

`UiVerifyFixture.TheCommittedMacrosProjectHoldsAnOpenACollapsedAndANestedMacro` fails if the committed copy stops
loading or loses a macro.
