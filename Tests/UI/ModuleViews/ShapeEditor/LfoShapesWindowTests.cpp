// LfoShapesWindowTests.cpp -- the shared shape list editor window, opened for an LFO: title, the list it shows, and
// edits (add, duplicate, rename, draw, delete, the cursor) reaching the module, each undoable.
#include "../../Graph/ModuleComponent/ModuleComponentTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"

using namespace synth::ui;

namespace {

struct LfoWindowRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;
    std::unique_ptr<ShapeListEditor> window;

    LfoWindowRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        card = std::make_unique<ModuleComponent>(lfo, node->nodeID, editor, &undo);
    }

    /** Opens the window through the card's entry point; a headless runner cannot show a real one. */
    ShapeListEditor& open(int shape = 0) {
        test_hooks::shapeEditorWindowHookForTest() = [this](std::unique_ptr<ShapeListEditor> e) {
            window = std::move(e);
        };
        card->openShapesWindow(shape);
        test_hooks::shapeEditorWindowHookForTest() = nullptr;
        return *window;
    }
};

void click(juce::Button& button) { button.onClick(); }

} // namespace

TEST_F(ModuleComponentTest, TheCardOpensTheShapesWindowForAnLfoWithItsTitleAndOneShape) {
    LfoWindowRig rig;
    auto& editor = rig.open(0);
    EXPECT_EQ(editor.getNumShapes(), 1);
    EXPECT_EQ(editor.getSelected(), 0);
    EXPECT_TRUE(editor.getWindowTitle().startsWith("LFO shapes: "));
    EXPECT_EQ(editor.getCurveEditor().getModel().getNumNodes(), (int)rig.lfo->getCustomWave().points.size());
}

TEST_F(ModuleComponentTest, NoShapesWindowOpensForAModuleThatIsNotAnLfo) {
    AudioEngine engine;
    GraphEditor editor{engine};
    auto* gain = new AttenuverterModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(gain));
    ModuleComponent card(gain, node->nodeID, editor, nullptr);
    EXPECT_EQ(card.openShapesWindow(0), nullptr);
}

TEST_F(ModuleComponentTest, EditsInTheShapesWindowReachTheLfoAndUndo) {
    LfoWindowRig rig;
    auto& editor = rig.open(0);
    const int serial = rig.undo.getEditSerial();

    click(editor.getAddButton());
    ASSERT_EQ(rig.lfo->getCustomShapes().shapes.size(), 2u);
    EXPECT_EQ(editor.getSelected(), 1);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);

    click(editor.getDuplicateButton());
    ASSERT_EQ(rig.lfo->getCustomShapes().shapes.size(), 3u);
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[2].curve, rig.lfo->getCustomShapes().shapes[1].curve);

    click(editor.getRenameButton());
    editor.getMap().onRenameDot(2, "wobble");
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[2].name, "wobble");

    editor.setSelected(0);
    editor.applyDrawShape(DrawShape::Square);
    int steps = 0;
    const auto& drawn = rig.lfo->getCustomShapes().shapes[0].curve;
    for (size_t i = 1; i < drawn.points.size(); ++i)
        steps += drawn.points[i].x == drawn.points[i - 1].x ? 1 : 0;
    EXPECT_GE(steps, 2) << "the drawn square has vertical steps";

    editor.setSelected(2);
    click(editor.getDeleteButton());
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes.size(), 2u);

    for (int i = 0; i < 5; ++i)
        rig.undo.undo();
    EXPECT_TRUE(rig.lfo->getCustomShapes().isDefault());
}

TEST_F(ModuleComponentTest, TheShapesWindowCursorMovesTheLfoMorphKnobs) {
    LfoWindowRig rig;
    auto& editor = rig.open(0);
    (void)editor;
    auto adapter = LfoShapesAdapter(*rig.lfo, &rig.undo, rig.engine.getGraph(), rig.node->nodeID, "LFO");
    adapter.setCursor({0.25f, 0.75f});
    EXPECT_NEAR(rig.lfo->getCurrentMorphX(), 0.25f, 1e-4f);
    EXPECT_NEAR(rig.lfo->getCurrentMorphY(), 0.75f, 1e-4f);
    EXPECT_TRUE(adapter.hasCursor());
}

TEST_F(ModuleComponentTest, ANewShapeStartsAsACopyOfTheBlendAtTheCursor) {
    LfoWindowRig rig;
    auto adapter = LfoShapesAdapter(*rig.lfo, &rig.undo, rig.engine.getGraph(), rig.node->nodeID, "LFO");
    const auto item = adapter.makeShapeAt(0.2f, 0.3f);
    EXPECT_EQ(item.position, (std::vector<float>{0.2f, 0.3f}));
    ASSERT_GE(item.curve.points.size(), 2u);
    for (float x : {0.1f, 0.4f, 0.8f})
        EXPECT_NEAR(item.curve.evaluate(x), rig.lfo->getCustomWave().evaluate(x), 0.05f);
}

TEST_F(ModuleComponentTest, ACopiedBlendKeepsOnlyThePointsItNeeds) {
    LfoWindowRig rig;
    auto adapter = LfoShapesAdapter(*rig.lfo, &rig.undo, rig.engine.getGraph(), rig.node->nodeID, "LFO");
    synth::LfoCustomShapes list;
    list.shapes.push_back({"sine", synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::SoftSine), {0.5f, 0.5f}});
    rig.lfo->setCustomShapes(list);
    const auto item = adapter.makeShapeAt(0.2f, 0.3f);
    EXPECT_LT(item.curve.points.size(), (size_t)synth::LfoCustomWave::kMaxPoints);
    for (int i = 0; i <= 100; ++i)
        EXPECT_NEAR(item.curve.evaluate((float)i / 100.0f), list.shapes[0].curve.evaluate((float)i / 100.0f), 0.03f);
}
