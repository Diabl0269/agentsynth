// Concern: patch-name/dirty-state tracking and the save/load/export round trip (project bundle,
// factory presets, legacy .json patches).
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <set>

TEST_F(MainComponentTest, PatchNameIsDefaultOnStartup) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_EQ(mc.getCurrentPatchName(), "Default");
}

// the dirty-title wiring: startup itself must never leave the document marked dirty. Nothing in
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
// Cmd+S saves the whole project (bundle, not patch-only json) and remembers the file.
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

// A plain .json preset (the legacy default, and still what Export Patch Only writes) must still
// open correctly — openFromFile's non-bundle branch must stay independent of the bundle save path, since a change to
// the save-side default could otherwise break it by omission.
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

// ---------------------------------------------------------------------------
// The patch dialogs (Open Patch / Export Patch Only) start where the "Patch save location"
// preference says. The shared-folder fallback is covered by PatchSaveLocationTests (it would create
// a folder under the real Music directory here).
// ---------------------------------------------------------------------------

TEST_F(MainComponentTest, PatchDialogStartsInTheOpenProjectsPatchesFolderByDefault) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    const auto bundle = tempRoot.getChildFile("PatchDir.agsproj");
    mc.saveProjectForTest(bundle);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));

    EXPECT_EQ(mc.patchDialogDirectoryForTest(), bundle.getChildFile("Patches"));
}

TEST_F(MainComponentTest, PatchDialogStartsInTheCustomFolderWhenChosen) {
    MainComponent mc(std::make_unique<MockProvider>());
    const auto mine = tempRoot.getChildFile("MyPatches");
    mine.createDirectory();
    auto* settings = mc.getAppPropertiesForTest().getUserSettings();
    settings->setValue("patchSaveMode", "custom");
    settings->setValue("patchSaveCustomDir", mine.getFullPathName());

    EXPECT_EQ(mc.patchDialogDirectoryForTest(), mine);
    settings->removeValue("patchSaveMode");
    settings->removeValue("patchSaveCustomDir");
}

// An LFO inside a macro used to crash (or scramble the layout) when its project was reopened: the card's
// constructor asked for "room" while the canvas was still being built. Saved, reopened, everything must
// be exactly where it was.
TEST_F(MainComponentTest, SavedProjectWithAnLfoInsideAMacroReopensAsSaved) {
    struct Placed {
        juce::String uuid;
        juce::Point<int> pos;
    };
    const auto snapshot = [](MainComponent& c) {
        std::vector<Placed> out;
        for (auto* node : c.getAudioEngine().getGraph().getNodes())
            out.push_back(
                {node->properties["uuid"].toString(),
                 {(int)node->properties.getWithDefault("x", -1), (int)node->properties.getWithDefault("y", -1)}});
        return out;
    };

    const auto bundleDir = tempRoot.getChildFile("LfoInMacro.agsproj");
    std::vector<Placed> saved;
    juce::String macroId;
    size_t nodeCount = 0;
    int cardCount = 0;
    {
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        auto& editor = mc.getGraphEditor();
        auto& graph = mc.getAudioEngine().getGraph();

        std::set<juce::AudioProcessorGraph::NodeID> before;
        for (auto* n : graph.getNodes())
            before.insert(n->nodeID);
        // The LFO goes in last: nodes load in creation order, so its card is built after its siblings' exist.
        editor.addModuleAtCanvasPosition("Oscillator", {1440, 1040}, {});
        editor.addModuleAtCanvasPosition("Oscillator", {1700, 1000}, {});
        editor.addModuleAtCanvasPosition("LFO", {1400, 1000}, {});
        std::vector<juce::AudioProcessorGraph::NodeID> added;
        for (auto* n : graph.getNodes())
            if (before.count(n->nodeID) == 0)
                added.push_back(n->nodeID);
        ASSERT_EQ(added.size(), 3u);

        editor.setSelectedNodes({added[0], added[2]}); // first oscillator + the LFO
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        ASSERT_FALSE(macroId.isEmpty());
        editor.getMacroController().setMacroCollapsed(macroId, false);

        // Park the loose oscillator on the hull's edge (a saved layout can hold such an overlap), so a stray
        // "make room" pass during the reload has something to push.
        const auto hull = editor.getMacroController().macroHullBounds(macroId);
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == added[1]) {
                comp->setTopLeftPosition(hull.getRight() - 20, hull.getY() + 8);
                graph.getNodeForId(added[1])->properties.set("x", comp->getX());
                graph.getNodeForId(added[1])->properties.set("y", comp->getY());
            }

        saved = snapshot(mc);
        nodeCount = saved.size();
        cardCount = editor.getModuleComponents().size();
        mc.saveProjectForTest(bundleDir);
        ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));
        editor.detachAllModuleComponents();
    }

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));

    auto& editor = reloaded.getGraphEditor();
    const auto* macro = editor.getMacros().getAll().empty() ? nullptr : &editor.getMacros().getAll().front();
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->members.size(), 2u);
    EXPECT_EQ(reloaded.getAudioEngine().getGraph().getNodes().size(), (int)nodeCount);

    for (auto* node : reloaded.getAudioEngine().getGraph().getNodes()) {
        int cards = 0;
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->getNodeId() == node->nodeID) {
                ++cards;
                const auto uuid = node->properties["uuid"].toString();
                for (const auto& p : saved)
                    if (p.uuid == uuid)
                        EXPECT_EQ(comp->getPosition(), p.pos) << "card moved by the reload: " << uuid;
            }
        EXPECT_LE(cards, 1);
    }
    EXPECT_EQ(editor.getModuleComponents().size(), cardCount);
    editor.detachAllModuleComponents();
}
