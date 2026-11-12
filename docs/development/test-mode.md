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
| `--settings-dir <absolute dir>` | Every setting and user-data folder of this process (settings file, themes, snippets, history, recordings, the default projects folder, the stretched-loop cache) lives under that directory; installed sound packs stay shared and are only read, unless the directory holds a `SoundPacks` folder: then that folder is the only one searched and downloads land in it (a scenario seeds it with `copyFixture: {name: SoundPacks}` and a `restart`). Under `--automation` without it, a fresh temporary directory is used, so a running real instance's files are never shared. |
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
| `mouseDown`, `mouseMoveHeld`, `mouseUp` | `id`, `x?`, `y?` (local, default centre), `mods`, `captured?` | `{id, point}`; one step of a drag each, so other commands can read the app while the button is held. A scenario that presses also releases: nothing lets go for it |
| `drag` | `from`, `to`, `fromX?`, `fromY?`, `toX?`, `toY?`, `steps` (12), `mods` | `{from, to}` |
| `key` | `keys` (`cmd+z`, `space`, `shift+tab`, `escape`, `up` ...), `target?` | `{handled}` |
| `keyDown`, `keyUp` | `keys` (an arrow with its modifiers, `alt+right`), `target?`; `keyUp` without `keys` lets go of all | `{handled}`, `{held}`; presses the arrow and keeps it held: every key hold (`KeyGlideHost`) reads it, with the chord's Shift and Alt, as physically down until `keyUp`, so a scenario can film a held Alt+arrow with `glide`. The hold moves on animation frames, so it needs `motion: true`; `keyUp` also sends a key-up, which ends a hold at once. A scenario that holds also lets go |
| `getState` | `id` | the `list` fields plus `enabled`, `hasFocus`, and per type: Button `text`/`toggleState`, Label `text`, Slider `value`/`min`/`max`, ComboBox `text`/`selectedId`, TextEditor `text`; a module card adds `selected`, `moduleType`, `title`, `nodeId`, `cableFlowPhase` (the canvas's cable flow-dot phase) and `animationMode` (the Animations mode in force) |
| `screenshot` | `id` or `window`, `path` (absolute), `scale` (1) | `{path, width, height}`; parent folders are created. `id: "screen"` is what a user sees (see [Screen frame](#screen-frame)) and also answers `windows` and `pointer` |
| `openProject` | `path` (a `.agsproj` bundle) | `{name}` once it has loaded; refused while the open project has unsaved changes or the bundle has an autosave sidecar, because opening would ask first |
| `saveProject` | `path` (ends in `.agsproj`) | `{path}` once written; what the Save dialog runs, without the dialog |
| `skipPluginForNextLoad` | `name`, `bundlePath?` | `{name}`; what answering "Open without it" at launch does (an automation launch never asks): that plugin is switched off for the next `openProject` only, so its card is a placeholder with a Turn on button |
| `waitForIdle` | `timeoutMs` (5000), `settleMs` (100) | `{waitedMs}` once no component animation has run for `settleMs` |
| `trackPresentedFrame` | `stop?` | `{following, width, height}`; starts (or stops) following what the main window shows, see [Stale pixels](#stale-pixels) |
| `checkStalePixels` | `settleMs` (1500), `tolerance` (6), `ignore` (component ids), `dir?` | `{differing: 0, waitedMs, paints, wholeWindowPaints}` once what the window shows equals a full repaint; fails naming the pixel count and box, and writes `presented.png`, `full-repaint.png`, `differences.png` into `dir` |
| `popWatchStart` | | `{armed, undoSerial?}`; follows the main window (as `trackPresentedFrame` does), keeps a full paint as the "before" picture and copies every frame the window presents for the next 600 ms (at most 40); `undoSerial` is the app's edit serial, see [No-pop check](#no-pop-check) |
| `popWatchCheck` | `ms` (300), `settleMs` (1500), `ignore` (component ids), `tolerance` (6), `dir?` | `{changed, framesSeen, pop, regionX/Y/W/H, regionPixels, intermediateFrames, undoSerialBefore, undoSerialAfter}` (`pop` and the region only when `changed`) once `ms` have passed since the start and no animation is running; fails "not idle" after `settleMs + ms`. A pop writes `before.png`, `first.png`, `final.png`, `differences.png` into `dir` |
| `audioPump` | `run` (true/false), or `note` (0-127) with `down` (true/false) | `{running, blocks}`; renders the open project in real time with no device, see [Audio pump](#audio-pump) |
| `quit` | | quits at once, without the unsaved-changes prompt |
| `requestQuit` | | what the Quit menu item (Cmd+Q) reaches: the app's own quit request, so with unsaved changes the Save / Discard / Cancel question appears first; the native menu and the key itself are not driven |

## Ids

`id` matches a component's `componentID`, then its name, then its accessibility title, searching every top-level
component depth first and preferring one that is showing. `window` is the main window. `parent > child` searches inside the
first match of `parent`.

Module cards carry `card:<title>` (`card:LFO 1`, `card:Oscillator 2`): the auto-numbered title makes two cards of one
type distinct, and a renamed card takes its new title. Add a `setComponentID` only where a component has no stable name;
do not rename existing ones.

A component with state worth reading implements `synth::testmode::StateSource` (`addTestModeState`), as `ModuleComponent`
(`selected`, `moduleType`, `nodeId`, `modHoverChip`: whether the chip stack under a hovered knob (or a dragged mod dot) is drawn, `modHoverChipRows`: its row texts, `macro`: the name of the macro the module sits in, empty when none, `macroPath`: every macro around it, outermost first, as `Outer macro > Nested macro`, `cabledTargets`: the modulation targets with a cable on them, `audioFrom`: the titles of the cards feeding its audio inputs, sorted and comma-separated, `cabledInputs`: every input jack with a cable, by its label, `cableEnd`, `cableEndOnKnob`, `cableEndGliding`: the first cable end on the card relative to it, whether it ends on a knob and whether an end glides, `cableEnds`: every cable end on the card as `x y` (plus ` knob` when on one) joined by `; `, `modDots`: the titles of the mod dots shown, `landingOutline`: the drag outline shown now as `x y w h`, empty when none, `canvasBounds`: its bounds in canvas coordinates) and
`MacroCardComponent` (id `macroCard`, or `macroCard:<macro name>` for one card among several: `name`, `collapsed`, `members`, `renaming`: its name editor is open) and a hidden child of the mixer panel (id `mixerFocusedColumn`: `focusedColumn`, the title of the column whose focus outline shows, empty when none; `focusedRow` and `focusedRowIndex`, the row the keys are on and its screen-reader text, empty and -1 in column mode) and a hidden child of the mixer panel (id `mixerTopBarRing`: `topBarRing`, the name of the control its key ring is on, empty when off) and the same child of the timeline panel (id `timelineTopBarRing`) and a hidden child of the toast (id `toastState`: `severity`, `info`, `warning` or `error`, and `message`) and `KeyPicker` (a 1x1 child, id `keyPicker`; the picker itself is `keyPickerPanel`, for screenshots: `auditioning`, `auditionKey`, `previewSoundShown`, `previewSound`, `fitProjectShown`, `fitProjectEnabled`, `playScaleOn`, `playScaleEnabled`) and a hidden 1x1 child of every number field (`CardStepper`; id `<name> state`, for example `Steps state`: `value`, `valueText`, `editing`, `editorText`, `repeating`, and `downX`, `numberX`, `upX`, the centre of each zone in the field's own coordinates) and a 1x1 child of the timeline's Sizing field (id `timelineClipSizingState`: `sizingMode`, `glyph` (the icon's mode name), `caption` (the text shown, empty with labels off), `fieldWidth`) and a hidden child of the timeline's clip lane (id `timelineClipPitches`: `clipPitches`, every clip's note pitches as `Clip 1:60,64;Clip 2:61`; `clipAudio`, every audio clip as `name:assetRef,sourceStartSeconds,lengthBeats,stretchOrigin`; `clipLengths`, every clip as `name:lengthBeats`; `clipWaveform`, every audio clip as `name:none|summary|columns`, the waveform it is drawn with; `waveformSource`, what the last waveform paint drew from: `peaks`, `samples` (exact columns from the audio file), `line` (the sample line) or `none`; `sizingReadout`, the percentage pill an edge drag shows, empty when none) and a 1x1 child of the timeline's playhead overlay (id `timelinePlayhead`: `frameCount` and `medianFrameIntervalMs`, the frames of the current or last frame run and the median gap of the last 64, and `frameRunning`) do. `getState` also reports a component's `tooltip`
when it has one, so a scenario can check the shortcut a button's tooltip names.
The minimap has the id `minimap` (a scenario clicks it by that id).
The tooltip window (id `tooltipWindow`) reports `tipText` and `tipAtPointer`. juce places a tip at the OS cursor, which synthetic input never moves, so `click`/`drag` tell the window where the synthetic pointer is (`AppTooltipWindow::setSyntheticPointer`) and tips land beside it, not at the user's own cursor; a scenario that leaves a tip up can `expect: {id: tooltipWindow, tipAtPointer: true}`.

## Screen frame

`screenshot` with `id: "screen"` paints the main window and every other visible top-level component of the app (pop-up
menus, call-out boxes, pickers, tooltips, the shape editor window) and composites each at its screen position. The frame
is the union of the main window's bounds and the other windows' bounds cut to the main window's display. Each window is
rendered offscreen like any other screenshot, since the windows are invisible in a hidden run and the OS compositor would
show nothing. The pointer is drawn at the last synthetic mouse position (`click`, `hover`, `drag`, `mouseDown` ...): the
image of the cursor the component under it shows when that is a custom image cursor (the tool cursors built by
`makeToolCursor`, which keeps the picture in a small registry because a `juce::MouseCursor` cannot be read back), else a
white arrow with a dark outline. The reply is `{path, width, height, windows: [{id,x,y,width,height}], pointer: {x,y,kind}}`
with positions in the frame's own coordinates and `kind` `arrow` or the cursor's name (`tool:range`). A window is drawn at its own opacity, so a popup mid-fade (or a leaving picture) shows as faint in a recorded strip. Code:
`Source/TestMode/TestModeScreenFrame.*`. `Tools/ui-verify` films this frame by default and `mark` saves it as proof, see
[Video and saved project](#video-and-saved-project).

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

An automation run is also silent: `muteAlertSounds()` (called by `hideAutomationWindows()`) makes `AppLookAndFeel::playAlertSound()` a no-op, and the `Tests` binary installs a silent default look and feel before every test (`Tests/SilentLookAndFeel.h`), so a test that presses outside a modal prompt does not play the system beep. the `AlertSoundSilence` tests in `PopupDismissPathsTests.cpp` check both.

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

## Stale pixels

A `screenshot` paints the component afresh, so it can never show a pixel the screen kept from an earlier frame: a row
that left without repainting where it stood, a flow dot outside the area a tick repaints. `trackPresentedFrame` keeps a
copy of what the main window shows instead: it starts from a full paint, and every paint the window's peer makes
(`MainWindow::paintOverChildren` reports its clip) repaints only that area of the copy, over what was there, as the
screen does. `checkStalePixels` compares the copy with a fresh full paint every 50 ms until they match or `settleMs`
runs out, so motion still running is not a failure and pixels nothing ever repaints are. A channel may differ by
`tolerance` levels: a zoomed canvas half-covers the pixel column at its edge and blends it over what was there, up to 4
levels; a pixel left behind differs by 9 and far more. `ignore` leaves out components that change on their own (a
meter). `differences.png` is the full paint with every differing pixel in magenta. Paints made for a screenshot or the
check itself are not counted.

It sees what JUCE repaints, not what macOS does with it (a backing store that drops a region JUCE asked for is
invisible to it). A scenario turns it on with `stale_pixel_check`, see [Scenarios](#scenarios).

## No-pop check

Anything added, removed or swapped on screen must animate; it never lands in one frame. `popWatchStart` keeps a full
paint of the window and, from the same presented frame the [stale-pixel check](#stale-pixels) keeps, a copy of every
frame the window presents afterwards. `popWatchCheck` waits until `ms` have passed and animations have stopped, paints the
window afresh, and takes the pixels that differ from the "before" picture (outside `ignore`) as the change. A change
**pops** when the first presented frame that shows any of it already equals the final picture there (all but 4 pixels or
1%), or when no presented frame shows it at all. A fade or slide has frames in between (`intermediateFrames`). The check
answers either way; the runner decides.

A scenario with `motion: true` runs it around every action that can change the window: `click`, `drag` (the whole drag),
`key`, `wheel`, `mouseUp`, `keyUp` and a `glide`'s `do`. Not checked: runner steps (`type` makes text appear per key
by design), `hover`, `mouseDown`, `mouseMoveHeld`, `keyDown`, `openProject`, `saveProject`, `restart`, `quit`, `wait`
and every query. Frames of a `record` video and of a `glide` are still taken before the check.

- `pop_check: {off: "<reason>"}` turns it off for the scenario; a bare `false` is refused. A mapping may also give
  `ignore: [ids]` (components that change on their own; the toolbar's Undo and Redo buttons are always ignored), `ms` and `undo: false`.
- `instant: "<reason>"` in a step's arguments (in a `glide`, beside `do`) lets that step pop: a deliberately instant
  change of text, a value, a selection or a caret. The reason is required and is not sent to the app.
- Undo and redo: when the app's edit serial changed during a checked step (an edit), the runner undoes and then redoes (after a step that is itself an undo, Cmd+Z or the Undo button, it redoes and then undoes, so the step stays undone), each under its own watch, and neither may pop (steps `NNu` and `NNr` in the log and in
  `<out>/pop-step-NNu/`). An `instant` step is also instant in its undo and redo. They click the toolbar's `undoButton` and
  `redoButton`, not Cmd+Z, which a focused text field would take as a text undo. Without an undo serial the app
  answers they are skipped and one line says so. The click takes focus, so a name field an edit opened (a new
  section's name) is closed before the next step can type into it: open it again (a double-click) before `type`.
- `Tools/ui-verify/known-pops.txt` lists pops that exist today, one per line: `<scenario> <step>[u|r] <ticket> <reason>`
  (`#` starts a comment; start the reason with an area tag such as `[timeline]`). A pop on a listed step passes with a
  `known pop` line; a listed step that runs and does not pop prints a `note:` to remove its line (a busy run can miss a pop, so it does not fail), and a listed
  scenario or step that does not exist fails too, so the file only shrinks. A pop on any other step fails the scenario,
  naming the region and the PNG folder `<out>/pop-step-NN/`.

## Limits

No image comparison against a reference (take a PNG and look at it; [Stale pixels](#stale-pixels) compares the window
only with itself). One client at a time per instance. `waitForIdle` sees component animations
(`ComponentAnimator`) and pending repaints, not a timer-driven effect inside a component; pass `--automation-motion` only
when the motion itself is under test. The fixture `Tools/ui-verify/fixtures/macros.agsproj` is regenerated by a disabled
test, see the ui-verify README.

Edge scroll (a drag held at the edge of the canvas) runs on its own 30 Hz timer, so it needs no `--automation-motion`;
`Tools/ui-verify/examples/edge_scroll_drags.py` holds each canvas drag at the edge and checks the scroll.

## Audio pump

A test-mode launch opens no audio device, so nothing renders the graph: a display fed by the audio thread (the
Wavetable frame under an LFO on Position, a filter curve under a cable) never moves. `audioPump {run: true}` renders the
open project on the message thread at the wall clock's pace, 512-sample blocks at 44.1 kHz, the way a bounce renders a
live app (`prepareForHost` once, then `processHostBlock` per block; at most 16 blocks per tick, so a stalled thread
skips ahead), until `audioPump {run: false}`. Nothing is heard. `audioPump {note: 60, down: true}` queues a note-on
into the next block's MIDI (`down: false` a note-off); it reaches the modules the graph's MIDI input feeds. The pump is
refused while a real device is running and stops on its own once the main window is gone
(`Source/TestMode/TestModeAudioPump.cpp`).

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
file before it first starts, for an install that has to look old; quote a string value such as `animationMode: "off"`, since YAML reads a bare `off`, `on`, `yes` or `no` as a boolean, written as 0 or 1), and `motion: true` to start with `--automation-motion`, for a check that needs a slide or fade to run; see also [Video and saved project](#video-and-saved-project)). A step is one mapping, `- <command>: {args}`: any
protocol command above (`click`, `drag`, `key`, `getState`, `openProject`, ...) with its arguments, plus:

| Step | Meaning |
| --- | --- |
| `type: {text, target?}` | one `key` per character, each sent to `target` when given (a text field of another window, e.g. `name` in the Save Snippet dialog) |
| `wait: {}` | `waitForIdle` |
| `restart: {}` | quit and start again on the same settings directory |
| `copyFixture: {name, from?}` | copies `${FIXTURES}/<name>` into `${SCRATCH}/<name>`, so a scenario can change a fixture without touching the committed copy; `from` (`~` expanded) copies a bundle outside the repository instead, such as the private Load test project, without its autosave sidecars, and fails the step when it is missing |
| `screenshot: {id?, name}` | saves `<name>.png` in the output folder (default id `window`) |
| `expect: {id, field: value, ...}` | equality on `getState` fields |
| `expect: {id, contains: {field: text}}` | case-insensitive substring |
| `expect: {id, exists: false}` | the component is not there |
| `expect: {count: {prefix, field?, equals}}` | how many ids (or `title`s) in `list` start with the prefix |
| `mark: {check, name}` | proof for one check, see [Proof marks](#proof-marks) |
| `glide: {name, do, then?, frames?, crop?}` | sends `do` (one command) and, when given, `then` (one more command, sent straight after it while the first motion still runs: a second Cmd+Z mid-fold), then films the screen `frames` times in a row (4 to 40, default 12, 0.1 s each) and saves `proof/<name>-strip.png`; see [Proof marks](#proof-marks) |

`stale_pixel_check: true` (or `{ignore: [ids], settle_ms: 1500}`) follows the window from launch and runs
`checkStalePixels` after every step that can change it (not after `wait`, `expect`, `getState`, `list`, `screenshot`, or
while a `mouseDown` is held), so a step that leaves pixels behind fails there, with the three PNGs in
`<out>/stale-step-NN/`. Pair it with `motion: true`: without motion nothing animates, and most stale pixels come from
an animation's last frame. Under motion a step that opens a menu or starts a slide may need a `wait: {}` after it.

`${FIXTURES}`, `${OUT}` and `${SCRATCH}` (the run's temporary settings directory) are replaced in every string. An `expect` is retried for `within_ms` (default 500), so a repaint
that is still settling is not a failure and one that never arrives still is.

### Video and saved project

Two more scenario options leave what someone needs to look at a run in seconds, beside its screenshots:

```yaml
record: true          # <out>/<scenario>.mp4
record_target: {id: randomizeRangePicker, scale: 4}   # optional: film this component (zoomed) instead of the main window
save_project: end     # <out>/end.agsproj, saved after the last step
steps: ...
```

`record` takes a [screen frame](#screen-frame) (the main window, every other window of the app and the pointer) after each step that can change the window, joined by ffmpeg, so it needs no Screen
Recording permission and works on hidden windows. It has no sound: for an audio check, attach an offline render's WAV.
`record_target` films one component instead; `scale` renders it larger. Each frame holds 0.8 s. The protocol answers one command at a time, so a recorded `drag` is sent as `mouseDown`,
`mouseMoveHeld` per step and `mouseUp` with a frame after each (0.1 s each, ten a second): the same gesture, only slower
in real time. A drag whose two ends are the same component is sent to that component in its own coordinates (so it also works in a window of its own, such as a shape window). A drag with an end outside the main window stays one command and shows only its result. Without ffmpeg
the run prints a warning and still passes. A failed scenario keeps both: they show what went wrong. `save_project` is a plain
name (it cannot leave `<out>`), overwrites the last run's bundle and fails the scenario if the save fails.

### Proof marks

`- mark: {check: N, name: <slug>}` (N an integer of 1 or more, `name` lower-case letters, digits and dashes, unique in the
scenario; anything else is refused when the file is read) is the moment a check's claim is on screen: put it while the menu,
picker or hover it shows is still open. It takes one screen frame held 1.5 s, saves it as `<out>/proof/<name>.png` (also
with `record` off) and, once the video is written, cuts `<out>/<scenario>-<name>.mp4` from 2.5 s before the frame to 3 s
after it (4 to 6 s when the video allows; needs ffmpeg). The run log gets a `mark` line with the still and clip, and the
summary lists both.

A motion check needs the motion itself on film, not one frame of it: a step records one frame, so a 220 ms slide caught
after its click shows a blur held for 0.8 s. Send the click as `glide: {name: slide, frames: 14, do: {click: {...}}}`: it
takes no frame of its own after the command, films `frames` screen frames back to back (about 90 ms apart) and tiles them
into `proof/slide-strip.png` (`crop: {x, y, w, h}` limits the strip to the part of the frame that moves, two to a row).
Then `wait`, and put the `mark` on the landed state, never mid-motion. Start the clip in the state the check starts from
and end the scenario in it too, so the `save_project` bundle opens there. Read the strip before attaching the clip.

A check of a static state (a selected dot, a tag's position) puts its mark right after the click that changes the screen
(click the dot, open the picker). With `record`, a `click` first films the pointer approaching: four `hover` frames
from 60 px up-left of the click point, 0.1 s each (`approach: false` on the step skips it; a click point outside the
main window is sent as it is), so the clip holds the before, the approach and the after, which the review needs at least
four distinct frames of. If there is truly nothing to see, the review says why there is no video.

To add one: copy the closest file, start the file with a comment naming the bug and the steps a user takes, and give each
check an id from `list` (add a `setComponentID` or a `StateSource` field where the component has none, never rename an
existing id). A UI change that moves or renames something a scenario uses updates that scenario in the same change, and a
regression that came back gets a scenario that fails with the fix taken out. The runner's own logic is covered by
`scripts/tests/ui-verify-run.test.sh`, which needs no app.

## Tests

`Tests/TestMode/`: protocol parsing, launch flags and policies, id lookup, synthetic input through a real peer, the
server over a real socket (including shutdown with a client connected), every command, a module card's state, and the
committed fixture.
