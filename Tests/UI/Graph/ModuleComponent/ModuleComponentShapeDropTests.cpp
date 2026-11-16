// A library shape dragged from the Sounds view onto the Filter card and the shape window: one rule everywhere -- on a
// map it adds a dot (over a dot it stamps that dot), on a curve it replaces the selected dot's curve, each as one undo
// step, and the hint says what the release does. The drag is routed the way juce::DragAndDropContainer routes it: the
// deepest component under the pointer that takes the shape, so a card that swallowed the drop would show here.
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

synth::LfoCustomWave factoryCurve(const juce::String& name) {
    return ShapeLibrary::shared().find(ShapeKind::Filter, true, name)->curve;
}

struct Landing {
    juce::Component* target = nullptr;
    juce::String hint; ///< what the target said while the pointer was over it
};

/** Drags the factory Filter shape `name` from outside `root` to `at` (root coordinates) and releases it. */
Landing dragAndRelease(juce::Component& root, const juce::String& name, juce::Point<int> at) {
    const auto payload = makeShapePayload({ShapeKind::Filter, true, name});
    juce::DragAndDropTarget::SourceDetails probe{payload, nullptr, at};
    root.setVisible(true); // a hidden component is never under the pointer
    Landing landing;
    for (auto* c = root.getComponentAt(at); c != nullptr && landing.target == nullptr; c = c->getParentComponent())
        if (auto* t = dynamic_cast<juce::DragAndDropTarget*>(c); t != nullptr && t->isInterestedInDragSource(probe))
            landing.target = c;
    if (landing.target == nullptr)
        return landing;
    auto* target = dynamic_cast<juce::DragAndDropTarget*>(landing.target);
    const auto local = landing.target->getLocalPoint(&root, at);
    const juce::DragAndDropTarget::SourceDetails details{payload, nullptr, local};
    target->itemDragEnter({payload, nullptr, local + juce::Point<int>(1, 1)});
    target->itemDragMove(details);
    if (auto* map = dynamic_cast<ShapeWindowMap*>(landing.target))
        landing.hint = map->getDropHint();
    if (auto* curve = dynamic_cast<ShapeDropCurveEditor*>(landing.target))
        landing.hint = curve->getDropHint();
    target->itemDropped(details);
    return landing;
}

struct CardRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    CardRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
        card->timerCallback();
    }
    MorphShapesCard& custom() { return *card->getMorphCard(); }
    ShapeWindowMap& map() { return custom().getMap(); }
    std::vector<ShapeItem> shapes() { return custom().getAdapter().getShapes(); }
    juce::Point<int> inCard(juce::Component& child, juce::Point<float> local) {
        return card->getLocalPoint(&child, local.roundToInt());
    }
    void select(int dot) {
        map().setSelected(dot);
        card->timerCallback(); // the curve view follows the selection on the card's tick
    }
};

} // namespace

// Regression test for FRO1600: a drop on the card's map replaced the dot nearest the cursor instead of adding one
TEST_F(ModuleComponentTest, AShapeDroppedOnEmptySpaceOfTheCardsMapAddsADotThereAsOneUndoStep) {
    CardRig rig;
    const auto before = rig.shapes();
    const int serial = rig.undo.getEditSerial();

    const auto landing = dragAndRelease(*rig.card, "ih", rig.inCard(rig.map(), rig.map().toPixels(0.9f, 0.9f)));

    EXPECT_EQ(landing.target, &rig.map()) << "the map takes the drop, not the card around it";
    EXPECT_EQ(landing.hint, "Drop to add ih here");
    const auto after = rig.shapes();
    ASSERT_EQ(after.size(), before.size() + 1);
    EXPECT_EQ(after.back().curve, factoryCurve("ih"));
    EXPECT_EQ(after.back().name, "ih");
    EXPECT_NEAR(after.back().position[0], 0.9f, 0.01f);
    EXPECT_NEAR(after.back().position[1], 0.9f, 0.01f);
    for (size_t i = 0; i < before.size();
         ++i) { // an add may give the dots colour slots, so compare what a drop could touch
        EXPECT_EQ(after[i].curve, before[i].curve) << "dot " << i;
        EXPECT_EQ(after[i].name, before[i].name) << "dot " << i;
        EXPECT_EQ(after[i].position, before[i].position) << "dot " << i;
    }
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_FALSE(rig.map().isDropOver());

    rig.undo.undo();
    EXPECT_EQ(rig.shapes(), before) << "one undo removes the new dot";
}

TEST_F(ModuleComponentTest, AShapeDroppedOnADotOfTheCardsMapStampsThatDotAndKeepsItsNameAndPlace) {
    CardRig rig;
    const auto before = rig.shapes();
    const int serial = rig.undo.getEditSerial();

    const auto landing = dragAndRelease(*rig.card, "eh", rig.inCard(rig.map(), rig.map().getDotCentre(1)));

    EXPECT_EQ(landing.target, &rig.map());
    EXPECT_EQ(landing.hint, "Drop to stamp eh onto " + before[1].name);
    const auto after = rig.shapes();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after[1].curve, factoryCurve("eh"));
    EXPECT_EQ(after[1].name, before[1].name);
    EXPECT_EQ(after[1].position, before[1].position);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
}

TEST_F(ModuleComponentTest, AtTheShapeCapTheCardsMapHintSaysThereIsNoRoomAndTheDropAddsNothing) {
    CardRig rig;
    auto full = rig.filter->getCustomShapes();
    while ((int)full.shapes.size() < rig.custom().getAdapter().getMaxShapes()) {
        auto copy = full.shapes[0];
        copy.name = "s" + juce::String((int)full.shapes.size());
        full.shapes.push_back(copy);
    }
    rig.filter->setCustomShapes(full);
    rig.card->timerCallback();
    const auto before = rig.shapes();

    const auto landing = dragAndRelease(*rig.card, "ih", rig.inCard(rig.map(), rig.map().toPixels(0.97f, 0.97f)));

    EXPECT_EQ(landing.hint, "No room for another shape");
    EXPECT_EQ(rig.shapes(), before);
}

// Regression test for FRO1600: the card's curve view took no drop at all
TEST_F(ModuleComponentTest, AShapeDroppedOnTheCardsCurveReplacesTheSelectedDotsCurveAsOneUndoStep) {
    CardRig rig;
    rig.select(2);
    ASSERT_TRUE(rig.custom().isCurveViewShown());
    const auto before = rig.shapes();
    const int serial = rig.undo.getEditSerial();
    auto& curve = rig.custom().getCurveView();

    const auto landing =
        dragAndRelease(*rig.card, "eh", rig.inCard(curve, curve.getLocalBounds().getCentre().toFloat()));

    EXPECT_EQ(landing.target, &curve) << "the curve takes the drop, not the card around it";
    EXPECT_EQ(landing.hint, "Drop to stamp eh onto " + before[2].name);
    const auto after = rig.shapes();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after[2].curve, factoryCurve("eh"));
    EXPECT_EQ(after[2].name, before[2].name);
    EXPECT_EQ(after[0], before[0]) << "only the selected dot changes";
    EXPECT_EQ(after[1], before[1]);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_FALSE(curve.isDropOver());

    rig.undo.undo();
    EXPECT_EQ(rig.shapes(), before) << "one undo puts the curve back";
}

TEST_F(ModuleComponentTest, AShapeOfTheOtherKindIsRefusedByTheCardsMapAndCurve) {
    CardRig rig;
    const auto payload = makeShapePayload({ShapeKind::Lfo, true, "ramp up"});
    EXPECT_FALSE(rig.map().isInterestedInDragSource({payload, nullptr, {5, 5}}));
    EXPECT_FALSE(rig.custom().getCurveView().isInterestedInDragSource({payload, nullptr, {5, 5}}));
}

namespace {

struct WindowRig {
    ShapeRig rig;
    ShapeListEditor editor{rig.adapter, 1};
};

} // namespace

// Regression test for FRO1600: the window's curve took no drop at all
TEST(ShapeWindowCurveDrop, AShapeDroppedOnTheWindowsCurveReplacesTheSelectedDotsCurveAsOneUndoStep) {
    WindowRig w;
    const auto before = w.rig.adapter->getShapes();
    const int serial = w.rig.undo.getEditSerial();
    auto& curve = w.editor.getCurveEditor();

    const auto landing =
        dragAndRelease(w.editor, "eh", w.editor.getLocalPoint(&curve, curve.getLocalBounds().getCentre()));

    EXPECT_EQ(landing.target, &curve);
    EXPECT_EQ(landing.hint, "Drop to stamp eh onto " + before[1].name);
    const auto after = w.rig.adapter->getShapes();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after[1].curve, factoryCurve("eh"));
    EXPECT_EQ(after[0], before[0]);
    EXPECT_EQ(after[2], before[2]);
    EXPECT_EQ(w.editor.getSelected(), 1);
    EXPECT_EQ(w.rig.undo.getEditSerial(), serial + 1);

    w.rig.undo.undo();
    EXPECT_EQ(w.rig.adapter->getShapes(), before);
}

TEST(ShapeWindowCurveDrop, TheWindowsMapHintSaysThereIsNoRoomAtTheCap) {
    WindowRig w;
    auto full = w.rig.filter->getCustomShapes();
    while ((int)full.shapes.size() < w.rig.adapter->getMaxShapes()) {
        auto copy = full.shapes[0];
        copy.name = "s" + juce::String((int)full.shapes.size());
        full.shapes.push_back(copy);
    }
    w.rig.filter->setCustomShapes(full);
    w.editor.getMap().itemDragEnter({makeShapePayload({ShapeKind::Filter, true, "ih"}), nullptr,
                                     w.editor.getMap().toPixels(0.97f, 0.97f).roundToInt()});
    EXPECT_EQ(w.editor.getMap().getDropHint(), "No room for another shape");
}

// Pick shape in the window: the same picker as the card, applied to the selected dot as one undo step
TEST(ShapeWindowCurveDrop, PickShapeInTheWindowAppliesTheChosenShapeToTheSelectedDotAsOneUndoStep) {
    WindowRig w;
    CapturedMenu captured;
    w.editor.showMenu = [&](juce::PopupMenu& m) { captured.capture(m); };
    const auto before = w.rig.adapter->getShapes();
    const int serial = w.rig.undo.getEditSerial();

    w.editor.getPickShapeButton().onClick();
    ASSERT_TRUE(captured.run("telephone band, Factory"));

    const auto after = w.rig.adapter->getShapes();
    EXPECT_EQ(after[1].curve, factoryCurve("telephone band"));
    EXPECT_EQ(after[0], before[0]);
    EXPECT_EQ(after[2], before[2]);
    EXPECT_EQ(w.rig.undo.getEditSerial(), serial + 1);
    w.rig.undo.undo();
    EXPECT_EQ(w.rig.adapter->getShapes(), before);
}
