// ShapeWindowGridTests.cpp -- the shape window's grid choice: what each adapter offers, what the curve snaps to under
// real mouse events and Alt+arrow moves, that Off and Shift stay free, and that the choice is remembered per kind.
#include "../../../TestSettingsHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/LFOModule.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

struct LfoRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    AppUndoManager undo;
    std::shared_ptr<LfoShapesAdapter> adapter;

    LfoRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    }
};

CurveGeometryConfig editorGeometryConfig() {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return config;
}

/** Drags node `index` from where it is to model point (x, level) with real mouse events; returns the model after. */
void dragNode(CurveEditorComponent& curve, int index, double x, float level, bool shift = false) {
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), editorGeometryConfig());
    const auto from = geometry.nodePosition(index);
    const juce::Point<float> to(geometry.xForTime(x), geometry.yForLevel(level));
    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(shift ? curveLeftDragShift(curve, to, from) : curveLeftDrag(curve, to, from));
    curve.mouseUp(curveLeftDrag(curve, to, from));
}

/** True when some node of the curve sits at (x, y). */
bool hasNodeAt(const CurveEditorComponent& curve, double x, float y, double tolerance = 1e-4) {
    const auto& model = curve.getModel();
    for (int i = 0; i < model.getNumNodes(); ++i)
        if (std::abs(model.getNode(i).x - x) < tolerance && std::abs(model.getNode(i).y - y) < tolerance)
            return true;
    return false;
}

struct GridKeysGuard {
    synth::test::PersistedKeysGuard guard{juce::StringArray{shapewindowprefs::gridKey("lfo"),
                                                            shapewindowprefs::gridKey("filter"),
                                                            shapewindowprefs::spectrumKey("filter")}};
};

} // namespace

TEST(ShapeWindowGrid, TheLfoOffersTheNoteLengthBoxAndTheFilterOffersOctaveSegments) {
    LfoRig lfo;
    ShapeListEditor lfoEditor(lfo.adapter, 0);
    ASSERT_NE(lfoEditor.getGridBox(), nullptr);
    EXPECT_EQ(lfoEditor.getGridSwitch(), nullptr);
    EXPECT_EQ(lfoEditor.getGridBox()->getChoices(), curvegrid::noteLengthNames());
    EXPECT_EQ(lfoEditor.getGridName(), "Off");

    ShapeRig filter;
    ShapeListEditor filterEditor(filter.adapter, 0);
    ASSERT_NE(filterEditor.getGridSwitch(), nullptr);
    EXPECT_EQ(filterEditor.getGridBox(), nullptr);
    EXPECT_EQ(filterEditor.getGridSwitch()->getNumSegments(), 4);
    EXPECT_EQ(filterEditor.getGridName(), "Off");
}

TEST(ShapeWindowGrid, AnLfoPointDraggedUnderAnEighthGridLandsOnAnEighth) {
    GridKeysGuard keys;
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    ASSERT_GE(curve.getModel().getNumNodes(), 3);
    editor.setGridName("1/8");
    dragNode(curve, 1, 0.30, 0.55f);
    EXPECT_TRUE(hasNodeAt(curve, 0.25, 0.5f)) << "0.30 snaps to 2/8 and 0.55 to 4/8";
}

TEST(ShapeWindowGrid, ATripletGridPacksThreeStepsIntoTwoStraightOnes) {
    GridKeysGuard keys;
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setGridName("1/4T"); // 6 divisions across
    dragNode(editor.getCurveEditor(), 1, 0.30, 0.55f);
    EXPECT_TRUE(hasNodeAt(editor.getCurveEditor(), 2.0 / 6.0, 0.5f));
}

TEST(ShapeWindowGrid, OffAndShiftLeaveThePointFree) {
    GridKeysGuard keys;
    LfoRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    dragNode(curve, 1, 0.30, 0.55f);
    EXPECT_TRUE(hasNodeAt(curve, 0.30, 0.55f, 1e-3)) << "Off: free";

    editor.setGridName("1/8");
    dragNode(curve, 1, 0.33, 0.61f, true);
    EXPECT_TRUE(hasNodeAt(curve, 0.33, 0.61f, 1e-3)) << "Shift bypasses the grid";
}

TEST(ShapeWindowGrid, AFilterPointDraggedUnderAnOctaveGridLandsOnAnOctaveOf1kHzAndA6DbStep) {
    GridKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    editor.setGridName("1 oct");
    // 1.1 kHz is nearest the 1 kHz line (not 500 Hz or 2 kHz); -4 dB is nearest -6 dB.
    dragNode(curve, 3, CustomFilterShapes::hzToX(1100.0f), CustomFilterShapes::dbToY(-4.0f));
    EXPECT_TRUE(hasNodeAt(curve, CustomFilterShapes::hzToX(1000.0f), CustomFilterShapes::dbToY(-6.0f)));
}

TEST(ShapeWindowGrid, AThirdOctaveGridHasLinesBetweenTheOctaves) {
    GridKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    editor.setGridName("1/3 oct");
    dragNode(curve, 3, CustomFilterShapes::hzToX(1200.0f), CustomFilterShapes::dbToY(0.0f));
    EXPECT_TRUE(hasNodeAt(curve, CustomFilterShapes::hzToX(1000.0f * std::pow(2.0f, 1.0f / 3.0f)),
                          CustomFilterShapes::dbToY(0.0f)))
        << "1.2 kHz is nearest 1.26 kHz";
}

TEST(ShapeWindowGrid, AnAltArrowMoveStepsToTheNextOctaveLine) {
    GridKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    editor.setGridName("1 oct");
    dragNode(curve, 3, CustomFilterShapes::hzToX(1000.0f), CustomFilterShapes::dbToY(0.0f));
    int index = -1;
    for (int i = 0; i < curve.getModel().getNumNodes(); ++i)
        if (std::abs(curve.getModel().getNode(i).x - CustomFilterShapes::hzToX(1000.0f)) < 1e-4)
            index = i;
    ASSERT_GE(index, 0);
    curve.selectNodeRelative(0);
    while (curve.getSelectedIndex() != index && curve.selectNodeRelative(1) >= 0) {
    }
    ASSERT_EQ(curve.getSelectedIndex(), index);

    EXPECT_TRUE(curve.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)));
    EXPECT_TRUE(hasNodeAt(curve, CustomFilterShapes::hzToX(2000.0f), CustomFilterShapes::dbToY(0.0f)));
}

TEST(ShapeWindowGrid, TheChoiceIsRememberedPerKindAcrossWindows) {
    GridKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveText(&props, shapewindowprefs::gridKey("lfo"), {});
    shapewindowprefs::saveText(&props, shapewindowprefs::gridKey("filter"), {});

    LfoRig lfo;
    ShapeRig filter;
    lfo.adapter->setApplicationProperties(&props);
    filter.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(lfo.adapter, 0);
        editor.setGridName("1/16");
    }
    {
        ShapeListEditor editor(filter.adapter, 0);
        EXPECT_EQ(editor.getGridName(), "Off") << "the LFO's choice is not the Filter's";
        editor.setGridName("1/2 oct");
    }
    ShapeListEditor lfoAgain(lfo.adapter, 0);
    EXPECT_EQ(lfoAgain.getGridName(), "1/16");
    dragNode(lfoAgain.getCurveEditor(), 1, 0.30, 0.5f);
    EXPECT_TRUE(hasNodeAt(lfoAgain.getCurveEditor(), 5.0 / 16.0, 0.5f)) << "the remembered grid is in force";
    ShapeListEditor filterAgain(filter.adapter, 0);
    EXPECT_EQ(filterAgain.getGridName(), "1/2 oct");

    filterAgain.setGridName("Off");
    ShapeListEditor filterOff(filter.adapter, 0);
    EXPECT_EQ(filterOff.getGridName(), "Off");
}

TEST(ShapeWindowGrid, TheGridControlsAreNamedTooltippedTabStops) {
    LfoRig lfo;
    ShapeListEditor lfoEditor(lfo.adapter, 0);
    EXPECT_FALSE(lfoEditor.getGridBox()->getTitle().isEmpty());
    EXPECT_FALSE(lfoEditor.getGridBox()->getTooltip().isEmpty());
    EXPECT_TRUE(lfoEditor.getGridBox()->getWantsKeyboardFocus());

    ShapeRig filter;
    ShapeListEditor filterEditor(filter.adapter, 0);
    EXPECT_FALSE(filterEditor.getGridSwitch()->getTooltip().isEmpty());
    EXPECT_TRUE(filterEditor.getGridSwitch()->getWantsKeyboardFocus());
}
