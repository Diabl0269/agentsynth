// Concern: the graph canvas view (zoom and pan) is part of the saved project, and neither saving it, loading it
// nor changing it is an edit: the document is never marked unsaved by it. Also the command-line open hook.
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "Project/ViewDoc.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

// The dirty flag is polled on a timer, so a slow (Debug, sanitized) run needs more than one short pump.
bool settleUntilDirty(MainComponent& mc) {
    for (int i = 0; i < 40 && !mc.isProjectDirty(); ++i)
        settle();
    return mc.isProjectDirty();
}

synth::ViewDoc editedView() {
    synth::ViewDoc v;
    v.zoom = 0.5f;
    v.panX = -240.0f;
    v.panY = 96.0f;
    return v;
}

struct PromptRecorder {
    int calls = 0;
    std::optional<MainComponent::UnsavedChangesChoice> answer;

    void installOn(MainComponent& mc) {
        mc.unsavedChangesPrompt = [this](const juce::String&,
                                         std::function<void(MainComponent::UnsavedChangesChoice)> onChoice) {
            ++calls;
            if (answer.has_value())
                onChoice(*answer);
        };
    }
};

} // namespace

TEST_F(MainComponentTest, CanvasViewSurvivesSaveAndReopenAndTheLoadLeavesTheDocumentClean) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().applyViewDoc(editedView());

    const auto bundleDir = tempRoot.getChildFile("View.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();
    ASSERT_EQ(reloaded.getGraphEditor().getViewDoc(), synth::ViewDoc{});

    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded.getGraphEditor().getViewDoc(), editedView());
    settle();
    EXPECT_FALSE(reloaded.getUndoManager().canUndo());
    EXPECT_FALSE(reloaded.isProjectDirty());
}

TEST_F(MainComponentTest, ReopeningInTheSameWindowRestoresTheSavedCanvasView) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getGraphEditor().applyViewDoc(editedView());
    const auto bundleDir = tempRoot.getChildFile("SameWindow.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    synth::ViewDoc moved;
    moved.zoom = 1.5f;
    moved.panX = 500.0f;
    mc.getGraphEditor().applyViewDoc(moved);

    ASSERT_TRUE(mc.openProjectForTest(bundleDir));
    EXPECT_EQ(mc.getGraphEditor().getViewDoc(), editedView());
    settle();
    EXPECT_FALSE(mc.isProjectDirty());
}

TEST_F(MainComponentTest, ProjectSavedWithoutAViewLeavesTheCurrentCanvasViewAlone) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    // A file from before the view was saved: strip the key from a real save.
    const auto bundleDir = tempRoot.getChildFile("Legacy.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));
    const auto file = bundleDir.getChildFile(synth::ProjectBundle::kProjectFileName);
    auto json = juce::JSON::parse(file);
    ASSERT_TRUE(json.hasProperty("view"));
    json.getDynamicObject()->removeProperty("view");
    ASSERT_TRUE(file.replaceWithText(juce::JSON::toString(json)));

    mc.getGraphEditor().applyViewDoc(editedView());
    ASSERT_TRUE(mc.openProjectForTest(bundleDir));
    EXPECT_EQ(mc.getGraphEditor().getViewDoc(), editedView());
}

TEST_F(MainComponentTest, ChangingTheCanvasViewIsNotAnEdit) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_FALSE(mc.isProjectDirty());

    mc.getGraphEditor().applyViewDoc(editedView());
    mc.getGraphEditor().zoomAroundCentre(1.0f);
    settle();

    EXPECT_FALSE(mc.isProjectDirty());
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(MainComponentTest, ApplyingAViewClampsZoomToTheWheelRange) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    synth::ViewDoc wild;
    wild.zoom = 40.0f;
    mc.getGraphEditor().applyViewDoc(wild);
    EXPECT_FLOAT_EQ(mc.getGraphEditor().getViewDoc().zoom, synth::ViewDoc::kMaxZoom);
}

TEST_F(MainComponentTest, OpeningAProjectFromTheCommandLineLoadsItAndHidesTheWelcomeScreen) {
    MainComponent source(std::make_unique<MockProvider>());
    source.setSize(1600, 900);
    source.getAudioEngine().suspendDeviceCallback();
    source.getGraphEditor().applyViewDoc(editedView());
    const auto bundleDir = tempRoot.getChildFile("FromCli.agsproj");
    ASSERT_TRUE(source.saveProjectForTest(bundleDir));

    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    mc.getWelcomeScreenForTest()->setVisible(true);

    mc.openProjectFromCommandLine(bundleDir);
    settle();

    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isVisible());
    EXPECT_EQ(mc.getGraphEditor().getViewDoc(), editedView())
        << "the project was loaded, not just the welcome screen closed";
    EXPECT_EQ(mc.getCurrentPatchName(), "FromCli");
    EXPECT_FALSE(mc.isProjectDirty());
}

TEST_F(MainComponentTest, CommandLineOpenAsksBeforeReplacingUnsavedWork) {
    MainComponent source(std::make_unique<MockProvider>());
    source.setSize(1600, 900);
    source.getAudioEngine().suspendDeviceCallback();
    const auto bundleDir = tempRoot.getChildFile("Guarded.agsproj");
    ASSERT_TRUE(source.saveProjectForTest(bundleDir));

    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.simulateAddMidiTrackClick();
    ASSERT_TRUE(settleUntilDirty(mc));

    PromptRecorder prompt;
    prompt.installOn(mc);

    // Cancel: the unsaved document is untouched and the welcome screen state is whatever it was.
    prompt.answer = MainComponent::UnsavedChangesChoice::Cancel;
    const auto nameBefore = mc.getCurrentPatchName();
    mc.openProjectFromCommandLine(bundleDir);
    settle();
    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(mc.getCurrentPatchName(), nameBefore);
    EXPECT_TRUE(mc.isProjectDirty());

    // Discard: the project opens.
    prompt.answer = MainComponent::UnsavedChangesChoice::Discard;
    mc.openProjectFromCommandLine(bundleDir);
    settle();
    EXPECT_EQ(prompt.calls, 2);
    EXPECT_EQ(mc.getCurrentPatchName(), "Guarded");
    EXPECT_FALSE(mc.isProjectDirty());
}
