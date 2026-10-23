// ModuleComponentMorphShapesCard tests: the Filter card's Custom section -- what shows in each type, the morph map
// and its sliders driven by real mouse events, the Show Morph Map pill and its remembered choice, where a cable for
// Morph X / Y lands, the editor window opening, the response view and the fades through the shared animation path.
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "ModuleComponentTestFixture.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

struct CardRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    explicit CardRig(bool custom = true, bool oneShape = false) {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        if (oneShape) {
            auto shapes = synth::CustomFilterShapes::defaultShapes();
            shapes.shapes.resize(1);
            filter->setCustomShapes(shapes);
        }
        if (custom)
            setType(FilterModule::kCustomTypeIndex);
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
    }

    void setType(int index) {
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) = index;
        if (card != nullptr)
            card->timerCallback(); // the card reads the type on its gated tick
    }
    MorphShapesCard& custom() { return *card->getMorphCard(); }
    MorphMap& map() { return custom().getMap(); }
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
    juce::Point<int> mapOrigin() { return map().getPosition(); }
};

juce::MouseEvent mouseOn(juce::Component& c, juce::Point<float> p, bool dragged = false, juce::Point<float> down = {}) {
    return makeCurveMouseEvent(c, p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), dragged,
                               dragged ? down : p);
}

} // namespace

TEST_F(ModuleComponentTest, FilterCustomSectionIsAbsentInTheLadderTypesAndKeepsTheirHeight) {
    CardRig rig(false);
    const int ladderHeight = rig.card->getHeight();
    EXPECT_FALSE(rig.custom().getMap().isVisible());
    EXPECT_FALSE(rig.custom().getEditButton().isVisible());
    EXPECT_FALSE(rig.custom().getToggle().isVisible());
    EXPECT_TRUE(rig.knobShown("Cutoff"));
    EXPECT_TRUE(rig.knobShown("Resonance"));
    EXPECT_FALSE(rig.knobShown("Shift"));
    EXPECT_FALSE(rig.knobShown("Depth"));

    rig.setType(FilterModule::kCustomTypeIndex);
    EXPECT_TRUE(rig.custom().getMap().isVisible());
    EXPECT_NE(rig.card->getHeight(), ladderHeight);
    rig.setType(0);
    EXPECT_FALSE(rig.custom().getMap().isVisible());
    EXPECT_EQ(rig.card->getHeight(), ladderHeight) << "back to a ladder type: exactly the old card";
}

TEST_F(ModuleComponentTest, CustomTypeSwapsCutoffAndResonanceForShiftAndDepthAndKeepsDrive) {
    CardRig rig;
    EXPECT_FALSE(rig.knobShown("Cutoff"));
    EXPECT_FALSE(rig.knobShown("Resonance"));
    EXPECT_TRUE(rig.knobShown("Shift"));
    EXPECT_TRUE(rig.knobShown("Depth"));
    EXPECT_TRUE(rig.knobShown("Drive"));
    EXPECT_TRUE(rig.knobShown("Key Track"));
    // Shift and Depth take Cutoff's and Resonance's cells.
    EXPECT_EQ(rig.knob("Shift")->getBounds().getTopLeft(), rig.knob("Cutoff")->getBounds().getTopLeft());
    EXPECT_EQ(rig.knob("Depth")->getBounds().getTopLeft(), rig.knob("Resonance")->getBounds().getTopLeft());
}

TEST_F(ModuleComponentTest, MorphMapIsShownByDefaultWithSeveralShapesAndHidesTheMorphKnobs) {
    CardRig rig;
    EXPECT_TRUE(rig.map().isVisible());
    EXPECT_TRUE(rig.custom().getToggle().getToggleState());
    EXPECT_FALSE(rig.knobShown("Morph X"));
    EXPECT_FALSE(rig.knobShown("Morph Y"));
    EXPECT_TRUE(rig.card->getLocalBounds().contains(rig.map().getBounds()));
    EXPECT_EQ(rig.map().getWidth(), 252);
    EXPECT_EQ(rig.map().getHeight(), 159);
}

TEST_F(ModuleComponentTest, MorphMapIsHiddenByDefaultWithOneShapeAndTheKnobsShow) {
    CardRig rig(true, true);
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_FALSE(rig.custom().getToggle().getToggleState());
    EXPECT_TRUE(rig.knobShown("Morph X"));
    EXPECT_TRUE(rig.knobShown("Morph Y"));
    EXPECT_TRUE(rig.custom().getEditButton().isVisible()) << "Edit shapes is there with one shape too";
}

TEST_F(ModuleComponentTest, ShowMorphMapToggleSwapsTheMapForTheKnobsAndIsRemembered) {
    CardRig rig;
    auto& toggle = rig.custom().getToggle();
    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_TRUE(rig.knobShown("Morph X"));
    EXPECT_TRUE(rig.knobShown("Morph Y"));
    ASSERT_TRUE(rig.filter->getCardViewState().showMorphMap.has_value());
    EXPECT_FALSE(*rig.filter->getCardViewState().showMorphMap);

    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(rig.map().isVisible());
    EXPECT_FALSE(rig.knobShown("Morph X"));

    toggle.setToggleState(false, juce::sendNotificationSync);
    ModuleComponent rebuilt(rig.filter, rig.node->nodeID, rig.editor, &rig.undo);
    EXPECT_FALSE(rebuilt.getMorphCard()->getMap().isVisible()) << "a rebuilt card keeps the choice";
}

// Regression test for FRO943: the toggle is drawn as a pill, not a tick box.
TEST_F(ModuleComponentTest, ShowMorphMapToggleHasThePillStyle) {
    CardRig rig;
    EXPECT_TRUE((bool)rig.custom().getToggle().getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty]);
}

// Regression test for FRO954: Shift and Depth share a cell with the hidden Cutoff and Resonance; a drop on
// them lands on the visible knob.
TEST_F(ModuleComponentTest, CableOverCustomShiftAndDepthLandsOnThem) {
    CardRig rig;
    ASSERT_FALSE(rig.knobShown("Cutoff"));
    for (const auto& [name, channel] : {std::pair<const char*, int>{"Shift", FilterModule::kShiftCVChannel},
                                        std::pair<const char*, int>{"Depth", FilterModule::kDepthCVChannel}}) {
        auto* knob = rig.knob(name);
        ASSERT_TRUE(knob != nullptr && knob->isVisible());
        const auto port = rig.card->getModTargetPortForPoint(knob->getBounds().getCentre());
        ASSERT_TRUE(port.has_value()) << name;
        EXPECT_EQ(port->index, channel) << name;
    }
}

TEST_F(ModuleComponentTest, MorphMapPillIsInTheFooterOnlyInCustom) {
    CardRig rig(false);
    EXPECT_FALSE(rig.custom().getToggle().isVisible());
    rig.setType(FilterModule::kCustomTypeIndex);
    EXPECT_TRUE(rig.custom().getToggle().isVisible());
    EXPECT_EQ(rig.custom().getToggle().getButtonText(), "Show Morph Map");
    EXPECT_FALSE(rig.custom().getToggle().getBounds().isEmpty());
    EXPECT_FALSE(rig.custom().getToggle().getTooltip().isEmpty());
}

TEST_F(ModuleComponentTest, DraggingTheCardsCursorSetsMorphWithOneGestureAndUndoStep) {
    CardRig rig;
    auto& map = rig.map();
    const int serial = rig.undo.getEditSerial();
    const auto from = map.getCursorCentre();
    const auto to = map.toPixels(0.8f, 0.2f);

    map.mouseDown(mouseOn(map, from));
    map.mouseDrag(mouseOn(map, to, true, from));
    map.mouseUp(mouseOn(map, to, true, from));

    EXPECT_NEAR(rig.filter->getCurrentMorphX(), 0.8f, 0.02f);
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), 0.2f, 0.02f);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
}

TEST_F(ModuleComponentTest, TheCardsMapFollowsTheKnobsAndUndoOnTheNextTick) {
    CardRig rig;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(rig.filter, "morphX")) = 0.9f;
    rig.card->timerCallback();
    EXPECT_NEAR(rig.map().getCursor().x, 0.9f, 1e-4);

    rig.custom().getController().addShapeAt(0.1f, 0.9f);
    EXPECT_EQ(rig.map().getNumDots(), 4);
    rig.undo.undo();
    rig.card->timerCallback();
    EXPECT_EQ(rig.map().getNumDots(), 3) << "the generation poll follows an undo";
}

TEST_F(ModuleComponentTest, DoubleClickingEmptyMapSpaceAddsAShapeFromTheCard) {
    CardRig rig;
    auto& map = rig.map();
    const auto at = map.toPixels(0.95f, 0.95f);
    map.mouseDown(mouseOn(map, at));
    map.mouseUp(mouseOn(map, at));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 3u) << "a single click adds nothing";
    map.mouseDown(mouseOn(map, at));
    map.mouseDoubleClick(mouseOn(map, at));
    map.mouseUp(mouseOn(map, at));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 4u);
}

TEST_F(ModuleComponentTest, DeletingDownToOneShapeHidesTheMapUnlessTheUserChose) {
    CardRig rig;
    rig.custom().getController().deleteShape(0);
    EXPECT_TRUE(rig.map().isVisible());
    rig.custom().getController().deleteShape(0);
    EXPECT_FALSE(rig.map().isVisible()) << "one shape: the default is hidden";
    EXPECT_TRUE(rig.knobShown("Morph X"));
}

TEST_F(ModuleComponentTest, EditButtonDoubleClickOnADotAndOnTheResponseViewOpenTheWindowOnAShape) {
    CardRig rig;
    std::vector<std::unique_ptr<ShapeListEditor>> opened;
    test_hooks::shapeEditorWindowHookForTest() = [&](std::unique_ptr<ShapeListEditor> e) {
        opened.push_back(std::move(e));
    };

    auto& map = rig.map();
    ASSERT_TRUE(map.getDotStop(1)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened.back()->getSelected(), 1);
    EXPECT_EQ(opened.back()->getWindowTitle(), "Filter shapes: " + rig.card->cardTitle());

    // The cursor starts at the middle, nearest the third shape (0.5, 0.8)?  No: nearest is the one by distance.
    rig.custom().getEditButton().onClick();
    ASSERT_EQ(opened.size(), 2u);
    EXPECT_EQ(opened.back()->getSelected(), rig.custom().nearestShapeToCursor());

    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(rig.filter, "morphX")) = 0.8f;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(rig.filter, "morphY")) = 0.25f;
    EXPECT_EQ(rig.custom().nearestShapeToCursor(), 1);
    test_hooks::shapeEditorWindowHookForTest() = nullptr;
}

TEST_F(ModuleComponentTest, ResponseViewDrawsTheCustomBlendAndFollowsTheCursor) {
    FilterModule filter;
    *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&filter, "filterType")) =
        FilterModule::kCustomTypeIndex;
    FrequencyResponseComponent view(filter);
    view.setSize(300, 120);
    view.timerCallback();

    const int i = 500;
    const auto expected = synth::customfilter::responseGridDb(filter.getCustomShapes(), 0.5f, 0.5f, 0.0f, 1.0f);
    EXPECT_NEAR(view.getMagnitudeDbForTest(i), expected[(size_t)i], 1e-3);
    EXPECT_NE(view.getMagnitudeDbForTest(i), 0.0f) << "not the flat 0 dB the view drew before";

    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "morphX")) = 0.2f;
    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "morphY")) = 0.25f;
    view.timerCallback();
    const auto onAh = synth::customfilter::responseGridDb(filter.getCustomShapes(), 0.2f, 0.25f, 0.0f, 1.0f);
    EXPECT_NEAR(view.getMagnitudeDbForTest(i), onAh[(size_t)i], 1e-3);

    *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&filter, "depth")) = 0.0f;
    view.timerCallback();
    EXPECT_NEAR(view.getMagnitudeDbForTest(i), 0.0f, 1e-3);
}

// ---- cables ------------------------------------------------------------------------------------------------

TEST_F(ModuleComponentTest, MorphCablesLandOnTheMapSlidersWhileItShowsAndOnTheKnobsWhileItIsHidden) {
    CardRig rig;
    const auto strips = {std::pair{MorphMap::Axis::X, FilterModule::kMorphXCVChannel},
                         std::pair{MorphMap::Axis::Y, FilterModule::kMorphYCVChannel}};
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
    // Hiding the map hands the landing back to the knobs.
    rig.custom().getToggle().setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.card
                     ->getModTargetPortForPoint(
                         (rig.map().getStripBounds(MorphMap::Axis::X).getCentre() + rig.map().getPosition().toFloat())
                             .roundToInt())
                     .has_value())
        << "the hidden map's sliders take no drop";
    auto* knob = rig.knob("Morph X");
    ASSERT_TRUE(knob != nullptr && knob->isVisible());
    const auto knobPort = rig.card->getModTargetPortForPoint(knob->getBounds().getCentre());
    ASSERT_TRUE(knobPort.has_value());
    EXPECT_EQ(knobPort->index, FilterModule::kMorphXCVChannel);
    const auto anchor = rig.card->getModTargetKnobAnchor(FilterModule::kMorphXCVChannel);
    ASSERT_TRUE(anchor.has_value());
    EXPECT_LT(anchor->getDistanceFrom(knob->getBounds().getCentre().toFloat()), (float)knob->getWidth());
}

TEST_F(ModuleComponentTest, DroppingACableOverASliderLightsIt) {
    CardRig rig;
    rig.card->setModDropTargetChannel(FilterModule::kMorphYCVChannel);
    // The slider stands in for the knob, so the highlight is on it (paint reads it); clearing removes it.
    EXPECT_EQ(rig.card->getModDropTargetChannel(), FilterModule::kMorphYCVChannel);
    rig.card->setModDropTargetChannel(-1);
    EXPECT_EQ(rig.card->getModDropTargetChannel(), -1);
}

// ---- motion ------------------------------------------------------------------------------------------------

TEST_F(ModuleComponentTest, ShowingAndHidingTheMapGoesThroughTheSharedFadePathAndEasesTheHeight) {
    CardRig rig;
    FadeAnimateGuard guard;
    auto& toggle = rig.custom().getToggle();
    const int shown = rig.card->getHeight();

    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_TRUE(rig.map().isVisible()) << "the map stays while it fades out";
    // The returning knobs are laid out at once (the shared conditional-section rule) and fade in; the map gives its
    // height back as it fades.
    const int atStart = rig.card->getHeight();
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(rig.map().getAlpha(), 0.5f, 0.05f);
    const int mid = rig.card->getHeight();
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(rig.map().isVisible());
    EXPECT_NEAR(rig.map().getAlpha(), 1.0f, 0.01f);
    const int hidden = rig.card->getHeight();
    EXPECT_NE(hidden, shown);
    EXPECT_NE(mid, hidden) << "the height eases through the fade, it does not jump to its end";
    EXPECT_NE(mid, atStart) << "and it moves while the map fades";
    EXPECT_TRUE(rig.knobShown("Morph X")) << "the knobs come back as the map leaves";

    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_NEAR(rig.map().getAlpha(), 0.0f, 0.05f) << "frame 0 is the first fade frame";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NE(rig.card->getHeight(), shown);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(rig.card->getHeight(), shown);
    EXPECT_TRUE(rig.map().isVisible());
    EXPECT_FALSE(rig.knobShown("Morph X"));
}

TEST_F(ModuleComponentTest, SwitchingToCustomFadesTheCardBodyPartsThroughTheSharedPathUnderFullMotion) {
    CardRig rig(false);
    FadeAnimateGuard guard;
    rig.setType(FilterModule::kCustomTypeIndex);
    rig.card->getCardBody()->flushPendingConditionUpdate();

    EXPECT_TRUE(rig.card->getCardBody()->isFadeRunning() || rig.custom().getEditButton().isVisible())
        << "the parts come in through the shared fade, not a pop";
    EXPECT_TRUE(rig.custom().getEditButton().isVisible());
    EXPECT_NEAR(rig.custom().getEditButton().getAlpha(), 0.0f, 0.05f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_NEAR(rig.custom().getEditButton().getAlpha(), 1.0f, 0.01f);
}

TEST_F(ModuleComponentTest, ReducedMotionAndOffLandTheMapAtOnce) {
    CardRig rig;
    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    rig.custom().getToggle().setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.map().isVisible()) << "Animations: Off lands on the final state before the call returns";
}
