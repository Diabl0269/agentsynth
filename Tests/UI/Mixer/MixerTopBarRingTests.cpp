// MixerTopBarRingTests.cpp -- the mixer's top bar (toolbar buttons, zones pane filter and chips, master pan law) is a
// key ring entered with Cmd+Up from the panel, which keeps real focus. Drives a real, off-screen MainComponent through
// MixerPanelComponent::keyPressed, like MixerPanelKeyboardFocusTests.cpp.
#include "../../TestWait.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MixerZonesTestRig.h"
#include "Modules/ChannelStripModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {

class MockProviderMTBR : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMTBR"; }
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

juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress cmdUp() { return juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0); }

float gainOf(MainComponent& mc, juce::AudioProcessorGraph::NodeID id) {
    auto* node = mc.getAudioEngine().getGraph().getNodeForId(id);
    if (node == nullptr)
        return 0.0f;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
            return f->get();
    return 0.0f;
}

struct RingFixture {
    MainComponent mc{std::make_unique<MockProviderMTBR>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    RingFixture() {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        panel = &mc.getBottomDock().getMixerPanel();
        panel->setSize(1400, 300);
        panel->toggleSidePane(true);
        panel->rebuild();
        panel->resized();
    }
    synth::ui::KeyRing& ring() { return panel->getTopBarRingForTest(); }
    // Home, then Right until the ring sits on `target`.
    void ringTo(juce::Component* target) {
        ring().handleKey(plain(juce::KeyPress::homeKey));
        for (int guard = 0; guard < 40 && ring().getRingedStop() != target; ++guard)
            ring().handleKey(plain(juce::KeyPress::rightKey));
        ASSERT_EQ(ring().getRingedStop(), target);
    }
};

} // namespace

// Regression test for FRO1292: the toolbar, the zones pane's filter and chips and the master pan law had no key path.
TEST(MixerTopBarRingTest, CmdUpEntersAndTheStopsRunInReadingOrder) {
    RingFixture f;
    auto& toolbar = f.panel->getToolbarForTest();
    auto& zones = f.panel->getZonesPaneForTest();
    using Chip = synth::ui::MixerZonesPane::Chip;
    const std::vector<juce::Component*> expected{&f.panel->getSidePaneButton(),
                                                 &toolbar.getSectionToggle(synth::ui::MixerSection::Inserts),
                                                 &toolbar.getSectionToggle(synth::ui::MixerSection::Sends),
                                                 &toolbar.getSectionToggle(synth::ui::MixerSection::Eq),
                                                 &toolbar.getResetMetersButtonForTest(),
                                                 &toolbar.getAddBusButtonForTest(),
                                                 &zones.getFilterForTest(),
                                                 &zones.getChipForTest(Chip::All),
                                                 &zones.getChipForTest(Chip::Tracks),
                                                 &zones.getChipForTest(Chip::Buses),
                                                 &f.panel->getMasterColumnForTest()->getPanLawButtonForTest()};
    EXPECT_FALSE(f.ring().isActive());
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    EXPECT_TRUE(f.ring().isActive());
    EXPECT_EQ(f.ring().getStops(), expected);

    for (size_t i = 1; i < expected.size(); ++i) {
        ASSERT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::rightKey)));
        EXPECT_EQ(f.ring().getRingedStop(), expected[i]) << "stop " << i;
    }
    ASSERT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::homeKey)));
    EXPECT_EQ(f.ring().getRingedStop(), expected.front());
    ASSERT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::endKey)));
    EXPECT_EQ(f.ring().getRingedStop(), expected.back());
}

TEST(MixerTopBarRingTest, ClosingTheSidePaneDropsTheZonesStops) {
    RingFixture f;
    f.panel->toggleSidePane();
    f.panel->resized();
    ASSERT_FALSE(f.panel->getZonesPaneForTest().isShowing() && f.panel->getZonesPaneForTest().isVisible());
    const auto stops = f.ring().getStops();
    EXPECT_EQ(std::find(stops.begin(), stops.end(), &f.panel->getZonesPaneForTest().getFilterForTest()), stops.end());
    EXPECT_NE(std::find(stops.begin(), stops.end(), &f.panel->getToolbarForTest().getAddBusButtonForTest()),
              stops.end());
}

TEST(MixerTopBarRingTest, ReturnOnAddBusAddsABus) {
    RingFixture f;
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    f.ringTo(&f.panel->getToolbarForTest().getAddBusButtonForTest());
    const int before = f.panel->getColumnCount();
    EXPECT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue(); // a button's click is delivered as a message
    EXPECT_EQ(f.panel->getColumnCount(), before + 1);
}

TEST(MixerTopBarRingTest, DownLeavesAndShiftUpNudgesTheFaderAgain) {
    RingFixture f;
    f.panel->keyPressed(plain(juce::KeyPress::rightKey)); // focus strip 0
    const auto nodeId = f.panel->getStripColumnForTest(0)->getNodeId();
    const float before = gainOf(f.mc, nodeId);

    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    EXPECT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::upKey))) << "the ring swallows Up";
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f) << "Up does not touch the fader while the ring is up";
    EXPECT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.ring().isActive());
    EXPECT_NEAR(gainOf(f.mc, nodeId), before, 1.0e-3f);

    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(gainOf(f.mc, nodeId), before + 1.0f, 1.0e-3f);
}

// Regression test for FRO1524: with no strip focused, Down from the bar left an unringed panel.
TEST(MixerTopBarRingTest, DownFromTheBarLandsOnTheFirstStripInOnePress) {
    RingFixture f;
    ASSERT_EQ(f.panel->getFocusedColumnIndexForTest(), -1);
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    EXPECT_FALSE(f.panel->isFocusedColumnOutlineShownForTest());

    EXPECT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.ring().isActive());
    EXPECT_EQ(f.panel->getFocusedColumnIndexForTest(), 0);
    EXPECT_TRUE(f.panel->isFocusedColumnOutlineShownForTest());
    EXPECT_TRUE(f.panel->getStripColumnForTest(0)->isKeyboardFocusedForTest());
}

TEST(MixerTopBarRingTest, EscapeAndLosingFocusLeaveTheRing) {
    RingFixture f;
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    EXPECT_TRUE(f.panel->keyPressed(plain(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.ring().isActive());
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    f.panel->focusLost(juce::Component::focusChangedDirectly);
    EXPECT_FALSE(f.ring().isActive());
}

TEST(MixerTopBarRingTest, TheEntryKeyIsRebindable) {
    RingFixture f;
    ShortcutManager shortcuts;
    f.panel->setShortcutManager(&shortcuts);
    shortcuts.setBinding("mixerFocusTopBar", juce::KeyPress('t', juce::ModifierKeys(), 0));
    f.panel->keyPressed(cmdUp());
    EXPECT_FALSE(f.ring().isActive()) << "the old chord no longer enters";
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress('t', juce::ModifierKeys(), 0)));
    EXPECT_TRUE(f.ring().isActive());
    f.panel->setShortcutManager(nullptr);
}

TEST(MixerTopBarRingTest, TheScreenReaderValueNamesTheRingedControlAndChangesWithEveryMove) {
    RingFixture f;
    auto handler = f.panel->createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    ASSERT_NE(handler->getValueInterface(), nullptr);
    const auto value = [&] { return handler->getValueInterface()->getCurrentValueAsString(); };

    EXPECT_TRUE(value().isEmpty());
    ASSERT_TRUE(f.panel->keyPressed(cmdUp()));
    const auto first = value();
    EXPECT_TRUE(first.isNotEmpty());
    f.panel->keyPressed(plain(juce::KeyPress::rightKey));
    EXPECT_EQ(value(), "Inserts");
    EXPECT_NE(value(), first);
    f.panel->keyPressed(plain(juce::KeyPress::rightKey));
    EXPECT_EQ(value(), "Sends");
    f.panel->keyPressed(plain(juce::KeyPress::escapeKey));
    EXPECT_TRUE(value().isEmpty());
}

// ---- MixerMouseDragUndoTests ----
// MixerMouseDragUndoTests.cpp: a mouse drag of a mixer strip's knobs is one undo step, like the key nudge beside it.
// The knobs are attached to their parameters (the attachment brackets the parameter gesture but records nothing), so
// each test drives the real mouse handlers of the slider (press, several moves, release) and then undoes once.

namespace {

// One drag of a rotary knob upwards: press at the centre, move in steps, release.
void dragUp(juce::Slider& knob, int pixels) {
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const auto start = knob.getLocalBounds().getCentre().toFloat();
    knob.mouseDown(makeClickEvent(knob, start, mods));
    for (int y = 1; y <= pixels; y += 4)
        knob.mouseDrag(makeDragEvent(knob, start.translated(0.0f, (float)-y), start, mods));
    knob.mouseUp(makeDragEvent(knob, start.translated(0.0f, (float)-pixels), start));
}

} // namespace

// Dragging a strip's pan knob with the mouse is one undo step, and one Cmd+Z restores the old pan.
TEST(MixerMouseDragUndo, APanKnobDragIsOneUndoStepAndUndoRestoresThePan) {
    MixerZonesRig r(1);
    auto* column = r.panel->getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    auto& pan = column->getPanSliderForTest();
    ASSERT_NEAR(pan.getValue(), 0.0, 1.0e-6);

    r.mc.getUndoManager().clearUndoHistory();
    dragUp(pan, 24);
    EXPECT_TRUE(r.mc.getUndoManager().canUndo()) << "the drag recorded a step";
    const double dragged = pan.getValue();
    EXPECT_GT(std::abs(dragged), 0.05) << "the drag moved the pan";

    ASSERT_TRUE(r.mc.getUndoManager().undo()) << "the drag recorded a step";
    column = r.panel->getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    EXPECT_NEAR(column->getPanSliderForTest().getValue(), 0.0, 1.0e-4) << "one undo takes the whole drag back";
}

TEST(MixerMouseDragUndo, APressOnThePanKnobWithoutMovingRecordsNothing) {
    MixerZonesRig r(1);
    auto* column = r.panel->getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    auto& undo = r.mc.getUndoManager();
    const bool couldUndoBefore = undo.canUndo();
    auto& pan = column->getPanSliderForTest();
    const auto at = pan.getLocalBounds().getCentre().toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    pan.mouseDown(makeClickEvent(pan, at, mods));
    pan.mouseUp(makeClickEvent(pan, at));
    EXPECT_EQ(undo.canUndo(), couldUndoBefore) << "no change, no undo step";
}

// The send rows' level and pan knobs: a drag is one undo step too.
TEST(MixerMouseDragUndo, ASendKnobDragIsOneUndoStepAndUndoRestoresIt) {
    MixerZonesRig r(1);
    const auto bus = r.panel->createBus();
    r.panel->rebuild();
    auto* column = r.panel->getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(bus);
    r.panel->rebuild();

    for (const bool pan : {false, true}) {
        column = r.panel->getStripColumnForTest(0);
        ASSERT_NE(column, nullptr);
        auto& sends = column->getSendListForTest();
        auto* knob = pan ? sends.getPanKnobForTest(0) : sends.getKnobForTest(0);
        ASSERT_NE(knob, nullptr);
        const double before = knob->getValue();

        r.mc.getUndoManager().clearUndoHistory();
        dragUp(*knob, 24);
        EXPECT_NE(knob->getValue(), before) << (pan ? "send pan" : "send level") << " moved";

        ASSERT_TRUE(r.mc.getUndoManager().undo()) << (pan ? "send pan" : "send level") << " drag recorded a step";
        column = r.panel->getStripColumnForTest(0);
        ASSERT_NE(column, nullptr);
        auto* restored =
            pan ? column->getSendListForTest().getPanKnobForTest(0) : column->getSendListForTest().getKnobForTest(0);
        ASSERT_NE(restored, nullptr);
        EXPECT_NEAR(restored->getValue(), before, 1.0e-3) << (pan ? "send pan" : "send level");
    }
}
