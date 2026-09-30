// MixerSendListDragTests.cpp (docs/mixer/sends-and-buses.md#reordering-sends): the
// drag-to-reorder gesture on a send row's name area, driven through REAL synthesized
// mouseDown/mouseDrag/mouseUp (docs/development/test-patterns.md's real-mouse-path convention,
// MixerFaderDragTests.cpp's template) rather than the headless moveRow() seam directly -- this is
// what actually proves the threshold/insertion-boundary mechanics work, not just the Core swap.
//
//   * a real drag past the threshold reorders the rows, as ONE undo step, and undo restores the
//     cables and the row order together
//   * during the drag nothing is committed: the held row follows the pointer, the other row glides
//     into the vacated slot (headless, so the animator lands every glide at once); Esc puts both
//     back and commits nothing
//   * the target-name area shows the grab cursor, the buttons and knobs do not
//   * a press that never crosses the threshold is a plain click: it still opens the target menu
//     (setShowMenuHookForTest, the same "PopupMenu never runs headless" seam
//     MixerColumnComponent::setShowContextMenuHookForTest already uses elsewhere) rather than
//     reordering anything

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// Same minimal mock as every other headless MainComponent test in this suite -- a unique name to
// avoid an ODR clash across test translation units.
class MockProviderMSLDT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSLDT"; }
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

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

juce::MouseEvent sendListMouseEvent(juce::Component& comp, juce::Point<float> pos, juce::Point<float> mouseDownPos,
                                    bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

/** One audio track (its own channel strip) plus two empty buses, with two active sends already
 *  wired -- slot 0 -> busA (row 0), slot 1 -> busB (row 1) -- and the panel really laid out at a
 *  pixel size, same convention MixerSendListTests.cpp's MuteButtonClick test uses. */
struct DragRig {
    std::unique_ptr<MainComponent> mc;
    NodeID busA, busB;

    DragRig() {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderMSLDT>());
        mc->setSize(1400, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        mc->simulateAddAudioTrackClick();
        busA = panel().createBus();
        busB = panel().createBus();
        panel().rebuild();

        // Re-fetch the column between mutations: addSendTo() -> recordGraphAndMacroChange fires
        // onMutated -> ... -> MainComponent::reconcileTimelineAfterGraphChange, which rebuilds every
        // MixerColumnComponent (bottomDock.rebuildMixer()) -- a `column` pointer captured before one
        // addSendTo() call is dangling by the time a SECOND call would use it.
        sourceColumn()->getSendListForTest().addSendTo(busA);
        sourceColumn()->getSendListForTest().addSendTo(busB);
        panel().rebuild();
        panel().setSize(1400, 300);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    synth::ui::MixerColumnComponent* sourceColumn() { return panel().getStripColumnForTest(0); }
};

} // namespace

TEST(MixerSendListDragTests, RealDragPastTheThresholdReordersAsOneUndoStepAndUndoRestoresIt) {
    DragRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    const auto sourceId = column->getNodeId();
    auto& sendList = column->getSendListForTest();
    ASSERT_EQ(sendList.getRowCountForTest(), 2);

    ASSERT_EQ(synth::findSendTarget(rig.graph(), sourceId, 0), rig.busA);
    ASSERT_EQ(synth::findSendTarget(rig.graph(), sourceId, 1), rig.busB);

    auto* row0Knob = sendList.getKnobForTest(0);
    auto* row1Knob = sendList.getKnobForTest(1);
    ASSERT_NE(row0Knob, nullptr);
    ASSERT_NE(row1Knob, nullptr);
    ASSERT_GT(row0Knob->getHeight(), 0) << "the row must be really laid out for a real drag to land";

    const float nameAreaX = 5.0f; // well left of the M button / knobs / PRE-POST / x (kRemoveWidth+...)
    const juce::Point<float> downPos(nameAreaX, (float)row0Knob->getBounds().getCentreY());
    const juce::Point<float> dragPos(nameAreaX, (float)row1Knob->getBounds().getCentreY() + 8.0f);

    sendList.mouseDown(sendListMouseEvent(sendList, downPos, downPos, false));
    sendList.mouseDrag(sendListMouseEvent(sendList, dragPos, downPos, true));
    sendList.mouseUp(sendListMouseEvent(sendList, dragPos, downPos, true));

    rig.panel().rebuild();

    auto* afterDrag = rig.sourceColumn();
    ASSERT_NE(afterDrag, nullptr);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), afterDrag->getNodeId(), 1), rig.busA)
        << "the row that was on top now feeds bus A from slot 1";
    EXPECT_EQ(synth::findSendTarget(rig.graph(), afterDrag->getNodeId(), 0), rig.busB);

    // ONE undo() call must restore everything the drag changed -- the whole gesture is a single
    // recordGraphTimelineAndMacroChange transaction (MixerPanelComponent::moveSendRow).
    ASSERT_TRUE(rig.mc->getUndoManager().undo());
    rig.panel().rebuild();
    auto* afterUndo = rig.sourceColumn();
    ASSERT_NE(afterUndo, nullptr);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), afterUndo->getNodeId(), 0), rig.busA)
        << "undo restores the original cables";
    EXPECT_EQ(synth::findSendTarget(rig.graph(), afterUndo->getNodeId(), 1), rig.busB);
}

TEST(MixerSendListDragTests, AClickWithoutMovementStillOpensTheTargetMenuAndReordersNothing) {
    DragRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    const auto sourceId = column->getNodeId();
    auto& sendList = column->getSendListForTest();

    bool menuShown = false;
    int itemCountSeen = 0;
    sendList.setShowMenuHookForTest([&](juce::PopupMenu& menu) {
        menuShown = true;
        itemCountSeen = menu.getNumItems();
    });

    auto* row0Knob = sendList.getKnobForTest(0);
    ASSERT_NE(row0Knob, nullptr);
    const juce::Point<float> pos(5.0f, (float)row0Knob->getBounds().getCentreY());

    const int editSerialBefore = rig.mc->getUndoManager().getEditSerial();
    sendList.mouseDown(sendListMouseEvent(sendList, pos, pos, false));
    sendList.mouseUp(sendListMouseEvent(sendList, pos, pos, false));

    EXPECT_TRUE(menuShown) << "a click that never crossed the drag threshold still opens the target menu";
    EXPECT_GT(itemCountSeen, 0);
    EXPECT_EQ(rig.mc->getUndoManager().getEditSerial(), editSerialBefore) << "no reorder, no undo step";
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 0), rig.busA) << "nothing moved";
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 1), rig.busB);
}

namespace {
constexpr float kNameX = 5.0f; // well left of the M button / knobs / PRE-POST / x

// A synthesized drag on row 0's name area, in the send list's own coordinates.
struct RowDrag {
    synth::ui::MixerSendList& list;
    juce::Point<float> down;

    explicit RowDrag(synth::ui::MixerSendList& l)
        : list(l)
        , down(kNameX, (float)l.getKnobForTest(0)->getBounds().getCentreY()) {}

    void press() { list.mouseDown(sendListMouseEvent(list, down, down, false)); }
    void dragBy(float dy) { list.mouseDrag(sendListMouseEvent(list, down.translated(0.0f, dy), down, true)); }
    void release(float dy) { list.mouseUp(sendListMouseEvent(list, down.translated(0.0f, dy), down, dy != 0.0f)); }
};
} // namespace

TEST(MixerSendListDragTests, TheReorderIsCommittedOnceOnReleaseAndNotWhileHeld) {
    DragRig rig;
    auto* column = rig.sourceColumn();
    ASSERT_NE(column, nullptr);
    const auto sourceId = column->getNodeId();
    auto& sendList = column->getSendListForTest();
    int commits = 0;
    const auto original = sendList.moveSendRow;
    sendList.moveSendRow = [&](NodeID strip, int from, int to) {
        ++commits;
        return original(strip, from, to);
    };

    RowDrag drag(sendList);
    drag.press();
    drag.dragBy(12.0f);
    drag.dragBy(25.0f);
    EXPECT_EQ(commits, 0) << "nothing is committed while the row is held";
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 0), rig.busA);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 1), rig.busB);
    drag.release(25.0f);

    EXPECT_EQ(commits, 1);
}

TEST(MixerSendListDragTests, TheHeldRowFollowsThePointerAndTheOtherRowGlidesIntoItsSlot) {
    DragRig rig;
    auto& sendList = rig.sourceColumn()->getSendListForTest();
    ASSERT_EQ(sendList.getRowTopForTest(0), 0);
    ASSERT_EQ(sendList.getRowTopForTest(1), 20);

    RowDrag drag(sendList);
    drag.press();
    drag.dragBy(12.0f);
    EXPECT_TRUE(sendList.isRowDragActiveForTest());
    EXPECT_EQ(sendList.getRowTopForTest(0), 12) << "the lifted row sits where the pointer holds it";
    EXPECT_EQ(sendList.getRowTopForTest(1), 20) << "not yet past the neighbour's midpoint";

    drag.dragBy(25.0f);
    EXPECT_EQ(sendList.getRowTopForTest(0), 20) << "held at the end of the list";
    EXPECT_EQ(sendList.getRowTopForTest(1), 0) << "the other row moved up into the vacated slot";
    drag.release(25.0f);
}

TEST(MixerSendListDragTests, EscapeMidDragCommitsNothingAndPutsBothRowsBack) {
    DragRig rig;
    auto* column = rig.sourceColumn();
    const auto sourceId = column->getNodeId();
    auto& sendList = column->getSendListForTest();
    int commits = 0;
    const auto original = sendList.moveSendRow;
    sendList.moveSendRow = [&](NodeID strip, int from, int to) {
        ++commits;
        return original(strip, from, to);
    };
    const int serialBefore = rig.mc->getUndoManager().getEditSerial();

    RowDrag drag(sendList);
    drag.press();
    drag.dragBy(25.0f);
    ASSERT_TRUE(sendList.isRowDragActiveForTest());
    EXPECT_TRUE(sendList.sendEscapeToRowDragForTest());
    drag.release(25.0f);

    EXPECT_EQ(commits, 0);
    EXPECT_EQ(rig.mc->getUndoManager().getEditSerial(), serialBefore) << "no undo step";
    EXPECT_FALSE(sendList.isRowDragActiveForTest());
    EXPECT_EQ(sendList.getRowTopForTest(0), 0);
    EXPECT_EQ(sendList.getRowTopForTest(1), 20);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 0), rig.busA);
    EXPECT_EQ(synth::findSendTarget(rig.graph(), sourceId, 1), rig.busB);
}

TEST(MixerSendListDragTests, TheDropSettleIsHandedToTheRebuiltListOfTheSameStrip) {
    DragRig rig;
    auto& oldList = rig.sourceColumn()->getSendListForTest();
    EXPECT_EQ(oldList.getLastSettleForTest().row, -1);

    RowDrag drag(oldList);
    drag.press();
    drag.dragBy(25.0f); // held at the end of the list: y 20
    drag.release(25.0f);

    // The commit rebuilt the mixer, so this is a NEW list (the old one is gone).
    EXPECT_FALSE(rig.panel().hasPendingSendSettleForTest()) << "consumed and cleared by the rebuild";
    const auto settle = rig.sourceColumn()->getSendListForTest().getLastSettleForTest();
    EXPECT_EQ(settle.row, 1) << "the dropped row's final index";
    EXPECT_FLOAT_EQ(settle.fromY, 20.0f) << "where it was drawn when released";
    EXPECT_FALSE(rig.sourceColumn()->getSendListForTest().isSettlingForTest()) << "headless lands at once";
}

TEST(MixerSendListDragTests, ARefusedOrUnmovedDropLeavesNoPendingSettle) {
    DragRig rig;
    auto& list = rig.sourceColumn()->getSendListForTest();
    RowDrag drag(list);
    drag.press();
    drag.dragBy(6.0f); // lifted, but not past the neighbour: released where it started
    drag.release(6.0f);
    EXPECT_FALSE(rig.panel().hasPendingSendSettleForTest());
    EXPECT_EQ(rig.sourceColumn()->getSendListForTest().getLastSettleForTest().row, -1);
}

TEST(MixerSendListDragTests, TheTargetNameAreaShowsTheGrabCursorAndTheButtonsDoNot) {
    DragRig rig;
    auto& sendList = rig.sourceColumn()->getSendListForTest();
    const float y = (float)sendList.getKnobForTest(0)->getBounds().getCentreY();

    const juce::Point<float> onName(kNameX, y);
    sendList.mouseMove(sendListMouseEvent(sendList, onName, onName, false));
    EXPECT_TRUE(sendList.getMouseCursor() == synth::ui::dragGrabCursor());

    const juce::Point<float> onRemove((float)sendList.getWidth() - 4.0f, y);
    sendList.mouseMove(sendListMouseEvent(sendList, onRemove, onRemove, false));
    EXPECT_FALSE(sendList.getMouseCursor() == synth::ui::dragGrabCursor());
}
