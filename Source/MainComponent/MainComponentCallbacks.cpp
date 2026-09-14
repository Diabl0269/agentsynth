// MainComponentCallbacks.cpp — MainComponent's change/focus/timer callbacks, hosted-plugin
// latency plumbing, AI patch apply hooks, and the preset/project load & export entry points
// (open/save dialogs, factory presets, patch/audio/stems export). MainComponent is declared in
// MainComponent.h; the rest of its implementation lives in the sibling MainComponent*.cpp units
// next to this one.
#include "MainComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include <cmath>

namespace {

// The file filter both patch dialogs use. A `.agsproj` project bundle only exists in a build that
// has the timeline compiled in — offering it otherwise would let a user save a "project" whose
// timeline half can never be non-empty.
constexpr const char* kPatchFileFilter = "*.json;*.agsproj";
// Subdirectories a saved bundle gets its exports/patch-only snapshots written into by default (P8-5
// follow-up). Deliberately NOT reserved names on ProjectBundle: unlike Audio/Peaks they carry no
// asset-integrity contract and AssetManager::cleanUnusedAssets never looks past Audio/, so nesting
// them inside the bundle is safe - they are just a destination choice, not part of the bundle's
// asset policy.
constexpr const char* kExportsFolderName = "Exports";
constexpr const char* kPatchesFolderName = "Patches";
// The folder a "Export Audio..."/"Export Patch Only..." dialog starts in: <bundle>/<subFolderName>
// when a real bundle is open (created on demand), otherwise the same Music/AgentSynth root every
// other save/open dialog defaults to.
juce::File resolveExportSubdirectory(const juce::File& currentBundleDir, const char* subFolderName) {
    if (currentBundleDir != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir)) {
        auto dir = currentBundleDir.getChildFile(subFolderName);
        dir.createDirectory();
        return dir;
    }
    return synth::ProjectBundle::getDefaultProjectsDirectory();
}

} // namespace

void MainComponent::applyStoredDualIOPreferenceToPatch() {
    // Runs once, right after AudioEngine::initialise() has built the opening patch. Storing the
    // preference on the GraphEditor is not enough on its own: the default preset's modules are
    // constructed by the preset loader, which knows nothing about preferences, so they come up
    // holding their constructor defaults. The voice modules default to dual — so a user who had
    // chosen single jacks got a split Oscillator and Filter on every launch.
    //
    // A patch the user saved carries an explicit dualIO value per module and is applied later by
    // applyJSONToGraph, so this governs the factory patch rather than a reload of their own work.
    graphEditor.applyDualIOToExistingModules(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false));
}

juce::String MainComponent::computeOutputDeviceInfoText() const {
    // Hosted mode (plugin): AudioEngine never opens a device or touches deviceManager — the host
    // owns the clock and the hardware (see HostMode::Hosted in docs/architecture.md) — so there is
    // no device to describe, only which world this editor is running in.
    if (audioEngine.isHosted())
        return "Host audio";

    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return {}; // No device open yet (headless/CI, or between devices) — the card hides the line.

    const double sampleRateHz = device->getCurrentSampleRate();
    const double khz = sampleRateHz / 1000.0;
    // "48 kHz" for a whole number, "44.1 kHz" when it isn't — matches how the format is normally
    // spoken, rather than always showing a decimal point.
    const juce::String khzText =
        (std::abs(khz - std::round(khz)) < 0.01) ? juce::String((int)std::round(khz)) : juce::String(khz, 1);

    const int numOutputChannels = device->getActiveOutputChannels().countNumberOfSetBits();

    // Plain ASCII separator. A "\xc2\xb7" (UTF-8 U+00B7 MIDDLE DOT) used to sit here, on the theory
    // that an escape survives a non-UTF-8 editor better than a literal byte — but the escape is the
    // same three bytes, and juce::String's `const char*` constructor decodes bytes as LATIN-1, so
    // both spellings reached the status bar as mojibake. ASCII or juce::CharPointer_UTF8; see the
    // string-literal invariant in CLAUDE.md, guarded by scripts/tests/check-nonascii-literals.test.sh.
    return device->getName() + " - " + khzText + " kHz - " + juce::String(numOutputChannels) + "ch";
}

// ---- Change callbacks: theme re-skin, and the live settings-file path ----
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // The undo manager broadcasts on every perform/undo/redo/new-transaction. This RECOMPUTES the
    // answer from the edit serial rather than blindly setting dirty, because that broadcast is
    // ASYNC (juce::ChangeBroadcaster::sendChangeMessage) and therefore always potentially stale:
    // New Patch clears the timeline and the graph — two real undoable steps — and only then resets
    // the document, so a notification for those steps is still queued when the reset runs. Setting
    // the flag on arrival would re-dirty a brand-new "Untitled" document one message-loop pass
    // after it was created; comparing serials makes the late notification a no-op instead, since
    // markDocumentClean() captured the baseline AFTER those steps.
    // Checked before the settings/theme dispatch below since it is neither of those broadcasters.
    if (source != nullptr && source == &undoManager.getUndoManager()) {
        const bool nowDirty = undoManager.getEditSerial() != savedEditSerial_;
        if (nowDirty != isDirty_) {
            isDirty_ = nowDirty;
            notifyDocumentTitleChanged();
        }
        return;
    }

    // Dispatch on the source, not "assume theme": two broadcasters reach here now. A settings write
    // must NOT trigger a full re-skin (persisting a panel height or a snap division would re-theme
    // the whole window), and a theme switch must not re-read preferences.
    if (source != nullptr && source == appProperties.getUserSettings()) {
        applyNaturalScrollingPreference();
        applyZoomScrollPreference();
        // Piano-roll key-label mode and note colour overrides live in the same properties file —
        // re-read them on every settings write so an Appearance-tab edit shows up immediately,
        // the same "re-read on notify" treatment as the two calls above. No startup call needed:
        // TimelinePanelComponent::setApplicationProperties already does the initial load.
        timelinePanel.reloadPianoRollAppearancePrefs();
        return;
    }

    // Push new theme values into the LookAndFeel (colours / treatment / metrics), then
    // propagate lookAndFeelChanged() + a single repaint so every widget re-skins.
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    if (auto* top = getTopLevelComponent())
        top->sendLookAndFeelChange();
    // Re-tint the toolbar / status-bar icons from the already-retinted IconLibrary cache.
    applyToolbarIcons();
    repaint();
}

// T159: fires on EVERY keyboard-focus change in the process, not just ones inside our own regions
// (a focus change elsewhere, e.g. a native file-chooser, still reaches here) — cheap to over-fire
// since this is just repaint() calls on a handful of components, and correctness needs both the
// region losing focus and the one gaining it repainted (a still-focused root never calls repaint()
// on its own, since Component::focusGained/focusLost are no-op virtuals for most components).
void MainComponent::globalFocusChanged(juce::Component*) {
    for (const auto& region : focusRegions_.getRegions())
        if (region.root != nullptr)
            region.root->repaint();
}

void MainComponent::timerCallback() {
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());

    // Message-thread driver, on the timer we already run: drains the gesture ring and polls
    // the transport (Write spans open on play; every open span commits on stop). Allocation-free
    // and — like everything else in this callback — completely silent.
    automationRecorder.update();

    // Mirrors AutomationRecorder's own playing->stopped edge detection (see its update()) —
    // a MIDI take still open when the transport stops (the user hit Space/Stop rather than the
    // record button itself) commits exactly the same way an explicit Record-off click does. Read
    // unconditionally (cheap, lock-free) rather than only when the panel is visible: recording must
    // not depend on the timeline panel staying open.
    const auto position = audioEngine.getTransport().getPositionSnapshot();
    if (wasTransportPlaying_ && !position.playing && midiRecorder.isRecording())
        commitMidiRecording();

    // The audio half of the same rule — COMMIT ON STOP, mirroring the MIDI one above: a take
    // still open when the transport stops (the user hit Space rather than the record button) commits
    // down the same path. Capture itself starts at the Record-on click, not here; the pre-roll is
    // trimmed at commit time from the tap's own sample-accurate anchor.
    if (wasTransportPlaying_ && !position.playing && audioTake_.capturing)
        commitAudioRecording();

    // A device/sample-rate change strands whatever take was rolling — unlike the two commit-
    // on-stop checks above, the transport typically keeps PLAYING right through a format change (see
    // TransportService::prepare), so neither of those edge-triggered checks would ever fire. Consumed
    // once here (exchange-back-to-false, same contract as consumeFeedbackGuardTripped) and routed
    // through the SAME commit choke points a manual Record-off or a transport stop already use — see
    // AudioEngine::handleStreamFormatChange, which is what actually sets the flag, at the moment of
    // the change. At most one of the two checks below ever fires: recording never runs both an audio
    // and a MIDI take at once (first-armed-wins — see onRecordToggled).
    if (audioEngine.consumeFormatChangedDuringCapture()) {
        if (audioTake_.capturing)
            commitAudioRecording();
        if (midiRecorder.isRecording())
            commitMidiRecording();
        statusBar.showMessage("Recording stopped: audio device changed");
    }

    // Autosave's gate, on the same driver — see maybeAutosave()'s own comment. Placed AFTER the
    // three commit-on-stop checks above (not before): a take that just committed on this very tick
    // must be allowed to autosave immediately, not wait for the take flag to clear on some later
    // tick, and a take still genuinely in flight must still block it.
    maybeAutosave();

    // Polls BounceRunner/StemRunner's progress onto the dialog's progress bar - on the SAME 10 Hz
    // driver as everything else here, rather than a second timer just for this. exportDialog_ is a
    // SafePointer: the dialog can only go away by the user closing the (modal) window, but nothing
    // stops that from racing a tick. Only one of the two runners is ever non-null at once - see
    // isBounceInProgress_'s own comment.
    if (isBounceInProgress_ && exportDialog_ != nullptr) {
        if (bounceRunner_ != nullptr)
            exportDialog_->reportProgress(bounceRunner_->getProgress());
        else if (stemRunner_ != nullptr)
            exportDialog_->reportProgress(stemRunner_->getProgress());
    }

    wasTransportPlaying_ = position.playing;

    // Clears the count-in pre-roll's forced-on click once the transport reaches the punch-in
    // point. Gated on isRecording() so this never fires outside an actual take; idempotent
    // otherwise (setForcedOn(false) on an already-off metronome is a no-op), so polling it every
    // tick while recording costs nothing once the pre-roll has already ended. An audio take's
    // pre-roll rides the same rule, ending at its own punch-in beat.
    if ((midiRecorder.isRecording() && position.ppq >= midiRecorder.getPunchInBeat()) ||
        (audioTake_.capturing && position.ppq >= audioTake_.punchInBeat))
        audioEngine.getMetronome().setForcedOn(false);

    // The input-monitoring gate's poll-side half. Any Audio-kind track armed -> monitoring
    // should be on; none armed -> off. A guard trip (audioEngine.consumeFeedbackGuardTripped(),
    // consumed exactly once here) latches monitoring off for as long as the SAME arm state
    // persists — disarming every Audio track and re-arming one is the explicit reset gesture, and
    // that is exactly the false->true edge of "is any Audio track armed" below. See
    // docs/architecture.md's "Input monitoring & feedback guard".
    bool anyAudioTrackArmed = false;
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.kind == synth::TrackKind::Audio && track.armed) {
            anyAudioTrackArmed = true;
            break;
        }
    }
    if (anyAudioTrackArmed && !wasAnyAudioTrackArmed_)
        feedbackGuardLatched_ = false; // the reset gesture: disarmed, then armed again
    wasAnyAudioTrackArmed_ = anyAudioTrackArmed;

    if (audioEngine.consumeFeedbackGuardTripped()) {
        feedbackGuardLatched_ = true;
        statusBar.showMessage("Input muted - sustained clipping (feedback protection)");
    }

    audioEngine.setInputMonitoringEnabled(anyAudioTrackArmed && !feedbackGuardLatched_);

    // The timeline panel's low-rate transport poll, on the same existing timer — no new
    // timer, and nothing at all when the panel is hidden (a collapsed timeline must cost exactly
    // what it did before). This is what starts/stops the playhead's playing-only 30 Hz strip
    // repaint; see docs/layout.md §11.
    if (timelinePanel.isVisible()) {
        // Device-buffer latency only. The graph's own reported latency is deliberately left out:
        // it is report-only, patch-dependent and mostly zero, whereas the output buffer is the term
        // that actually separates "rendered" from "heard".
        const double sampleRate = position.sampleRate > 0.0 ? position.sampleRate : 44100.0;
        const double outputLatencySeconds = (double)audioEngine.getOutputLatencySamples() / sampleRate;
        timelinePanel.updateFromTransport(position, outputLatencySeconds);

        // The clip lane's growing recording strip, on the same poll. Cheap and a no-op when
        // nothing is capturing — synth::ui::TimelineClipLaneArea::updateLiveRecording() internally
        // repaints only when new peak buckets actually arrived (see its own comment), never merely
        // because the transport tick moved.
        synth::ui::TimelineClipLaneArea::LiveRecordingInfo liveInfo;
        if (audioTake_.capturing) {
            if (auto* tap = findMasterRecordTap()) {
                liveInfo.active = true;
                liveInfo.track = audioTake_.track;
                liveInfo.punchBeat = audioTake_.punchInBeat;
                liveInfo.currentBeat = position.ppq;
                liveInfo.tap = tap;
            }
        }
        timelinePanel.getClipLaneArea().updateLiveRecording(liveInfo);
    }

    // Status bar polls at 5 Hz (every 2nd tick of the 10 Hz timer). update() is gated — it
    // only repaints the status bar when a displayed value actually changes. ZERO logging.
    if (++statusBarTickCount_ >= 2) {
        statusBarTickCount_ = 0;
        // Hosted (plugin) mode has no device manager of its own — the host owns the device, so
        // getCpuUsage() would report a constant 0. Show 0 rather than a misleading reading.
        const float cpu = audioEngine.isHosted() ? 0.0f : (float)(audioEngine.getDeviceManager().getCpuUsage() * 100.0);
        statusBar.update(cpu, audioEngine.getDisplayVoiceCount(), currentPatchName_);

        // The round-trip readout, on the same 5 Hz tick.
        updateRoundTripLatencyReadout();

        // The always-visible transport cluster (play/stop + position + BPM) — fed from `position`,
        // which is read UNCONDITIONALLY above (before the timelinePanel.isVisible() guard), so this
        // is identical whether the timeline panel is open or closed; see docs/layout.md §5. Reuses
        // TimelineTransportBar's own static formatBarBeat() for the "bar.beat.ticks" text rather
        // than reimplementing it — StatusBarComponent can't call it itself (Core cannot depend on
        // AppUI), so this is the one call site that does the formatting.
        statusBar.updateTransport(position.playing,
                                  synth::ui::TimelineTransportBar::formatBarBeat(
                                      position.ppq, position.timeSigNumerator, position.timeSigDenominator),
                                  position.bpm);
    }
}

void MainComponent::updateRoundTripLatencyReadout() {
    // Gated by its own string diff inside StatusBarComponent — so calling it more
    // often than the 5 Hz poll (a hosted plugin's latency change does) costs nothing when
    // the number has not moved. Hosted mode has no device of ours to report a round trip for (the
    // host owns both ends), so it shows the placeholder rather than a made-up 0.0 ms.
    const double statusRate = audioEngine.getTransport().getPositionSnapshot().sampleRate;
    const double roundTripMs =
        statusRate > 0.0 ? 1000.0 * (double)audioEngine.getRecordingLatencySamples() / statusRate : 0.0;
    statusBar.updateRoundTripLatency(roundTripMs, !audioEngine.isHosted());
}

// ---- Hosted-plugin latency compensation ----

void MainComponent::installHostedPluginObservers() {
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor());
        if (hosted == nullptr)
            continue;

        // Two SEPARATE slots, neither of them onInstanceChanged — that one belongs to
        // HostedPluginEditorWindow and reassigning it here would close the user's plugin
        // window on the next graph change.
        hosted->onLatencyChanged = [this] { rebuildGraphForLatencyChange(); };
        hosted->onInstancePublished = [this] {
            // A lane bound to a hosted-plugin parameter cannot resolve until the instance exists,
            // so re-run the reconcile when an async load completes — otherwise the lane sits
            // orphaned until some unrelated graph edit happens to trigger the next pass.
            reconcileTimelineBindingsOnly();
            // ...and a publish takes the node's latency 0 -> N, so the graph's compensation delays
            // are stale for exactly the same reason a runtime change leaves them stale.
            rebuildGraphForLatencyChange();
        };
    }
}

void MainComponent::rebuildGraphForLatencyChange() {
    // juce::AudioProcessorGraph bakes {bus layout, latencySamples} per node into
    // its render sequence and only re-derives the parallel-path compensation delays when that
    // sequence is rebuilt — so without this, a plugin reporting 512 samples of lookahead is simply
    // uncompensated and its branch of the patch drifts against every parallel one. rebuild() is
    // public, message-thread-safe (it dispatches to the message thread if called from anywhere
    // else) and a no-op when nothing about the sequence actually changed.
    //
    // Host-agnostic: in a plugin build this is our own INNER graph, which we own in both modes.
    audioEngine.getGraph().rebuild();

    // The graph term of AudioEngine::getRecordingLatencySamples() just moved, so the status bar's
    // number is wrong until its next poll. Same feed as that poll, not a second one.
    updateRoundTripLatencyReadout();
}

void MainComponent::aiPatchAboutToApply() {
    // Runs synchronously before the AI patch clears/rebuilds the graph. Detach module components now so
    // their ScopeComponent timers stop and no component references a soon-to-be-freed VisualBuffer.
    graphEditor.detachAllModuleComponents();
    // Everything the apply writes into parameters is programmatic. Assignment closes any scope an
    // earlier, failed apply abandoned (see the member's comment).
    aiApplyScope = std::make_unique<ProgrammaticApplyScope>(*this);
}

void MainComponent::aiPatchApplied() {
    aiApplyScope.reset();
    // The graph is fully applied by the time this fires (on the initial apply AND on undo/redo,
    // which reuse this pair as their restore hooks), so bindings can be reconciled straight away —
    // no need to wait for the async updateComponents() below.
    reconcileTimelineAfterGraphChange();
    setCurrentPatchName("AI Patch");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->graphEditor.updateComponents();
    });
}

void MainComponent::simulateLoadFactoryPresetForTest(int index) {
    auto presets = synth::PresetManager::getPresetList();
    if (index < 0 || index >= presets.size())
        return;
    loadFactoryPresetAtIndex(index);
    setCurrentPatchName(presets[index].name);
}

void MainComponent::loadFactoryPresetAtIndex(int index) {
    ProgrammaticApplyScope guard(*this);
    graphEditor.loadFactoryPreset(index);
    reconcileTimelineAfterGraphChange();
    // A factory preset is not the bundle that was open, so the next Cmd+S must prompt for a new
    // location rather than silently overwrite it — same reasoning as the newPatch case's "A new
    // document is not the old bundle" comment.
    currentBundleDir_ = juce::File();
    refreshAssetRoots();
    // NOT markDocumentClean(), unlike newPatch/open/save. A factory preset replaces the GRAPH and
    // leaves the live timeline exactly where it was, so "this document now matches something on
    // disk" would be a lie the moment the timeline holds anything: an unsaved arrangement would
    // survive the load with the dirty flag cleared, and quitting after that would discard it
    // without ever asking. The load records no undo transaction of its own (it goes through
    // PresetManager, not AppUndoManager), so leaving the flag alone is also accurate in the other
    // direction - a clean document stays clean, a dirty one stays dirty.
}

// T114/P8-10: shared by the Load menu's own factory-preset branch and the welcome screen's "Open
// our default project" button (index 0). hideWelcomeScreen() is the LAST line inside `proceed` —
// never before or after guardUnsavedChanges() itself — so a Cancel answer leaves the welcome screen
// exactly as it was (see DirtyDocumentIsGuardedBeforeWelcomeScreenReplacesIt in
// WelcomeScreenTests.cpp).
void MainComponent::loadPresetGuarded(int index) {
    auto presets = synth::PresetManager::getPresetList();
    if (index < 0 || index >= presets.size())
        return;
    guardUnsavedChanges("Loading a preset", [this, presets, index] {
        statusBar.showMessage("Loading preset...");
        loadFactoryPresetAtIndex(index);
        setCurrentPatchName(presets[(size_t)index].name);
        statusBar.showMessage("Loaded: " + presets[(size_t)index].name);
        hideWelcomeScreen();
    });
}

// T114/P8-10: shared by the Load menu's "Recent Projects" submenu and the welcome screen's recent-
// project rows. Goes through openFromFile like every other recent-project open, so autosave
// recovery and the bundle/plain-preset split both apply unchanged — see openFromFile/
// loadBundleFromFile/loadAutosaveFromFile's own hideWelcomeScreen() calls on their success paths.
void MainComponent::openRecentProjectGuarded(const juce::File& file) {
    guardUnsavedChanges("Opening a recent project", [this, file] { openFromFile(file); });
}

// Guards BEFORE the dialog opens — the chooser itself is the post-guard half, below.
void MainComponent::openPresetFromFile() {
    // P8-31: no top-level guard here. Loading a patch first offers to REPLACE or APPEND onto the
    // current patch; only the destructive REPLACE arm guards unsaved changes (an append keeps them),
    // so the guard lives inside the Replace branch, reached after the user picked a file.
    launchOpenPresetChooser();
}

void MainComponent::openProjectFromFile() {
    guardUnsavedChanges("Opening a project", [this] { launchOpenProjectChooser(); });
}

// P8-31: the patch half - a plain `.json` preset, an ordinary file pick (never a directory). Once
// the user has chosen a file, promptPatchLoadMode asks whether to REPLACE the current patch or add
// the loaded one on top of it; openFromFile() branches on that flag.
void MainComponent::launchOpenPresetChooser() {
    fileChooser = std::make_unique<juce::FileChooser>("Load Patch", synth::ProjectBundle::getDefaultProjectsDirectory(),
                                                      "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            promptPatchLoadMode([this, file](PatchLoadMode mode) {
                switch (mode) {
                case PatchLoadMode::Cancel:
                    return;
                case PatchLoadMode::Append: // keep the current patch, add on top
                    openFromFile(file, /*append=*/true);
                    return;
                case PatchLoadMode::Replace: // destructive, so guard unsaved changes
                default:
                    guardUnsavedChanges("Replacing the patch", [this, file] { openFromFile(file, /*append=*/false); });
                }
            });
    });
}

// P8-31: the project half - a `.agsproj` bundle is a DIRECTORY (project.json + Audio/ + Peaks/),
// so the browser must let the user pick a directory.
void MainComponent::launchOpenProjectChooser() {
    // Empty filter: an extension filter (e.g. `*.agsproj`) would make a macOS NSOpenPanel restrict
    // selection to that file name and refuse to let the user pick the folder itself. No filter lets
    // the OS list directories; openFromFile() is the one gate that validates the pick is a bundle.
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Project", synth::ProjectBundle::getDefaultProjectsDirectory(), juce::String());
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            openFromFile(file);
    });
}

// Cmd+S's decision (see the header comment): resave silently to the remembered bundle when one is
// open and the caller isn't forcing the chooser, otherwise prompt. The suggested name defaults to
// `.agsproj` — not because the filter forbids `.json` (it still lists both, and saveToFile still
// branches on whatever extension comes back), but because a first-time saver who just hits Enter
// should land on the bundle format, which is what actually keeps the timeline.
void MainComponent::performSaveProject(bool forceChooser, std::function<void(bool saved)> onFinished) {
    if (!forceChooser && currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_)) {
        const bool ok = saveToFile(currentBundleDir_);
        if (onFinished)
            onFinished(ok);
        return;
    }

    const auto suggested = synth::ProjectBundle::getDefaultProjectsDirectory().getChildFile(
        currentPatchName_ + synth::ProjectBundle::kBundleExtension);
    fileChooser = std::make_unique<juce::FileChooser>("Save Project", suggested, kPatchFileFilter);
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this, onFinished](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File{}) {
            if (onFinished)
                onFinished(false);
            return;
        }
        const bool ok = saveToFile(file);
        if (onFinished)
            onFinished(ok);
    });
}

// The legacy patch-only export — see the header comment for why this calls graphEditor.savePreset
// directly rather than saveToFile: exporting a snapshot from an open bundle must never look like
// the project itself was (re)saved.
void MainComponent::exportPatchOnly(const juce::File& file) {
    graphEditor.savePreset(file);
    statusBar.showMessage("Exported patch: " + file.getFileNameWithoutExtension());
}

void MainComponent::promptExportPatchOnly() {
    const auto suggested =
        resolveExportSubdirectory(currentBundleDir_, kPatchesFolderName).getChildFile(currentPatchName_ + ".json");
    fileChooser = std::make_unique<juce::FileChooser>("Export Patch Only", suggested, "*.json");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{})
            exportPatchOnly(file);
    });
}

// The offline bounce/export flow (P8-5): show the options dialog, then drive a BounceRunner from
// what it reports. See Source/Transport/BounceRunner.h and Source/UI/Chrome/ExportAudioDialog.h for why
// the render is chunked rather than blocking, and docs/architecture.md for the full design.
void MainComponent::promptExportAudio() {
    if (isBounceInProgress_)
        return; // the command is reported inactive while one is running - see getCommandInfo.

    const double arrangementEndBeat = timelineDoc.getArrangementEndBeat();
    const auto position = audioEngine.getTransport().getPositionSnapshot();
    // "Current loop range" is offered as a bounce range whenever the loop LOCATORS describe a
    // non-degenerate region, independent of whether looping is currently ARMED (P8-17). The region
    // is the source, not the live loop: a disengaged loop still names a real span. TransportService
    // always carries a valid [start, end) (its own default is [0, 4)), so only a collapsed region
    // (end <= start) disables the option; there is no separate "locators unset" state to detect. A
    // bounce renders that span linearly regardless (BounceExporter unloops for the duration and
    // restores it), so arming state never changes what lands in the file.
    const bool hasLoopRange = position.loopEndPpq > position.loopStartPpq;
    const bool projectIsSaved = currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_);

    auto* dialog = new synth::ui::ExportAudioDialog(
        arrangementEndBeat, hasLoopRange, position.loopStartPpq, position.loopEndPpq, position.bpm, projectIsSaved,
        resolveExportSubdirectory(currentBundleDir_, kExportsFolderName), currentPatchName_);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = dialog->getWindowTitle();
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    // T153: ExportAudioDialog's own keyPressed() override is the ONE Escape route (page-aware —
    // it must route to onCancelRender, not onRequestClose, while a bounce is in flight; see its
    // own comment) — not juce::DialogWindow's default, which cannot tell the two pages apart and
    // would silently orphan a running render.
    options.escapeKeyTriggersCloseButton = false;
    auto* window = options.launchAsync();
    exportDialog_ = dialog;

    dialog->onRequestClose = [window] {
        if (window != nullptr)
            window->exitModalState(0);
    };
    dialog->onCancelRender = [this] {
        if (bounceRunner_ != nullptr)
            bounceRunner_->cancel();
    };
    dialog->onExport = [this, dialog](synth::BounceOptions bounceOptions, juce::File destination) {
        // Publish unconditionally right before rendering rather than gate on "was it ever
        // published": publishTimeline is cheap and always correct to re-call (see its own header
        // comment), and this makes a stale-binding bug impossible instead of merely detected.
        publishTimelineAndRebindRecorder();

        isBounceInProgress_ = true;
        dialog->showProgressPage();

        bounceRunner_ = std::make_unique<synth::BounceRunner>(audioEngine, destination, bounceOptions,
                                                              [this](synth::BounceResult result) {
                                                                  isBounceInProgress_ = false;
                                                                  bounceRunner_.reset();
                                                                  // exportDialog_ is a SafePointer: if the window was
                                                                  // somehow closed while the render was still going (a
                                                                  // bounce keeps running to completion regardless - it
                                                                  // is owned by MainComponent, not by the dialog), this
                                                                  // is simply null rather than dangling, and the two
                                                                  // lines above are still what matters: the flag clears
                                                                  // and the next Export Audio is not permanently locked
                                                                  // out.
                                                                  if (exportDialog_ != nullptr)
                                                                      exportDialog_->reportComplete(result);
                                                                  statusBar.showMessage(result.message);
                                                              });
    };

    // Genuinely modal, not just visible - New Patch/Open/Load preset/Quit refuse to run while
    // isBounceInProgress_ is true (see guardUnsavedChanges), but nothing stops the user from
    // reaching them if the window itself is merely floating. enterModalState's `deleteWhenDismissed`
    // means the window (and dialog) are freed once exitModalState() runs above.
    window->enterModalState(true, nullptr, true);
}

// The stem export flow (P9-8, docs/mixer.md §5.12) — the same options dialog as Export Audio,
// opened in its stems mode, driving a StemRunner instead of a BounceRunner. Mirrors
// promptExportAudio() above closely on purpose: same modal choreography, same isBounceInProgress_
// gate (shared across both — see its own comment), same progress polling in timerCallback().
void MainComponent::promptExportStems() {
    if (isBounceInProgress_)
        return; // the command is reported inactive while one is running - see getCommandInfo.

    // A patch with no mixer channels has nothing to export - tell the user why instead of opening
    // a dialog whose render would just fail with the same message once Export is pressed.
    if (!synth::StemExporter::hasChannelStrips(audioEngine)) {
        statusBar.showMessage(synth::StemExporter::kNoChannelsMessage);
        return;
    }

    const double arrangementEndBeat = timelineDoc.getArrangementEndBeat();
    const auto position = audioEngine.getTransport().getPositionSnapshot();
    const bool hasLoopRange = position.loopEndPpq > position.loopStartPpq;
    const bool projectIsSaved = currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_);

    auto* dialog = new synth::ui::ExportAudioDialog(
        arrangementEndBeat, hasLoopRange, position.loopStartPpq, position.loopEndPpq, position.bpm, projectIsSaved,
        resolveExportSubdirectory(currentBundleDir_, kExportsFolderName), currentPatchName_, /*stemsMode=*/true);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = dialog->getWindowTitle();
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = false;
    auto* window = options.launchAsync();
    exportDialog_ = dialog;

    dialog->onRequestClose = [window] {
        if (window != nullptr)
            window->exitModalState(0);
    };
    dialog->onCancelRender = [this] {
        if (stemRunner_ != nullptr)
            stemRunner_->cancel();
    };
    dialog->onExport = [this, dialog](synth::BounceOptions bounceOptions, juce::File destinationFolder) {
        publishTimelineAndRebindRecorder();

        isBounceInProgress_ = true;
        dialog->showProgressPage();

        stemRunner_ = std::make_unique<synth::StemRunner>(
            audioEngine, destinationFolder, bounceOptions, [this](synth::StemResult result) {
                isBounceInProgress_ = false;
                stemRunner_.reset();
                if (exportDialog_ != nullptr)
                    exportDialog_->reportComplete(result.ok, result.message);
                statusBar.showMessage(result.message);
            });
    };

    window->enterModalState(true, nullptr, true);
}
