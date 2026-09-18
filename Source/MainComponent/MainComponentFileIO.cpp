// MainComponentFileIO.cpp — MainComponent's save/open file I/O (.json patch and .agsproj
// bundle), autosave-recovery and patch-load-mode prompts, and the document-lifecycle helpers
// (dirty tracking, guardUnsavedChanges, autosave scheduling). MainComponent is declared in
// MainComponent.h; the rest of its implementation lives in the sibling MainComponent*.cpp units
// next to this one.
#include "MainComponent.h"
#include "MainComponentInternal.h"

#include "Branding.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"

namespace {

// Autosave preferences, read directly (no cached member — the 10 Hz timerCallback() cost of a
// juce::PropertiesFile lookup is negligible, and a direct read means Preferences can never go stale
// between a settings write and the next tick). Duplicated from PreferencesSettingsTab's own key
// constants, the same "one-line string not worth a header dependency" reasoning as
// kNaturalScrollingKey there. DEFAULT ON at 2 minutes: autosave is a safety net, not an opt-in.
constexpr const char* kAutosaveEnabledKey = "autosaveEnabled";
constexpr const char* kAutosaveIntervalMinutesKey = "autosaveIntervalMinutes";
constexpr int kDefaultAutosaveIntervalMinutes = 2;
// Cubase-style rotating backup history (see ProjectBundle::saveAutosave) - how many PREVIOUS
// sidecars are kept as numbered autosave-<n>.json files alongside the live autosave.json. 0
// disables rotation (plain overwrite); clamped to [0, 50] the same way the combo/slider limits it.
constexpr const char* kAutosaveBackupCountKey = "autosaveBackupCount";
constexpr int kDefaultAutosaveBackupCount = 5;

} // namespace

// ---- Save / open: one `.json` preset path, one `.agsproj` bundle path ----

// `file` is whatever the chooser returned; the .agsproj branch is what makes a bundle a bundle.
// Returns whether the save actually succeeded — guardUnsavedChanges' Save arm only continues
// past a save that returned true.
bool MainComponent::saveToFile(const juce::File& file) {
    statusBar.showMessage("Saving...");

    if (file.getFileExtension() == synth::ProjectBundle::kBundleExtension) {
        // Adopt any Recordings/-convention takes (recorded before this project had ever
        // been saved) into THIS bundle's own Audio/ BEFORE serialising below, so project.json is
        // written with the post-adoption refs — the reserved Recordings/ prefix must never end up
        // inside a saved bundle. A plain, direct doc mutation: saving must never create undo
        // history (see synth::AssetManager::adoptRecordingsAssets's own comment). Safe to call
        // every save, including a resave with nothing left to adopt (a no-op — see that method).
        const auto recordingsRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(synth::branding::kSettingsFolderName)
                                        .getChildFile(detail::kRecordingsFolderName);
        synth::AssetManager::adoptRecordingsAssets(timelineDoc, recordingsRoot, file);

        // The bundle carries the graph, timeline AND macros; PatchDocument comes from the graph
        // editor so the unknown-top-level-key stash a plain preset load filled is re-merged here too.
        const auto result =
            synth::ProjectBundle::save(file, audioEngine.getGraph(), timelineDoc, graphEditor.getPatchDocument(),
                                       graphEditor.getMacros(), midiRemoteDoc);
        if (!result.ok) {
            statusBar.showMessage("Save failed: " + result.message);
            return false;
        }
        // From here on this document IS a bundle, so the next take is written into it
        // (Audio/ + Peaks/) rather than into app data.
        currentBundleDir_ = file;
        refreshAssetRoots(); // Clip playback resolves against the bundle we just became
        // A fresh explicit save supersedes any pending autosave sidecar — project.json now carries
        // everything the sidecar would have offered to recover.
        synth::ProjectBundle::discardAutosave(file);
        recentProjects.addProject(file);
        saveRecentProjects();
        // Clear BEFORE setCurrentPatchName, which is what fires the title notify — the notify must
        // see the just-saved, clean state.
        markDocumentClean();
        setCurrentPatchName(file.getFileNameWithoutExtension());
        statusBar.showMessage("Saved: " + file.getFileNameWithoutExtension());
        return true;
    }

    // Plain preset save — byte-identical to what it has always written.
    graphEditor.savePreset(file);
    markDocumentClean();
    setCurrentPatchName(file.getFileNameWithoutExtension());
    statusBar.showMessage("Saved: " + file.getFileNameWithoutExtension());
    return true;
}

bool MainComponent::openFromFile(const juce::File& file, bool append) {
    statusBar.showMessage("Loading preset...");

    if (file.isDirectory() || file.getFileExtension() == synth::ProjectBundle::kBundleExtension) {
        if (!synth::ProjectBundle::isBundle(file)) {
            statusBar.showMessage("Not a project bundle: " + file.getFileName());
            return false;
        }

        // A prior session left a sidecar this bundle's project.json has never seen (autosave, or a
        // crash before the next explicit save) — ask BEFORE either file loads, rather than loading
        // project.json and silently discarding a possibly-newer autosave. Asynchronous, so this
        // reports "handled" rather than the eventual load's own success/failure; the only reader of
        // openFromFile's return today (openProjectForTest) is exercised exclusively by tests that
        // don't pre-seed a sidecar, so this branch changes nothing about any existing synchronous
        // assertion — new autosave-recovery tests drive autosaveRecoveryPrompt directly instead, the
        // same idiom promptUnsavedChanges's own tests already use.
        if (synth::ProjectBundle::hasAutosave(file)) {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            promptAutosaveRecovery([safeThis, file](AutosaveRecoveryChoice choice) {
                if (auto* self = safeThis.getComponent())
                    self->applyAutosaveRecoveryAnswer(choice, file);
            });
            return true;
        }

        return loadBundleFromFile(file);
    }

    ProgrammaticApplyScope guard(*this);
    graphEditor.loadPreset(file, append);
    reconcileTimelineAfterGraphChange();
    // A legacy patch is not a bundle, so the document that is now open has no bundle to resave to;
    // leaving the previous bundle's path installed would make the next Cmd+S overwrite a project
    // this patch was never part of, timeline included. (The .agsproj branch above already sets
    // currentBundleDir_ = file, so only this plain-preset tail needs the reset.)
    currentBundleDir_ = juce::File();
    refreshAssetRoots();
    markDocumentClean();
    setCurrentPatchName(file.getFileNameWithoutExtension());
    statusBar.showMessage("Loaded: " + file.getFileNameWithoutExtension());
    // T114/P8-10 + P8-31: covers the plain-.json patch load (the menu-only "Open Patch") that still
    // routes through this tail when the welcome screen is up. The "Open an existing project" button now
    // opens a .agsproj bundle, which returns through loadBundleFromFile/loadAutosaveFromFile instead,
    // each with its own call on its own success tail.
    hideWelcomeScreen();
    return true;
}

// The actual bundle load (graph + timeline from `<bundleDir>/project.json`), extracted out of
// openFromFile's bundle branch so the autosave-recovery continuation below can also reach it on
// the Discard arm without duplicating the load/reconcile/markDocumentClean sequence.
bool MainComponent::loadBundleFromFile(const juce::File& bundleDir) {
    ProgrammaticApplyScope guard(*this);
    // Detach BEFORE the load frees the current graph's processors — the same ordering
    // GraphEditor::loadPreset uses, and for the same reason (a live ScopeComponent timer would
    // otherwise read a freed VisualBuffer).
    graphEditor.detachAllModuleComponents();

    // The roots move to the new bundle BEFORE the load, not after it. ProjectBundle::load moves
    // the timeline into the live doc, and that fires timelineChanged synchronously — publishing
    // to the engine and the clip streamer while the load is still running. With the old roots
    // still installed, that publish resolves this bundle's clip refs against the PREVIOUS
    // bundle's Audio/ folder: the wrong file, or silence. Takes recorded from here on belong to
    // this bundle for the same reason.
    const juce::File previousBundleDir = currentBundleDir_;
    currentBundleDir_ = bundleDir;
    refreshAssetRoots();

    const auto result =
        synth::ProjectBundle::load(bundleDir, audioEngine.getGraph(), timelineDoc, graphEditor.getPatchDocument(),
                                   graphEditor.getMacros(), midiRemoteDoc);
    // Reconcile the view whatever happened: on failure the load left the graph exactly as it
    // was, and the components still have to come back after the detach above.
    graphEditor.updateComponents();
    if (!result.ok) {
        // load() is all-or-nothing, so a failure has to leave the previous project intact —
        // roots included, or the still-open document would start resolving its clips against a
        // bundle it was never part of.
        currentBundleDir_ = previousBundleDir;
        refreshAssetRoots();
        statusBar.showMessage("Load failed: " + result.message);
        return false;
    }

    // FRO127: ProjectBundle::load just replaced midiRemoteDoc wholesale — the engine's own copy
    // must follow before the reconcile below re-resolves targets against it.
    remoteEngine.setAssignments(midiRemoteDoc.assignments);
    // ProjectBundle::load already reconciled once; this republishes the freshly loaded document
    // (and rebinds the recorder) against the graph as it now stands.
    reconcileTimelineAfterGraphChange();
    markDocumentClean();
    recentProjects.addProject(bundleDir);
    saveRecentProjects();
    setCurrentPatchName(bundleDir.getFileNameWithoutExtension());
    statusBar.showMessage("Loaded: " + bundleDir.getFileNameWithoutExtension());
    // T114/P8-10: covers both the welcome screen's "Open an existing project" bundle path AND its
    // recent-project rows (both go through openFromFile -> here).
    hideWelcomeScreen();
    return true;
}

// The Restore arm: loads `<bundleDir>/autosave.json` in place of project.json and deliberately
// does NOT call markDocumentClean() — the loaded state is not what's on disk, so the document
// must read as dirty. isDirty_ is set true directly here, the one exception to "never write
// isDirty_ outside the recompute-from-serial path" (see markDocumentClean()'s comment): there is
// no undo action to derive dirtiness from, since this mutates the graph/timeline the same
// programmatic way ProjectBundle::load always has.
bool MainComponent::loadAutosaveFromFile(const juce::File& bundleDir) {
    ProgrammaticApplyScope guard(*this);
    graphEditor.detachAllModuleComponents();

    const juce::File previousBundleDir = currentBundleDir_;
    currentBundleDir_ = bundleDir;
    refreshAssetRoots();

    const auto result =
        synth::ProjectBundle::loadAutosave(bundleDir, audioEngine.getGraph(), timelineDoc,
                                           graphEditor.getPatchDocument(), graphEditor.getMacros(), midiRemoteDoc);
    graphEditor.updateComponents();
    if (!result.ok) {
        currentBundleDir_ = previousBundleDir;
        refreshAssetRoots();
        statusBar.showMessage("Recovery failed: " + result.message);
        return false;
    }

    // FRO127: loadAutosave just replaced midiRemoteDoc wholesale, same as loadBundleFromFile above.
    remoteEngine.setAssignments(midiRemoteDoc.assignments);
    reconcileTimelineAfterGraphChange();
    // Deliberately NOT markDocumentClean(): the recovered state is not what's on disk (project.json
    // still holds the older, last-explicitly-saved content), so the document must read as dirty —
    // see the header comment on loadAutosaveFromFile for why isDirty_ is written directly here
    // rather than through the usual recompute-from-serial path.
    isDirty_ = true;
    // Rebase both autosave baselines to this instant: the in-memory state now exactly matches what
    // the (about to be discarded) sidecar held, so nothing "new" exists to autosave yet — the next
    // autosave should only fire once the user edits further, same as right after an explicit save.
    lastAutosavedEditSerial_ = undoManager.getEditSerial();
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
    recentProjects.addProject(bundleDir);
    saveRecentProjects();
    // setCurrentPatchName() calls notifyDocumentTitleChanged() at its end and nowhere else in this
    // file (see that function's comment) — isDirty_ is set BEFORE this call so the notify's " *"
    // marker reflects the just-restored dirty state, not a stale one.
    setCurrentPatchName(bundleDir.getFileNameWithoutExtension());
    statusBar.showMessage("Recovered unsaved changes: " + bundleDir.getFileNameWithoutExtension());
    // T114/P8-10: the autosave-recovery Restore arm is one more way a recent-project row (or "Open
    // an existing project") can finish opening a bundle — see openFromFile's own comment.
    hideWelcomeScreen();
    return true;
}

// openFromFile's bundle branch, continued: reached either immediately (no sidecar) or from the
// async autosaveRecoveryPrompt's answer.
void MainComponent::applyAutosaveRecoveryAnswer(AutosaveRecoveryChoice choice, const juce::File& bundleDir) {
    if (choice == AutosaveRecoveryChoice::Restore) {
        // A corrupt/invalid sidecar must not strand the user on whatever was open before, nor
        // silently destroy the only copy of the data it held: on failure, fall back to the normal
        // load and keep the sidecar so the user isn't left with neither the restore nor the file.
        if (loadAutosaveFromFile(bundleDir))
            synth::ProjectBundle::discardAutosave(bundleDir);
        else
            loadBundleFromFile(bundleDir);
        return;
    }
    // Discard: the sidecar is stale/unwanted either way, so it goes before the normal load runs —
    // a load failure here must not leave a discarded-but-still-on-disk sidecar behind.
    synth::ProjectBundle::discardAutosave(bundleDir);
    loadBundleFromFile(bundleDir);
}

// The real dialog behind the has-autosave branch of openFromFile, same async/test-hook shape as
// promptUnsavedChanges below.
void MainComponent::promptAutosaveRecovery(std::function<void(AutosaveRecoveryChoice)> onChoice) {
    if (autosaveRecoveryPrompt) {
        autosaveRecoveryPrompt(std::move(onChoice));
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Recover Unsaved Changes")
                       .withMessage("An autosave from a previous session was found for this project. "
                                    "Restore it, or discard it and open the last saved version?")
                       .withButton("Restore")
                       .withButton("Discard");
    // ASYNC, never a modal loop — same reasoning as promptUnsavedChanges.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, onChoice](int result) {
        if (safeThis.getComponent() == nullptr)
            return;
        // juce::AlertWindow::showAsync's documented TWO-button result convention (see its header
        // comment — different from the three-button one promptUnsavedChanges uses): button[0]
        // ("Restore") returns 1, button[1] ("Discard") returns 0 — and a dismissed/closed window
        // ALSO returns 0, which lands on Discard here. That is deliberately the non-destructive arm:
        // project.json, the last known-good state, is what a dismissed prompt falls back to, never a
        // silent Restore the user never asked for.
        onChoice(result == 1 ? AutosaveRecoveryChoice::Restore : AutosaveRecoveryChoice::Discard);
    });
}

// P8-31: ask whether loading a patch should REPLACE the current one or ADD it on top of it.
// Mirrors promptUnsavedChanges' three-button async shape: the FIRST .withButton ("Add on top")
// returns 1, the next ("Replace") returns 2, and "Cancel" AND a dismissed/closed window return 0,
// so the non-destructive Cancel arm is the safe fallback for a keyboard-closed window.
// Ask whether to replace the current patch or add the loaded one on top of it (P8-31). Same
// async/test-hook shape as promptAutosaveRecovery; routes through the patchLoadPrompt seam when set.
void MainComponent::promptPatchLoadMode(std::function<void(PatchLoadMode)> onChoice) {
    if (patchLoadPrompt) {
        patchLoadPrompt(std::move(onChoice));
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Load Patch")
                       .withMessage("Replace the current patch, or add this one on top of it?")
                       .withButton("Add on top")
                       .withButton("Replace")
                       .withButton("Cancel");
    // ASYNC, never a modal loop - a headless run has no message loop to answer a real modal.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, onChoice](int result) {
        if (safeThis.getComponent() == nullptr)
            return;
        PatchLoadMode mode = PatchLoadMode::Cancel;
        if (result == 1)
            mode = PatchLoadMode::Append;
        else if (result == 2)
            mode = PatchLoadMode::Replace;
        onChoice(mode);
    });
}

// ---- Patch name (status bar) ----
// Update the displayed patch name (status bar). Immediate repaint, no timer delay.
void MainComponent::setCurrentPatchName(const juce::String& name) {
    currentPatchName_ = name;
    statusBar.repaint();
    notifyDocumentTitleChanged();
}

// Fires onDocumentTitleChanged with currentPatchName_ plus a " *" dirty marker. Called at the
// end of setCurrentPatchName() and nowhere else — every save/load/new-patch path already routes
// through it.
void MainComponent::notifyDocumentTitleChanged() {
    if (onDocumentTitleChanged)
        onDocumentTitleChanged(currentPatchName_ + (isDirty_ ? juce::String(" *") : juce::String()));
}

// The ONE way the document becomes clean — every save/load/new-document path calls this instead of
// writing isDirty_ directly. Capturing the undo manager's serial here is what makes the flag
// immune to the async change broadcast (see changeListenerCallback): the baseline is taken AFTER
// whatever edits the caller just made, so a notification still queued for those edits recomputes
// to "clean" rather than undoing this reset. Callers clear BEFORE setCurrentPatchName(), which is
// what fires the title notify — the notify has to see the settled state.
// The ONE way the document becomes clean: clears isDirty_ AND rebases savedEditSerial_ on the
// undo manager's current serial, which is what makes the reset survive an async change
// notification that was already queued when it ran. Never write isDirty_ = false directly.
// Also rebases autosave's OWN baseline (lastAutosavedEditSerial_/lastAutosaveMs_) to match: the
// document now matches what's on disk (an explicit save/load/new-patch), so there is nothing an
// autosave sidecar would capture beyond it, and resetting the elapsed-time baseline stops the
// very next qualifying tick from firing off a stale "elapsed since epoch" gap. This does NOT
// couple autosave to isDirty_/savedEditSerial_ in the other direction — maybeAutosave() never
// reads either of those, and performAutosave() never writes them.
void MainComponent::markDocumentClean() {
    savedEditSerial_ = undoManager.getEditSerial();
    isDirty_ = false;
    // See the header comment: autosave's own baseline rebases here too, both fields, so the very
    // next qualifying tick doesn't fire off zero real edits or a stale elapsed-time gap.
    lastAutosavedEditSerial_ = savedEditSerial_;
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
}

// True while an audio or MIDI take is actively capturing — checked by the autosave gate so it
// never fires mid-take (see docs/architecture.md). No public accessor for the underlying
// AudioTake/MidiRecorder state on purpose; go through isRecordingActiveForTest() in tests.
bool MainComponent::isRecordingActive() const { return audioTake_.capturing || midiRecorder.isRecording(); }

// The autosave gate, run once per timerCallback() tick (no second juce::Timer). Fires
// performAutosave() only when ALL of: enabled in preferences, a bundle is open, no take is
// recording, the undo edit serial has moved since the last autosave (NOT isDirty_/
// isProjectDirty() — see markDocumentClean()'s comment: isDirty_ is never cleared by autosave,
// so gating on it alone would rewrite the sidecar every interval forever with zero new edits),
// and the configured interval has elapsed. Also gates on isBounceInProgress_: a bounce now
// renders in chunks via BounceRunner, ticking a juce::Timer between chunks instead of blocking
// the message thread for the whole take (see Transport/BounceRunner.h), so timerCallback() DOES
// run mid-render and this check is what stops a sidecar write from firing into it.
void MainComponent::maybeAutosave() {
    if (!synth::ProjectBundle::isBundle(currentBundleDir_))
        return; // an unsaved project has no bundle to put a sidecar in — inert until first save.
    if (isRecordingActive())
        return;
    if (isBounceInProgress_)
        return; // see isBounceInProgress_'s comment - a bounce is chunked over timer ticks now.

    auto* settings = appProperties.getUserSettings();
    const bool enabled = settings == nullptr || settings->getBoolValue(kAutosaveEnabledKey, true);
    if (!enabled)
        return;

    if (undoManager.getEditSerial() == lastAutosavedEditSerial_)
        return; // nothing new since the last autosave (or the last explicit save/load).

    const int intervalMinutes =
        settings == nullptr ? kDefaultAutosaveIntervalMinutes
                            : settings->getIntValue(kAutosaveIntervalMinutesKey, kDefaultAutosaveIntervalMinutes);
    const juce::uint32 intervalMs = (juce::uint32)juce::jmax(1, intervalMinutes) * 60000u;
    if (juce::Time::getMillisecondCounter() - lastAutosaveMs_ < intervalMs)
        return;

    performAutosave();
    // Bumped regardless of performAutosave()'s outcome: a persistently failing write (disk full,
    // permissions) must not retry every single tick, only every interval.
    lastAutosaveMs_ = juce::Time::getMillisecondCounter();
}

// Writes the sidecar via ProjectBundle::saveAutosave and, only on success, rebases
// lastAutosavedEditSerial_. Never calls markDocumentClean() — isDirty_/savedEditSerial_ and
// project.json itself are untouched by autosave.
void MainComponent::performAutosave() {
    auto* settings = appProperties.getUserSettings();
    const int backupCount =
        settings == nullptr
            ? kDefaultAutosaveBackupCount
            : juce::jlimit(0, 50, settings->getIntValue(kAutosaveBackupCountKey, kDefaultAutosaveBackupCount));
    const auto result = synth::ProjectBundle::saveAutosave(currentBundleDir_, audioEngine.getGraph(), timelineDoc,
                                                           graphEditor.getPatchDocument(), graphEditor.getMacros(),
                                                           backupCount, midiRemoteDoc);
    if (result.ok)
        lastAutosavedEditSerial_ = undoManager.getEditSerial();
    else
        statusBar.showMessage("Autosave failed: " + result.message);
}

// ---- Unsaved-changes guard ----

/** THE gate every document-replacing action goes through: runs `proceed` straight away on a clean
 *  document, otherwise asks first and runs it only on Save (successful) or Discard. Asynchronous by
 *  nature - the caller must treat `proceed` as "maybe later, maybe never" and must not do the
 *  destructive work itself. */
void MainComponent::guardUnsavedChanges(const juce::String& actionLabel, std::function<void()> proceed) {
    if (!proceed)
        return;
    if (isBounceInProgress_) {
        // New Patch/Open/Load preset/Quit all fund through here - none of them may mutate or
        // replace the graph while BounceRunner's offline driver owns it. Refuse rather than queue:
        // the export's own progress window is modal, so the user cannot even reach this path
        // without first cancelling or waiting for it to finish.
        statusBar.showMessage(actionLabel + " must wait for the export to finish, or cancel it first.");
        return;
    }

    // FRO42 review fix: wrapped ONCE here so every arm below that actually goes ahead (not-dirty,
    // Discard, Save-succeeded) bumps documentGeneration_ right before replacing the document — never
    // on Cancel, a failed Save, or a cancelled Save chooser, all of which return without calling
    // `proceed` at all. See documentGeneration_'s own comment for what reads this.
    std::function<void()> proceedAndBumpGeneration = [this, proceed] {
        ++documentGeneration_;
        proceed();
    };

    if (!isDirty_) {
        proceedAndBumpGeneration();
        return;
    }
    promptUnsavedChanges(actionLabel, [this, proceedAndBumpGeneration](UnsavedChangesChoice choice) {
        applyUnsavedChangesAnswer(choice, proceedAndBumpGeneration);
    });
}

// The real dialog behind guardUnsavedChanges, split from applyUnsavedChangesAnswer for exactly
// the reason PianoRollComponent::promptExtendClipToFitNotes is split from
// applyExtendPromptAnswer: a headless test has no message loop to answer a real AlertWindow with,
// so the ANSWER logic has to be reachable without one. Async (never a modal loop) and
// SafePointer-guarded — the answer can arrive after this component is gone.
void MainComponent::promptUnsavedChanges(const juce::String& actionLabel,
                                         std::function<void(UnsavedChangesChoice)> onChoice) {
    if (unsavedChangesPrompt) {
        unsavedChangesPrompt(actionLabel, std::move(onChoice));
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Unsaved Changes")
                       .withMessage(actionLabel + " will discard unsaved changes to \"" + currentPatchName_ + "\".")
                       .withButton("Save")
                       .withButton("Discard")
                       .withButton("Cancel");
    // ASYNC, never a modal loop — same reasoning as PianoRollComponent::promptExtendClipToFitNotes.
    // SafePointer because the answer can arrive after this component is gone (Quit's own
    // continuation destroys it).
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, onChoice](int result) {
        // onChoice (via guardUnsavedChanges) closes over `this`, so the guard below is what keeps
        // a dismissed component from having a member function invoked on it after destruction —
        // the same SafePointer role PianoRollComponent::promptExtendClipToFitNotes's callback plays.
        if (safeThis.getComponent() == nullptr)
            return;
        // Verified against JUCE's own showAsync plumbing (build/_deps/juce-src/modules/
        // juce_gui_basics/lookandfeel/juce_LookAndFeel_V2.cpp, createAlertWindow's 3-button branch):
        // button 1 (the FIRST .withButton, "Save") returns 1, button 2 ("Discard") returns 2, button
        // 3 ("Cancel") returns 0 — and 0 is also what a dismissed/closed window returns, so anything
        // but 1 or 2 has to fall back to Cancel rather than a destructive arm.
        UnsavedChangesChoice choice = UnsavedChangesChoice::Cancel;
        if (result == 1)
            choice = UnsavedChangesChoice::Save;
        else if (result == 2)
            choice = UnsavedChangesChoice::Discard;
        onChoice(choice);
    });
}

// What each arm of the dialog DOES. `proceed` is invoked LAST in every arm that continues, so a
// continuation that destroys this component (Quit does exactly that) can never return into a
// method that still touches members.
void MainComponent::applyUnsavedChangesAnswer(UnsavedChangesChoice choice, std::function<void()> proceed) {
    switch (choice) {
    case UnsavedChangesChoice::Cancel:
        return;
    case UnsavedChangesChoice::Discard:
        // The user just explicitly said to throw these changes away — a pending autosave sidecar
        // holds exactly that same discarded content, so leaving it behind would prompt to "recover"
        // it again next time this bundle is opened. discardAutosave() is a safe no-op when
        // currentBundleDir_ is empty (never-yet-saved project) or has no sidecar.
        synth::ProjectBundle::discardAutosave(currentBundleDir_);
        proceed();
        return;
    case UnsavedChangesChoice::Save:
        // A failed save (or a cancelled chooser, which reports saved=false) must not continue —
        // the whole point of the guard is that Save only counts once it actually happened.
        performSaveProject(false, [proceed](bool saved) {
            if (saved)
                proceed();
        });
        return;
    }
}
