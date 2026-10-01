// Concern: the transport (tempo, time signature, loop) is part of the saved project and its edits are
// debounced undo steps that mark the document unsaved.
#include "AudioEngine/AudioEngine.h"
#include "MainComponentTestFixture.h"
#include "ProjectBundle.h"
#include "Transport/TransportDoc.h"

namespace {

constexpr juce::uint32 kStart = 1000;
constexpr juce::uint32 kHeld = 400; // past the 300 ms debounce

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

synth::TransportDoc editedTransport() {
    synth::TransportDoc t;
    t.bpm = 96.0;
    t.timeSigNumerator = 3;
    t.timeSigDenominator = 4;
    t.loopStartBeat = 4.0;
    t.loopEndBeat = 16.0;
    t.loopEnabled = true;
    return t;
}

void applyEdited(MainComponent& mc) {
    auto& transport = mc.getAudioEngine().getTransport();
    ASSERT_TRUE(transport.setBpm(96.0));
    ASSERT_TRUE(transport.setTimeSignature(3, 4));
    ASSERT_TRUE(transport.setLoop(4.0, 16.0, true));
}

} // namespace

TEST_F(MainComponentTest, TransportSurvivesSaveAndReopenAndTheLoadLeavesTheDocumentClean) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    applyEdited(mc);

    const auto bundleDir = tempRoot.getChildFile("Transport.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();
    ASSERT_EQ(reloaded.getAudioEngine().getTransport().getDocumentState(), synth::TransportDoc{});

    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded.getAudioEngine().getTransport().getDocumentState(), editedTransport());

    // The load itself is not an edit: however often the poll runs, it must not record a step.
    reloaded.pollTransportEditsForTest(kStart);
    reloaded.pollTransportEditsForTest(kStart + kHeld);
    reloaded.pollTransportEditsForTest(kStart + 2 * kHeld);
    settle();
    EXPECT_FALSE(reloaded.getUndoManager().canUndo());
    EXPECT_FALSE(reloaded.isProjectDirty());
}

TEST_F(MainComponentTest, ReopeningInTheSameWindowRestoresTheSavedTransport) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    applyEdited(mc);
    const auto bundleDir = tempRoot.getChildFile("SameWindow.agsproj");
    ASSERT_TRUE(mc.saveProjectForTest(bundleDir));

    ASSERT_TRUE(mc.getAudioEngine().getTransport().setBpm(150.0));
    mc.pollTransportEditsForTest(kStart);
    mc.pollTransportEditsForTest(kStart + kHeld);

    ASSERT_TRUE(mc.openProjectForTest(bundleDir));
    EXPECT_EQ(mc.getAudioEngine().getTransport().getDocumentState(), editedTransport());
    mc.pollTransportEditsForTest(kStart + 2 * kHeld);
    mc.pollTransportEditsForTest(kStart + 3 * kHeld);
    settle();
    EXPECT_FALSE(mc.isProjectDirty());
}

TEST_F(MainComponentTest, HeldTransportEditMarksTheDocumentUnsavedAndUndoRestoresIt) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    ASSERT_FALSE(mc.isProjectDirty());

    ASSERT_TRUE(transport.setBpm(96.0));
    mc.pollTransportEditsForTest(kStart);         // first sight of the new value
    mc.pollTransportEditsForTest(kStart + kHeld); // held long enough
    settle();

    EXPECT_TRUE(mc.isProjectDirty());
    ASSERT_TRUE(mc.getUndoManager().canUndo());

    // More polls with the same value add nothing.
    mc.pollTransportEditsForTest(kStart + 2 * kHeld);
    mc.pollTransportEditsForTest(kStart + 3 * kHeld);

    mc.getUndoManager().undo();
    settle();
    EXPECT_DOUBLE_EQ(transport.getDocumentState().bpm, 120.0);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the edit was exactly one undo step";
    // The edit serial counts undos too, so undoing back to the saved state still reads as unsaved.
    EXPECT_TRUE(mc.isProjectDirty());

    // The undo is not re-recorded as a fresh edit.
    const int serialAfterUndo = mc.getUndoManager().getEditSerial();
    mc.pollTransportEditsForTest(kStart + 4 * kHeld);
    mc.pollTransportEditsForTest(kStart + 5 * kHeld);
    settle();
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_EQ(mc.getUndoManager().getEditSerial(), serialAfterUndo);

    mc.getUndoManager().redo();
    settle();
    EXPECT_DOUBLE_EQ(transport.getDocumentState().bpm, 96.0);
    EXPECT_TRUE(mc.isProjectDirty());
}

TEST_F(MainComponentTest, ContinuouslyChangingTransportRecordsNoStepUntilItSettles) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    // The poll is driven by hand with a fake clock below; a real 10 Hz tick landing in settle() would compare
    // the wall clock against that fake one and record the step early.
    mc.stopTimer();
    auto& transport = mc.getAudioEngine().getTransport();

    // A drag: the value differs on every poll, so it never holds for the debounce interval.
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(transport.setBpm(100.0 + i));
        mc.pollTransportEditsForTest(kStart + static_cast<juce::uint32>(i) * 200);
    }
    settle();
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_FALSE(mc.isProjectDirty());

    // Once it stops, the whole drag is one step.
    mc.pollTransportEditsForTest(kStart + 2000);
    mc.pollTransportEditsForTest(kStart + 2000 + kHeld);
    settle();
    EXPECT_TRUE(mc.isProjectDirty());
    mc.getUndoManager().undo();
    EXPECT_DOUBLE_EQ(transport.getDocumentState().bpm, 120.0);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}
