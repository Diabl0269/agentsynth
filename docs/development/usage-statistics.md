# Usage statistics (opt-in, anonymous)

Agent Synth can send one small summary per day of which features are used. It is **off unless the user
turns it on** (Settings > Preferences > Privacy > "Share anonymous usage statistics"). The public privacy
policy promises exactly what this page describes; change one, change the other.

## What a summary holds

Only counts and platform facts, all from a fixed allow-list (the contract, below):

- a random telemetry id (UUID v4), the local day (`YYYY-MM-DD`), the app version (`1.2.3` form), OS
  (`windows`/`macos`/`linux`), CPU (`x64`/`arm64`) and format (`standalone`/`vst3`/`au`);
- the number of sessions that day and a bucket for foreground time (`<15`, `15-60`, `60-180`, `180+` minutes);
- how many modules of each type were added (one counter per module type in the contract);
- how many times each of these happened: macro created, timeline used, AI request, preset saved, preset
  loaded, project opened.

It never holds patch contents, module or project names, file paths, prompts or any free text, or audio.

## What it is filed under

A separate random id, **not** the device id used for the free trial and **not** tied to an account. It is created
the moment the user opts in and deleted the moment they opt out. A request carries only
`Content-Type: application/json`: never an `Authorization` header, never `X-Device-Id`.

Where to find the id (the Preferences row shows it too, with a Copy ID button):

| OS | Folder |
| --- | --- |
| macOS | `~/Library/Agent Synth/` |
| Windows | `%APPDATA%\Agent Synth\` |
| Linux | `~/.config/Agent Synth/` |

`telemetry_id` is the id; `telemetry_queue.json` holds the days not sent yet. Neither file exists while the
setting is off.

## Rules

- **Off means nothing is recorded.** Every counting call returns at once while disabled, and no file is written.
  A launch with the setting off deletes any id or queue a previous opt-in left behind.
- **Opting out deletes the id, the unsent queue and the counts held in memory**, before the toggle returns. A send
  that is already running stops before its next request, and a result that arrives afterwards changes nothing.
- **One summary per local day.** Counts for today are kept in `telemetry_queue.json`; when the local date changes,
  the day moves into the unsent queue. A relaunch on the same day resumes today's counts.
- **At most 14 unsent days** are kept; the oldest is dropped.
- **Sending**: a few seconds after launch the app POSTs every queued day strictly before today to
  `<API base>/v1/telemetry/daily` on a background thread, fire and forget. A day leaves the queue on any 2xx or a
  4xx other than 429; a 429, a 5xx or a network failure keeps it for the next launch. Today is never sent.
- **Standalone app only.** The plugin build does not record or send (a plugin editor is opened and closed at the
  host's whim, and several instances would share one queue file). The setting is shared, so switching it on in a
  plugin's Settings window creates the id but nothing is collected until the standalone app runs.

## Where the code is

| Piece | File |
| --- | --- |
| The contract struct (vendored, never edited) | `Source/Telemetry/generated/Telemetry.g.h` |
| Id file | `Source/Telemetry/TelemetryIdStore.h` |
| Counters, day rollover, queue, opt-out | `Source/Telemetry/TelemetryRecorder.h` |
| Request body (hand-written, exactly the contract's keys) and the queue file format | `Source/Telemetry/TelemetryJson.h` |
| Module name mapping table | `moduleTable()` in `Source/Telemetry/TelemetryDay.cpp` |
| Launch-time send | `Source/Telemetry/TelemetrySender.h` |
| Owner: minute timer, flush, delayed send | `Source/Telemetry/TelemetryService.h` |
| Wiring | `Source/MainComponent/MainComponentTelemetry.cpp` |
| The setting keys | `kShareUsageStatsSettingKey` and `kUsageStatsAskedSettingKey` in `Source/UserSettings.h` |
| The one write of the choice (both places) | `applyShareUsageStatsChoice()` in `Source/Telemetry/UsageStatsChoice.h` |
| The Welcome screen card | `Source/UI/Chrome/UsageStatsPromptComponent.h`, placed by `WelcomeScreenComponent` |
| The Preferences rows | `Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabUsageStats.cpp` |

The setting `shareUsageStats` is the single source of truth for "on". The Preferences toggle and the Welcome
screen's card both write it through `applyShareUsageStatsChoice()`, which also creates or deletes the id and queue
file at once and sets `usageStatsAsked`; `MainComponent` re-reads it on every settings-file change and
calls `TelemetryService::applySetting`, which is idempotent with what the toggle already did.

### The Welcome screen card

Standalone only. A card under the Welcome screen's subtitle asks once: "Help make Agent Synth better", with Share
and No thanks as two equal buttons and a "What we collect" link to `https://agentsynth.app/privacy#telemetry`
(through the same URL seam as Contribute). It shows while `usageStatsAsked` is false **and** `shareUsageStats`
is false, so an existing user sees it once after updating and an existing opt-in never does. Either button, and
either way of turning the Preferences toggle, sets `usageStatsAsked`, so it never returns. It has no close
control and never blocks the start buttons. After an answer it collapses out (180 ms, height and fade); Share
first shows "Thanks. You can change this in Preferences." for about a second. Reduced Motion removes both.

### Counting

All counting happens on the message thread, from user actions only, through single-slot callbacks so the heavy
headers stay out of it: `GraphEditor::onModuleAdded`, `MacroGroupController::onMacroCreated`,
`AIChatComponent::onMessageSent`, and `MainComponent::countUsage` for the rest. Never count from anything reached
by loading a project, rebuilding cards or applying an AI patch: those create modules without the user adding them.

| Counter | Counted when |
| --- | --- |
| module added | a module is dropped on, or inserted into, the canvas (`GraphEditor::addModuleAtCanvasPosition`) |
| macro created | `MacroGroupController::groupSelectionIntoMacro` made a macro |
| timeline used | a record take starts (`handleRecordToggle`) or the `transportPlay` command starts the transport |
| AI request | `AIChatComponent::sendButtonClicked` sends a message |
| preset saved | a project bundle, a plain patch or an exported patch is saved |
| preset loaded | a factory preset or a plain patch file is opened |
| project opened | a project bundle finishes loading, including restoring its autosave |

Counters stop at the contract's ceilings (500 sessions, 2000 per module type, 5000 per feature).

## The contract

`Telemetry.g.h` is generated in the platform repo (`packages/contracts`, from `src/telemetry.ts`) and copied
here verbatim, as a manual commit: this repo has no build-time dependency on the generator. A `.clang-format` in that
folder switches formatting off, so the copy stays byte-identical to the generated file. To re-vendor after
the contract changes, run `pnpm --filter @platform/contracts codegen:telemetry` there, copy
`packages/contracts/generated/Telemetry.g.h` over `Source/Telemetry/generated/Telemetry.g.h` unchanged, add any
new module type to `moduleTable()` (and `kModuleCount`) or new feature to `TelemetryJson.cpp`, and commit both.
The server's schema is strict: an extra key rejects the whole request, so the serializer writes only keys the
struct has.

## Tests

`Tests/Telemetry/` (id store, serializer, recorder, sender with a fake `HttpPerformer`, `MainComponent` wiring)
and `Tests/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabUsageStatsTests.cpp`. Tests give the stores
explicit temp paths, or rely on the settings-directory redirect in `Tests/TestMain.cpp`.
