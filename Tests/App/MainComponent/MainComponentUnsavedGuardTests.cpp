// Concern: P8-2 -- guardUnsavedChanges(), the async dialog-fronted gate on New Patch/Open/the
// factory-preset Load branch. See the safety-rule comment below for why every test here must
// install mc.unsavedChangesPrompt before touching a dirty document.
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"

// ---------------------------------------------------------------------------
// P8-2: dirty-state tracking and the unsaved-changes guard.
//
// SAFETY RULE FOR EVERY TEST BELOW: never let a real dialog open, because a headless run has no
// message loop to answer one with and the test would hang forever.
//   * Install mc.unsavedChangesPrompt BEFORE anything can reach the guard on a DIRTY document —
//     an unset seam means a real juce::AlertWindow.
//   * Never answer Discard (or Save) to a prompt raised by the OPEN path: continuing there calls
//     launchOpenPresetChooser(), which opens a real native FileChooser.
//   * Never answer Save unless a bundle is already open (wouldPromptOnSaveForTest() == false), or
//     performSaveProject's chooser branch opens a real native save dialog.
// ---------------------------------------------------------------------------

namespace {

/** Stands in for the unsaved-changes dialog: records what the guard asked, and answers only if the
 *  test explicitly supplied an answer. Leaving `answer` empty models "the dialog is still up", which
 *  is what lets a test assert the document is untouched WHILE the question is outstanding. */
struct PromptRecorder {
    int calls = 0;
    juce::String lastLabel;
    std::optional<MainComponent::UnsavedChangesChoice> answer;

    void installOn(MainComponent& mc) {
        mc.unsavedChangesPrompt = [this](const juce::String& label,
                                         std::function<void(MainComponent::UnsavedChangesChoice)> onChoice) {
            ++calls;
            lastLabel = label;
            if (answer.has_value())
                onChoice(*answer);
        };
    }
};

/** The dirty flag moves on juce::UndoManager's ASYNC change broadcast, so every assertion about it
 *  has to follow a dispatch pass — same idiom as DirtyFlagTracksARealUndoStepThenClearsOnSave. */
void pumpMessageLoop() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

/** One edit that dirties the document, with the pump already folded in. */
void makeDirty(MainComponent& mc) {
    mc.simulateAddMidiTrackClick();
    pumpMessageLoop();
}

} // namespace

// A clean document must never be interrogated — the guard's whole job is to be invisible until
// there is actually something to lose.
TEST_F(MainComponentTest, NewPatchOnACleanDocumentDoesNotPrompt) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.installOn(mc);
    ASSERT_FALSE(mc.isProjectDirty());

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_EQ(prompt.calls, 0) << "a clean document has nothing to discard, so nothing to ask about";
    EXPECT_EQ(mc.getCurrentPatchName(), "Untitled") << "and the action must have run straight through";
}

// THE headline test for this ticket. The guard is ASYNCHRONOUS, so the destructive work must not
// have happened yet while the question is still outstanding — a guard that cleared the canvas and
// then asked would be worse than no guard at all.
TEST_F(MainComponentTest, NewPatchOnADirtyDocumentPromptsBeforeClearing) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt; // no answer supplied: the dialog stays "open"
    prompt.installOn(mc);

    makeDirty(mc);
    ASSERT_TRUE(mc.isProjectDirty());

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(prompt.lastLabel, "New Patch");
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u)
        << "the document must still be intact while the question is unanswered";
}

TEST_F(MainComponentTest, CancellingTheUnsavedChangesPromptKeepsTheDocument) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.answer = MainComponent::UnsavedChangesChoice::Cancel;
    prompt.installOn(mc);

    makeDirty(mc);
    const auto nameBefore = mc.getCurrentPatchName();

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u) << "Cancel means the action never happened";
    EXPECT_TRUE(mc.isProjectDirty()) << "and the document is still unsaved";
    EXPECT_EQ(mc.getCurrentPatchName(), nameBefore);
}

TEST_F(MainComponentTest, DiscardingUnsavedChangesRunsTheAction) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.answer = MainComponent::UnsavedChangesChoice::Discard;
    prompt.installOn(mc);

    makeDirty(mc);

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_TRUE(mc.getTimelineDoc().getTracks().empty()) << "Discard means the action ran";
    EXPECT_EQ(mc.getCurrentPatchName(), "Untitled");
}

// The Save arm must WRITE before it discards, not merely intend to. Answering Save is only safe
// here because a bundle is already open, so performSaveProject resaves silently instead of opening
// a real chooser (see the safety rule at the top of this section).
TEST_F(MainComponentTest, SavingFromThePromptWritesTheProjectThenRunsTheAction) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    makeDirty(mc);
    const auto bundleDir = tempRoot.getChildFile("GuardSave.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));
    ASSERT_FALSE(mc.wouldPromptOnSaveForTest()) << "a silent resave is what makes answering Save safe";

    // A second edit AFTER the save, so what the Save arm writes is strictly newer than the file.
    mc.simulateAddMidiTrackClick();
    pumpMessageLoop();
    ASSERT_TRUE(mc.isProjectDirty());
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 2u);

    PromptRecorder prompt;
    prompt.answer = MainComponent::UnsavedChangesChoice::Save;
    prompt.installOn(mc);

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_TRUE(mc.getTimelineDoc().getTracks().empty()) << "Save must continue into the action once it succeeded";

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded.getTimelineDoc().getTracks().size(), 2u)
        << "the Save arm must have written the SECOND edit before discarding it";
}

// The guard must run before the file chooser opens, not after the user has already picked a file.
// NOTE: this prompt is deliberately left UNANSWERED — answering Discard here would call
// launchOpenPresetChooser() and open a real native dialog, hanging the run.

TEST_F(MainComponentTest, LoadingAPatchAppendsOnTopWhileReplacingDiscards) {
    MainComponent source(std::make_unique<MockProvider>());
    source.setSize(1600, 900);
    source.getAudioEngine().suspendDeviceCallback();
    auto& srcEditor = source.getGraphEditor();
    srcEditor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("VCA"), &srcEditor, {300, 300}));
    ASSERT_GT(source.getAudioEngine().getGraph().getNumNodes(), 0);
    const auto patchFile = tempRoot.getChildFile("AppendSource.json");
    source.saveProjectForTest(patchFile);
    ASSERT_TRUE(patchFile.existsAsFile());
    const int sourceNodes = source.getAudioEngine().getGraph().getNumNodes();

    // APPEND: the loaded VCA lives alongside a pre-existing Oscillator.
    {
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        auto& editor = mc.getGraphEditor();
        editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {100, 100}));
        const int withSeed = mc.getAudioEngine().getGraph().getNumNodes();
        ASSERT_GT(withSeed, 0);
        EXPECT_TRUE(mc.openPatchForTest(patchFile, /*append=*/true));
        EXPECT_GT(mc.getAudioEngine().getGraph().getNumNodes(), withSeed)
            << "adding on top must keep the seed and add the loaded module, not replace it";
    }

    // REPLACE: the loaded patch discards the seed, leaving exactly its own modules.
    {
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        auto& editor = mc.getGraphEditor();
        editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {100, 100}));
        EXPECT_TRUE(mc.openPatchForTest(patchFile, /*append=*/false));
        EXPECT_EQ(mc.getAudioEngine().getGraph().getNumNodes(), sourceNodes)
            << "replacing must load exactly the source patch, discarding the seed";
    }
}

TEST_F(MainComponentTest, LoadingAPatchFitsTheNewlyLoadedModulesIntoView) {
    MainComponent source(std::make_unique<MockProvider>());
    source.setSize(1600, 900);
    source.getAudioEngine().suspendDeviceCallback();
    auto& srcEditor = source.getGraphEditor();
    srcEditor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("VCA"), &srcEditor, {300, 300}));
    srcEditor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("LFO"), &srcEditor, {700, 700}));
    const auto patchFile = tempRoot.getChildFile("FitSource.json");
    source.saveProjectForTest(patchFile);
    ASSERT_TRUE(patchFile.existsAsFile());

    // A narrow view the loaded patch's saved layout cannot fill the way it was arranged on screen.
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(500, 300);
    mc.getAudioEngine().suspendDeviceCallback();

    EXPECT_TRUE(mc.openPatchForTest(patchFile, /*append=*/false));
    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_GT(graph.getNumNodes(), 0);

    // Every module must now fall within the editor's visible canvas rect: fitViewToModules scrolls +
    // zooms to show them all rather than leaving the loaded patch off-screen.
    auto model = mc.getGraphEditor().buildMinimapModel();
    ASSERT_GE(model.nodes.size(), 2u);
    for (const auto& n : model.nodes)
        EXPECT_TRUE(model.viewport.contains(n.bounds))
            << "the loaded patch must be fit into the visible canvas, not left off-screen";
}

// P8-31: the WHOLE-PROJECT half of the split Load menu guards under "Opening a project", routing
// through openProjectFromFile() just as the patch path routes through openPresetFromFile().
TEST_F(MainComponentTest, OpeningAProjectAsksBeforeTheChooserOpens) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.installOn(mc);

    makeDirty(mc);

    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::openProject, false));

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(prompt.lastLabel, "Opening a project");
}

// P8-31 / bug #2: importing a patch onto an existing one (the "Add on top" arm) must be one reversible
// undo step - undo returns to the pre-import patch, redo restores the imported modules. An unwrapped
// clear-and-rebuild left the history unaware of the swap, so a following redo had nothing to restore.
TEST_F(MainComponentTest, AddingAPatchOnTopIsOneReversibleUndoStep) {
    MainComponent source(std::make_unique<MockProvider>());
    source.setSize(1600, 900);
    source.getAudioEngine().suspendDeviceCallback();
    auto& srcEditor = source.getGraphEditor();
    srcEditor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("VCA"), &srcEditor, {300, 300}));
    srcEditor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("LFO"), &srcEditor, {700, 700}));
    const auto patchFile = tempRoot.getChildFile("UndoSource.json");
    source.saveProjectForTest(patchFile);
    ASSERT_TRUE(patchFile.existsAsFile());
    const int sourceNodes = source.getAudioEngine().getGraph().getNumNodes();

    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& um = mc.getUndoManager();

    // Seed a module, then add the loaded patch on top of it.
    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {100, 100}));
    const int seedCount = graph.getNumNodes();
    ASSERT_GT(seedCount, 0);
    const int preSerial = um.getEditSerial();

    EXPECT_TRUE(mc.openPatchForTest(patchFile, /*append=*/true));
    const int afterAppend = graph.getNumNodes();
    // The append must ADD the source patch on top of the seed, not replace it.
    EXPECT_GT(afterAppend, seedCount) << "append must add the imported modules on top, not overwrite the current patch";
    EXPECT_GT(um.getEditSerial(), preSerial) << "the import must register as an undo step";

    // Undo returns to the pre-import patch (just the seed).
    EXPECT_TRUE(um.undo());
    EXPECT_EQ(graph.getNumNodes(), seedCount) << "undoing the import must return the graph to its pre-import state";

    // Redo restores the imported modules - the direction that broke when the swap went undrawn.
    EXPECT_TRUE(um.canRedo());
    EXPECT_TRUE(um.redo());
    EXPECT_EQ(graph.getNumNodes(), afterAppend) << "redoing must restore the imported patch that undo removed";
}

// P8-31: the mode prompt that openPresetFromFile reaches after a file is chosen. Headless runs have
// no message loop to answer a real AlertWindow, so the test drives patchLoadPrompt straight and asserts
// it routes to the chosen load mode - Cancel, Append and Replace each must reach the callback.
// The mechanism Main.cpp's quit path uses, exercised directly: AppApplication itself is not
// constructible in a headless run, so this is what stands in for the quit test.
TEST_F(MainComponentTest, TheGuardRunsACleanDocumentsActionImmediately) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.installOn(mc);
    ASSERT_FALSE(mc.isProjectDirty());

    bool ran = false;
    mc.guardUnsavedChanges("Quitting", [&ran] { ran = true; });

    EXPECT_TRUE(ran) << "a clean document's action runs synchronously, with no dialog in the way";
    EXPECT_EQ(prompt.calls, 0);
}

// REGRESSION LOCK. New Patch clears the timeline and the graph as two REAL undoable steps, and
// juce::UndoManager's change broadcast is async — so the notification for those steps is still
// queued when the document is reset. Before the edit-serial fix, that queued notification arrived
// on the next dispatch pass and re-dirtied a brand-new "Untitled" document; with the guard in
// place, that would then have interrogated the user about discarding a patch they had just
// created. The serial assertion is what proves the broadcast was really queued and neutralised,
// rather than never having been queued at all.
TEST_F(MainComponentTest, NewPatchLeavesTheDocumentClean) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    PromptRecorder prompt;
    prompt.installOn(mc);

    const int serialBefore = mc.getUndoManager().getEditSerial();
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false));
    ASSERT_EQ(prompt.calls, 0) << "precondition: this ran unguarded, on a clean document";
    EXPECT_GT(mc.getUndoManager().getEditSerial(), serialBefore)
        << "New Patch really does record undoable steps - so a change broadcast really was queued";

    pumpMessageLoop();
    EXPECT_FALSE(mc.isProjectDirty()) << "a freshly created document must not come back dirty";
}

// Loading is the other half of "the document now matches something on disk". The second pump is
// the point: a queued async notification must not resurrect the flag after the load cleared it.
TEST_F(MainComponentTest, LoadingAProjectClearsTheDirtyFlag) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto bundleDir = tempRoot.getChildFile("LoadClears.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    makeDirty(mc);
    ASSERT_TRUE(mc.isProjectDirty());

    ASSERT_TRUE(mc.openProjectForTest(bundleDir));
    EXPECT_FALSE(mc.isProjectDirty());

    pumpMessageLoop();
    EXPECT_FALSE(mc.isProjectDirty()) << "a stale undo notification must not resurrect the flag";
}

// DOCUMENTS THE CHOSEN SEMANTICS (docs/architecture/project-bundle.md#dirty-state-and-the-unsaved-changes-guard),
// "Dirty state, and the unsaved-changes guard"): undoing back to the state that was saved still reads as dirty. This is
// deliberate, not a bug - the edit serial is a monotonic count rather than a position on the stack, because
// juce::UndoManager exposes no stable save-point index, and the two failure directions are not
// symmetrical: a false "clean" loses work silently, a false "dirty" costs one extra prompt.
TEST_F(MainComponentTest, UndoBackToTheSavedStateIsStillDirty) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    ASSERT_TRUE(mc.saveProjectForTest(tempRoot.getChildFile("UndoSemantics.agsproj")));
    ASSERT_FALSE(mc.isProjectDirty());

    makeDirty(mc);
    ASSERT_TRUE(mc.isProjectDirty());

    ASSERT_TRUE(mc.getUndoManager().undo());
    pumpMessageLoop();

    EXPECT_TRUE(mc.isProjectDirty()) << "undoing back to the saved state is still treated as dirty, by design";
}

// A factory preset is not the bundle that was open. Without dropping the bundle target, the next
// silent Cmd+S would overwrite that project with the factory preset - plus the OLD project's
// timeline, since a factory load leaves the live timeline alone.
TEST_F(MainComponentTest, FactoryPresetLoadDropsTheBundleTarget) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    ASSERT_TRUE(mc.saveProjectForTest(tempRoot.getChildFile("Victim.agsproj")));
    ASSERT_FALSE(mc.wouldPromptOnSaveForTest());

    mc.simulateLoadFactoryPresetForTest(1);

    EXPECT_TRUE(mc.wouldPromptOnSaveForTest()) << "Cmd+S must prompt rather than overwrite the project that was open";
}

// A factory preset replaces the GRAPH and leaves the live timeline alone, so it must NOT report the
// document as clean: an unsaved arrangement survives the load, and clearing the flag here would
// disarm the guard for it — load a preset over unsaved tracks, quit, and they would be gone with no
// prompt. (newPatch is the opposite case, and does clear: it empties the timeline too.)
TEST_F(MainComponentTest, FactoryPresetLoadKeepsADirtyTimelineDirty) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    makeDirty(mc);
    ASSERT_TRUE(mc.isProjectDirty());
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);

    mc.simulateLoadFactoryPresetForTest(1);
    pumpMessageLoop();

    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u) << "the load leaves the timeline in place...";
    EXPECT_TRUE(mc.isProjectDirty()) << "...so the document is still unsaved, and must still say so";
}

// Same defect, other path: opening a legacy plain .json over an open bundle left the bundle
// installed as the silent save target.
TEST_F(MainComponentTest, OpeningALegacyPatchDropsTheBundleTarget) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    ASSERT_TRUE(mc.saveProjectForTest(tempRoot.getChildFile("Victim2.agsproj")));
    ASSERT_FALSE(mc.wouldPromptOnSaveForTest());

    const auto legacy = tempRoot.getChildFile("Plain.json");
    mc.exportPatchOnlyForTest(legacy);
    ASSERT_TRUE(legacy.existsAsFile());

    ASSERT_TRUE(mc.openProjectForTest(legacy));
    EXPECT_TRUE(mc.wouldPromptOnSaveForTest()) << "a legacy patch has no bundle to resave to";
    EXPECT_FALSE(mc.isProjectDirty());
}

// REGRESSION LOCK (f7cba4a): MainComponent must refresh models AFTER setProvider(). The
// AIChatComponent ctor's own refreshModels() runs before the provider exists, so without
// the post-setProvider refresh currentModel stays empty and every /api/chat is a 400
// "model is required".
