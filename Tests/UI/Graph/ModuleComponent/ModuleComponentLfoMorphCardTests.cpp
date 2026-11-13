// ModuleComponentLfoMorphCard tests: the LFO card's Custom shape with several shapes -- the morph map appearing from
// two shapes up, its sliders and cursor driven by real mouse events, where a Morph X / Y cable lands, the Edit shapes
// button and the double-clicks opening the shape editor, the curve editor following the shape nearest the cursor, and
// the shared fade path. The Filter's card is covered in ModuleComponentMorphShapesCardTests.cpp.
#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapePicker.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

struct LfoCardRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    /** `shapes` shapes laid on the diagonal (the first at the corner), the card on the Custom shape. */
    explicit LfoCardRig(int shapes = 3, bool custom = true) {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        synth::LfoCustomShapes list;
        for (int i = 0; i < shapes; ++i) {
            const float t = shapes > 1 ? (float)i / (float)(shapes - 1) : 0.5f;
            list.shapes.push_back({"s" + juce::String(i),
                                   synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Triangle),
                                   {t * 0.8f + 0.1f, t * 0.8f + 0.1f}});
        }
        lfo->setCustomShapes(list);
        card = std::make_unique<ModuleComponent>(lfo, node->nodeID, editor, &undo);
        if (custom)
            setShape(LFOModule::kCustomShapeIndex);
    }

    void setShape(int index) {
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(lfo, "shape")) = index;
        card->timerCallback(); // the card reads the shape on its gated tick
        card->getCardBody()->flushPendingConditionUpdate();
    }
    void setMorph(float x, float y) {
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(lfo, "morphX")) = x;
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(lfo, "morphY")) = y;
        card->timerCallback();
    }
    MorphShapesCard& morph() { return *card->getMorphCard(); }
    MorphMap& map() { return morph().getMap(); }
    juce::Slider* knob(const juce::String& name) {
        for (auto* child : card->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child))
                if (s->getComponentID() == name)
                    return s;
        return nullptr;
    }
    bool knobShown(const juce::String& name) {
        auto* s = knob(name);
        return s != nullptr && s->isVisible();
    }
    CurveEditorComponent* curve() {
        for (auto* child : card->getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                return c;
        return nullptr;
    }
};

juce::MouseEvent mouseOn(juce::Component& c, juce::Point<float> p, bool dragged = false, juce::Point<float> down = {}) {
    return makeCurveMouseEvent(c, p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), dragged,
                               dragged ? down : p);
}

/** Captures the editors the card opens instead of showing real windows. */
struct WindowCapture {
    std::vector<std::unique_ptr<ShapeListEditor>> opened;
    WindowCapture() {
        test_hooks::shapeEditorWindowHookForTest() = [this](std::unique_ptr<ShapeListEditor> e) {
            opened.push_back(std::move(e));
        };
    }
    ~WindowCapture() { test_hooks::shapeEditorWindowHookForTest() = nullptr; }
};

} // namespace

// Regression test for FRO943: the toggle is drawn as a pill, not a tick box.
TEST_F(ModuleComponentTest, LfoShowMorphMapToggleHasThePillStyle) {
    LfoCardRig rig;
    EXPECT_TRUE((bool)rig.morph().getToggle().getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty]);
}

TEST_F(ModuleComponentTest, LfoMorphMapAppearsWithSeveralShapesAndNotWithOne) {
    LfoCardRig several(3);
    EXPECT_TRUE(several.map().isVisible());
    EXPECT_TRUE(several.morph().getToggle().getToggleState());
    EXPECT_TRUE(several.morph().getEditButton().isVisible());
    EXPECT_FALSE(several.knobShown("Morph X")) << "the map's sliders stand in for the knobs";
    EXPECT_FALSE(several.knobShown("Morph Y"));
    EXPECT_TRUE(several.card->getLocalBounds().contains(several.map().getBounds()));
    EXPECT_GT(several.map().getY(), several.curve()->getBottom()) << "the map sits under the curve editor";

    LfoCardRig single(1);
    EXPECT_FALSE(single.map().isVisible());
    EXPECT_FALSE(single.morph().getToggle().getToggleState());
    EXPECT_TRUE(single.morph().getEditButton().isVisible()) << "Edit shapes is there with one shape too";
    EXPECT_TRUE(single.knobShown("Morph X"));
    EXPECT_TRUE(single.knobShown("Morph Y"));
}

TEST_F(ModuleComponentTest, LfoMorphPartsAreAbsentOutsideCustomAndTheCardKeepsItsHeight) {
    LfoCardRig rig(3, false);
    const int sineHeight = rig.card->getHeight();
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_FALSE(rig.morph().getEditButton().isVisible());
    EXPECT_FALSE(rig.morph().getToggle().isVisible());
    EXPECT_FALSE(rig.knobShown("Morph X"));

    rig.setShape(LFOModule::kCustomShapeIndex);
    EXPECT_TRUE(rig.map().isVisible());
    EXPECT_GT(rig.card->getHeight(), sineHeight);
    rig.setShape(0);
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_FALSE(rig.morph().getToggle().isVisible());
    EXPECT_EQ(rig.card->getHeight(), sineHeight) << "back to a built-in shape: exactly the old card";
}

TEST_F(ModuleComponentTest, LfoShowMorphMapToggleSwapsTheMapForTheKnobsAndIsRemembered) {
    LfoCardRig rig;
    auto& toggle = rig.morph().getToggle();
    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_TRUE(rig.knobShown("Morph X"));
    EXPECT_TRUE(rig.knobShown("Morph Y"));
    ASSERT_TRUE(rig.lfo->getCardViewState().showMorphMap.has_value());
    EXPECT_FALSE(*rig.lfo->getCardViewState().showMorphMap);

    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(rig.map().isVisible());
    EXPECT_FALSE(rig.knobShown("Morph X"));

    toggle.setToggleState(false, juce::sendNotificationSync);
    ModuleComponent rebuilt(rig.lfo, rig.node->nodeID, rig.editor, &rig.undo);
    EXPECT_FALSE(rebuilt.getMorphCard()->getMap().isVisible()) << "a rebuilt card keeps the choice";
}

TEST_F(ModuleComponentTest, LfoMapSlidersSetMorphXAndYWithOneGestureEach) {
    LfoCardRig rig;
    auto& map = rig.map();
    for (const auto axis : {MorphMap::Axis::X, MorphMap::Axis::Y}) {
        const int serial = rig.undo.getEditSerial();
        const auto from = map.getStripBounds(axis).getCentre();
        const auto to =
            axis == MorphMap::Axis::X ? from + juce::Point<float>(60.0f, 0.0f) : from - juce::Point<float>(0.0f, 40.0f);
        map.mouseDown(mouseOn(map, from));
        map.mouseDrag(mouseOn(map, to, true, from));
        map.mouseUp(mouseOn(map, to, true, from));
        EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one drag is one undo step";
    }
    EXPECT_GT(rig.lfo->getCurrentMorphX(), 0.5f);
    EXPECT_LT(rig.lfo->getCurrentMorphY(), 0.5f) << "the map's y runs downwards, so dragging up lowers Y";
}

TEST_F(ModuleComponentTest, LfoDraggingTheMapCursorSetsMorphWithOneGestureAndUndoStep) {
    LfoCardRig rig;
    auto& map = rig.map();
    const int serial = rig.undo.getEditSerial();
    const auto from = map.getCursorCentre();
    const auto to = map.toPixels(0.8f, 0.2f);
    map.mouseDown(mouseOn(map, from));
    map.mouseDrag(mouseOn(map, to, true, from));
    map.mouseUp(mouseOn(map, to, true, from));
    EXPECT_NEAR(rig.lfo->getCurrentMorphX(), 0.8f, 0.02f);
    EXPECT_NEAR(rig.lfo->getCurrentMorphY(), 0.2f, 0.02f);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    rig.undo.undo();
    EXPECT_NEAR(rig.lfo->getCurrentMorphX(), 0.5f, 0.02f);
}

TEST_F(ModuleComponentTest, LfoMorphCablesLandOnTheSlidersWhileTheMapShowsAndOnTheKnobsWhileItIsHidden) {
    LfoCardRig rig;
    const auto strips = {std::pair{MorphMap::Axis::X, LFOModule::kMorphXChannel},
                         std::pair{MorphMap::Axis::Y, LFOModule::kMorphYChannel}};
    for (const auto& [axis, channel] : strips) {
        const auto centre =
            (rig.map().getStripBounds(axis).getCentre() + rig.map().getPosition().toFloat()).roundToInt();
        const auto port = rig.card->getModTargetPortForPoint(centre);
        ASSERT_TRUE(port.has_value());
        EXPECT_EQ(port->index, channel);
        EXPECT_TRUE(port->isInput);
        const auto anchor = rig.card->getModTargetKnobAnchor(channel);
        ASSERT_TRUE(anchor.has_value());
        const auto strip = rig.map().getStripBounds(axis).translated((float)rig.map().getX(), (float)rig.map().getY());
        EXPECT_TRUE(strip.expanded(16.0f).contains(*anchor)) << "the cable lands on the slider, not on a hidden knob";
    }
    rig.card->setModDropTargetChannel(LFOModule::kMorphYChannel);
    EXPECT_EQ(rig.card->getModDropTargetChannel(), LFOModule::kMorphYChannel);
    rig.card->setModDropTargetChannel(-1);

    rig.morph().getToggle().setToggleState(false, juce::sendNotificationSync);
    const auto stripCentre =
        (rig.map().getStripBounds(MorphMap::Axis::X).getCentre() + rig.map().getPosition().toFloat()).roundToInt();
    EXPECT_FALSE(rig.card->getModTargetPortForPoint(stripCentre).has_value()) << "the hidden map takes no drop";
    auto* knob = rig.knob("Morph X");
    ASSERT_TRUE(knob != nullptr && knob->isVisible());
    const auto knobPort = rig.card->getModTargetPortForPoint(knob->getBounds().getCentre());
    ASSERT_TRUE(knobPort.has_value());
    EXPECT_EQ(knobPort->index, LFOModule::kMorphXChannel);
    const auto anchor = rig.card->getModTargetKnobAnchor(LFOModule::kMorphXChannel);
    ASSERT_TRUE(anchor.has_value());
    EXPECT_LT(anchor->getDistanceFrom(knob->getBounds().getCentre().toFloat()), (float)knob->getWidth());
}

TEST_F(ModuleComponentTest, LfoEditShapesButtonAndDoubleClicksOpenTheWindowOnTheNearestShape) {
    LfoCardRig rig;
    WindowCapture capture;
    auto& map = rig.map();

    ASSERT_TRUE(map.onEditDot);
    map.onEditDot(2); // the dot menu's Edit item
    ASSERT_EQ(capture.opened.size(), 1u);
    EXPECT_EQ(capture.opened.back()->getSelected(), 2);
    EXPECT_EQ(capture.opened.back()->getWindowTitle(), "LFO shapes: " + rig.card->cardTitle());

    rig.setMorph(0.85f, 0.88f);
    rig.morph().getEditButton().onClick();
    ASSERT_EQ(capture.opened.size(), 2u);
    EXPECT_EQ(capture.opened.back()->getSelected(), 2) << "the shape nearest the cursor";
    rig.setMorph(0.1f, 0.1f);
    rig.morph().getEditButton().onClick();
    ASSERT_EQ(capture.opened.size(), 3u);
    EXPECT_EQ(capture.opened.back()->getSelected(), 0);

    auto* curve = rig.curve();
    ASSERT_NE(curve, nullptr);
    const auto spot = curve->getLocalBounds().toFloat().getCentre() + juce::Point<float>(10.0f, -30.0f);
    const auto points = rig.lfo->getCustomShapes().shapes[0].curve.points.size();
    curve->mouseDoubleClick(mouseOn(*curve, spot));
    ASSERT_EQ(capture.opened.size(), 4u) << "a double-click on the curve view opens the window with several shapes";
    EXPECT_EQ(capture.opened.back()->getSelected(), 0);
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[0].curve.points.size(), points) << "and adds no point";
}

TEST_F(ModuleComponentTest, LfoDoubleClickOnTheCurveStillAddsAPointWithOneShape) {
    LfoCardRig rig(1);
    WindowCapture capture;
    auto* curve = rig.curve();
    ASSERT_NE(curve, nullptr);
    const auto spot = curve->getLocalBounds().toFloat().getCentre() + juce::Point<float>(10.0f, -30.0f);
    const auto points = rig.lfo->getCustomWave().points.size();
    curve->mouseDoubleClick(mouseOn(*curve, spot));
    EXPECT_TRUE(capture.opened.empty());
    EXPECT_EQ(rig.lfo->getCustomWave().points.size(), points + 1);
}

TEST_F(ModuleComponentTest, LfoCurveEditorFollowsTheShapeNearestTheCursorAndEditsIt) {
    LfoCardRig rig;
    rig.lfo->setCustomShapeCurve(2, synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Square));
    rig.card->timerCallback();
    rig.setMorph(0.1f, 0.1f);
    EXPECT_EQ(rig.curve()->getModel().getNumNodes(), (int)rig.lfo->getCustomShapes().shapes[0].curve.points.size());

    rig.setMorph(0.9f, 0.9f);
    EXPECT_EQ(rig.curve()->getModel().getNumNodes(), (int)rig.lfo->getCustomShapes().shapes[2].curve.points.size())
        << "the small editor now shows the Square shape";
    EXPECT_NE(rig.curve()->getDescription().indexOfIgnoreCase("s2"), -1);

    const auto first = rig.lfo->getCustomShapes().shapes[0].curve;
    rig.card->applyLfoWaveTool(0); // Invert: edits the shape under the cursor only
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[0].curve, first);
    EXPECT_NE(rig.lfo->getCustomShapes().shapes[2].curve,
              synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Square));
}

TEST_F(ModuleComponentTest, LfoShowingAndHidingTheMapGoesThroughTheSharedFadePathAndEasesTheHeight) {
    LfoCardRig rig;
    FadeAnimateGuard guard;
    auto& toggle = rig.morph().getToggle();
    const int shown = rig.card->getHeight();

    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_TRUE(rig.map().isVisible()) << "the map stays while it fades out";
    const int atStart = rig.card->getHeight();
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(rig.map().getAlpha(), 0.5f, 0.05f);
    const int mid = rig.card->getHeight();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.map().isVisible());
    const int hidden = rig.card->getHeight();
    EXPECT_NE(hidden, shown);
    EXPECT_NE(mid, hidden) << "the height eases through the fade, it does not jump to its end";
    EXPECT_NE(mid, atStart);
    EXPECT_TRUE(rig.knobShown("Morph X")) << "the knobs come back as the map leaves";

    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_NEAR(rig.map().getAlpha(), 0.0f, 0.05f) << "frame 0 is the first fade frame";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), shown);
    EXPECT_FALSE(rig.knobShown("Morph X"));
}

// A right-click on a map slider, on the card or in its window, opens the
// Morph knob's usual menu for that slider's parameter, and the card's own dot menu still has Edit.
TEST_F(ModuleComponentTest, LfoSliderRightClickOnTheCardAndInTheWindowOpenTheSameKnobMenuAndTheCardDotMenuKeepsEdit) {
    LfoCardRig rig;
    CapturedMenu captured;
    rig.card->setShowContextMenuHookForTest([&](juce::PopupMenu& m) { captured.capture(m); });
    juce::String automated;
    rig.editor.onAutomateParameterRequested = [&](juce::AudioProcessorGraph::NodeID, const juce::String& id) {
        automated = id;
    };
    std::vector<std::unique_ptr<ShapeListEditor>> opened;
    test_hooks::shapeEditorWindowHookForTest() = [&](std::unique_ptr<ShapeListEditor> e) {
        opened.push_back(std::move(e));
    };
    rig.morph().openEditor(0);
    test_hooks::shapeEditorWindowHookForTest() = nullptr;
    ASSERT_EQ(opened.size(), 1u);
    MorphMap& windowMap = opened.back()->getMap();
    const auto rightClick = [](juce::Component& c, juce::Point<float> p) { return curveRightClick(c, p); };

    for (const auto& [axis, id] : {std::pair{MorphMap::Axis::X, "morphX"}, std::pair{MorphMap::Axis::Y, "morphY"}}) {
        const auto expected = "Automate '" + findParameterByID(rig.lfo, id)->getName(100) + "'";
        for (MorphMap* map : {&rig.map(), &windowMap}) {
            captured.items.clear();
            automated.clear();
            map->mouseDown(rightClick(*map, map->getStripBounds(axis).getCentre()));
            ASSERT_FALSE(captured.items.empty()) << id;
            EXPECT_EQ(captured.items.front(), expected.toStdString());
            ASSERT_TRUE(captured.run(expected));
            EXPECT_EQ(automated, id) << (map == &windowMap ? "window" : "card");
        }
    }

    captured.items.clear();
    rig.map().mouseDown(rightClick(rig.map(), rig.map().getDotCentre(0)));
    ASSERT_FALSE(captured.items.empty());
    EXPECT_EQ(captured.items.front(), "Edit") << "the card's dot menu keeps Edit";
}

using namespace synth::ui;
using namespace synth::ui::test;
using synth::shapes::ShapeKind;
using synth::shapes::ShapeLibrary;

namespace {

struct TempLibrary {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("shape-picker-" + juce::Uuid().toDashedString());
    TempLibrary() { ShapeLibrary::shared().setRootForTest(dir); }
    ~TempLibrary() {
        ShapeLibrary::shared().setRootForTest({});
        dir.deleteRecursively();
    }
};

struct PickerRig {
    AudioEngine engine;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;
    CapturedMenu captured;

    MorphShapesCard& morph() { return *card->getMorphCard(); }

    void captureMenus() {
        card->setShowContextMenuHookForTest([this](juce::PopupMenu& m) { captured.capture(m); });
    }
    /** The menu item whose text is `title`, or null. */
    bool hasThumbnail(const juce::String& title) {
        juce::PopupMenu::MenuItemIterator it(captured.menu, false);
        while (it.next())
            if (it.getItem().text == title)
                return it.getItem().customComponent != nullptr;
        return false;
    }
};

struct FilterPickerRig : PickerRig {
    FilterModule* filter = nullptr;
    FilterPickerRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
        card->timerCallback();
        captureMenus();
    }
};

struct LfoPickerRig : PickerRig {
    LFOModule* lfo = nullptr;
    LfoPickerRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        synth::LfoCustomShapes list;
        for (int i = 0; i < 3; ++i) {
            const float t = (float)i / 2.0f;
            list.shapes.push_back({"s" + juce::String(i),
                                   synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Triangle),
                                   {t * 0.8f + 0.1f, t * 0.8f + 0.1f}});
        }
        lfo->setCustomShapes(list);
        card = std::make_unique<ModuleComponent>(lfo, node->nodeID, editor, &undo);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(lfo, "shape")) = LFOModule::kCustomShapeIndex;
        card->timerCallback();
        card->getCardBody()->flushPendingConditionUpdate();
        captureMenus();
    }
};

} // namespace

TEST_F(ModuleComponentTest, PickShapeOnTheFilterCardListsEveryShapeWithAThumbnailAndStampsTheOneNearestTheCursor) {
    TempLibrary library;
    ASSERT_EQ(ShapeLibrary::shared().save(ShapeKind::Filter, "mine",
                                          synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::RampUp)),
              ShapeLibrary::SaveResult::Saved);
    FilterPickerRig rig;
    auto& pick = rig.morph().getPickButton();
    ASSERT_TRUE(pick.isVisible());
    EXPECT_EQ(pick.getTitle(), "Pick shape");
    EXPECT_FALSE(pick.getTooltip().isEmpty());
    EXPECT_TRUE(pick.getWantsKeyboardFocus());
    EXPECT_FALSE(pick.getBounds().intersects(rig.morph().getEditButton().getBounds()));

    pick.onClick();
    ASSERT_FALSE(rig.captured.items.empty());
    EXPECT_TRUE(rig.hasThumbnail("mine, Yours")) << "a saved shape is a thumbnail row badged Yours";
    EXPECT_TRUE(rig.hasThumbnail("low-pass ramp, Factory"));
    EXPECT_TRUE(rig.hasThumbnail("double peak, Factory"));

    const auto nearest = (size_t)rig.morph().nearestShapeToCursor();
    const auto before = rig.filter->getCustomShapes();
    const int serial = rig.undo.getEditSerial();
    ASSERT_TRUE(rig.captured.run("low-pass ramp, Factory"));
    const auto expected = ShapeLibrary::shared().find(ShapeKind::Filter, true, "low-pass ramp")->curve;
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[nearest].curve, expected);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    rig.undo.undo();
    EXPECT_EQ(rig.filter->getCustomShapes(), before);
}

namespace {
/** A real click on dot `index` of the card's map: it becomes the selected dot. Returns it. */
int clickDotAwayFromTheCursor(MorphShapesCard& morph) {
    auto& map = morph.getMap();
    const int index = (morph.nearestShapeToCursor() + 1) % morph.getNumShapes();
    const auto at = map.getDotCentre(index);
    map.mouseDown(mouseOn(map, at));
    map.mouseUp(mouseOn(map, at));
    return index;
}
} // namespace

// Regression test for FRO1585: Pick shape changed the dot nearest the cursor, not the dot the user had selected.
TEST_F(ModuleComponentTest, PickShapeOnTheFilterCardStampsTheSelectedDotNotTheOneNearestTheCursor) {
    TempLibrary library;
    FilterPickerRig rig;
    const int selected = clickDotAwayFromTheCursor(rig.morph());
    ASSERT_NE(selected, rig.morph().nearestShapeToCursor());
    ASSERT_EQ(rig.morph().getMap().getSelected(), selected);
    EXPECT_EQ(rig.morph().targetShapeIndex(), selected);

    const auto before = rig.filter->getCustomShapes();
    const int serial = rig.undo.getEditSerial();
    rig.morph().getPickButton().onClick();
    ASSERT_TRUE(rig.captured.run("low-pass ramp, Factory"));
    const auto expected = ShapeLibrary::shared().find(ShapeKind::Filter, true, "low-pass ramp")->curve;
    const auto after = rig.filter->getCustomShapes();
    for (size_t i = 0; i < after.shapes.size(); ++i) {
        if ((int)i == selected)
            EXPECT_EQ(after.shapes[i].curve, expected);
        else
            EXPECT_EQ(after.shapes[i].curve, before.shapes[i].curve) << "dot " << i << " is not the selected one";
    }
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one undo step";
    rig.undo.undo();
    EXPECT_EQ(rig.filter->getCustomShapes(), before);
}

TEST_F(ModuleComponentTest, PickShapeOnTheLfoCardStampsTheSelectedDotAndFallsBackToTheNearestOne) {
    TempLibrary library;
    LfoPickerRig rig;
    const auto triangle = synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Triangle);
    const auto heartbeat = ShapeLibrary::shared().find(ShapeKind::Lfo, true, "heartbeat")->curve;

    // Nothing selected: the nearest dot, as before.
    ASSERT_EQ(rig.morph().getMap().getSelected(), -1);
    const auto nearest = (size_t)rig.morph().nearestShapeToCursor();
    EXPECT_EQ((size_t)rig.morph().targetShapeIndex(), nearest);

    const int selected = clickDotAwayFromTheCursor(rig.morph());
    const int serial = rig.undo.getEditSerial();
    rig.morph().getPickButton().onClick();
    ASSERT_TRUE(rig.captured.run("heartbeat, Factory"));
    const auto shapes = rig.lfo->getCustomShapes().shapes;
    for (size_t i = 0; i < shapes.size(); ++i)
        EXPECT_EQ(shapes[i].curve, (int)i == selected ? heartbeat : triangle) << "dot " << i;
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    rig.undo.undo();
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[(size_t)selected].curve, triangle);
}

// Regression test for FRO1585: the cursor being the selection has no dot, so Pick shape and a drop use the nearest.
TEST_F(ModuleComponentTest, PickShapeWithTheCursorSelectedUsesTheNearestDot) {
    LfoPickerRig rig;
    clickDotAwayFromTheCursor(rig.morph());
    auto& map = rig.morph().getMap();
    const auto at = map.getCursorCentre();
    map.mouseDown(mouseOn(map, at));
    map.mouseUp(mouseOn(map, at));
    ASSERT_TRUE(map.isCursorSelected());
    EXPECT_EQ(rig.morph().targetShapeIndex(), rig.morph().nearestShapeToCursor());
}

// A drop on the card keeps replacing the shape under the cursor, whatever dot is selected.
TEST_F(ModuleComponentTest, ADropOnTheCardStillReplacesTheShapeNearestTheCursorNotTheSelectedDot) {
    TempLibrary library;
    LfoPickerRig rig;
    const int selected = clickDotAwayFromTheCursor(rig.morph());
    const auto nearest = (size_t)rig.morph().nearestShapeToCursor();
    const auto shape = *ShapeLibrary::shared().find(ShapeKind::Lfo, true, "heartbeat");
    rig.morph().shapeDropped(shape);
    const auto shapes = rig.lfo->getCustomShapes().shapes;
    EXPECT_EQ(shapes[nearest].curve, shape.curve);
    EXPECT_NE(shapes[(size_t)selected].curve, shape.curve);
}

// Regression test for FRO1586: Edit shapes opened on the dot nearest the cursor, not the selected one.
TEST_F(ModuleComponentTest, EditShapesOpensTheWindowOnTheSelectedDotOnBothCards) {
    WindowCapture capture;
    {
        FilterPickerRig rig;
        const int selected = clickDotAwayFromTheCursor(rig.morph());
        rig.morph().getEditButton().onClick();
        ASSERT_EQ(capture.opened.size(), 1u);
        EXPECT_EQ(capture.opened.back()->getSelected(), selected);
    }
    {
        LfoPickerRig rig;
        const int selected = clickDotAwayFromTheCursor(rig.morph());
        rig.morph().getEditButton().onClick();
        ASSERT_EQ(capture.opened.size(), 2u);
        EXPECT_EQ(capture.opened.back()->getSelected(), selected);
    }
}

TEST_F(ModuleComponentTest, EditShapesWithNothingSelectedOpensOnTheNearestDot) {
    WindowCapture capture;
    LfoPickerRig rig;
    ASSERT_EQ(rig.morph().getMap().getSelected(), -1);
    rig.morph().getEditButton().onClick();
    ASSERT_EQ(capture.opened.size(), 1u);
    EXPECT_EQ(capture.opened.back()->getSelected(), rig.morph().nearestShapeToCursor());
}

TEST_F(ModuleComponentTest, PickShapeOpensFromTheKeyboardWithReturn) {
    TempLibrary library;
    FilterPickerRig rig;
    auto& pick = rig.morph().getPickButton();
    EXPECT_TRUE(pick.getWantsKeyboardFocus()) << "a Tab stop";
    EXPECT_TRUE(static_cast<juce::Component&>(pick).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(synth::test::waitUntil([&] { return !rig.captured.items.empty(); }, 2000))
        << "Return on the focused button opens the picker";
}

TEST_F(ModuleComponentTest, PickShapeOnTheLfoCardOffersTheLfoShapesNotTheFilterOnes) {
    TempLibrary library;
    LfoPickerRig rig;
    rig.morph().getPickButton().onClick();
    ASSERT_FALSE(rig.captured.items.empty());
    EXPECT_TRUE(rig.hasThumbnail("heartbeat, Factory"));
    EXPECT_TRUE(rig.hasThumbnail("ramp up, Factory"));
    EXPECT_FALSE(rig.hasThumbnail("low-pass ramp, Factory"));

    const int serial = rig.undo.getEditSerial();
    const auto nearest = (size_t)rig.morph().nearestShapeToCursor();
    ASSERT_TRUE(rig.captured.run("heartbeat, Factory"));
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[nearest].curve,
              ShapeLibrary::shared().find(ShapeKind::Lfo, true, "heartbeat")->curve);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    rig.undo.undo();
    EXPECT_EQ(rig.lfo->getCustomShapes().shapes[nearest].curve,
              synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::Triangle));
}
