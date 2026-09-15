// MixerPanelKeyboardFocusTests.cpp -- FRO18: MixerPanelComponent as the mixer's own keyboard
// focus-region ROOT (docs/shortcuts.md's "Mixer column navigation") -- Left/Right column walk,
// Up/Down fader nudge, Enter select-on-canvas, and the rebindable M/S/R actions, modeled on
// TimelineTrackFocusTests.cpp's own drive-keyPressed()-directly style.
//
// Drives a real, off-screen MainComponent (MixerPanelComponentTests.cpp's own rig style:
// newPatchForTest() + simulateAddAudioTrackClick(), which boxes {Track Audio, EQ, Compressor,
// Strip} into one macro per T173a) rather than a hand-built graph/doc/macros trio, so the arm key
// exercises the REAL MainComponent::performTrackEdit wiring end to end, not a stub.
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMPKFT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPKFT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
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

juce::KeyPress leftKey() { return juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress rightKey() { return juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress upKey() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress shiftUpKey() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0); }
juce::KeyPress returnKey() { return juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress mKey() { return juce::KeyPress('m', juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress sKey() { return juce::KeyPress('s', juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress rKey() { return juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0); }

ChannelStripModule* stripFor(MainComponent& mc, juce::AudioProcessorGraph::NodeID id) {
    auto* node = mc.getAudioEngine().getGraph().getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

float gainOf(MainComponent& mc, juce::AudioProcessorGraph::NodeID id) {
    auto* node = mc.getAudioEngine().getGraph().getNodeForId(id);
    if (node == nullptr)
        return 0.0f;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            return f->get();
    return 0.0f;
}

struct PanelFixture {
    MainComponent mc{std::make_unique<MockProviderMPKFT>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    explicit PanelFixture(int audioTracks = 2) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < audioTracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getMixerDock().getMixerPanel();
        panel->rebuild();
        panel->setSize(1400, 300);
        panel->resized();
    }
};

} // namespace

TEST(MixerPanelKeyboardFocusTest, PanelWantsKeyboardFocus) {
    PanelFixture f;
    EXPECT_TRUE(f.panel->getWantsKeyboardFocus())
        << "the panel is the single focusable leaf -- every child control gives it up";
}

TEST(MixerPanelKeyboardFocusTest, RightFromNothingFocusedLandsOnColumnZero) {
    PanelFixture f;
    ASSERT_EQ(f.panel->getFocusedColumnIndexForTest(), -1);
    EXPECT_TRUE(f.panel->keyPressed(rightKey()));
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 0);
}

TEST(MixerPanelKeyboardFocusTest, RightWalksForwardAndLeftWalksBackward) {
    PanelFixture f;
    f.panel->keyPressed(rightKey()); // -> 0
    f.panel->keyPressed(rightKey()); // -> 1
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 1);
    f.panel->keyPressed(leftKey()); // -> 0
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 0);
}

TEST(MixerPanelKeyboardFocusTest, RightClampsAtTheLastColumnRatherThanWrapping) {
    PanelFixture f; // 2 strips + Direct + Master = 4 columns
    for (int i = 0; i < 8; ++i)
        f.panel->keyPressed(rightKey());
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 3) << "clamped at the last column, never wraps to 0";
}

TEST(MixerPanelKeyboardFocusTest, LeftClampsAtFirstColumn) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    f.panel->keyPressed(rightKey());
    for (int i = 0; i < 8; ++i)
        f.panel->keyPressed(leftKey());
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 0);
}

TEST(MixerPanelKeyboardFocusTest, RightIncludesDirectAndMasterColumnsInOrder) {
    PanelFixture f; // strip0, strip1, Direct, Master
    f.panel->keyPressed(rightKey());
    EXPECT_TRUE(f.panel->getStripColumnForTest(0)->isKeyboardFocusedForTest());
    f.panel->keyPressed(rightKey());
    EXPECT_TRUE(f.panel->getStripColumnForTest(1)->isKeyboardFocusedForTest());
    f.panel->keyPressed(rightKey());
    EXPECT_TRUE(f.panel->getDirectColumnForTest()->isKeyboardFocusedForTest());
    EXPECT_FALSE(f.panel->getStripColumnForTest(1)->isKeyboardFocusedForTest());
    f.panel->keyPressed(rightKey());
    EXPECT_TRUE(f.panel->getMasterColumnForTest()->isKeyboardFocusedForTest());
    EXPECT_FALSE(f.panel->getDirectColumnForTest()->isKeyboardFocusedForTest());
}

TEST(MixerPanelKeyboardFocusTest, UpNudgesFocusedFaderByOneDbAsOneUndoStep) {
    PanelFixture f;
    f.panel->keyPressed(rightKey()); // focus strip 0
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    EXPECT_TRUE(f.panel->keyPressed(upKey()));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 1.0f, 1.0e-3f);

    ASSERT_TRUE(f.mc.getUndoManager().canUndo());
    f.mc.getUndoManager().undo();
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f) << "one undo must restore the pre-nudge gain";
}

TEST(MixerPanelKeyboardFocusTest, ShiftUpNudgesByPointOneDb) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    EXPECT_TRUE(f.panel->keyPressed(shiftUpKey()));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 0.1f, 1.0e-3f);
}

TEST(MixerPanelKeyboardFocusTest, UpIsANoOpOnTheDirectColumn) {
    PanelFixture f(1);               // strip0, Direct, Master
    f.panel->keyPressed(rightKey()); // -> strip0
    f.panel->keyPressed(rightKey()); // -> Direct
    ASSERT_TRUE(f.panel->getDirectColumnForTest()->isKeyboardFocusedForTest());
    EXPECT_FALSE(f.panel->keyPressed(upKey()));
}

TEST(MixerPanelKeyboardFocusTest, EnterSelectsTheFocusedColumnsMacroOnCanvas) {
    PanelFixture f(1);
    ASSERT_EQ(f.mc.getGraphEditor().getMacros().size(), 1);
    const auto macroId = f.mc.getGraphEditor().getMacros().getAll().front().id;
    EXPECT_FALSE(f.mc.getGraphEditor().isMacroSelected(macroId));

    f.panel->keyPressed(rightKey()); // -> strip0
    EXPECT_TRUE(f.panel->keyPressed(returnKey()));
    EXPECT_TRUE(f.mc.getGraphEditor().isMacroSelected(macroId));
}

TEST(MixerPanelKeyboardFocusTest, MuteKeyTogglesFocusedColumnThroughTheSameOnClickPath) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    auto* strip = stripFor(f.mc, nodeId);
    ASSERT_NE(strip, nullptr);
    ASSERT_FALSE(strip->isMuted());

    EXPECT_TRUE(f.panel->keyPressed(mKey()));
    EXPECT_TRUE(strip->isMuted());
    EXPECT_TRUE(f.panel->keyPressed(mKey()));
    EXPECT_FALSE(strip->isMuted());
}

TEST(MixerPanelKeyboardFocusTest, SoloKeyTogglesFocusedColumnThroughAudioEngineSetChannelStripSoloed) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    auto* strip = stripFor(f.mc, nodeId);
    ASSERT_NE(strip, nullptr);
    ASSERT_FALSE(strip->isSoloed());

    EXPECT_TRUE(f.panel->keyPressed(sKey()));
    EXPECT_TRUE(strip->isSoloed());
}

TEST(MixerPanelKeyboardFocusTest, ArmKeyTogglesTheLinkedTrackThroughPerformTrackEdit) {
    PanelFixture f(1);
    const auto trackId = f.mc.getTimelineDoc().getTracks().front().id;
    ASSERT_FALSE(f.mc.getTimelineDoc().getTrack(trackId)->armed);

    f.panel->keyPressed(rightKey()); // -> the linked strip
    EXPECT_TRUE(f.panel->keyPressed(rKey()));
    EXPECT_TRUE(f.mc.getTimelineDoc().getTrack(trackId)->armed);

    ASSERT_TRUE(f.mc.getUndoManager().canUndo());
    f.mc.getUndoManager().undo();
    EXPECT_FALSE(f.mc.getTimelineDoc().getTrack(trackId)->armed)
        << "must go through performTrackEdit, the same one-undo-step path a real Arm click uses";
}

TEST(MixerPanelKeyboardFocusTest, ArmKeyIsANoOpWhenTheColumnIsNotLinked) {
    PanelFixture f(1);               // strip0, Direct, Master
    f.panel->keyPressed(rightKey()); // -> strip0
    f.panel->keyPressed(rightKey()); // -> Direct (never linked to a track)
    ASSERT_TRUE(f.panel->getDirectColumnForTest()->isKeyboardFocusedForTest());

    EXPECT_FALSE(f.panel->keyPressed(rKey()));
    const auto trackId = f.mc.getTimelineDoc().getTracks().front().id;
    EXPECT_FALSE(f.mc.getTimelineDoc().getTrack(trackId)->armed);
}

TEST(MixerPanelKeyboardFocusTest, MSRKeysFallBackToBareLettersWithNoShortcutManagerInstalled) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    EXPECT_TRUE(f.panel->keyPressed(mKey()));
    EXPECT_TRUE(f.panel->keyPressed(sKey()));
    EXPECT_TRUE(f.panel->keyPressed(rKey()));
}

TEST(MixerPanelKeyboardFocusTest, MSRKeysAreRebindableThroughAnInstalledShortcutManager) {
    PanelFixture f;
    ShortcutManager manager;
    manager.setBinding("timelineMuteFocusedTrack", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    f.panel->setShortcutManager(&manager);

    f.panel->keyPressed(rightKey());
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    auto* strip = stripFor(f.mc, nodeId);

    EXPECT_FALSE(f.panel->keyPressed(mKey())) << "the bare 'm' default was rebound away -- an unset "
                                                 "binding has NO key, it never falls back";
    EXPECT_FALSE(strip->isMuted());

    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_TRUE(strip->isMuted());
}

TEST(MixerPanelKeyboardFocusTest, ShortcutManagerInstalledAfterColumnsExistStillReachesThem) {
    // setShortcutManager() must propagate even though the columns (and their focus) were built
    // BEFORE it was called -- keyPressed() always reads the member, never a value captured at
    // bind time (MixerPanelComponent.h's own contract).
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    ShortcutManager manager;
    manager.setBinding("timelineSoloFocusedTrack", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    f.panel->setShortcutManager(&manager);

    EXPECT_FALSE(f.panel->keyPressed(sKey())) << "bare 's' was rebound away after columns already existed";
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
}

TEST(MixerPanelKeyboardFocusTest, UnrelatedKeysAreNotClaimedByThePanel) {
    PanelFixture f;
    EXPECT_FALSE(f.panel->keyPressed(juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_FALSE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
}

TEST(MixerPanelKeyboardFocusTest, FocusSurvivesARebuildOfTheSameStrip) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    f.panel->keyPressed(rightKey()); // -> strip 1
    ASSERT_EQ(f.panel->getFocusedColumnIndexForTest(), 1);

    f.panel->rebuild(); // nothing changed in the graph -- same strips, same order
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_TRUE(f.panel->getStripColumnForTest(1)->isKeyboardFocusedForTest());
}

TEST(MixerPanelKeyboardFocusTest, DeletingTheFocusedStripClearsFocus) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    f.panel->keyPressed(rightKey()); // -> strip 1
    ASSERT_EQ(f.panel->getFocusedColumnIndexForTest(), 1);

    ASSERT_EQ(f.mc.getGraphEditor().getMacros().size(), 2);
    const auto macroId = f.mc.getGraphEditor().getMacros().getAll()[1].id;
    f.mc.getGraphEditor().deleteMacroAndMembers(macroId);

    f.panel->rebuild();
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), -1)
        << "the strip the focus pointed at no longer exists -- it must clear, never silently "
           "reattach to whatever now sits at the old index";
}

TEST(MixerPanelKeyboardFocusTest, RebuildPreservesFocusAcrossAnUnrelatedGraphChange) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    f.panel->keyPressed(rightKey()); // -> strip 1
    ASSERT_EQ(f.panel->getFocusedColumnIndexForTest(), 1);

    f.mc.simulateAddAudioTrackClick(); // a third, unrelated strip
    f.panel->rebuild();

    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 1) << "strip 1 is still at index 1 -- the new "
                                                             "strip was appended after it";
    EXPECT_TRUE(f.panel->getStripColumnForTest(1)->isKeyboardFocusedForTest());
}
