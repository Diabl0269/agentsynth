// The Filter card's small curve view of the shape under the morph cursor: on a dot it shows and edits that shape
// (one undo step per drag), between dots it shows the blend and refuses edits, and the Show Curve pill is remembered.
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "ModuleComponentTestFixture.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

struct CurveCardRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    CurveCardRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
        card->timerCallback();
    }

    MorphShapesCard& custom() { return *card->getMorphCard(); }
    CurveEditorComponent& curve() { return custom().getCurveView(); }

    void moveCursor(float x, float y) {
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphX")) = x;
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphY")) = y;
        card->timerCallback();
    }
};

} // namespace

TEST_F(ModuleComponentTest, CurveViewIsShownByDefaultWithSeveralShapes) {
    CurveCardRig rig;
    EXPECT_TRUE(rig.curve().isVisible());
    EXPECT_TRUE(rig.custom().getCurveToggle().getToggleState());
    EXPECT_EQ(rig.custom().getCurveToggle().getButtonText(), "Show Curve");
}

// Regression test for FRO1334: the Custom card had no way to see the shape under the morph cursor.
TEST_F(ModuleComponentTest, CurveViewShowsTheShapeTheCursorIsOn) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    for (int i = 0; i < 3; ++i) {
        rig.moveCursor(defaults.shapes[(size_t)i].position[0], defaults.shapes[(size_t)i].position[1]);
        EXPECT_EQ(rig.custom().curveViewShape(), i);
        EXPECT_EQ(rig.curve().getModel().getNumNodes(), (int)defaults.shapes[(size_t)i].curve.points.size());
        EXPECT_TRUE(rig.curve().getTitle().isNotEmpty());
        EXPECT_NE(rig.curve().getDescription().indexOf(defaults.shapes[(size_t)i].name), -1);
    }
}

TEST_F(ModuleComponentTest, CurveViewBetweenDotsShowsTheBlendAndRefusesEdits) {
    CurveCardRig rig;
    rig.moveCursor(0.5f, 0.5f);
    EXPECT_EQ(rig.custom().curveViewShape(), -1);
    EXPECT_NE(rig.curve().getDescription().indexOf("read only"), -1);
    bool self = true;
    bool children = true;
    rig.curve().getInterceptsMouseClicks(self, children);
    EXPECT_FALSE(self);

    const int generation = rig.filter->getCustomShapesGeneration();
    const auto before = rig.filter->getCustomShapes();
    rig.curve().dragNodeTo(3, {0.5f, 0.9f});
    rig.curve().onGestureEnd();
    EXPECT_EQ(rig.filter->getCustomShapesGeneration(), generation);
    EXPECT_TRUE(rig.filter->getCustomShapes() == before);
}

TEST_F(ModuleComponentTest, DraggingAPointInTheCurveViewIsOneUndoStepAndTheSoundFollows) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    rig.moveCursor(defaults.shapes[1].position[0], defaults.shapes[1].position[1]);
    ASSERT_EQ(rig.custom().curveViewShape(), 1);
    auto& curve = rig.curve();
    ASSERT_GT(curve.getWidth(), 0);
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), [] {
        CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return config;
    }());
    const int node = 3;
    const auto from = geometry.nodePosition(node);
    const auto to = from + juce::Point<float>(0.0f, -20.0f);
    const int serial = rig.undo.getEditSerial();
    const int generation = rig.filter->getCustomShapesGeneration();

    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDrag(curve, to, from));
    EXPECT_GT(rig.filter->getCustomShapesGeneration(), generation) << "the module has the edit mid-drag";
    curve.mouseUp(curveLeftDrag(curve, to, from));

    EXPECT_GT(rig.filter->getCustomShapes().shapes[1].curve.points[(size_t)node].y,
              defaults.shapes[1].curve.points[(size_t)node].y);
    EXPECT_TRUE(rig.filter->getCustomShapes().shapes[0] == defaults.shapes[0]);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "a whole drag is one undo step";

    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
    rig.card->timerCallback();
    EXPECT_EQ(rig.curve().getModel().getNode(node).y, defaults.shapes[1].curve.points[(size_t)node].y)
        << "the view follows an undo";
}

TEST_F(ModuleComponentTest, ShowCurveToggleHidesTheViewClosesUpTheCardAndSurvivesASaveLoad) {
    CurveCardRig rig;
    const int shownHeight = rig.card->getHeight();
    auto& toggle = rig.custom().getCurveToggle();
    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.curve().isVisible());
    EXPECT_LT(rig.card->getHeight(), shownHeight);
    ASSERT_TRUE(rig.filter->getCardViewState().showCurveView.has_value());
    EXPECT_FALSE(*rig.filter->getCardViewState().showCurveView);

    const auto restored = synth::CardViewState::fromVar(rig.filter->getCardViewState().toVar());
    ASSERT_TRUE(restored.showCurveView.has_value());
    EXPECT_FALSE(*restored.showCurveView);

    ModuleComponent again(rig.filter, rig.node->nodeID, rig.editor, &rig.undo);
    again.timerCallback();
    EXPECT_FALSE(again.getMorphCard()->getCurveView().isVisible()) << "a rebuilt card keeps the choice";
}
