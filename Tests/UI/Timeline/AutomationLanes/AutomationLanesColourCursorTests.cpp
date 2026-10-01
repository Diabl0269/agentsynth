// AutomationLanesColourCursorTests.cpp -- an automation lane is drawn in its owning track's colour
// (curve and points), follows a recolour, stays readable on a light background, and shows the pen
// under the Draw tool. Real panel, real doc edits.

#include "AutomationLanesTestFixture.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TrackColour.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::EditTool;

namespace {

// The editor's headless background (no themed look-and-feel): what paintGridBackdrop fills.
juce::Colour headlessBackground() { return juce::Colours::darkgrey.darker(0.6f); }

struct ColouredLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    synth::ui::AutomationLaneEditor* editor = nullptr;

    ColouredLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        doc.setTrackColour(bass, 0xff7FD962);
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
    }
};

} // namespace

TEST(AutomationLanesColourTest, TheCurveAndPointsUseTheOwningTracksColour) {
    ColouredLane f;
    ASSERT_NE(f.editor, nullptr);
    const juce::Colour track(0xff7FD962);
    EXPECT_EQ(f.editor->getResolvedCurveColour(), synth::ui::readableOn(track, headlessBackground()));

    // A point's fill is that same colour: the centre pixel of its handle, painted for real.
    ASSERT_TRUE(f.doc.addBreakpoint(f.lane, 2.0, 50.0));
    const auto handle = f.editor->getHandleRectForTest(2.0);
    ASSERT_FALSE(handle.isEmpty());
    const auto snapshot = f.editor->createComponentSnapshot(f.editor->getLocalBounds(), false, 1.0f);
    EXPECT_EQ(snapshot.getPixelAt(handle.getCentreX(), handle.getCentreY()), f.editor->getResolvedCurveColour());
}

TEST(AutomationLanesColourTest, RecolouringTheTrackRecoloursItsLanesWithoutAnyRefresh) {
    ColouredLane f;
    ASSERT_NE(f.editor, nullptr);
    const auto before = f.editor->getResolvedCurveColour();
    ASSERT_TRUE(f.doc.setTrackColour(f.bass, 0xffC792EA));
    ASSERT_NE(f.panel.laneEditorForTest(f.lane), nullptr);
    EXPECT_EQ(f.editor->getResolvedCurveColour(),
              synth::ui::readableOn(juce::Colour(0xffC792EA), headlessBackground()));
    EXPECT_NE(f.editor->getResolvedCurveColour(), before);
}

TEST(AutomationLanesColourTest, AnUnassignedLaneKeepsTheNeutralColourWhateverTheTracksAreColoured) {
    LanesPanel f;
    f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto automation = f.doc.addTrack(TrackKind::Automation, "Automation");
    const auto lane = f.addLane(automation, "cutoff");
    auto* editor = f.panel.laneEditorForTest(lane);
    ASSERT_NE(editor, nullptr);
    const auto neutral = editor->getResolvedCurveColour();
    EXPECT_EQ(neutral, synth::ui::readableOn(juce::Colour(0xff8A93A0), headlessBackground()));
    f.doc.setTrackColour(f.doc.getTracks().front().id, 0xffFF7AB2);
    EXPECT_EQ(editor->getResolvedCurveColour(), neutral);
}

TEST(AutomationLanesColourTest, EveryPaletteColourReadsOnALightAndADarkLaneBackground) {
    const juce::Colour light(0xffF4F6F8), dark(0xff14171C);
    for (auto argb : synth::ui::trackPaletteArgb())
        for (auto background : {light, dark}) {
            const auto drawn = synth::ui::readableOn(juce::Colour(argb), background);
            EXPECT_GE(synth::ui::colourContrast(drawn, background), 3.0f) << std::hex << argb;
        }
    // A colour that already reads is left alone; a pale one on white is darkened, not recoloured.
    EXPECT_EQ(synth::ui::readableOn(juce::Colour(0xff7FD962), dark), juce::Colour(0xff7FD962));
    const auto amber = juce::Colour(0xffFFB454);
    const auto darkened = synth::ui::readableOn(amber, light);
    EXPECT_LT(darkened.getBrightness(), amber.getBrightness());
    EXPECT_NEAR(darkened.getHue(), amber.getHue(), 0.05f);
}

TEST(AutomationLanesCursorTest, TheLaneShowsThePenUnderTheDrawToolAndTheArrowOtherwise) {
    ColouredLane f;
    ASSERT_NE(f.editor, nullptr);
    const juce::MouseCursor arrow(juce::MouseCursor::NormalCursor);
    EXPECT_TRUE(f.editor->getMouseCursor() == arrow);

    f.panel.setActiveTool(EditTool::Draw);
    EXPECT_TRUE(f.editor->getMouseCursor() == f.panel.getClipLaneArea().getMouseCursor())
        << "the same pen the clip lanes show";
    EXPECT_FALSE(f.editor->getMouseCursor() == arrow);

    f.panel.setActiveTool(EditTool::Erase);
    EXPECT_TRUE(f.editor->getMouseCursor() == arrow) << "only Draw makes the lane a pen";
    f.panel.setActiveTool(EditTool::Select);
    EXPECT_TRUE(f.editor->getMouseCursor() == arrow);
}
