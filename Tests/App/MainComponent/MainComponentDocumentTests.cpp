// Concern: patch-name/dirty-state tracking and the save/load/export round trip (project bundle,
// factory presets, legacy .json patches).
#include "MainComponentTestFixture.h"

TEST_F(MainComponentTest, PatchNameIsDefaultOnStartup) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_EQ(mc.getCurrentPatchName(), "Default");
}

// P8-1's dirty-title wiring: startup itself must never leave the document marked dirty. Nothing in
// construction (loading the default preset, applying the dual-IO preference, restoring
// preferences) goes through AppUndoManager, so canUndo() — and therefore isDirty_ — must both be
// false the instant construction finishes. A regression here would show up as a stray " *" in the
// title bar of a freshly launched, untouched app.
TEST_F(MainComponentTest, FreshlyConstructedDocumentIsNotDirty) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_FALSE(mc.isProjectDirty());
}

// juce::UndoManager (unlike ShortcutManager's own ChangeBroadcaster) notifies via the ASYNC
// sendChangeMessage(), not sendSynchronousChangeMessage() — so isDirty_ only flips once the message
// loop actually runs a dispatch pass, hence the runDispatchLoopUntil() pump (the same idiom
// AIChatComponentTests.cpp/AccountServiceTests.cpp use for other async JUCE notifications).
TEST_F(MainComponentTest, DirtyFlagTracksARealUndoStepThenClearsOnSave) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_FALSE(mc.isProjectDirty());

    mc.simulateAddMidiTrackClick();
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_TRUE(mc.isProjectDirty());

    mc.saveProjectForTest(tempRoot.getChildFile("DirtyFlag.agsproj"));
    EXPECT_FALSE(mc.isProjectDirty()) << "a successful save must clear the dirty flag";
}

TEST_F(MainComponentTest, PatchNameUpdatesOnFactoryPresetLoad) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto presets = synth::PresetManager::getPresetList();
    ASSERT_GE(presets.size(), 2);

    // simulateLoadFactoryPresetForTest mirrors the loadButton popup call site exactly:
    // loadFactoryPreset(index) + setCurrentPatchName(presets[index].name).
    mc.simulateLoadFactoryPresetForTest(1);
    EXPECT_EQ(mc.getCurrentPatchName(), presets[1].name);
}

// ---------------------------------------------------------------------------
// P8-1: Cmd+S saves the whole project (bundle, not patch-only json) and remembers the file.
// ---------------------------------------------------------------------------

// A freshly constructed document has never been saved, so there is no bundle to resave to
// silently — Cmd+S is about to prompt. This is the regression's root cause, pinned directly:
// before this fix, saveButton.onClick ALWAYS launched a chooser regardless of this state.
TEST_F(MainComponentTest, SaveWithNoCurrentBundlePromptsForLocation) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.wouldPromptOnSaveForTest());
}

// Once a project has been saved as a bundle, Cmd+S must resave to that SAME path silently — no
// chooser. Asserting the predicate alone (rather than driving the save button/command) is
// deliberate: performSaveProject's chooser-launching branch is only reached when this predicate is
// true, so proving it is false here is what proves the button's onClick can never reach
// fileChooser->launchAsync for this document — exactly the property a headless test can check
// without ever risking a real native dialog.
TEST_F(MainComponentTest, SaveWithCurrentBundleResavesSilently) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto bundleDir = tempRoot.getChildFile("Resave.agsproj");
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    EXPECT_FALSE(mc.wouldPromptOnSaveForTest());
}

// THE regression test: a project with a timeline track, saved via Cmd+S's own file handler, must
// come back with that track intact when reopened — this is exactly what broke when Cmd+S always
// wrote a patch-only .json (which carries no "timeline" key at all).
TEST_F(MainComponentTest, SavedThenReloadedProjectRetainsTimeline) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    mc.simulateAddMidiTrackClick();
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);

    const auto bundleDir = tempRoot.getChildFile("RoundTrip.agsproj");
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();

    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded.getTimelineDoc().getTracks().size(), 1u);
}

// Export Patch Only must write BYTE-IDENTICAL output to the legacy plain-.json save path — both
// go through GraphEditor::savePreset under the hood, and the whole point of keeping this escape
// hatch is that it is exactly the old behaviour, not a reimplementation of it.
TEST_F(MainComponentTest, ExportPatchOnlyWritesByteIdenticalLegacyJson) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto exported = tempRoot.getChildFile("exported.json");
    const auto legacy = tempRoot.getChildFile("legacy.json");
    mc.exportPatchOnlyForTest(exported);
    mc.saveProjectForTest(legacy);

    ASSERT_TRUE(exported.existsAsFile());
    ASSERT_TRUE(legacy.existsAsFile());
    // Raw text, not a parsed-var comparison: juce::var's equality for an object/array is REFERENCE
    // identity (see VariantType::objectEquals), so two independently parsed vars would never
    // compare equal even for byte-identical JSON. The patch serialiser writes no timestamps or
    // fresh random ids on save, so the raw text from two back-to-back saves of the same graph is
    // expected to match exactly.
    EXPECT_EQ(exported.loadFileAsString(), legacy.loadFileAsString());
}

// A plain .json preset (the pre-P8-1 default, and still what Export Patch Only writes) must still
// open correctly — openFromFile's non-bundle branch is untouched by this ticket, but the save-side
// default changing is exactly the kind of change that could have silently broken it by omission.
TEST_F(MainComponentTest, OpeningLegacyJsonPresetStillWorks) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto jsonFile = tempRoot.getChildFile("Legacy.json");
    mc.saveProjectForTest(jsonFile);
    ASSERT_TRUE(jsonFile.existsAsFile());

    EXPECT_TRUE(mc.openProjectForTest(jsonFile));
    EXPECT_EQ(mc.getCurrentPatchName(), "Legacy");
}
