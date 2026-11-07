// MixerPanelKeyboardFocusTests.cpp -- MixerPanelComponent as the mixer's own keyboard
// focus-region ROOT (docs/control/shortcuts.md's "Mixer column navigation") -- Left/Right column walk,
// Up/Down fader nudge, Enter select-on-canvas, and the rebindable M/S/R actions, modeled on
// TimelineTrackFocusTests.cpp's own drive-keyPressed()-directly style.
//
// Drives a real, off-screen MainComponent (MixerPanelComponentTests.cpp's own rig style:
// newPatchForTest() + simulateAddAudioTrackClick(), which boxes {Track Audio, Gate, EQ, Compressor,
// Strip} into one macro) rather than a hand-built graph/doc/macros trio, so the arm key
// exercises the REAL MainComponent::performTrackEdit wiring end to end, not a stub.
#include "../../TestWait.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MixerNativeHost.h"
#include "MixerZonesTestRig.h"
#include "Modules/ChannelStripModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/TestModeInput.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
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
juce::KeyPress shiftDownKey() { return juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0); }
juce::KeyPress altShiftUpKey() {
    return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier,
                          0);
}
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
        panel = &mc.getBottomDock().getMixerPanel();
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

TEST(MixerPanelKeyboardFocusTest, ArrowWalkPointsAccessibilityFocusAtEachColumnsFader) {
    // The app's own Left/Right column-walk must move REAL accessibility focus
    // (not just the visual outline setKeyboardFocused() paints), or VoiceOver never announces the
    // newly-focused column at all. getAccessibilityFocusTargetForTest() resolves WHICH control
    // MixerPanelComponent::grabAccessibilityFocusForFocusedColumn() targets for the current
    // focusedColumnIndex_ -- real AccessibilityHandler::hasFocus() movement itself needs a native
    // peer this suite deliberately never creates (see MixerAccessibilityTests.cpp's header), so
    // this is the headlessly-testable half: proving the walk targets the right control.
    PanelFixture f; // strip0, strip1, Direct, Master
    EXPECT_EQ(f.panel->getAccessibilityFocusTargetForTest(), nullptr) << "nothing focused yet";

    f.panel->keyPressed(rightKey()); // -> strip 0
    EXPECT_EQ(f.panel->getAccessibilityFocusTargetForTest(),
              &f.panel->getStripColumnForTest(0)->getAccessibilityFocusTargetForTest())
        << "a strip's own fader slider -- the control the ticket's click path names";

    f.panel->keyPressed(rightKey()); // -> strip 1
    EXPECT_EQ(f.panel->getAccessibilityFocusTargetForTest(),
              &f.panel->getStripColumnForTest(1)->getAccessibilityFocusTargetForTest());

    f.panel->keyPressed(rightKey()); // -> Direct
    EXPECT_EQ(f.panel->getAccessibilityFocusTargetForTest(),
              &f.panel->getDirectColumnForTest()->getAccessibilityFocusTargetForTest())
        << "Direct has no fader -- targets the column itself";

    f.panel->keyPressed(rightKey()); // -> Master
    EXPECT_EQ(f.panel->getAccessibilityFocusTargetForTest(),
              &f.panel->getMasterColumnForTest()->getAccessibilityFocusTargetForTest())
        << "Master's own fader, same as a strip";
}

TEST(MixerPanelKeyboardFocusTest, ShiftUpNudgesFocusedFaderByOneDbAsOneUndoStep) {
    PanelFixture f;
    f.panel->keyPressed(rightKey()); // focus strip 0
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    EXPECT_TRUE(f.panel->keyPressed(upKey())) << "plain Up is swallowed on a column";
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f) << "plain Up no longer moves the fader";
    EXPECT_TRUE(f.panel->keyPressed(shiftUpKey()));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 1.0f, 1.0e-3f);

    ASSERT_TRUE(f.mc.getUndoManager().canUndo());
    f.mc.getUndoManager().undo();
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f) << "one undo must restore the pre-nudge gain";
}

TEST(MixerPanelKeyboardFocusTest, AltShiftUpNudgesByPointOneDbAndShiftDownLowers) {
    PanelFixture f;
    f.panel->keyPressed(rightKey());
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    EXPECT_TRUE(f.panel->keyPressed(altShiftUpKey()));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 0.1f, 1.0e-3f);
    EXPECT_TRUE(f.panel->keyPressed(shiftDownKey()));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before - 0.9f, 1.0e-3f);
    EXPECT_FALSE(f.panel->getFocusedRowForTest().has_value()) << "Shift+Down nudges, it never enters the rows";
}

TEST(MixerPanelKeyboardFocusTest, TheFaderNudgeKeysAreRebindableAndTheTooltipNamesThem) {
    PanelFixture f;
    ShortcutManager shortcuts;
    f.panel->setShortcutManager(&shortcuts);
    f.panel->keyPressed(rightKey());
    auto& slider = f.panel->getStripColumnForTest(0)->getFaderForTest().getSlider();
    EXPECT_TRUE(slider.getTooltip().contains("Shift + Up")) << slider.getTooltip();
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    shortcuts.setBinding("mixerNudgeFaderUp", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    f.panel->refreshShortcutHints();
    EXPECT_TRUE(slider.getTooltip().contains("U /")) << slider.getTooltip();
    f.panel->keyPressed(shiftUpKey());
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f) << "the old chord no longer nudges";
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 1.0f, 1.0e-3f);
    f.panel->setShortcutManager(nullptr);
}

TEST(MixerPanelKeyboardFocusTest, UpIsANoOpOnTheDirectColumn) {
    PanelFixture f(1);               // strip0, Direct, Master
    f.panel->keyPressed(rightKey()); // -> strip0
    f.panel->keyPressed(rightKey()); // -> Direct
    ASSERT_TRUE(f.panel->getDirectColumnForTest()->isKeyboardFocusedForTest());
    EXPECT_FALSE(f.panel->keyPressed(shiftUpKey())) << "Direct has no fader";
}

TEST(MixerPanelKeyboardFocusTest, EnterSelectsTheFocusedColumnsMacroOnCanvas) {
    PanelFixture f(1);
    ASSERT_EQ(f.mc.getGraphEditor().getMacros().size(), 1);
    const auto macroId = f.mc.getGraphEditor().getMacros().getAll().front().id;
    EXPECT_FALSE(f.mc.getGraphEditor().getMacroController().isMacroSelected(macroId));

    f.panel->keyPressed(rightKey()); // -> strip0
    EXPECT_TRUE(f.panel->keyPressed(returnKey()));
    EXPECT_TRUE(f.mc.getGraphEditor().getMacroController().isMacroSelected(macroId));
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
    f.mc.getGraphEditor().getMacroController().deleteMacroAndMembers(macroId);

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

// Clicks and Cmd+D: a press anywhere in a mixer column focuses it and shows its outline, the outline
// leaves when another control takes the keyboard, Cmd+D copies the focused strip's track every time, and Cmd+Up from a
// clicked strip reaches the top bar ring. Clicks travel the real ComponentPeer path the test mode uses, on a native
// window holding the real panel of an off-screen MainComponent.

namespace {

using namespace synth::test::mixer;

juce::KeyPress cmdD() { return juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0); }
juce::KeyPress cmdUp() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0); }

// A point in the column where nothing but the column itself is hit: its empty space.
std::optional<juce::Point<int>> emptySpaceIn(juce::Component& column) {
    for (int y = column.getHeight() - 1; y > 0; y -= 3)
        for (int x = 1; x < column.getWidth(); x += 3)
            if (column.getComponentAt(x, y) == &column)
                return juce::Point<int>(x, y);
    return std::nullopt;
}

int showingRings(synth::ui::MixerPanelComponent& panel) {
    int rings = 0;
    for (int i = 0; auto* column = panel.getStripColumnForTest(i); ++i)
        rings += column->isKeyboardFocusedForTest() ? 1 : 0;
    return rings;
}

} // namespace

// Regression test for FRO1421: clicking a strip showed no focus, so rename and duplicate acted on an unseen strip.
TEST(MixerStripClickFocusTest, APressOnAStripsEmptySpaceOrAControlFocusesThatStrip) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(3);
    NativeHost host(*r.panel);
    auto* second = r.panel->getStripColumnForTest(1);
    auto* third = r.panel->getStripColumnForTest(2);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), -1);

    const auto empty = emptySpaceIn(*second);
    ASSERT_TRUE(empty.has_value()) << "the strip has no empty space to press";
    ASSERT_TRUE(click(*second, *empty));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 1);
    EXPECT_TRUE(second->isKeyboardFocusedForTest()) << "the pressed strip shows the focus outline";
    EXPECT_EQ(showingRings(*r.panel), 1);

    // A press on a control (the fader) focuses its strip as well; a right-click on a header does too.
    auto& fader = third->getFaderForTest();
    ASSERT_TRUE(click(fader, fader.getLocalBounds().getCentre()));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 2);
    EXPECT_TRUE(third->isKeyboardFocusedForTest());
    EXPECT_FALSE(second->isKeyboardFocusedForTest());

    r.panel->setShowZoneMenuHookForTest([](juce::PopupMenu&) {});
    // On the name, not the header's corner: a press on the colour dot would open its picker.
    auto* name = second->getHeaderForTest().findChildWithID("mixerColumnHeaderName");
    ASSERT_NE(name, nullptr);
    ASSERT_TRUE(click(*name, name->getLocalBounds().getCentre(), synth::testmode::Button::right));
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 1) << "the outline shows which strip the menu acts on";
}

// Regression test for FRO1421: the outline stayed on the strip after a click elsewhere took the keyboard.
TEST(MixerStripClickFocusTest, TheOutlineLeavesWhenAnotherControlTakesTheKeyboardAndComesBack) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    auto* first = r.panel->getStripColumnForTest(0);
    ASSERT_NE(first, nullptr);
    const auto empty = emptySpaceIn(*first);
    ASSERT_TRUE(empty.has_value());
    ASSERT_TRUE(click(*first, *empty));
    if (!r.panel->hasKeyboardFocus(true))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    EXPECT_TRUE(r.panel->hasKeyboardFocus(false)) << "the panel holds the keyboard, not the column's colour dot";
    ASSERT_TRUE(first->isKeyboardFocusedForTest());

    ASSERT_TRUE(click(host.canvas, host.canvas.getLocalBounds().getCentre()));
    ASSERT_TRUE(host.canvas.hasKeyboardFocus(false));
    EXPECT_FALSE(first->isKeyboardFocusedForTest()) << "the outline leaves with the keyboard";
    EXPECT_EQ(r.panel->getFocusedColumnIndexForTest(), 0) << "the column keeps its place for the way back";

    r.panel->grabKeyboardFocus();
    EXPECT_TRUE(first->isKeyboardFocusedForTest()) << "and comes back with it";
}

// Regression test for FRO1421: Duplicate worked once, then stopped acting on the strip the user had chosen.
TEST(MixerStripClickFocusTest, CmdDCopiesTheFocusedStripsTrackEveryTimeAndFocusStaysOnIt) {
    MixerZonesRig r(2);
    auto& panel = *r.panel;
    ASSERT_TRUE(panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_TRUE(panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), 1);
    const auto source = panel.getStripColumnForTest(1)->getUuid();
    const auto sourceName = r.mc.getTimelineDoc().getTracks()[1].name;
    const int rowBefore = r.mc.getTimelinePanel().getFocusedTrackIndexForTest();

    for (int press = 1; press <= 3; ++press) {
        ASSERT_TRUE(panel.keyPressed(cmdD())) << "press " << press;
        ASSERT_EQ(r.mc.getTimelineDoc().getTracks().size(), (size_t)(2 + press)) << "press " << press;
        const int focused = panel.getFocusedColumnIndexForTest();
        ASSERT_NE(panel.getStripColumnForTest(focused), nullptr);
        EXPECT_EQ(panel.getStripColumnForTest(focused)->getUuid(), source) << "focus stays on the source strip";
    }
    int copies = 0;
    for (const auto& track : r.mc.getTimelineDoc().getTracks())
        copies += track.name == sourceName + " copy" ? 1 : 0;
    EXPECT_EQ(copies, 3) << "every press copied the focused strip's own track";
    EXPECT_EQ(r.mc.getTimelinePanel().getFocusedTrackIndexForTest(), rowBefore)
        << "a duplicate from the mixer leaves the timeline's keyboard row alone";
}

TEST(MixerStripClickFocusTest, CmdDIsLeftToBubbleOnDirectAndMaster) {
    MixerZonesRig r(1);
    auto& panel = *r.panel;
    for (int i = 0; i < panel.getColumnCount(); ++i)
        panel.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    ASSERT_EQ(panel.getFocusedColumnIndexForTest(), panel.getColumnCount() - 1) << "Master, the last column";
    EXPECT_FALSE(panel.keyPressed(cmdD()));
    EXPECT_EQ(r.mc.getTimelineDoc().getTracks().size(), 1u);
}

TEST(MixerStripClickFocusTest, TheStripMenusDuplicateTrackNamesItsKey) {
    MixerZonesRig r(1);
    juce::String shown;
    r.panel->setShowZoneMenuHookForTest([&](juce::PopupMenu& menu) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            if (it.getItem().text == "Duplicate Track")
                shown = it.getItem().shortcutKeyDescription;
    });
    r.panel->showChannelMenuForTest(r.stripId(0));
    const auto expected =
        ShortcutManager::keyPressToDisplayString(r.mc.getShortcutManager().getBinding("timelineDuplicateFocusedTrack"));
    EXPECT_FALSE(expected.isEmpty());
    EXPECT_EQ(shown, expected);
}

// Regression test for FRO1421: Cmd+Up after a click on a strip did not reach the top bar ring.
TEST(MixerStripClickFocusTest, CmdUpAfterAClickOnAStripReachesTheTopBarRing) {
    if (nativeWindowsUnavailable())
        GTEST_SKIP() << "needs a display for a native window";
    MixerZonesRig r(2);
    NativeHost host(*r.panel);
    auto* first = r.panel->getStripColumnForTest(0);
    ASSERT_NE(first, nullptr);
    auto& fader = first->getFaderForTest();
    ASSERT_TRUE(click(fader, fader.getLocalBounds().getCentre()));
    ASSERT_EQ(r.panel->getFocusedColumnIndexForTest(), 0);

    // The key walks up from whatever holds the keyboard, as the real dispatch does.
    auto* target = juce::Component::getCurrentlyFocusedComponent();
    if (target == nullptr || !(target == r.panel || r.panel->isParentOf(target)))
        target = r.panel;
    bool handled = false;
    for (auto* c = target; c != nullptr && !handled; c = c->getParentComponent())
        handled = c->keyPressed(cmdUp());
    EXPECT_TRUE(handled);
    EXPECT_TRUE(r.panel->getTopBarRingForTest().isActive());
}
