// AutomationLanesHeightTests.cpp -- each row's own height: the strip on the bottom edge of a lane row and of each
// modulator row (real mouse events on the real handle), the keys of a focused lane header or modulator row, and every
// other row keeping its size.

#include "AutomationLanesMenuFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/EdgeResizeHandle.h"

using namespace lane_menu_test;
using namespace automation_lanes_test;
using synth::ui::EdgeResizeHandle;
using synth::ui::ModulatorInfo;

namespace {

struct ModulatingHost : PickHost {
    std::vector<ModulatorInfo> modulators;
    // Only the lane on the modulators' parameter carries them.
    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String& paramId) override {
        std::vector<ModulatorInfo> found;
        if (!uuid.startsWith("atten-"))
            for (const auto& info : modulators)
                if (info.paramId == paramId)
                    found.push_back(info);
        return found;
    }
    bool canModulate(const juce::String&, const juce::String&) override { return true; }
};

// "Bass" with lanes cutoff and res, open.
struct HeightPanel : LanesPanel {
    ModulatingHost host;
    synth::TrackId bass;
    synth::LaneId cutoff, res;

    HeightPanel() {
        bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
        cutoff = addLane(bass, "cutoff");
        res = addLane(bass, "res");
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(bass, true);
    }
    ~HeightPanel() { panel.setTrackHeaderHost(nullptr); }

    int rowHeight(synth::LaneId id) { return panel.laneHeaderForTest(id)->getHeight(); }
    EdgeResizeHandle& handle(synth::LaneId id) {
        auto* h = panel.getAutomationLanes().heightHandleFor(id);
        EXPECT_NE(h, nullptr);
        return *h;
    }
    double scale(synth::LaneId id) { return doc.getLane(id)->heightScale; }

    // The press, drag and release of a real pointer: fixed on screen while the handle moves under it.
    static void drag(EdgeResizeHandle& h, int delta, int steps = 4) {
        const auto start = h.localPointToGlobal(h.getLocalBounds().getCentre().toFloat());
        const auto local = [&](float t) {
            return h.getLocalPoint(nullptr, start + juce::Point<float>(0.0f, (float)delta * t));
        };
        h.mouseDown(makeClickEvent(h, local(0.0f), leftButton()));
        for (int i = 1; i <= steps; ++i)
            h.mouseDrag(makeDragEvent(h, local((float)i / (float)steps), local(0.0f), leftButton()));
        h.mouseUp(makeDragEvent(h, local(1.0f), local(0.0f), leftButton()));
    }

    void addModulator(int count = 1) {
        host.modulators.clear();
        for (int i = 1; i <= count; ++i) {
            ModulatorInfo info;
            info.sourceUuid = "lfo-" + juce::String(i);
            info.sourceTitle = "LFO " + juce::String(i);
            info.isLfo = true;
            info.attenuverterUuid = "atten-" + juce::String(i);
            info.paramId = "cutoff";
            info.targetChannel = 2;
            host.modulators.push_back(info);
        }
        panel.setTrackAutomationExpanded(bass, false);
        panel.setTrackAutomationExpanded(bass, true);
    }
    synth::ui::ModulatorRow& mod(int index) {
        auto* row = panel.modulatorRowForTest(cutoff, index);
        EXPECT_NE(row, nullptr);
        return *row;
    }
    EdgeResizeHandle& modHandle(int index) {
        auto* h = panel.getAutomationLanes().heightHandleFor(cutoff, index);
        EXPECT_NE(h, nullptr);
        return *h;
    }
    double modScale(const juce::String& key) { return doc.getLane(cutoff)->modulatorRowScale(key); }
    int trackBottom() {
        const auto layout = panel.getClipLaneArea().getRowLayout();
        return layout.trackTop(0) + layout.trackRowHeight(0) + layout.trackExtraHeight(0);
    }
};

juce::KeyPress altKey(int code) { return juce::KeyPress(code, juce::ModifierKeys::altModifier, 0); }

} // namespace

TEST(AutomationLanesHeightTest, DraggingTheHandleResizesTheLaneAndPushesTheLanesBelow) {
    HeightPanel f;
    const int base = f.rowHeight(f.cutoff);
    const int resY = f.panel.laneHeaderForTest(f.res)->getY();

    HeightPanel::drag(f.handle(f.cutoff), 20);

    EXPECT_EQ(f.rowHeight(f.cutoff), base + 20);
    EXPECT_EQ(f.rowHeight(f.res), base) << "only the dragged lane";
    EXPECT_EQ(f.panel.laneHeaderForTest(f.res)->getY(), resY + 20) << "the lane below follows";
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), (double)(base + 20) / (double)base);
}

TEST(AutomationLanesHeightTest, TheDragPreviewsAndWritesTheDocOnceOnReleaseAsOneUndoStep) {
    HeightPanel f;
    const int base = f.rowHeight(f.cutoff);
    auto& h = f.handle(f.cutoff);
    const auto centre = h.getLocalBounds().getCentre().toFloat();
    h.mouseDown(makeClickEvent(h, centre, leftButton()));
    h.mouseDrag(makeDragEvent(h, centre + juce::Point<float>(0.0f, 30.0f), centre, leftButton()));

    EXPECT_EQ(f.rowHeight(f.cutoff), base + 30) << "the layout follows the pointer";
    EXPECT_EQ(f.scale(f.cutoff), 1.0) << "the doc is untouched mid-drag";
    const auto revision = f.doc.getRevision();

    auto& moved = f.handle(f.cutoff); // the handle moved under the pointer
    moved.mouseUp(makeDragEvent(moved, moved.getLocalBounds().getCentre().toFloat(), centre, leftButton()));
    EXPECT_EQ(f.doc.getRevision(), revision + 1) << "one doc write";
    EXPECT_EQ(f.rowHeight(f.cutoff), base + 30) << "no jump on release";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.scale(f.cutoff), 1.0);
    EXPECT_EQ(f.rowHeight(f.cutoff), base);
    EXPECT_TRUE(f.undo.redo());
    EXPECT_EQ(f.rowHeight(f.cutoff), base + 30);
}

TEST(AutomationLanesHeightTest, TheLastLanesHandleSitsJustAboveTheTracksOwnResizeStrip) {
    HeightPanel f;
    const int base = f.rowHeight(f.res);
    const int trackBottomBefore = f.panel.getClipLaneArea().getRowLayout().trackTop(0) +
                                  f.panel.getClipLaneArea().getRowLayout().trackRowHeight(0) +
                                  f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0);
    HeightPanel::drag(f.handle(f.res), 16);
    EXPECT_EQ(f.rowHeight(f.res), base + 16);
    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackTop(0) + layout.trackRowHeight(0) + layout.trackExtraHeight(0), trackBottomBefore + 16)
        << "the track's block grew, so the tracks below move down";
}

TEST(AutomationLanesHeightTest, ADragIsClampedAndADoubleClickResetsToTheDefault) {
    HeightPanel f;
    HeightPanel::drag(f.handle(f.cutoff), 5000);
    EXPECT_EQ(f.scale(f.cutoff), synth::AutomationLane::kMaxHeightScale);
    HeightPanel::drag(f.handle(f.cutoff), -5000);
    EXPECT_EQ(f.scale(f.cutoff), synth::AutomationLane::kMinHeightScale);
    auto& h = f.handle(f.cutoff);
    h.mouseDoubleClick(makeClickEvent(h, h.getLocalBounds().getCentre().toFloat(), leftButton()));
    EXPECT_EQ(f.scale(f.cutoff), 1.0);
}

TEST(AutomationLanesHeightTest, TheHandleIsNamedAndShowsItsKeysInTheTooltip) {
    HeightPanel f;
    auto& h = f.handle(f.cutoff);
    EXPECT_EQ(h.getTitle(), "Resize cutoff lane");
    EXPECT_TRUE(h.getTooltip().contains("Drag to resize"));
    EXPECT_TRUE(h.getTooltip().contains("taller"));
    EXPECT_EQ(h.getMouseCursor(), juce::MouseCursor(juce::MouseCursor::UpDownResizeCursor));
}

TEST(AutomationLanesHeightTest, ResizingTheLanesOwnRowLeavesItsModulatorRowsAlone) {
    HeightPanel f;
    f.addModulator();
    const int rowBase = f.mod(0).getHeight();
    const int laneBase = f.rowHeight(f.cutoff);
    const int rowY = f.mod(0).getY();

    HeightPanel::drag(f.handle(f.cutoff), laneBase); // the lane's row twice as tall
    EXPECT_NEAR(f.scale(f.cutoff), 2.0, 0.02);
    EXPECT_EQ(f.rowHeight(f.cutoff), laneBase * 2);
    EXPECT_EQ(f.mod(0).getHeight(), rowBase) << "the modulator row keeps its size";
    EXPECT_EQ(f.mod(0).getY(), rowY + laneBase) << "and slides down";
    EXPECT_EQ(f.modScale("atten-1"), 1.0);
    EXPECT_EQ(f.handle(f.cutoff).getBottom(), f.mod(0).getY()) << "the lane's strip is on its own row's bottom edge";
}

TEST(AutomationLanesHeightTest, DraggingAModulatorRowsEdgeResizesOnlyThatRowAsOneUndoStep) {
    HeightPanel f;
    f.addModulator(2);
    const int laneBase = f.rowHeight(f.cutoff);
    const int firstBase = f.mod(0).getHeight();
    const int secondBase = f.mod(1).getHeight();
    const int secondY = f.mod(1).getY();
    const int resY = f.panel.laneHeaderForTest(f.res)->getY();
    EXPECT_EQ(f.modHandle(0).getBottom(), f.mod(1).getY()) << "the strip is on the first row's bottom edge";
    EXPECT_EQ(f.modHandle(0).getTitle(), "Resize LFO 1 mod lane on cutoff");
    EXPECT_TRUE(f.modHandle(0).getTooltip().contains("only"));

    auto& h = f.modHandle(0);
    const auto centre = h.getLocalBounds().getCentre().toFloat();
    h.mouseDown(makeClickEvent(h, centre, leftButton()));
    h.mouseDrag(makeDragEvent(h, centre + juce::Point<float>(0.0f, (float)firstBase), centre, leftButton()));
    const int previewed = f.mod(0).getHeight();
    EXPECT_EQ(previewed, firstBase * 2) << "the layout follows the pointer";
    EXPECT_EQ(f.modScale("atten-1"), 1.0) << "the doc is untouched mid-drag";
    const auto revision = f.doc.getRevision();
    auto& moved = f.modHandle(0);
    moved.mouseUp(makeDragEvent(moved, moved.getLocalBounds().getCentre().toFloat(), centre, leftButton()));

    EXPECT_EQ(f.doc.getRevision(), revision + 1) << "one doc write";
    EXPECT_EQ(f.mod(0).getHeight(), previewed) << "what the drag showed is what the drop kept";
    EXPECT_NEAR(f.modScale("atten-1"), 2.0, 0.02);
    EXPECT_EQ(f.modScale("atten-2"), 1.0);
    EXPECT_EQ(f.scale(f.cutoff), 1.0) << "the lane's own row keeps its size";
    EXPECT_EQ(f.rowHeight(f.cutoff), laneBase);
    EXPECT_EQ(f.mod(1).getHeight(), secondBase) << "the other modulator row keeps its size";
    EXPECT_EQ(f.mod(1).getY(), secondY + firstBase) << "everything below slides down";
    EXPECT_EQ(f.panel.laneHeaderForTest(f.res)->getY(), resY + firstBase);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.modScale("atten-1"), 1.0);
    EXPECT_EQ(f.mod(0).getHeight(), firstBase);
    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(f.mod(0).getHeight(), previewed);
}

TEST(AutomationLanesHeightTest, EachRowHasOneStripAndTheLastRowsSitsJustAboveTheTracksOwn) {
    HeightPanel f;
    f.addModulator(2);
    // res is the track's last lane and has no modulator rows; cutoff's rows are inside the track's block.
    EXPECT_EQ(f.modHandle(1).getBottom(), f.panel.laneHeaderForTest(f.res)->getY()) << "not shifted mid-track";
    const int bottom = f.panel.laneHeaderForTest(f.res)->getBottom(); // the track's block ends here
    EXPECT_EQ(f.handle(f.res).getBottom(), bottom - f.handle(f.res).getHeight())
        << "the last row's strip sits one strip above the block's bottom edge, where the track's strip is";
    int strips = 0;
    for (auto* child : f.panel.laneHeaderForTest(f.cutoff)->getParentComponent()->getChildren())
        strips += dynamic_cast<EdgeResizeHandle*>(child) != nullptr && child->isVisible() &&
                          (child->getComponentID() == "laneHeightHandle" ||
                           child->getComponentID() == "modulatorHeightHandle")
                      ? 1
                      : 0;
    EXPECT_EQ(strips, 4) << "one per row: two lane rows and two modulator rows";
}

TEST(AutomationLanesHeightTest, TheLastModulatorRowOfTheTracksLastLaneTakesTheShiftedStrip) {
    HeightPanel f;
    ASSERT_TRUE(f.doc.moveLane(f.cutoff, 1)); // cutoff (with the rows) is now the last lane
    f.addModulator(1);
    const int bottom = f.mod(0).getBottom(); // the track's block ends here
    EXPECT_EQ(f.modHandle(0).getBottom(), bottom - f.modHandle(0).getHeight());
    EXPECT_EQ(f.handle(f.cutoff).getBottom(), f.mod(0).getY()) << "the lane's own strip is not shifted";
    const int before = f.trackBottom();
    HeightPanel::drag(f.modHandle(0), 12);
    EXPECT_EQ(f.trackBottom(), before + 12) << "the tracks below follow";
}

TEST(AutomationLanesHeightTest, TheKeysOnAFocusedModulatorRowStepOnlyThatRow) {
    HeightPanel f;
    f.addModulator(2);
    const int laneBase = f.rowHeight(f.cutoff);
    EXPECT_TRUE(f.mod(0).keyPressed(altKey('=')));
    EXPECT_DOUBLE_EQ(f.modScale("atten-1"), 1.25);
    EXPECT_EQ(f.modScale("atten-2"), 1.0);
    EXPECT_EQ(f.scale(f.cutoff), 1.0);
    EXPECT_EQ(f.rowHeight(f.cutoff), laneBase);
    EXPECT_TRUE(f.mod(1).keyPressed(altKey('-')));
    EXPECT_DOUBLE_EQ(f.modScale("atten-2"), 0.8);
    EXPECT_TRUE(f.mod(0).keyPressed(altKey('0')));
    EXPECT_EQ(f.modScale("atten-1"), 1.0);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.modScale("atten-1"), 1.25) << "each key is one undo step";

    auto* band = f.panel.modulatorBandForTest(f.cutoff, 1);
    ASSERT_NE(band, nullptr);
    EXPECT_TRUE(band->keyPressed(altKey('='))) << "the row's band takes the same keys";
    EXPECT_DOUBLE_EQ(f.modScale("atten-2"), 1.0);
}

TEST(AutomationLanesHeightTest, ALaneFromAnOlderProjectKeepsItsModulatorRowsAtTheLanesScale) {
    HeightPanel f;
    f.addModulator();
    const int rowBase = f.mod(0).getHeight();
    auto saved = f.doc.toVar();
    auto* lane = saved.getProperty("tracks", {})[0].getProperty("lanes", {})[0].getDynamicObject();
    ASSERT_NE(lane, nullptr);
    lane->setProperty("heightScale", 2.0); // what an older file holds for a lane resized with its rows
    ASSERT_TRUE(f.doc.fromVar(saved));
    f.panel.setTrackAutomationExpanded(f.bass, false);
    f.panel.setTrackAutomationExpanded(f.bass, true);
    EXPECT_EQ(f.mod(0).getHeight(), rowBase * 2) << "the rows look exactly as they did";
}

TEST(AutomationLanesHeightTest, TheKeysOnAFocusedLaneHeaderStepItsHeightAsUndoableSteps) {
    HeightPanel f;
    auto& header = *f.panel.laneHeaderForTest(f.cutoff);

    EXPECT_TRUE(header.keyPressed(altKey('=')));
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), 1.25);
    EXPECT_TRUE(header.keyPressed(altKey('-')));
    EXPECT_TRUE(header.keyPressed(altKey('-')));
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), 0.8);
    EXPECT_EQ(f.scale(f.res), 1.0) << "only the focused lane";
    EXPECT_TRUE(header.keyPressed(altKey('0')));
    EXPECT_EQ(f.scale(f.cutoff), 1.0);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), 0.8);
}

TEST(AutomationLanesHeightTest, TheKeysFollowARebindAndTheOldKeyIsFree) {
    HeightPanel f;
    ShortcutManager manager;
    f.panel.setShortcutManager(&manager);
    manager.setBinding("timelineIncreaseTrackHeight", juce::KeyPress('k', juce::ModifierKeys::altModifier, 0));
    auto& header = *f.panel.laneHeaderForTest(f.cutoff);

    EXPECT_TRUE(header.keyPressed(altKey('k')));
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), 1.25);
    EXPECT_FALSE(header.keyPressed(altKey('=')));
    f.panel.setShortcutManager(nullptr);
}

TEST(AutomationLanesHeightTest, TheHeightIsKeptWhenALaneIsRetargetedAndCopiedByADuplicate) {
    HeightPanel f;
    ASSERT_TRUE(f.doc.setLaneHeightScale(f.cutoff, 2.0));
    synth::AutomationLane::RangeSnapshot range;
    range.minValue = 0.0f;
    range.maxValue = 10.0f;
    range.defaultValue = 5.0f;

    ASSERT_TRUE(f.doc.retargetLane(f.cutoff, "node-other", "other", range, -1));
    EXPECT_DOUBLE_EQ(f.scale(f.cutoff), 2.0) << "Change parameter keeps the height";

    const auto copy = f.doc.duplicateLane(f.cutoff, "node-copy", "copy", range, -1);
    ASSERT_TRUE(copy.isValid());
    EXPECT_DOUBLE_EQ(f.scale(copy), 2.0);
}
