// ControllerSurfaceTests.cpp -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): headless
// tests for ControllerSurfaceComponent/ControllerSurfaceCell, driven through their REAL
// mouseDown/mouseDrag/mouseUp overrides with synthesized juce::MouseEvents -- the "test the real
// mouse path" convention (Source/UI/CLAUDE.md, Tests/UI/Mixer/MixerFaderDragTests.cpp's template)
// -- and a PNG render test mirroring
// Tests/UI/Graph/ModuleComponent/ModuleComponentLayoutTests.cpp's AdsrCardRendersToPngForVisualInspection
// exactly, per this doc's own "Tests" section.

#include "ControllerSurfaceTestHelpers.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>

using midiremote_surface_test::fourControlModel;
using midiremote_surface_test::makeCellModel;
using midiremote_surface_test::surfaceMouseEvent;
using synth::Control;
using synth::ControlKind;
using synth::ui::ControllerSurfaceCell;
using synth::ui::ControllerSurfaceComponent;

namespace {

ControllerSurfaceCell* findCell(ControllerSurfaceComponent& surface, const juce::String& controlId) {
    return midiremote_surface_test::findCell(surface, controlId);
}

juce::Slider* findSliderChild(juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            return slider;
    return nullptr;
}

juce::Button* findButtonChild(juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            return button;
    return nullptr;
}

} // namespace

TEST(ControllerSurfaceComponentTest, SetControlsBuildsOneCellPerModelPositionedByLayout) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    auto* knob = findCell(surface, "knob1");
    auto* fader = findCell(surface, "fader1");
    auto* pad = findCell(surface, "pad1");
    auto* button = findCell(surface, "button1");
    ASSERT_NE(knob, nullptr);
    ASSERT_NE(fader, nullptr);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(button, nullptr);

    const int margin = ControllerSurfaceComponent::kCellMargin;
    const int size = ControllerSurfaceComponent::kCellSize;

    EXPECT_EQ(knob->getBounds(), juce::Rectangle<int>(margin, margin, size, size));
    EXPECT_EQ(fader->getBounds(), juce::Rectangle<int>(margin + (size + margin), margin, size, size));
    EXPECT_EQ(pad->getBounds(), juce::Rectangle<int>(margin, margin + (size + margin), size, size));
    EXPECT_EQ(button->getBounds(),
              juce::Rectangle<int>(margin + (size + margin), margin + (size + margin), size, size));

    EXPECT_EQ(surface.getProfileId(), "profileA");
}

TEST(ControllerSurfaceComponentTest, ClickingACellFiresOnSelectionChangedAndHighlightsIt) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    std::vector<juce::String> selected;
    surface.onSelectionChanged = [&](const std::vector<juce::String>& ids) { selected = ids; };

    auto* fader = findCell(surface, "fader1");
    ASSERT_NE(fader, nullptr);
    EXPECT_FALSE(fader->isSelected());

    const auto pos = fader->getLocalBounds().getCentre().toFloat();
    fader->mouseDown(surfaceMouseEvent(*fader, pos, pos, false));
    fader->mouseUp(surfaceMouseEvent(*fader, pos, pos, false));

    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], "fader1");
    EXPECT_TRUE(fader->isSelected());
    EXPECT_EQ(surface.getSelectedControlId(), "fader1");

    // A plain click (no movement between mouseDown/mouseUp) must not also report a drag.
}

TEST(ControllerSurfaceComponentTest, PlainClickWithNoMovementDoesNotFireOnControlsMoved) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    bool movedFired = false;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>&) { movedFired = true; };

    auto* knob = findCell(surface, "knob1");
    ASSERT_NE(knob, nullptr);
    const auto pos = knob->getLocalBounds().getCentre().toFloat();
    knob->mouseDown(surfaceMouseEvent(*knob, pos, pos, false));
    knob->mouseUp(surfaceMouseEvent(*knob, pos, pos, false));

    EXPECT_FALSE(movedFired) << "a plain click must never fire a spurious onControlsMoved(...)";
}

TEST(ControllerSurfaceComponentTest, RealDragPastAFullCellFiresOnControlsMovedWithPlausiblePosition) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    std::vector<ControllerSurfaceComponent::MovedCell> moves;
    surface.onControlsMoved = [&](const std::vector<ControllerSurfaceComponent::MovedCell>& m) { moves = m; };
    std::vector<juce::String> selected;
    surface.onSelectionChanged = [&](const std::vector<juce::String>& ids) { selected = ids; };

    auto* knob = findCell(surface, "knob1"); // starts at (col 0, row 0)
    ASSERT_NE(knob, nullptr);

    const juce::Point<float> downPos = knob->getLocalBounds().getCentre().toFloat();
    const int cellSize = ControllerSurfaceCell::kCellSize;
    // Two full cell widths right, no vertical movement -- (2,0) is empty in fourControlModel()'s
    // 2x2 layout (unlike (1,0), which fader1 occupies -- a drag landing on another control is
    // refused, see ControllerSurfaceGroupDragTests.cpp), so expect newCol == 2, newRow == 0.
    const juce::Point<float> dragPos = downPos + juce::Point<float>((float)cellSize * 2.0f, 0.0f);

    knob->mouseDown(surfaceMouseEvent(*knob, downPos, downPos, false));
    EXPECT_EQ(selected, std::vector<juce::String>{"knob1"}) << "mouseDown must still select even though a drag follows";
    knob->mouseDrag(surfaceMouseEvent(*knob, dragPos, downPos, true));
    knob->mouseUp(surfaceMouseEvent(*knob, dragPos, downPos, true));

    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].controlId, "knob1");
    EXPECT_EQ(moves[0].col, 2);
    EXPECT_EQ(moves[0].row, 0);
}

TEST(ControllerSurfaceComponentTest, NoteActivityAbsoluteSetsSliderValue) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    auto* knob = findCell(surface, "knob1");
    ASSERT_NE(knob, nullptr);
    auto* slider = findSliderChild(*knob);
    ASSERT_NE(slider, nullptr);
    EXPECT_NEAR(slider->getValue(), 0.0, 1.0e-6);

    surface.noteActivity("knob1", synth::midi::RemoteEventKind::absolute, 0.75f);
    EXPECT_NEAR(slider->getValue(), 0.75, 1.0e-6);
}

// FRO262: an already-mapped, already-touched control must show its real current value on build,
// not always start at rest -- before this fix every cell configured at lastValue_ = 0.0f regardless
// of CellModel::initialValue (which didn't exist).
TEST(ControllerSurfaceComponentTest, SetControlsSeedsSliderFromCellModelInitialValue) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    std::vector<ControllerSurfaceComponent::CellModel> cells = {
        makeCellModel("knob1", "Cutoff", ControlKind::knob, 0, 0, "Filter - Cutoff", false, true, 0.65f),
    };
    surface.setControls("profileA", cells);

    auto* knob = findCell(surface, "knob1");
    ASSERT_NE(knob, nullptr);
    auto* slider = findSliderChild(*knob);
    ASSERT_NE(slider, nullptr);
    EXPECT_NEAR(slider->getValue(), 0.65, 1.0e-6);

    // Live activity still takes over from the seeded value exactly as before.
    surface.noteActivity("knob1", synth::midi::RemoteEventKind::absolute, 0.2f);
    EXPECT_NEAR(slider->getValue(), 0.2, 1.0e-6);
}

TEST(ControllerSurfaceComponentTest, SetControlsSeedsButtonToggleFromCellModelInitialValue) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    std::vector<ControllerSurfaceComponent::CellModel> cells = {
        makeCellModel("button1", "Solo", ControlKind::button, 0, 0, "Master . Solo", false, true, 1.0f),
    };
    surface.setControls("profileA", cells);

    auto* button = findCell(surface, "button1");
    ASSERT_NE(button, nullptr);
    auto* toggleButton = findButtonChild(*button);
    ASSERT_NE(toggleButton, nullptr);
    EXPECT_TRUE(toggleButton->getToggleState());
}

TEST(ControllerSurfaceComponentTest, SetControlsWithNoInitialValueStartsAtRestAsBefore) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel()); // makeCellModel's default initialValue = 0.0f

    auto* knob = findCell(surface, "knob1");
    ASSERT_NE(knob, nullptr);
    auto* slider = findSliderChild(*knob);
    ASSERT_NE(slider, nullptr);
    EXPECT_NEAR(slider->getValue(), 0.0, 1.0e-6);
}

TEST(ControllerSurfaceComponentTest, NoteActivityRelativeDeltaAccumulatesAndClamps) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    auto* knob = findCell(surface, "knob1");
    ASSERT_NE(knob, nullptr);
    auto* slider = findSliderChild(*knob);
    ASSERT_NE(slider, nullptr);

    surface.noteActivity("knob1", synth::midi::RemoteEventKind::absolute, 0.5f);
    EXPECT_NEAR(slider->getValue(), 0.5, 1.0e-6);

    surface.noteActivity("knob1", synth::midi::RemoteEventKind::relativeDelta, 0.2f);
    EXPECT_NEAR(slider->getValue(), 0.7, 1.0e-6);

    // Clamp at the top.
    surface.noteActivity("knob1", synth::midi::RemoteEventKind::relativeDelta, 0.9f);
    EXPECT_NEAR(slider->getValue(), 1.0, 1.0e-6);
}

TEST(ControllerSurfaceComponentTest, NoteActivityButtonPressAndReleaseToggleTheRightCell) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    auto* pad = findCell(surface, "pad1");
    auto* fader = findCell(surface, "fader1");
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(fader, nullptr);
    auto* padButton = findButtonChild(*pad);
    ASSERT_NE(padButton, nullptr);
    EXPECT_FALSE(padButton->getToggleState());

    surface.noteActivity("pad1", synth::midi::RemoteEventKind::buttonPress, 1.0f);
    EXPECT_TRUE(padButton->getToggleState());

    // The other cell (a fader, no button) must not be touched -- noteActivity finds the right cell
    // by id, and a mismatched kind's own noteActivity() call is simply a no-op internally.
    auto* faderSlider = findSliderChild(*fader);
    ASSERT_NE(faderSlider, nullptr);
    EXPECT_NEAR(faderSlider->getValue(), 0.0, 1.0e-6);

    surface.noteActivity("pad1", synth::midi::RemoteEventKind::buttonRelease, 0.0f);
    EXPECT_FALSE(padButton->getToggleState());
}

TEST(ControllerSurfaceComponentTest, NoteActivityForUnknownControlIdIsANoOp) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    // No assignment beyond "does not throw / does not crash" -- the header's contract is that the
    // caller doesn't pre-filter, so an id not on the grid must be silently ignored.
    EXPECT_NO_THROW(surface.noteActivity("no-such-control", synth::midi::RemoteEventKind::absolute, 1.0f));
}

TEST(ControllerSurfaceComponentTest, DeleteKeyWithSelectionFiresOnDeleteControlsRequested) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    std::vector<juce::String> deleteRequestedIds;
    surface.onDeleteControlsRequested = [&](const std::vector<juce::String>& ids) { deleteRequestedIds = ids; };

    // No selection yet -- Delete must do nothing.
    EXPECT_FALSE(surface.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_TRUE(deleteRequestedIds.empty());

    surface.setSelectedControlId("button1");
    EXPECT_TRUE(surface.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(deleteRequestedIds, std::vector<juce::String>{"button1"});

    deleteRequestedIds.clear();
    surface.setSelectedControlId("pad1");
    EXPECT_TRUE(surface.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_EQ(deleteRequestedIds, std::vector<juce::String>{"pad1"});
}

TEST(ControllerSurfaceComponentTest, EscClearsSelection) {
    ControllerSurfaceComponent surface;
    surface.setSize(400, 400);
    surface.setControls("profileA", fourControlModel());

    surface.setSelectedControlId("button1");
    ASSERT_EQ(surface.getSelectedControlId(), "button1");

    EXPECT_TRUE(surface.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(surface.getSelectedControlIds().empty());

    // Nothing selected -- Esc is a no-op, reported as unhandled so it doesn't swallow a key some
    // other focus region might want.
    EXPECT_FALSE(surface.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
}

TEST(ControllerSurfaceComponentTest, SurfaceRendersToPngForVisualInspection) {
    ControllerSurfaceComponent surface;
    surface.setSize(300, 200);
    surface.setControls("profileA", fourControlModel());
    surface.noteActivity("knob1", synth::midi::RemoteEventKind::absolute, 0.6f);
    surface.noteActivity("pad1", synth::midi::RemoteEventKind::buttonPress, 1.0f);

    // Install the app's real LookAndFeel before painting -- without it the surface and its child
    // widgets render as flat default JUCE grey instead of the themed look the real app shows (same
    // pattern as ModuleComponentLayoutTests.cpp's AdsrCardRendersToPngForVisualInspection).
    synth::theme::AppLookAndFeel lf;
    surface.setLookAndFeel(&lf);

    const int width = surface.getWidth();
    const int height = surface.getHeight();
    ASSERT_GT(width, 0);
    ASSERT_GT(height, 0);

    // SoftwareImageType(): on Windows the default (native) image type is Direct2D-backed, and
    // painting into it then reading pixels back on a GPU-less CI runner yields an all-zero image
    // (FRO242). Force a software-backed bitmap so getPixelAt() reads what paint() actually drew.
    juce::Image img(juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    juce::Graphics g(img);
    EXPECT_NO_THROW(surface.paintEntireComponent(g, true));

    // Meaningful-content assertion that always runs, regardless of whether the PNG gets written.
    bool hasOpaquePixel = false;
    for (int y = 0; y < img.getHeight() && !hasOpaquePixel; ++y)
        for (int x = 0; x < img.getWidth() && !hasOpaquePixel; ++x)
            if (img.getPixelAt(x, y).getAlpha() > 0)
                hasOpaquePixel = true;
    EXPECT_TRUE(hasOpaquePixel) << "rendered surface image should have at least one opaque pixel";

    std::cout << "SurfaceRendersToPngForVisualInspection: image " << width << "x" << height << std::endl;

    surface.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("MIDI_SURFACE_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set MIDI_SURFACE_PNG=<path> to write the rendered surface for visual inspection";

    juce::File outFile(pngPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk()) << "failed to open " << pngPath << " for writing";
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream)) << "failed to encode PNG to " << pngPath;
}
