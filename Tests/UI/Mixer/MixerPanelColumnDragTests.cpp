// MixerPanelColumnDragTests.cpp: dragging a track strip column by its header reorders the
// timeline's tracks (one undo step), Esc cancels, and buses / Direct / Master stay put. Drives a
// real off-screen MainComponent with hand-built mouse events on the column headers, so the whole
// path is exercised: header hooks, animator, TimelineDoc::moveTrack, the mixer rebuild.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesPane.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesRow.h"
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

TEST(MixerPanelColumnDragTests, DirectAndMasterHeadersAreNotDragHandlesButBusHeadersAre) {
    ColumnDragRig r(2);
    ASSERT_NE(r.panel->createBus(), juce::AudioProcessorGraph::NodeID{});
    r.panel->rebuild();

    EXPECT_TRUE(static_cast<bool>(r.panel->getStripColumnForTest(0)->getHeaderForTest().reorderHooks.onGrab));
    EXPECT_TRUE(static_cast<bool>(r.panel->getStripColumnForTest(1)->getHeaderForTest().reorderHooks.onGrab));
    auto* bus = r.panel->getStripColumnForTest(2);
    ASSERT_NE(bus, nullptr);
    EXPECT_TRUE(static_cast<bool>(bus->getHeaderForTest().reorderHooks.onGrab)) << "buses reorder among buses";
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

namespace {

std::vector<juce::AudioProcessorGraph::NodeID> columnNodes(synth::ui::MixerPanelComponent& panel, int count) {
    std::vector<juce::AudioProcessorGraph::NodeID> ids;
    for (int i = 0; i < count; ++i)
        ids.push_back(panel.getStripColumnForTest(i)->getNodeId());
    return ids;
}

// The ids of the bus rows in the side pane, in the order the pane lists them.
std::vector<juce::String> paneBusIds(synth::ui::MixerPanelComponent& panel) {
    std::vector<juce::String> ids;
    auto& pane = panel.getZonesPaneForTest();
    for (int i = 0; i < pane.getRowCountForTest(); ++i)
        if (pane.getRowForTest(i)->getChannel().kind == synth::ui::MixerZoneChannelKind::Bus)
            ids.push_back(pane.getRowForTest(i)->getChannel().id);
    return ids;
}

// Two tracks (columns 0-1) and two buses (columns 2-3).
struct BusDragRig : ColumnDragRig {
    BusDragRig()
        : ColumnDragRig(2) {
        panel->createBus();
        panel->createBus();
        panel->rebuild();
    }
};

} // namespace

TEST(MixerPanelColumnDragTests, DraggingABusBeforeAnotherSavesTheOrderInOneUndoStep) {
    BusDragRig r;
    const auto original = columnNodes(*r.panel, 4);
    const auto trackOrder = r.trackOrder();
    const auto originalPane = paneBusIds(*r.panel);
    ASSERT_EQ(originalPane.size(), 2u);
    EXPECT_TRUE(r.panel->getViewDoc().getBusOrder().empty());

    auto* second = r.panel->getStripColumnForTest(3);
    HeaderDrag drag{second->getHeaderForTest(), *second->getParentComponent(), headerGrabX(*second)};
    drag.down();
    drag.dragTo(drag.pressX - kPitch - 20);
    EXPECT_TRUE(r.panel->isColumnReorderActiveForTest());
    drag.up(drag.pressX - kPitch - 20);
    second = nullptr; // the drop rebuilds the columns

    const auto reordered = columnNodes(*r.panel, 4);
    EXPECT_EQ(reordered,
              (std::vector<juce::AudioProcessorGraph::NodeID>{original[0], original[1], original[3], original[2]}));
    EXPECT_EQ(r.panel->getViewDoc().getBusOrder(), (std::vector<juce::String>{originalPane[1], originalPane[0]}));
    EXPECT_EQ(paneBusIds(*r.panel), (std::vector<juce::String>{originalPane[1], originalPane[0]}))
        << "the side pane lists the buses in the same order";
    EXPECT_EQ(r.trackOrder(), trackOrder) << "a bus drag never moves a track";
    EXPECT_FALSE(r.panel->isColumnReorderActiveForTest());

    ASSERT_TRUE(r.mc.getUndoManager().undo());
    EXPECT_EQ(columnNodes(*r.panel, 4), original) << "one undo step puts the buses back";
    EXPECT_TRUE(r.panel->getViewDoc().getBusOrder().empty());
}

TEST(MixerPanelColumnDragTests, ABusDragIsHeldAmongTheBusesAndATrackDragNeverMovesABus) {
    BusDragRig r;
    const auto original = columnNodes(*r.panel, 4);

    auto* bus = r.panel->getStripColumnForTest(2);
    HeaderDrag busDrag{bus->getHeaderForTest(), *bus->getParentComponent(), headerGrabX(*bus)};
    busDrag.down();
    busDrag.dragTo(busDrag.pressX - 3 * kPitch);
    EXPECT_EQ(bus->getX(), 2 * kPitch) << "held at the first bus slot, short of the track strips";
    busDrag.up(busDrag.pressX - 3 * kPitch);
    EXPECT_EQ(columnNodes(*r.panel, 4), original);
    EXPECT_TRUE(r.panel->getViewDoc().getBusOrder().empty()) << "dropping in place saves nothing";

    const auto tracks = r.trackOrder();
    auto* track = r.panel->getStripColumnForTest(0);
    HeaderDrag trackDrag{track->getHeaderForTest(), *track->getParentComponent(), headerGrabX(*track)};
    trackDrag.down();
    trackDrag.dragTo(trackDrag.pressX + 5 * kPitch);
    EXPECT_EQ(track->getX(), kPitch) << "held at the last track slot, short of the buses";
    trackDrag.up(trackDrag.pressX + 5 * kPitch);
    EXPECT_EQ(r.panel->getStripColumnForTest(2)->getNodeId(), original[2]);
    EXPECT_EQ(r.panel->getStripColumnForTest(3)->getNodeId(), original[3]);
    EXPECT_TRUE(r.panel->getViewDoc().getBusOrder().empty()) << "a track drag does not touch the bus order";
    EXPECT_NE(r.trackOrder(), tracks);
}

TEST(MixerPanelColumnDragTests, TheBusOrderSurvivesSavingAndReloadingTheProject) {
    BusDragRig r;
    const auto originalPane = paneBusIds(*r.panel);
    auto* second = r.panel->getStripColumnForTest(3);
    HeaderDrag drag{second->getHeaderForTest(), *second->getParentComponent(), headerGrabX(*second)};
    drag.down();
    drag.dragTo(drag.pressX - kPitch - 20);
    drag.up(drag.pressX - kPitch - 20);
    ASSERT_EQ(paneBusIds(*r.panel), (std::vector<juce::String>{originalPane[1], originalPane[0]}));

    const auto bundle = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("busorder-" + juce::Uuid().toString())
                            .withFileExtension(synth::ProjectBundle::kBundleExtension);
    ASSERT_TRUE(r.mc.saveProjectForTest(bundle));
    r.mc.newPatchForTest();
    ASSERT_TRUE(r.mc.openProjectForTest(bundle));
    bundle.deleteRecursively();

    auto& panel = r.mc.getBottomDock().getMixerPanel();
    panel.rebuild();
    EXPECT_EQ(panel.getViewDoc().getBusOrder(), (std::vector<juce::String>{originalPane[1], originalPane[0]}));
    EXPECT_EQ(paneBusIds(panel), (std::vector<juce::String>{originalPane[1], originalPane[0]}))
        << "NodeIDs are reassigned on load, the saved uuids still order the buses";
}

// The header is a grab handle only while it has reorder hooks (track strips and buses); a Direct or
// Master header is a plain click target and keeps the normal cursor.
TEST(MixerPanelColumnDragTests, OnlyAHeaderWithReorderHooksShowsTheDraggingHandCursor) {
    ColumnDragRig r(2);
    ASSERT_NE(r.panel->createBus(), juce::AudioProcessorGraph::NodeID{});
    r.panel->rebuild();

    auto* strip = r.panel->getStripColumnForTest(0);
    ASSERT_NE(strip, nullptr);
    auto& stripHeader = strip->getHeaderForTest();
    EXPECT_TRUE(stripHeader.getMouseCursor() == juce::MouseCursor::DraggingHandCursor) << "hover";

    HeaderDrag drag{stripHeader, *strip->getParentComponent(), headerGrabX(*strip)};
    drag.down();
    drag.dragTo(drag.pressX + kPitch);
    EXPECT_TRUE(stripHeader.getMouseCursor() == juce::MouseCursor::DraggingHandCursor) << "during the drag";
    drag.up(drag.pressX + kPitch);
    strip = nullptr; // the drop rebuilds the columns

    auto* bus = r.panel->getStripColumnForTest(2);
    ASSERT_NE(bus, nullptr);
    EXPECT_TRUE(bus->getHeaderForTest().getMouseCursor() == juce::MouseCursor::DraggingHandCursor)
        << "a bus reorders among the buses";
    ASSERT_NE(r.panel->getMasterColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getMasterColumnForTest()->getHeaderForTest().getMouseCursor() ==
                juce::MouseCursor::NormalCursor);
    ASSERT_NE(r.panel->getDirectColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getDirectColumnForTest()->getHeaderForTest().getMouseCursor() ==
                juce::MouseCursor::NormalCursor);
}

// The name label and the source line under it are children of the handle, and JUCE asks the deepest
// component for the cursor, so each delegates to the header.
TEST(MixerPanelColumnDragTests, TheNameLabelAndSourceLineShowTheGrabHandOnlyWhileTheColumnCanBeDragged) {
    ColumnDragRig r(2);
    auto* strip = r.panel->getStripColumnForTest(0);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->getHeaderForTest().getNameLabelForTest().getMouseCursor() ==
                juce::MouseCursor::DraggingHandCursor);
    EXPECT_TRUE(strip->getSourceLineLabelForTest().getMouseCursor() == juce::MouseCursor::DraggingHandCursor);

    ASSERT_NE(r.panel->getMasterColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getMasterColumnForTest()->getHeaderForTest().getNameLabelForTest().getMouseCursor() ==
                juce::MouseCursor::NormalCursor);
    ASSERT_NE(r.panel->getDirectColumnForTest(), nullptr);
    EXPECT_TRUE(r.panel->getDirectColumnForTest()->getHeaderForTest().getNameLabelForTest().getMouseCursor() ==
                juce::MouseCursor::NormalCursor);

    strip->getHeaderForTest().reorderHooks = {};
    EXPECT_TRUE(strip->getSourceLineLabelForTest().getMouseCursor() == juce::MouseCursor::NormalCursor);
}

TEST(MixerPanelColumnDragTests, PressingAndDraggingTheSourceLineGrabsAndMovesTheColumn) {
    ColumnDragRig r(2);
    auto* strip = r.panel->getStripColumnForTest(0);
    ASSERT_NE(strip, nullptr);
    auto& header = strip->getHeaderForTest();
    auto& line = strip->getSourceLineLabelForTest();
    int grabs = 0, drags = 0;
    auto hooks = header.reorderHooks;
    hooks.onGrab = [&](const juce::MouseEvent&) { ++grabs; };
    hooks.onDrag = [&](const juce::MouseEvent&) { ++drags; };
    header.reorderHooks = hooks;

    // Events whose source is the source line, as the header receives them through its mouse listener.
    const auto down = makeClickEvent(line, {4.0f, 4.0f});
    const auto drag = makeDragEvent(line, {20.0f, 4.0f}, {4.0f, 4.0f});
    header.mouseDown(down);
    header.mouseDrag(drag);
    EXPECT_EQ(grabs, 1);
    EXPECT_EQ(drags, 1);

    int selected = 0;
    header.onHeaderClicked = [&] { ++selected; };
    header.reorderHooks = {};
    header.mouseUp(down);
    EXPECT_EQ(selected, 1) << "a plain click on the source line selects the column like the header background";
}
