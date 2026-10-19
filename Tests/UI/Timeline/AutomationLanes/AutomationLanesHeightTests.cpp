// AutomationLanesHeightTests.cpp -- one automation lane's own height: the strip on the bottom edge of its block
// (real mouse events on the real handle), the keys of a focused lane header, and its modulator rows scaling with it.

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
    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String&) override {
        return uuid.startsWith("atten-") ? std::vector<ModulatorInfo>{} : modulators;
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

    void addModulator() {
        ModulatorInfo info;
        info.sourceUuid = "lfo-1";
        info.sourceTitle = "LFO 1";
        info.isLfo = true;
        info.attenuverterUuid = "atten-1";
        info.paramId = "cutoff";
        info.targetChannel = 2;
        host.modulators = {info};
        panel.setTrackAutomationExpanded(bass, false);
        panel.setTrackAutomationExpanded(bass, true);
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

TEST(AutomationLanesHeightTest, ModulatorRowsScaleWithTheirLane) {
    HeightPanel f;
    f.addModulator();
    auto* row = f.panel.modulatorRowForTest(f.cutoff, 0);
    ASSERT_NE(row, nullptr);
    const int rowBase = row->getHeight();
    const int laneBase = f.rowHeight(f.cutoff);

    ASSERT_TRUE(f.doc.setLaneHeightScale(f.cutoff, 2.0));
    row = f.panel.modulatorRowForTest(f.cutoff, 0);
    EXPECT_EQ(f.rowHeight(f.cutoff), laneBase * 2);
    EXPECT_EQ(row->getHeight(), rowBase * 2);
    EXPECT_EQ(f.rowHeight(f.res), laneBase) << "a lane's neighbours keep their height";
}

TEST(AutomationLanesHeightTest, DraggingTheBlockEdgeScalesTheLaneAndItsModulatorsTogether) {
    HeightPanel f;
    f.addModulator();
    const int laneBase = f.rowHeight(f.cutoff);
    const int rowBase = f.panel.modulatorRowForTest(f.cutoff, 0)->getHeight();
    const int block = laneBase + rowBase;

    HeightPanel::drag(f.handle(f.cutoff), block); // the block twice as tall
    EXPECT_NEAR(f.scale(f.cutoff), 2.0, 0.02);
    EXPECT_NEAR(f.rowHeight(f.cutoff), laneBase * 2, 1);
    EXPECT_NEAR(f.panel.modulatorRowForTest(f.cutoff, 0)->getHeight(), rowBase * 2, 1);
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
