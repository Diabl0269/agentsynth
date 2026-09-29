// MainComponent's test/automation hooks: every *ForTest / simulate* entry point runs the same code
// path the real button, dialog or callback does, minus the parts a headless test process cannot host
// (native file choosers, juce::PopupMenu, real timers). Declared in MainComponent.h.

#include "MainComponent.h"

void MainComponent::setUrlOpenerForTest(std::function<void(const juce::URL&)> opener) {
    urlOpener_ = std::move(opener);
}

// Consulted before any real focus check in resolveEditSurface(). See docs/development/test-patterns.md.
void MainComponent::setEditSurfaceOverrideForTest(std::optional<EditSurface> surface) {
    editSurfaceOverrideForTest_ = surface;
}

void MainComponent::simulateToggleAiPanelClick() {
    if (toggleAiPanelButton.onClick)
        toggleAiPanelButton.onClick();
}

void MainComponent::simulateToggleModMatrixClick() {
    if (toggleModMatrixButton.onClick)
        toggleModMatrixButton.onClick();
}

void MainComponent::simulateToggleMinimapClick() {
    if (toggleMinimapButton.onClick)
        toggleMinimapButton.onClick();
}

void MainComponent::simulateToggleLibraryClick() {
    if (toggleLibraryButton.onClick)
        toggleLibraryButton.onClick();
}

void MainComponent::simulateToggleBottomPanelClick() {
    if (toggleBottomPanelButton.onClick)
        toggleBottomPanelButton.onClick();
}

void MainComponent::simulateNewPatchClick() {
    if (newButton.onClick)
        newButton.onClick();
}

void MainComponent::simulateUndoClick() {
    if (undoButton.onClick)
        undoButton.onClick();
}

void MainComponent::simulateRedoClick() {
    if (redoButton.onClick)
        redoButton.onClick();
}

// Panel-slide seams (docs/layout/animation.md); the fractions ARE the layout.
float MainComponent::getPanelOpenProgressForTest(SlidingPanel p) const noexcept { return panelSlide(p).getProgress(); }

void MainComponent::setPanelOpenProgressForTest(SlidingPanel p, float progress) {
    panelSlide(p).snapTo(progress);
    resized();
}

// The fraction the in-flight tween STARTED from (never 0 or 1 mid-slide).
float MainComponent::getPanelSlideStartForTest(SlidingPanel p) const noexcept { return panelSlide(p).getTweenStart(); }

// True only while the shared slide driver is actually running.
bool MainComponent::isPanelSlideAnimatingForTest() const noexcept { return panelSlideAnim_.isRunning(); }

// juce::PopupMenu never runs in a test process: these drive the "+ Track" menu's own headless seam
// (TimelinePanelComponent::applyAddTrackMenuChoice) directly.
void MainComponent::simulateAddMidiTrackClick() {
    timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
}

void MainComponent::simulateAddAudioTrackClick() {
    timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
}

// Drives the Instrument submenu's headless seam directly, by menu id.
void MainComponent::simulateAddInstrumentTrackClick(int menuId) { timelinePanel.applyAddTrackMenuChoice(menuId); }

// Exactly what the Save dialog's callback runs: `.agsproj` writes a bundle, else a preset.
bool MainComponent::saveProjectForTest(const juce::File& file) { return saveToFile(file); }

// The post-guard half of New Patch only: bypasses guardUnsavedChanges (same idiom as
// saveProjectForTest bypassing the save chooser).
void MainComponent::newPatchForTest() { newPatch(); }

// Exactly what the Open dialog's callback runs: an `.agsproj` bundle directory loads graph +
// timeline, anything else a plain `.json` preset. A bundle carrying a pending autosave sidecar
// kicks off the async recovery prompt instead and returns true before any load has happened - a
// test drives autosaveRecoveryPrompt directly, the same idiom unsavedChangesPrompt uses.
bool MainComponent::openProjectForTest(const juce::File& file) { return openFromFile(file); }

// Reaches openFromFile's PATCH branch with an explicit load mode (`append == true` adds onto the
// live graph; false replaces it), without a native file chooser.
bool MainComponent::openPatchForTest(const juce::File& file, bool append) { return openFromFile(file, append); }

// Runs performAutosave()'s exact gate check once, synchronously: the same call timerCallback()
// makes on every tick, so a test can drive it without a real juce::Timer.
void MainComponent::runAutosaveTickForTest() { maybeAutosave(); }

// Back-dates the "last autosave" wall-clock baseline by `elapsedMs`, so a test can simulate the
// configured interval having elapsed without a real sleep. Computed relative to the CURRENT
// counter (not a fixed small value) so it is correct however large
// juce::Time::getMillisecondCounter() already is when the test runs.
void MainComponent::setAutosaveElapsedMsForTest(juce::uint32 elapsedMs) {
    lastAutosaveMs_ = juce::Time::getMillisecondCounter() - elapsedMs;
}

// True once an audio or MIDI take is capturing (see isRecordingActive()).
bool MainComponent::isRecordingActiveForTest() const { return isRecordingActive(); }

// Forces AudioTake::capturing without the real record-arm machinery, to verify the autosave gate
// respects this flag. Never commits a clip; the caller must reset it before the test ends.
void MainComponent::setAudioTakeCapturingForTest(bool capturing) { audioTake_.capturing = capturing; }

// What performSaveProject(false) will do next: true if there is no bundle to resave to silently,
// so Cmd+S is about to prompt for a location.
bool MainComponent::wouldPromptOnSaveForTest() const {
    return !(currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_));
}

// Exactly what the "Export Patch Only" chooser callback runs once a file is picked.
void MainComponent::exportPatchOnlyForTest(const juce::File& file) { exportPatchOnly(file); }

// Exactly what the production "Relink audio..." FileChooser callback runs.
void MainComponent::relinkClipAssetForTest(synth::ClipId id, const juce::File& chosenFile) {
    relinkClipAsset(id, chosenFile);
}

// What the clip lane area reports on an audio-file drop/chooser pick, bypassing the OS drag/dialog.
void MainComponent::importAudioFileToClipForTest(synth::TrackId track, double startBeat, const juce::File& sourceFile) {
    importAudioFileToClip(track, startBeat, sourceFile);
}

// Sweeps `<bundle>/Audio/` (+ `Peaks/`) for files no clip references and deletes them (see
// synth::AssetManager::cleanUnusedAssets). A no-op outside a saved bundle.
int MainComponent::cleanUnusedAssetsForTest() { return cleanUnusedAssets(); }

void MainComponent::loadPresetGuardedForTest(int index) { loadPresetGuarded(index); }

// hasTracksNeedingChannels() is a private TrackHeaderHost override, so a test cannot call it
// directly; this wrapper lets a test assert the "+ Track" menu's own enabled/disabled state.
bool MainComponent::hasTracksNeedingChannelsForTest() const { return hasTracksNeedingChannels(); }

// "Insert Track Preset from File..." has no real FileChooser in a headless test process: this
// drives insertTrackPresetFromFile() directly with an injected file. Returns the inserted track's
// name, or empty on rejection/failure (nothing added).
juce::String MainComponent::insertTrackPresetFromFileForTest(const juce::File& file) {
    return insertTrackPresetFromFile(file);
}
