// P8-4: autosave. MainComponent-level gate + recovery-flow tests — the sidecar FILE FORMAT itself
// (ProjectBundle::saveAutosave/loadAutosave/hasAutosave/discardAutosave) is covered directly in
// ProjectBundleTests.cpp, which needs no MainComponent.
//
// SAFETY RULE, same as MainComponentTests.cpp's P8-2 section: never let a real dialog open — a
// headless run has no message loop to answer one with. Every test that reaches the autosave-
// recovery prompt installs mc.autosaveRecoveryPrompt BEFORE opening a bundle with a pending sidecar.
//
// autosaveEnabled defaults OFF in this whole test binary (see MainComponentTests.cpp's
// resetPanelKeys()) — every test here turns it ON explicitly on its own MainComponent instance
// right after construction, and TearDown resets the shared on-disk key back to "0" so no other
// test file in the same binary run is affected by what this one leaves behind.

#include "../Source/AI/AIProvider.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Transport/BounceExporter.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class MockProviderAutosave : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockAutosave"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

class AutosaveTest : public ::testing::Test {
protected:
    // Same on-disk "Agent Synth" ApplicationProperties file MainComponentTests.cpp's
    // resetPanelKeys() resets — autosaveEnabled forced OFF here too, for the same reason.
    void resetAutosaveKey() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("autosaveEnabled", "0");
            s->removeValue("autosaveIntervalMinutes");
            s->removeValue("autosaveBackupCount");
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetAutosaveKey();
        tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-autosave-tests");
        tempRoot.deleteRecursively();
        tempRoot.createDirectory();
    }
    void TearDown() override {
        resetAutosaveKey();
        tempRoot.deleteRecursively();
    }

    // Constructs a MainComponent, turns autosave ON (overriding the suite-wide default-off reset
    // above), and leaves it ready for a save. Every test needs exactly this starting point.
    std::unique_ptr<MainComponent> makeComponentWithAutosaveOn() {
        auto mc = std::make_unique<MainComponent>(std::make_unique<MockProviderAutosave>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->getAppPropertiesForTest().getUserSettings()->setValue("autosaveEnabled", "1");
        return mc;
    }

    juce::File tempRoot;
};

// ---------------------------------------------------------------------------
// The gate itself: enabled, a bundle open, not recording, edit serial moved, interval elapsed.
// ---------------------------------------------------------------------------

TEST_F(AutosaveTest, DoesNotFireWithNoBundleOpen) {
    auto mc = makeComponentWithAutosaveOn();
    // Never saved — currentBundleDir_ is invalid, so there is nowhere to put a sidecar.
    ASSERT_TRUE(mc->wouldPromptOnSaveForTest());
    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc->isProjectDirty());

    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();

    // No bundle directory exists at all, so there is nowhere an autosave.json could have landed —
    // this just confirms the tick was a no-op rather than, say, crashing on an invalid File.
    SUCCEED();
}

TEST_F(AutosaveTest, FiresWhenEditedSinceLastAutosaveAndIntervalHasElapsed) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Fires.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir)); // markDocumentClean() rebases both baselines

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc->isProjectDirty());

    ASSERT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));
    mc->setAutosaveElapsedMsForTest(999999); // well past the default 2-minute interval
    mc->runAutosaveTickForTest();

    EXPECT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));
    // Autosave must NEVER touch the canonical file or clear the dirty flag - it is not a save.
    EXPECT_TRUE(mc->isProjectDirty());
}

TEST_F(AutosaveTest, DoesNotFireBeforeTheIntervalHasElapsed) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("TooSoon.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc->isProjectDirty());

    // Only 1 second "elapsed" - the default interval is 2 minutes.
    mc->setAutosaveElapsedMsForTest(1000);
    mc->runAutosaveTickForTest();

    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));
}

// THE regression test: gating on isDirty_ (never cleared by autosave) rather than the edit serial
// would rewrite the sidecar every interval forever even with ZERO new edits since the last one.
// Proven here by deleting the sidecar after a real autosave and showing a second qualifying tick,
// with no new edit in between, does NOT bring it back.
TEST_F(AutosaveTest, DoesNotRefireWhenNothingChangedSinceTheLastAutosave) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("NoRefire.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc->isProjectDirty());

    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));

    // Remove the sidecar by hand, then simulate ANOTHER full interval elapsing with NO new edit.
    // isDirty_ is still true (autosave never clears it) - if the gate were isDirty_-based, this
    // tick would recreate the file. It must not.
    bundleDir.getChildFile("autosave.json").deleteFile();
    ASSERT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));
    ASSERT_TRUE(mc->isProjectDirty()) << "autosave must never clear isDirty_";

    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir))
        << "gate must key off the edit serial, not isDirty_ - nothing changed since the last autosave";

    // A genuine new edit, though, must autosave again once the interval has (still) elapsed.
    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    EXPECT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));
}

TEST_F(AutosaveTest, DoesNotFireWhenDisabledInPreferences) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Disabled.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    mc->getAppPropertiesForTest().getUserSettings()->setValue("autosaveEnabled", "0");

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();

    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));
}

TEST_F(AutosaveTest, DoesNotFireDuringAnActiveAudioTake) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("AudioTake.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);

    mc->setAudioTakeCapturingForTest(true);
    ASSERT_TRUE(mc->isRecordingActiveForTest());
    mc->runAutosaveTickForTest();
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir)) << "must not fire mid-take";

    mc->setAudioTakeCapturingForTest(false);
    mc->runAutosaveTickForTest();
    EXPECT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir)) << "fires once the take is no longer active";
}

TEST_F(AutosaveTest, DoesNotFireDuringAnActiveMidiRecording) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("MidiTake.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_EQ(mc->getTimelineDoc().getTracks().size(), 1u);
    const auto trackId = mc->getTimelineDoc().getTracks()[0].id;

    mc->setAutosaveElapsedMsForTest(999999);
    mc->getMidiRecorderForTest().startRecording(trackId, 0.0);
    ASSERT_TRUE(mc->isRecordingActiveForTest());

    mc->runAutosaveTickForTest();
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir)) << "must not fire mid-take";
}

// A failing bounce test here would mean a future change (e.g. a P8-5 progress dialog that pumps
// the message loop from inside BounceExporter::bounce()'s ProgressCallback) reopened the exact
// re-entrancy window this file's header comment and docs/architecture.md warn about: bounce()
// today never pumps juce::MessageManager internally, so MainComponent's own real 10 Hz
// juce::Timer (started in its constructor) cannot be serviced while a bounce call is on the stack
// - there is no running dispatch loop to deliver it. This test proves that invariant holds for the
// REAL, unmodified bounce() call, rather than only asserting it in a comment.
TEST_F(AutosaveTest, DoesNotFireDuringABounce) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Bounce.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc->isProjectDirty());
    mc->setAutosaveElapsedMsForTest(999999); // the gate would otherwise be wide open

    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 1.0;
    options.sampleRate = 44100.0;
    options.blockSize = 512;
    const auto outFile = tempRoot.getChildFile("bounce.wav");
    const auto result = synth::BounceExporter::bounce(mc->getAudioEngine(), outFile, options);
    ASSERT_TRUE(result.ok) << result.message;

    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir))
        << "bounce() never pumps the message loop, so the real Timer cannot fire mid-bounce";

    // The gate is still wide open afterwards - proves the absence above was about the bounce
    // itself, not some unrelated reason the gate never passes on this document.
    mc->runAutosaveTickForTest();
    EXPECT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));
}

// ---------------------------------------------------------------------------
// Recovery on open: a pending sidecar prompts before either file loads.
// ---------------------------------------------------------------------------

TEST_F(AutosaveTest, RestoringTheSidecarLoadsItAndLeavesTheDocumentDirty) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Restore.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir)); // project.json: 0 tracks

    mc->simulateAddMidiTrackClick(); // the autosave will capture 1 track, project.json still has 0
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));

    auto reloaded = std::make_unique<MainComponent>(std::make_unique<MockProviderAutosave>());
    reloaded->setSize(1600, 900);
    reloaded->getAudioEngine().suspendDeviceCallback();

    bool promptShown = false;
    reloaded->autosaveRecoveryPrompt = [&](std::function<void(MainComponent::AutosaveRecoveryChoice)> onChoice) {
        promptShown = true;
        onChoice(MainComponent::AutosaveRecoveryChoice::Restore);
    };

    ASSERT_TRUE(reloaded->openProjectForTest(bundleDir));
    EXPECT_TRUE(promptShown);
    EXPECT_EQ(reloaded->getTimelineDoc().getTracks().size(), 1u) << "restored from the sidecar, not project.json";
    EXPECT_TRUE(reloaded->isProjectDirty()) << "the recovered state diverges from what's on disk";
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir)) << "consumed by the recovery answer";
}

TEST_F(AutosaveTest, DiscardingTheSidecarLoadsTheSavedProjectAndClearsIt) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Discard.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir)); // project.json: 0 tracks

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));

    auto reloaded = std::make_unique<MainComponent>(std::make_unique<MockProviderAutosave>());
    reloaded->setSize(1600, 900);
    reloaded->getAudioEngine().suspendDeviceCallback();

    reloaded->autosaveRecoveryPrompt = [](std::function<void(MainComponent::AutosaveRecoveryChoice)> onChoice) {
        onChoice(MainComponent::AutosaveRecoveryChoice::Discard);
    };

    ASSERT_TRUE(reloaded->openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded->getTimelineDoc().getTracks().size(), 0u) << "loaded project.json, not the sidecar";
    EXPECT_FALSE(reloaded->isProjectDirty());
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir)) << "consumed by the recovery answer";
}

TEST_F(AutosaveTest, OpeningABundleWithNoSidecarNeverPromptsAndBehavesExactlyAsBefore) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("Plain.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    ASSERT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));

    auto reloaded = std::make_unique<MainComponent>(std::make_unique<MockProviderAutosave>());
    reloaded->setSize(1600, 900);
    reloaded->getAudioEngine().suspendDeviceCallback();
    // No autosaveRecoveryPrompt installed - a real AlertWindow would hang a headless test if this
    // path were reached, so simply reaching this assertion proves it wasn't.
    EXPECT_TRUE(reloaded->openProjectForTest(bundleDir));
    EXPECT_FALSE(reloaded->isProjectDirty());
}

TEST_F(AutosaveTest, AnExplicitSaveDiscardsAnyPendingAutosave) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("SaveDiscards.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));

    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir));
}

// P8-4 follow-up: performAutosave() reads the configurable backup-count preference and passes it
// straight to ProjectBundle::saveAutosave - this is the integration point, the rotation mechanics
// themselves are covered directly in ProjectBundleTests.cpp.
TEST_F(AutosaveTest, PerformAutosaveHonoursTheConfiguredBackupCount) {
    auto mc = makeComponentWithAutosaveOn();
    mc->getAppPropertiesForTest().getUserSettings()->setValue("autosaveBackupCount", 2);
    const auto bundleDir = tempRoot.getChildFile("BackupCount.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    for (int i = 0; i < 3; ++i) {
        mc->simulateAddMidiTrackClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        mc->setAutosaveElapsedMsForTest(999999);
        mc->runAutosaveTickForTest();
    }

    EXPECT_TRUE(bundleDir.getChildFile("autosave.json").existsAsFile());
    EXPECT_TRUE(bundleDir.getChildFile("autosave-1.json").existsAsFile());
    EXPECT_TRUE(bundleDir.getChildFile("autosave-2.json").existsAsFile());
    EXPECT_FALSE(bundleDir.getChildFile("autosave-3.json").existsAsFile())
        << "backup count of 2 must not retain a third backup";
}

// A corrupt/hand-edited sidecar must not strand the user on the previous document AND destroy the
// only copy of the data that failed to load - answering Restore has to fall back to the normal
// project.json load, and the sidecar must survive so nothing is silently lost.
TEST_F(AutosaveTest, RestoringACorruptSidecarFallsBackToTheSavedProjectAndKeepsTheSidecar) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("CorruptRestore.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir)); // project.json: 0 tracks

    ASSERT_TRUE(bundleDir.getChildFile("autosave.json").replaceWithText("{not valid json"));
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));

    auto reloaded = std::make_unique<MainComponent>(std::make_unique<MockProviderAutosave>());
    reloaded->setSize(1600, 900);
    reloaded->getAudioEngine().suspendDeviceCallback();

    bool promptShown = false;
    reloaded->autosaveRecoveryPrompt = [&](std::function<void(MainComponent::AutosaveRecoveryChoice)> onChoice) {
        promptShown = true;
        onChoice(MainComponent::AutosaveRecoveryChoice::Restore);
    };

    EXPECT_TRUE(reloaded->openProjectForTest(bundleDir)) << "falls back to loading project.json";
    EXPECT_TRUE(promptShown);
    EXPECT_EQ(reloaded->getTimelineDoc().getTracks().size(), 0u) << "loaded the last saved project, not the sidecar";
    EXPECT_FALSE(reloaded->isProjectDirty());
    EXPECT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir))
        << "the failed sidecar is never destroyed - there is no other copy of what it held";
}

// Choosing Discard on the unsaved-changes guard means "throw these changes away" - a pending
// autosave sidecar holds exactly that same content, so it must go too, or the next open of this
// bundle would offer to "recover" changes the user just explicitly discarded.
TEST_F(AutosaveTest, DiscardingUnsavedChangesAlsoDiscardsAnyPendingAutosaveForTheOpenBundle) {
    auto mc = makeComponentWithAutosaveOn();
    const auto bundleDir = tempRoot.getChildFile("GuardDiscardsSidecar.agsproj");
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));

    mc->simulateAddMidiTrackClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    mc->setAutosaveElapsedMsForTest(999999);
    mc->runAutosaveTickForTest();
    ASSERT_TRUE(synth::ProjectBundle::hasAutosave(bundleDir));
    ASSERT_TRUE(mc->isProjectDirty()) << "autosave never clears the dirty flag";

    mc->unsavedChangesPrompt = [](const juce::String&,
                                  std::function<void(MainComponent::UnsavedChangesChoice)> onChoice) {
        onChoice(MainComponent::UnsavedChangesChoice::Discard);
    };
    ASSERT_TRUE(mc->getCommandManager().invokeDirectly(AppCommands::newPatch, false));

    EXPECT_FALSE(synth::ProjectBundle::hasAutosave(bundleDir))
        << "Discard must not leave a sidecar behind that reoffers the same discarded edit";
}
