// MixerPanelColumnDragTests.cpp: dragging a track strip column by its header reorders the
// timeline's tracks (one undo step), Esc cancels, and buses / Direct / Master stay put. Drives a
// real off-screen MainComponent with hand-built mouse events on the column headers, so the whole
// path is exercised: header hooks, animator, TimelineDoc::moveTrack, the mixer rebuild.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMPCD : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMPCD"; }
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

constexpr int kPitch = 144; // column width 140 + gap 4

struct ColumnDragRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderMPCD>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    explicit ColumnDragRig(int tracks) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getBottomDock().getMixerPanel();
        panel->setSize(1400, 300);
        panel->rebuild();
    }

    std::vector<synth::TrackId> trackOrder() {
        std::vector<synth::TrackId> ids;
        for (const auto& track : mc.getTimelineDoc().getTracks())
            ids.push_back(track.id);
        return ids;
    }
};

// Hand-built header events at content-space x positions, re-deriving the header-local position at
// every event (the column moves under the pointer during a drag, exactly as live).
struct HeaderDrag {
    synth::ui::MixerColumnHeader& header;
    juce::Component& content;
    int pressX;

    juce::Point<float> local(int contentX) const {
        return header.getLocalPoint(&content, juce::Point<float>((float)contentX, 12.0f));
    }
    void down() { header.mouseDown(makeClickEvent(header, local(pressX))); }
    void dragTo(int x) { header.mouseDrag(makeDragEvent(header, local(x), local(pressX))); }
    void up(int x) { header.mouseUp(makeClickEvent(header, local(x))); }
};

// A point on the header's own background (left of the name label), in content coordinates.
int headerGrabX(synth::ui::MixerColumnComponent& column) {
    return column.getX() + column.getHeaderForTest().getX() + 3;
}

} // namespace

TEST(MixerPanelColumnDragTests, DraggingAColumnPastAnotherReordersTheTimelineInOneUndoStep) {
    ColumnDragRig r(3);
    const auto original = r.trackOrder();
    ASSERT_EQ(original.size(), 3u);
    auto* first = r.panel->getStripColumnForTest(0);
    const auto firstNode = first->getNodeId();
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};

    drag.down();
    drag.dragTo(drag.pressX + kPitch + 30);
    EXPECT_TRUE(r.panel->isColumnReorderActiveForTest());
    EXPECT_EQ(first->getX(), kPitch + 30) << "the column follows the pointer at the grab offset";
    drag.dragTo(drag.pressX + 2 * kPitch + 20);
    EXPECT_EQ(first->getX(), 2 * kPitch) << "and is held at the last track slot after crossing a second column";
    EXPECT_EQ(r.trackOrder(), original) << "nothing moves in the timeline until the drop";

    drag.up(drag.pressX + 2 * kPitch + 20);
    first = nullptr; // the drop rebuilds the columns
    const std::vector<synth::TrackId> moved{original[1], original[2], original[0]};
    EXPECT_EQ(r.trackOrder(), moved);
    EXPECT_FALSE(r.panel->isColumnReorderActiveForTest());
    ASSERT_NE(r.panel->getStripColumnForTest(2), nullptr);
    EXPECT_EQ(r.panel->getStripColumnForTest(2)->getNodeId(), firstNode) << "the mixer follows the new order";

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder(), original) << "one undo step puts the tracks back";
    EXPECT_EQ(r.panel->getStripColumnForTest(0)->getNodeId(), firstNode) << "and the mixer with them";

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder().size(), 2u) << "the next undo step is the third track's creation, not a second move";
}

TEST(MixerPanelColumnDragTests, DraggingALaterColumnLeftMovesItsTrackBeforeTheOneItTakesThePlaceOf) {
    ColumnDragRig r(3);
    const auto original = r.trackOrder();
    auto* last = r.panel->getStripColumnForTest(2);
    HeaderDrag drag{last->getHeaderForTest(), *last->getParentComponent(), headerGrabX(*last)};
    drag.down();
    drag.dragTo(drag.pressX - kPitch - 20);
    drag.dragTo(drag.pressX - 2 * kPitch - 300); // far left: held at the first slot
    EXPECT_EQ(last->getX(), 0);
    drag.up(drag.pressX - 2 * kPitch - 300);

    const std::vector<synth::TrackId> moved{original[2], original[0], original[1]};
    EXPECT_EQ(r.trackOrder(), moved);
}

// A strip fed by several tracks sits at its FIRST track; dragging it moves that track.
TEST(MixerPanelColumnDragTests, AStripFedByTwoTracksMovesItsFirstFeedingTrack) {
    ColumnDragRig r(3);
    const auto original = r.trackOrder();
    auto& doc = r.mc.getTimelineDoc();
    const auto shared = doc.getTrack(original[0])->bindingUuid;
    ASSERT_TRUE(shared.isNotEmpty());
    doc.setTrackBinding(original[2], shared); // tracks 0 and 2 now feed the same strip
    r.panel->rebuild();
    const auto sharedNode = r.panel->getStripColumnForTest(0)->getNodeId();

    auto* shareColumn = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{shareColumn->getHeaderForTest(), *shareColumn->getParentComponent(), headerGrabX(*shareColumn)};
    drag.down();
    drag.dragTo(drag.pressX + kPitch + 40); // past the second strip (fed by track 1)
    drag.up(drag.pressX + kPitch + 40);

    const std::vector<synth::TrackId> moved{original[1], original[0], original[2]};
    EXPECT_EQ(r.trackOrder(), moved) << "track 0 moved behind track 1; track 2 stayed where it was";
    EXPECT_EQ(r.panel->getStripColumnForTest(1)->getNodeId(), sharedNode);
}

TEST(MixerPanelColumnDragTests, PressBelowTheThresholdIsAnOrdinaryHeaderClick) {
    ColumnDragRig r(2);
    const auto original = r.trackOrder();
    auto& macros = r.mc.getGraphEditor().getMacros();
    ASSERT_GE(macros.size(), 1);
    bool anySelected = false;
    for (const auto& macro : macros.getAll())
        anySelected = anySelected || r.mc.getGraphEditor().getMacroController().isMacroSelected(macro.id);
    ASSERT_FALSE(anySelected);

    auto* first = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};
    drag.down();
    drag.dragTo(drag.pressX + 2);
    EXPECT_FALSE(r.panel->isColumnReorderActiveForTest());
    EXPECT_EQ(first->getX(), 0);
    drag.up(drag.pressX + 2);

    for (const auto& macro : macros.getAll())
        anySelected = anySelected || r.mc.getGraphEditor().getMacroController().isMacroSelected(macro.id);
    EXPECT_TRUE(anySelected) << "the release selects the channel on the canvas like any header click";
    EXPECT_EQ(r.trackOrder(), original);
}

TEST(MixerPanelColumnDragTests, ADragReleaseDoesNotAlsoSelectTheColumn) {
    ColumnDragRig r(2);
    auto* first = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};
    drag.down();
    drag.dragTo(drag.pressX + 60);
    drag.up(drag.pressX + 60);
    for (const auto& macro : r.mc.getGraphEditor().getMacros().getAll())
        EXPECT_FALSE(r.mc.getGraphEditor().getMacroController().isMacroSelected(macro.id));
}

TEST(MixerPanelColumnDragTests, BusDirectAndMasterHeadersAreNotDragHandles) {
    ColumnDragRig r(2);
    ASSERT_NE(r.panel->createBus(), juce::AudioProcessorGraph::NodeID{});
    r.panel->rebuild();

    EXPECT_TRUE(static_cast<bool>(r.panel->getStripColumnForTest(0)->getHeaderForTest().reorderHooks.onGrab));
    EXPECT_TRUE(static_cast<bool>(r.panel->getStripColumnForTest(1)->getHeaderForTest().reorderHooks.onGrab));
    auto* bus = r.panel->getStripColumnForTest(2);
    ASSERT_NE(bus, nullptr);
    EXPECT_FALSE(static_cast<bool>(bus->getHeaderForTest().reorderHooks.onGrab)) << "a bus has no track to move";
    ASSERT_NE(r.panel->getMasterColumnForTest(), nullptr);
    EXPECT_FALSE(static_cast<bool>(r.panel->getMasterColumnForTest()->getHeaderForTest().reorderHooks.onGrab));
    ASSERT_NE(r.panel->getDirectColumnForTest(), nullptr);
    EXPECT_FALSE(static_cast<bool>(r.panel->getDirectColumnForTest()->getHeaderForTest().reorderHooks.onGrab));
}

// A track column dragged towards the bus / Direct / Master end stays inside the track strips.
TEST(MixerPanelColumnDragTests, ADragIsHeldInsideTheTrackStripsAndNeverPassesABus) {
    ColumnDragRig r(2);
    ASSERT_NE(r.panel->createBus(), juce::AudioProcessorGraph::NodeID{});
    r.panel->rebuild();
    const auto original = r.trackOrder();
    auto* first = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};
    drag.down();
    drag.dragTo(drag.pressX + 5 * kPitch);
    EXPECT_EQ(first->getX(), kPitch) << "held at the last track slot, short of the bus column";
    drag.up(drag.pressX + 5 * kPitch);

    const std::vector<synth::TrackId> moved{original[1], original[0]};
    EXPECT_EQ(r.trackOrder(), moved);
}

// Esc: nothing is committed, and the release that follows neither clicks nor moves a track.
TEST(MixerPanelColumnDragTests, EscapeMidDragCancelsWithoutMovingTracksOrAddingAnUndoStep) {
    ColumnDragRig r(3);
    const auto original = r.trackOrder();
    auto* first = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};
    drag.down();
    drag.dragTo(drag.pressX + kPitch + 40);
    ASSERT_TRUE(r.panel->isColumnReorderActiveForTest());

    ASSERT_TRUE(r.panel->sendEscapeToColumnDragForTest());
    EXPECT_FALSE(r.panel->isColumnReorderActiveForTest());
    EXPECT_EQ(first->getX(), 0) << "the column is back in its origin slot";
    EXPECT_FALSE(r.panel->sendEscapeToColumnDragForTest()) << "the key listener is gone once cancelled";

    drag.up(drag.pressX + kPitch + 40);
    EXPECT_EQ(r.trackOrder(), original);
    for (const auto& macro : r.mc.getGraphEditor().getMacros().getAll())
        EXPECT_FALSE(r.mc.getGraphEditor().getMacroController().isMacroSelected(macro.id))
            << "the release of a cancelled drag is not a click";
    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(r.trackOrder().size(), 2u) << "the last undo step is still the third track's creation";
}

TEST(MixerPanelColumnDragTests, AnUnrelatedRebuildMidDragDiscardsTheGesture) {
    ColumnDragRig r(2);
    auto* first = r.panel->getStripColumnForTest(0);
    HeaderDrag drag{first->getHeaderForTest(), *first->getParentComponent(), headerGrabX(*first)};
    drag.down();
    drag.dragTo(drag.pressX + kPitch + 40);
    ASSERT_TRUE(r.panel->isColumnReorderActiveForTest());
    r.panel->rebuild();
    EXPECT_FALSE(r.panel->isColumnReorderActiveForTest());
    EXPECT_EQ(r.panel->getStripColumnForTest(0)->getX(), 0);
}
