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

Both take `--app <path to Agent Synth.app>` and `--out <dir>`. Each run starts its own app with `--automation 0`
(a free port) and a fresh temporary settings directory, so it runs next to your own Agent Synth and next to other runs.

## Writing a script

`agentsynth_testmode.TestModeApp` is a context manager: `app.call("click", id="card:LFO 1")` returns the result or raises
`TestModeError`. Ids come from `list`; `card:<title>` finds a module card.

## The fixture

`fixtures/macros.agsproj` holds an open macro, a collapsed macro and a nested macro, each with LFO and ADSR cards, and a
free oscillator. It is written by a disabled test through the app's real save path:

```bash
build-ci-local/Tests/Tests --gtest_also_run_disabled_tests --gtest_filter=UiVerifyFixture.DISABLED_WritesMacrosProject
```

`UiVerifyFixture.TheCommittedMacrosProjectHoldsAnOpenACollapsedAndANestedMacro` fails if the committed copy stops
loading or loses a macro.
