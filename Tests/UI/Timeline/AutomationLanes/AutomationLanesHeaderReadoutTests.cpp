// AutomationLanesHeaderReadoutTests.cpp -- the lane header's one value slot: the value at the playhead in the muted
// colour, switching to the selected point's value in the accent colour and back. Real clicks, keys and transport
// polls on the real panel.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/ReducedMotion.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::AutomationLaneHeaderComponent;

namespace {

constexpr juce::uint32 kAccent = 0xff00D1FF; // the theme's default accent, what a bare component paints
constexpr juce::uint32 kMuted = 0xff8A93A0;

// Two lanes on one open track, each with points at beats 1, 2 and 3 (lane A: 20, 60, 40; lane B: 10, 30, 90).
struct ReadoutLanes : LanesPanel {
    synth::TrackId bass;
    synth::LaneId laneA, laneB;
    AutomationLaneEditor* editorA = nullptr;
    AutomationLaneEditor* editorB = nullptr;
    AutomationLaneHeaderComponent* headerA = nullptr;
    AutomationLaneHeaderComponent* headerB = nullptr;

    ReadoutLanes() {
        synth::ui::setReducedMotionForTest(true);
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        laneA = addLane(bass, "cutoff");
        laneB = addLane(bass, "resonance");
        for (const auto& [lane, values] :
             {std::pair{laneA, std::array{20.0, 60.0, 40.0}}, std::pair{laneB, std::array{10.0, 30.0, 90.0}}})
            for (int i = 0; i < 3; ++i)
                doc.addBreakpoint(lane, 1.0 + i, values[(size_t)i]);
        panel.setTrackAutomationExpanded(bass, true);
        editorA = panel.laneEditorForTest(laneA);
        editorB = panel.laneEditorForTest(laneB);
        headerA = panel.laneHeaderForTest(laneA);
        headerB = panel.laneHeaderForTest(laneB);
        editorA->setTool(AutomationLaneEditor::Tool::Pointer);
        editorB->setTool(AutomationLaneEditor::Tool::Pointer);
        poll(1.0);
    }
    ~ReadoutLanes() { synth::ui::setReducedMotionForTest(std::nullopt); }

    void poll(double beat) {
        synth::TransportService::PositionSnapshot snapshot;
        snapshot.ppq = beat;
        panel.updateFromTransport(snapshot, 0.0);
    }
    juce::Point<float> at(AutomationLaneEditor* e, double beat, double value) {
        return {(float)panel.getViewState().beatToX(beat), (float)e->valueToY(value)};
    }
    void click(AutomationLaneEditor* e, juce::Point<float> p) {
        e->mouseDown(makeClickEvent(*e, p, leftButton()));
        e->mouseUp(makeClickEvent(*e, p, leftButton()));
    }
    static juce::String text(AutomationLaneHeaderComponent* h) { return h->getReadout().getDisplayedText(); }
    // Whether any pixel of the painted slot is (nearly) `colour`.
    static bool paintsColour(AutomationLaneHeaderComponent* h, juce::uint32 colour) {
        auto& readout = h->getReadout();
        juce::Image image(juce::Image::ARGB, readout.getWidth(), readout.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        readout.paintEntireComponent(g, false);
        const juce::Colour want(colour);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x) {
                const auto c = image.getPixelAt(x, y);
                if (c.getAlpha() > 200 && std::abs(c.getRed() - want.getRed()) < 24 &&
                    std::abs(c.getGreen() - want.getGreen()) < 24 && std::abs(c.getBlue() - want.getBlue()) < 24)
                    return true;
            }
        return false;
    }
};

} // namespace

TEST(AutomationLanesHeaderReadoutTest, ShowsTheValueAtThePlayheadInTheMutedColourUntilAPointIsSelected) {
    ReadoutLanes f;
    ASSERT_NE(f.headerA, nullptr);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "20.0");
    EXPECT_FALSE(f.headerA->getReadout().isShowingSelectedPoint());
    EXPECT_TRUE(ReadoutLanes::paintsColour(f.headerA, kMuted));
    EXPECT_FALSE(ReadoutLanes::paintsColour(f.headerA, kAccent));
    f.poll(2.0);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "60.0") << "still follows the playhead";
}

TEST(AutomationLanesHeaderReadoutTest, ClickingAPointShowsItsValueInTheAccentColourAndEscapeGoesBack) {
    ReadoutLanes f;
    f.click(f.editorA, f.at(f.editorA, 3.0, 40.0));
    EXPECT_TRUE(f.headerA->getReadout().isShowingSelectedPoint());
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "40.0") << "the point's value, not the playhead's 20.0";
    EXPECT_FLOAT_EQ(f.headerA->getReadout().getAccentMixForTest(), 1.0f);
    EXPECT_TRUE(ReadoutLanes::paintsColour(f.headerA, kAccent));

    f.poll(2.0);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "40.0") << "the playhead moving does not take the slot back";
    EXPECT_EQ(f.headerA->getValueText(), "60.0") << "the playhead value is still tracked underneath";

    EXPECT_TRUE(f.editorA->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.headerA->getReadout().isShowingSelectedPoint());
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "60.0");
    EXPECT_TRUE(ReadoutLanes::paintsColour(f.headerA, kMuted));
    EXPECT_FALSE(ReadoutLanes::paintsColour(f.headerA, kAccent));
}

TEST(AutomationLanesHeaderReadoutTest, ClickingEmptySpaceReturnsToThePlayheadValue) {
    ReadoutLanes f;
    f.click(f.editorA, f.at(f.editorA, 2.0, 60.0));
    ASSERT_TRUE(f.headerA->getReadout().isShowingSelectedPoint());
    f.click(f.editorA, {(float)f.panel.getViewState().beatToX(6.0), 4.0f});
    EXPECT_FALSE(f.headerA->getReadout().isShowingSelectedPoint());
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "20.0");
}

TEST(AutomationLanesHeaderReadoutTest, SeveralSelectedPointsShowTheCursorPointElseTheFirst) {
    ReadoutLanes f;
    f.editorA->getPointSelection().setSelection({2.0, 3.0});
    f.editorA->getPointSelection().setCursor(3.0);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "40.0") << "the cursor point";
    f.editorA->getPointSelection().setCursor(std::nullopt);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "60.0") << "no cursor: the first selected point";
}

TEST(AutomationLanesHeaderReadoutTest, MovingAndNudgingTheSelectedPointUpdatesTheText) {
    ReadoutLanes f;
    f.click(f.editorA, f.at(f.editorA, 2.0, 60.0));
    ASSERT_EQ(ReadoutLanes::text(f.headerA), "60.0");

    dragAcross(*f.editorA, f.at(f.editorA, 2.0, 60.0), f.at(f.editorA, 2.0, 80.0), 6);
    const auto moved = f.doc.getLane(f.laneA)->points[1].value;
    ASSERT_NE(moved, 60.0);
    EXPECT_TRUE(f.headerA->getReadout().isShowingSelectedPoint()) << "the point stays selected through the commit";
    EXPECT_EQ(ReadoutLanes::text(f.headerA), juce::String(moved, 1));

    EXPECT_TRUE(f.editorA->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    const auto nudged = f.doc.getLane(f.laneA)->points[1].value;
    ASSERT_NE(nudged, moved);
    EXPECT_EQ(ReadoutLanes::text(f.headerA), juce::String(nudged, 1));
}

TEST(AutomationLanesHeaderReadoutTest, ASelectionOnOneLaneLeavesTheOtherLanesHeaderAlone) {
    ReadoutLanes f;
    f.click(f.editorA, f.at(f.editorA, 2.0, 60.0));
    EXPECT_TRUE(f.headerA->getReadout().isShowingSelectedPoint());
    EXPECT_FALSE(f.headerB->getReadout().isShowingSelectedPoint());
    EXPECT_EQ(ReadoutLanes::text(f.headerB), "10.0");
    EXPECT_FALSE(ReadoutLanes::paintsColour(f.headerB, kAccent));

    f.click(f.editorB, f.at(f.editorB, 3.0, 90.0));
    EXPECT_EQ(ReadoutLanes::text(f.headerB), "90.0");
    EXPECT_EQ(ReadoutLanes::text(f.headerA), "60.0") << "A keeps its own selected point";
}

TEST(AutomationLanesHeaderReadoutTest, TheAccessibleNameAndTooltipSayWhichValueItIs) {
    ReadoutLanes f;
    auto& readout = f.headerA->getReadout();
    EXPECT_EQ(readout.getTitle(), "cutoff value at playhead");
    EXPECT_EQ(readout.getTooltip(), "Value at the playhead");
    EXPECT_EQ(readout.getDescription(), "20.0");

    f.click(f.editorA, f.at(f.editorA, 2.0, 60.0));
    EXPECT_EQ(readout.getTitle(), "cutoff selected point value");
    EXPECT_EQ(readout.getTooltip(), "Value of the selected point");
    EXPECT_EQ(readout.getDescription(), "60.0");
    EXPECT_TRUE(readout.isAccessible());
}
