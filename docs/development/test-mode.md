# Test mode

A dev build started with `--automation <port>` listens on `127.0.0.1` and lets a script open a project, click, drag, press
keys, read a control's state and save a PNG of any component. Nothing moves the real mouse or touches the real keyboard,
and no macOS privacy prompt appears (no microphone, no Accessibility, no Screen Recording). It runs next to another
Agent Synth, including your own.

Scripts for it live in [`Tools/ui-verify/`](../../Tools/ui-verify/README.md).

## Flags

| Flag | Effect |
|---|---|
| `--automation <port>` | Starts the server. Port `0` picks a free port, which is what the client always uses. Implies `--no-audio-device` and `--no-keychain`. |
| `--settings-dir <absolute dir>` | Every setting and user-data folder of this process (settings file, themes, snippets, history, recordings, the default projects folder, the stretched-loop cache) lives under that directory; installed sound packs stay shared and are only read. Under `--automation` without it, a fresh temporary directory is used, so a running real instance's files are never shared. |
| `--automation-info <file>` | Also writes `{"port":N,"pid":P}` to the file once the server listens. |
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
| `click` | `id`, `x?`, `y?` (local, default centre), `button` (`left`/`right`), `clicks` (1/2), `mods` (`cmd`,`shift`,`alt`,`ctrl`) | `{id, point}` |
| `drag` | `from`, `to`, `fromX?`, `fromY?`, `toX?`, `toY?`, `steps` (12), `mods` | `{from, to}` |
| `key` | `keys` (`cmd+z`, `space`, `shift+tab`, `escape`, `up` ...), `target?` | `{handled}` |
| `getState` | `id` | the `list` fields plus `enabled`, `hasFocus`, and per type: Button `text`/`toggleState`, Label `text`, Slider `value`/`min`/`max`, ComboBox `text`/`selectedId`, TextEditor `text`; a module card adds `selected`, `moduleType`, `title`, `nodeId` |
| `screenshot` | `id` or `window`, `path` (absolute), `scale` (1) | `{path, width, height}`; parent folders are created |
| `openProject` | `path` (a `.agsproj` bundle) | `{name}` once it has loaded; refused while the open project has unsaved changes or the bundle has an autosave sidecar, because opening would ask first |
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
does.

## Input

Mouse and key events go through the target's `ComponentPeer` (`handleMouseEvent`, `handleKeyPress`), the entry the OS
itself uses, never into a component's `mouseDown` directly and never through the OS event queue. A drag is one message
thread hop per move, so JUCE's drag threshold, auto-scroll and async repaints behave as they do under a hand.

Stock JUCE on macOS delivers a mouse event only to a window the window server says is topmost at that point, so a
window behind another app's (or behind a second test-mode instance at the same spot) would get nothing. The app carries
a small JUCE patch for this (`juce-testmode-window-order.patch`, see [`juce-patches.md`](juce-patches.md)): the flag
`juce::ComponentPeer::ignoreWindowOrderForHitTests`, which an automation launch sets at startup and nothing else sets.
With it on, test windows are never raised or moved, and a click only has to land inside the window (else the reply is
"the point is outside the window"). Keyboard modifiers of a mouse event are set through `ModifierKeys::currentModifiers`
for the duration of the event, because JUCE ignores the flags of the event itself.

## Running in parallel

Any number of instances can run at once (several worktrees, several agents, plus your own app). Per instance:

- its own port (`--automation 0`, never a fixed one) and its own settings directory (temporary unless `--settings-dir`);
- no shared files: settings, themes, snippets, history, recordings, the default projects folder and the
  stretched-loop cache all follow that directory (installed sound packs stay shared and are only read);
- no audio or MIDI device, no keychain, no update check, no telemetry, no plugin scan.

The repository test lock (`scripts/lib/test-lock.sh`) guards the shared settings file of the GoogleTest suite and is not
needed for test-mode runs. Give each run its own output directory for screenshots (`select_card.py --out`, and
`parallel.py` does this per instance). One thing still shared: the out-of-process plugin scan's temporary folder, which
test mode never starts.

Try it: `python3 Tools/ui-verify/examples/parallel.py`.

## Limits

No image diffing (take a PNG and look at it). One client at a time per instance. `waitForIdle` sees component animations
(`ComponentAnimator`) and pending repaints, not a timer-driven effect inside a component; pass `--automation-motion` only
when the motion itself is under test. The fixture `Tools/ui-verify/fixtures/macros.agsproj` is regenerated by a disabled
test, see the ui-verify README.

## Tests

`Tests/TestMode/`: protocol parsing, launch flags and policies, id lookup, synthetic input through a real peer, the
server over a real socket (including shutdown with a client connected), every command, a module card's state, and the
committed fixture.
