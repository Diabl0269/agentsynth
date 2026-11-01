# Test mode

A dev build started with `--automation <port>` listens on `127.0.0.1` and lets a script open a project, click, drag, press
keys, read a control's state and save a PNG of any component. Nothing moves the real mouse or touches the real keyboard,
and no macOS privacy prompt appears (no microphone, no Accessibility, no Screen Recording). It runs next to another
Agent Synth, including your own.

Scripts for it live in [`Tools/ui-verify/`](../../Tools/ui-verify/README.md).

## Flags

| Flag | Effect |
|---|---|
| `--automation <port>` | Starts the server. Port `0` picks a free port, which is what the client always uses. Implies `--no-audio-device` and `--no-keychain`; the client passes both anyway (and sets their environment variables), and refuses a binary built without test mode, since such a build ignores `--automation`, starts normally and asks for the keychain. Use the build of your own worktree. |
| `--settings-dir <absolute dir>` | Every setting and user-data folder of this process (settings file, themes, snippets, history, recordings, the default projects folder, the stretched-loop cache) lives under that directory; installed sound packs stay shared and are only read. Under `--automation` without it, a fresh temporary directory is used, so a running real instance's files are never shared. |
| `--automation-info <file>` | Also writes `{"port":N,"pid":P}` to the file once the server listens. |
| `--automation-visible` | Keeps the windows on screen and focusable, for a person who wants to watch (see [Hidden windows](#hidden-windows)). Without it an automation launch is hidden. |
| `--automation-motion` | Keeps animations on. Under `--automation` they are otherwise switched off, so a click lands in its final state. |

On start the app prints exactly one line to stdout: `AGENTSYNTH_AUTOMATION_PORT <port> <pid>`.

An automation launch also stays off the network and off shared machine state: no update check, no usage statistics, no
eager plugin scan, no MIDI device opened.

Packaged builds configure `-DAGENTSYNTH_AUTOMATION=OFF` (`.github/workflows/build-artifacts.yml`); there `--automation` only
logs that it is unavailable. The server code is compiled everywhere so CI tests and coverage reach it; only the activation
in `Source/Main.cpp` sits behind the option.

## Protocol

One JSON object per line in each direction, one client at a time.

```
-> {"id":1,"cmd":"click","args":{"id":"card:LFO 1","x":40,"y":12},"timeoutMs":5000}
<- {"id":1,"ok":true,"result":{"id":"card:LFO 1","point":{"x":40,"y":12}}}
<- {"id":2,"ok":false,"error":"no component \"nope\""}
```

A malformed line, an unknown command (the reply lists the known ones) or a failing command all answer `ok:false`; the
connection stays open. A command that does not finish within `timeoutMs` (default 30 s) answers a timeout error.

The socket thread does I/O only. Every command runs on the message thread, one short step at a time: a command that has
to wait (`drag`, `openProject`, `waitForIdle`) answers "pending" and is called again a moment later, so the message
thread is never blocked and timers and animations run in between.

## Commands

| Command | Arguments | Result |
|---|---|---|
| `ping` | | `{pid, version}` |
| `list` | `root?`, `depth` (6), `all` (false) | tree of `{id,name,title,type,bounds,visible,showing,children}`; bounds are relative to the main window; without `root`, every showing top-level |
| `click` | `id`, `x?`, `y?` (local, default centre), `button` (`left`/`right`), `clicks` (1/2), `mods` (one of `cmd`,`shift`,`alt`,`ctrl` as a string, or a list of them; an unknown name is an error naming it) | `{id, point}` |
| `wheel` | `id`, `x?`, `y?` (local, default centre), `dx?`, `dy?` (JUCE wheel units; 0.25 is about one notch), `mods` | `{id, point}` |
| `hover` | `id`, `x?`, `y?` (local, default centre) | `{id, point}`; moves the pointer there with no button down |
| `mouseDown`, `mouseMoveHeld`, `mouseUp` | `id`, `x?`, `y?` (local, default centre), `mods` | `{id, point}`; one step of a drag each, so other commands can read the app while the button is held. A scenario that presses also releases: nothing lets go for it |
| `drag` | `from`, `to`, `fromX?`, `fromY?`, `toX?`, `toY?`, `steps` (12), `mods` | `{from, to}` |
| `key` | `keys` (`cmd+z`, `space`, `shift+tab`, `escape`, `up` ...), `target?` | `{handled}` |
| `getState` | `id` | the `list` fields plus `enabled`, `hasFocus`, and per type: Button `text`/`toggleState`, Label `text`, Slider `value`/`min`/`max`, ComboBox `text`/`selectedId`, TextEditor `text`; a module card adds `selected`, `moduleType`, `title`, `nodeId` |
| `screenshot` | `id` or `window`, `path` (absolute), `scale` (1) | `{path, width, height}`; parent folders are created |
| `openProject` | `path` (a `.agsproj` bundle) | `{name}` once it has loaded; refused while the open project has unsaved changes or the bundle has an autosave sidecar, because opening would ask first |
| `saveProject` | `path` (ends in `.agsproj`) | `{path}` once written; what the Save dialog runs, without the dialog |
| `waitForIdle` | `timeoutMs` (5000), `settleMs` (100) | `{waitedMs}` once no component animation has run for `settleMs` |
| `quit` | | quits at once, without the unsaved-changes prompt |

## Ids

`id` matches a component's `componentID`, then its name, then its accessibility title, searching every top-level
component depth first and preferring one that is showing. `window` is the main window. `parent > child` searches inside the
first match of `parent`.

Module cards carry `card:<title>` (`card:LFO 1`, `card:Oscillator 2`): the auto-numbered title makes two cards of one
type distinct, and a renamed card takes its new title. Add a `setComponentID` only where a component has no stable name;
do not rename existing ones.

A component with state worth reading implements `synth::testmode::StateSource` (`addTestModeState`), as `ModuleComponent`
(`selected`, `moduleType`, `nodeId`, `modHoverChip`: whether the chip under a hovered knob is drawn, `macro`: the name of the macro the module sits in, empty when none, `macroPath`: every macro around it, outermost first, as `Outer macro > Nested macro`, `cabledTargets`: the modulation targets with a cable on them, `audioFrom`: the titles of the cards feeding its audio inputs, sorted and comma-separated) and
`MacroCardComponent` (id `macroCard`: `name`, `collapsed`, `members`) and `KeyPicker` (id `keyPicker`: `auditioning`, `auditionKey`, `previewSoundShown`, `previewSound`, `fitProjectShown`, `fitProjectEnabled`) and a hidden child of the timeline's clip lane (id `timelineClipPitches`: `clipPitches`, every clip's note pitches as `Clip 1:60,64;Clip 2:61`; `clipAudio`, every audio clip as `name:assetRef,sourceStartSeconds,lengthBeats,stretchOrigin`) do. `getState` also reports a component's `tooltip`
when it has one, so a scenario can check the shortcut a button's tooltip names.

## Input

Mouse and key events go through the target's `ComponentPeer` (`handleMouseEvent`, `handleKeyPress`), the entry the OS
itself uses, never into a component's `mouseDown` directly and never through the OS event queue. A drag is one message
thread hop per move, so JUCE's drag threshold, auto-scroll and async repaints behave as they do under a hand.

Stock JUCE on macOS and Windows delivers a mouse event only to a window the OS says is topmost at that point, so a
window behind another app's (or behind a second test-mode instance at the same spot) would get nothing. The app carries
a small JUCE patch for this (`juce-testmode-window-order.patch`, see [`juce-patches.md`](juce-patches.md)): the flag
`juce::ComponentPeer::ignoreWindowOrderForHitTests`, which an automation launch sets at startup and nothing else sets.
With it on, test windows are never raised or moved, and a click only has to land inside the window (else the reply is
"the point is outside the window"). The patch covers the macOS and Windows peers: on Linux a covered window still
misses the click, so that test skips Linux. Keyboard modifiers of a mouse event are set through `ModifierKeys::currentModifiers`
for the duration of the event, because JUCE ignores the flags of the event itself.

## Hidden windows

An automation launch does not disturb whoever is using the machine: no window appears, no Dock icon, and the app never
takes keyboard focus. Unless `--automation-visible` is passed, `Source/Main.cpp` calls `hideAutomationWindows()`
(`Source/TestMode/AutomationHiding.*`) before the first window exists. On macOS that sets the JUCE flag
`juce::ComponentPeer::hideWindowsForAutomation` (second flag of the patch above), so each window is alpha 0, ignores
the real mouse and has no shadow, `setAlpha` cannot undo it, and nothing activates the app; it also switches the
process to the accessory activation policy and holds an `NSProcessInfo` activity for the process lifetime, so App Nap
cannot slow a process with no visible window. The windows are still ordered in and rendered, which is why screenshots
(they render the component) and display-linked animation keep working. Test-mode input goes to the `ComponentPeer`, not
through the OS event queue, so it needs neither visibility nor focus. Because the app is never active, the OS never
reports a window as key and `Process::isForegroundProcess()` is false; the patch therefore tracks focus inside JUCE
(`toFront(true)` gives the window JUCE focus) and counts a hidden process as foreground for pop-up menus, call-out
boxes, tooltips, key commands and the active top-level window. App code that closes something when the app is not in
front uses `synth::testmode::appIsInFront()` instead of `juce::Process::isForegroundProcess()`. Windows and Linux do
not hide anything (the flag is only set on macOS) and build and run as before.

The `Tests` binary does the same right after JUCE starts (`Tests/TestMain.cpp`); `AGENTSYNTH_TESTS_VISIBLE=1` skips it.
`Tools/ui-verify` is hidden by default and takes `--visible`.

## Running in parallel

Any number of instances can run at once (several worktrees, several agents, plus your own app). Per instance:

- its own port (`--automation 0`, never a fixed one) and its own settings directory (temporary unless `--settings-dir`);
- no shared files: settings, themes, snippets, history, recordings, the default projects folder and the
  stretched-loop cache all follow that directory (installed sound packs stay shared and are only read);
- no audio or MIDI device, no keychain, no update check, no telemetry, no plugin scan.

Test-mode runs take no test slot (`scripts/lib/test-shards.sh`, which limits the GoogleTest suite's processes). Give each run its own output directory for screenshots (`select_card.py --out`, and
`parallel.py` does this per instance). One thing still shared: the out-of-process plugin scan's temporary folder, which
test mode never starts.

Try it: `python3 Tools/ui-verify/examples/parallel.py`.

## Limits

No image diffing (take a PNG and look at it). One client at a time per instance. `waitForIdle` sees component animations
(`ComponentAnimator`) and pending repaints, not a timer-driven effect inside a component; pass `--automation-motion` only
when the motion itself is under test. The fixture `Tools/ui-verify/fixtures/macros.agsproj` is regenerated by a disabled
test, see the ui-verify README.

Edge scroll (a drag held at the edge of the canvas) runs on its own 30 Hz timer, so it needs no `--automation-motion`;
`Tools/ui-verify/examples/edge_scroll_drags.py` holds each canvas drag at the edge and checks the scroll.

## Scenarios

A scenario is a YAML file under `Tools/ui-verify/scenarios/` that replays what a user does and checks what the app then
shows. Run one file or the whole directory (needs python3 with PyYAML and a built app):

```bash
python3 Tools/ui-verify/run.py Tools/ui-verify/scenarios [--app <Agent Synth.app>] [--out <dir>]
```

Each scenario starts its own instance (port 0, temporary settings directory), runs its steps, writes screenshots and a
JSON-lines `run.log` under `<out>` (one folder per scenario when a directory runs), prints one PASS/FAIL line per scenario and a summary, and the exit status
is non-zero when any step failed. A failed `expect` names the field that differed.

A file is a list of steps, or a mapping with `steps:` and optional `settings:` (values written to the instance's settings
file before it first starts, for an install that has to look old), and `motion: true` to start with `--automation-motion`, for a check that needs a slide or fade to run). A step is one mapping, `- <command>: {args}`: any
protocol command above (`click`, `drag`, `key`, `getState`, `openProject`, ...) with its arguments, plus:

| Step | Meaning |
| --- | --- |
| `type: {text}` | one `key` per character |
| `wait: {}` | `waitForIdle` |
| `restart: {}` | quit and start again on the same settings directory |
| `copyFixture: {name}` | copies `${FIXTURES}/<name>` into `${SCRATCH}/<name>`, so a scenario can change a fixture without touching the committed copy |
| `screenshot: {id?, name}` | saves `<name>.png` in the output folder (default id `window`) |
| `expect: {id, field: value, ...}` | equality on `getState` fields |
| `expect: {id, contains: {field: text}}` | case-insensitive substring |
| `expect: {id, exists: false}` | the component is not there |
| `expect: {count: {prefix, field?, equals}}` | how many ids (or `title`s) in `list` start with the prefix |

`${FIXTURES}`, `${OUT}` and `${SCRATCH}` (the run's temporary settings directory) are replaced in every string. An `expect` is retried for `within_ms` (default 500), so a repaint
that is still settling is not a failure and one that never arrives still is.

To add one: copy the closest file, start the file with a comment naming the bug and the steps a user takes, and give each
check an id from `list` (add a `setComponentID` or a `StateSource` field where the component has none, never rename an
existing id). A UI change that moves or renames something a scenario uses updates that scenario in the same change, and a
regression that came back gets a scenario that fails with the fix taken out. The runner's own logic is covered by
`scripts/tests/ui-verify-run.test.sh`, which needs no app.

## Tests

`Tests/TestMode/`: protocol parsing, launch flags and policies, id lookup, synthetic input through a real peer, the
server over a real socket (including shutdown with a client connected), every command, a module card's state, and the
committed fixture.
