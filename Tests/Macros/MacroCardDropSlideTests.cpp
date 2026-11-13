// A cable dropped on a folded macro's card slides from where it was released to the port that takes it, instead of
// appearing there at once (docs/macros/ports.md#a-cable-dropped-on-a-folded-card). Driven through the real mouse path
// (ModuleComponent::mouseDown -> mouseDrag -> mouseUp, press point held fixed) and stepped through the slide's frames
// with the animator's test seam (no VBlank headless). Full motion slides; Reduce Motion fades the new cable in;
// Animations Off lands at once.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CableRetractAnimator/CableRetractAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace macro_card_drop_slide_tests {

using NodeID = juce::AudioProcessorGraph::NodeID;

NodeID addAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

NodeID nodeForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> local, juce::Point<int> down, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), down.toFloat(), juce::Time::getCurrentTime(), 1,
                            dragged);
}

// Sets the Animations preference for one test and puts the OS default back after it.
struct MotionMode {
    explicit MotionMode(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~MotionMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

// Lets the card's fades run although no test component is on screen, for one test.
struct FadesOffScreen {
    FadesOffScreen() { synth::ui::ShownFade::setAnimateOffScreenForTest(true); }
    ~FadesOffScreen() { synth::ui::ShownFade::setAnimateOffScreenForTest(false); }
};

// A folded two-module macro and an Oscillator outside it whose output is dragged onto the card.
struct SlideCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::String macroId;
    NodeID extOsc;

    SlideCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        const auto a = addAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        const auto b = addAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        extOsc = addAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    }

    MacroCardComponent& card() { return *editor.getMacroController().getMacroCardForTest(macroId); }
    const synth::Macro& macro() { return *editor.getMacros().find(macroId); }

    // Drags the Oscillator's output onto the card at `cardLocal` and lets go there; returns the release point in canvas
    // coordinates, which is where the slide starts.
    juce::Point<float> dropOnCard(juce::Point<int> cardLocal) {
        ModuleComponent* source = nullptr;
        for (auto* c : editor.getModuleComponents())
            if (c->getNodeId() == extOsc)
                source = c;
        const auto press = source->getPortCenter(0, false);
        const auto screen = card().localPointToGlobal(cardLocal);
        const auto canvasPoint = card().getBoundsInParent().getPosition() + cardLocal;
        source->mouseDown(mouseAt(*source, press, press, false));
        source->mouseDrag(mouseAt(*source, source->getLocalPoint(nullptr, screen), press, true));
        source->mouseUp(mouseAt(*source, source->getLocalPoint(nullptr, screen), press, true));
        return canvasPoint.toFloat();
    }
    juce::Point<float> dropOnCardBody() { return dropOnCard(card().getLocalBounds().getCentre().withY(60)); }

    // The cable drawn from the Oscillator into `portUuid`'s node, as the canvas draws it now.
    std::optional<graph_editor_types::VisibleCable> cableInto(const juce::String& portUuid) {
        const auto port = nodeForUuid(engine, portUuid);
        for (const auto& cable : editor.buildVisibleCables())
            if (cable.id.srcUid == extOsc.uid && cable.id.dstUid == port.uid)
                return cable;
        return std::nullopt;
    }
    juce::Point<float> jackOf(const juce::String& portUuid) {
        for (const auto& port : editor.getMacroController().macroCardPortLayout(macroId))
            if (port.nodeUuid == portUuid)
                return (card().getBoundsInParent().getPosition() + port.jackPos).toFloat();
        return {};
    }
};

void expectNear(juce::Point<float> a, juce::Point<float> b, const char* what) {
    EXPECT_NEAR(a.x, b.x, 1.0f) << what;
    EXPECT_NEAR(a.y, b.y, 1.0f) << what;
}

} // namespace macro_card_drop_slide_tests

using namespace macro_card_drop_slide_tests;

TEST(MacroCardDropSlide, ACableMakingANewPortSlidesFromTheDropPointToThePortsJack) {
    MotionMode full(synth::ui::AnimationMode::full);
    SlideCanvas c;
    const auto dropPoint = c.dropOnCardBody();
    ASSERT_EQ(c.macro().ports.size(), 1u);
    const auto port = c.macro().ports[0].nodeUuid;
    ASSERT_TRUE(c.editor.isMacroCrossingAnimLiveForTest()) << "the drop arms the slide instead of popping the cable in";

    c.editor.advanceMacroCrossingAnimForTest(0.0f);
    auto cable = c.cableInto(port);
    ASSERT_TRUE(cable.has_value());
    expectNear(cable->p2, dropPoint, "the first frame draws the port end where the cable was released");

    c.editor.advanceMacroCrossingAnimForTest(0.5f);
    const auto half = c.cableInto(port);
    ASSERT_TRUE(half.has_value());
    EXPECT_GT(half->p2.getDistanceFrom(dropPoint), 1.0f) << "mid-slide the end has left the drop point";
    EXPECT_GT(half->p2.getDistanceFrom(c.jackOf(port)), 1.0f) << "and has not reached the jack yet";

    c.editor.advanceMacroCrossingAnimForTest(1.0f);
    cable = c.cableInto(port);
    ASSERT_TRUE(cable.has_value());
    expectNear(cable->p2, c.jackOf(port), "the last frame lands on the new port's jack");
    c.editor.finishMacroCrossingAnimForTest();
    EXPECT_FALSE(c.editor.isMacroCrossingAnimLiveForTest());
}

TEST(MacroCardDropSlide, ACableDroppedOnAnExistingPortsJackSlidesToIt) {
    MotionMode full(synth::ui::AnimationMode::full);
    SlideCanvas c;
    const auto port = c.editor.getMacroController().addMacroPort(c.macroId, true, synth::MacroPortKind::AudioCV,
                                                                 MacroPortShape::Mono, 1, "In");
    // Just off the jack's centre, still within its hit radius, so the slide has a distance to cover.
    juce::Point<int> jackLocal;
    for (const auto& p : c.editor.getMacroController().macroCardPortLayout(c.macroId))
        if (p.nodeUuid == port)
            jackLocal = p.jackPos;
    const auto dropPoint = c.dropOnCard(jackLocal + juce::Point<int>(6, 3));
    ASSERT_EQ(c.macro().ports.size(), 1u);
    ASSERT_TRUE(c.editor.isMacroCrossingAnimLiveForTest());

    c.editor.advanceMacroCrossingAnimForTest(0.0f);
    expectNear(c.cableInto(port).value().p2, dropPoint, "starts at the release point");
    c.editor.advanceMacroCrossingAnimForTest(1.0f);
    expectNear(c.cableInto(port).value().p2, c.jackOf(port), "lands on the port's jack");
}

TEST(MacroCardDropSlide, UnderReduceMotionTheNewCableFadesInAndNothingSlides) {
    MotionMode reduced(synth::ui::AnimationMode::reduced);
    SlideCanvas c;
    c.dropOnCardBody();
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FALSE(c.editor.isMacroCrossingAnimLiveForTest()) << "Reduce Motion moves nothing";
    EXPECT_EQ(c.editor.getCableRetractForTest().numGrowing(), 1u) << "the new cable fades in";
    EXPECT_EQ(c.editor.getCableRetractForTest().durationMs(), CableRetractAnimator::kReducedMs);
}

TEST(MacroCardDropSlide, UnderAnimationsOffTheCableIsAtItsPortAtOnce) {
    MotionMode off(synth::ui::AnimationMode::off);
    SlideCanvas c;
    c.dropOnCardBody();
    ASSERT_EQ(c.macro().ports.size(), 1u);
    const auto port = c.macro().ports[0].nodeUuid;
    EXPECT_FALSE(c.editor.isMacroCrossingAnimLiveForTest());
    EXPECT_EQ(c.editor.getCableRetractForTest().numGrowing(), 0u);
    expectNear(c.cableInto(port).value().p2, c.jackOf(port), "already on the jack");
}

TEST(MacroCardDropSlide, UndoTakesTheCableBackIntoItsSourceAndRedoGrowsItOut) {
    MotionMode full(synth::ui::AnimationMode::full);
    SlideCanvas c;
    c.dropOnCardBody();
    ASSERT_EQ(c.macro().ports.size(), 1u);
    c.editor.finishMacroCrossingAnimForTest();

    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(c.macro().ports.empty());
    EXPECT_TRUE(c.editor.getCableRetractForTest().isLive()) << "undo retracts the cable instead of dropping it";
    c.editor.finishCableRetractForTest();

    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(c.macro().ports.size(), 1u);
    EXPECT_EQ(c.editor.getCableRetractForTest().numGrowing(), 1u) << "redo grows the cable back out of its source";
}

TEST(MacroCardDropSlide, TheNewPortsRowGrowsInWithTheSlideInsteadOfPopping) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    c.dropOnCardBody();
    ASSERT_EQ(c.macro().ports.size(), 1u);
    const auto port = c.macro().ports[0].nodeUuid;
    EXPECT_LT(c.card().getPortShown(port), 0.05f) << "the new row starts hidden, not fully drawn";
    c.card().stepPortEnterForTest(85.0);
    const float mid = c.card().getPortShown(port);
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 1.0f) << "half way it is part shown";
    c.card().stepPortEnterForTest(170.0);
    EXPECT_FLOAT_EQ(c.card().getPortShown(port), 1.0f) << "and it ends fully shown";
}

TEST(MacroCardDropSlide, UnderReduceMotionTheNewRowFadesInOverTheShortFade) {
    MotionMode reduced(synth::ui::AnimationMode::reduced);
    FadesOffScreen fades;
    SlideCanvas c;
    c.dropOnCardBody();
    const auto port = c.macro().ports[0].nodeUuid;
    EXPECT_LT(c.card().getPortShown(port), 1.0f);
    c.card().stepPortEnterForTest(synth::ui::ShownFade::kReducedMs);
    EXPECT_FLOAT_EQ(c.card().getPortShown(port), 1.0f);
}

TEST(MacroCardDropSlide, UnderAnimationsOffTheNewRowIsThereAtOnce) {
    MotionMode off(synth::ui::AnimationMode::off);
    FadesOffScreen fades;
    SlideCanvas c;
    c.dropOnCardBody();
    EXPECT_FLOAT_EQ(c.card().getPortShown(c.macro().ports[0].nodeUuid), 1.0f);
}

// ---- The row a cable drop made fades out on undo and grows back on redo ----

namespace {
// Drops the Oscillator on the folded card (a new port), lets the slide and the row's grow-in land, and returns the
// new port's uuid.
juce::String dropAndLand(SlideCanvas& c) {
    c.dropOnCardBody();
    const auto port = c.macro().ports[0].nodeUuid;
    c.editor.finishMacroCrossingAnimForTest();
    c.card().stepPortEnterForTest(170.0);
    return port;
}
} // namespace

TEST(MacroCardPortExit, UndoKeepsTheRemovedRowDrawnAndFadesItOutThenItIsGone) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    dropAndLand(c);
    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.macro().ports.empty());
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 1) << "the live macro has no row: a ghost holds its place";
    EXPECT_GT(c.card().getLeavingPortShownForTest(), 0.95f) << "it starts whole, not already gone";
    c.card().stepPortExitForTest(85.0);
    const float mid = c.card().getLeavingPortShownForTest();
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 0.95f) << "half way it is part faded";
    c.card().stepPortExitForTest(170.0);
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0) << "and nothing is kept once it has faded";
}

TEST(MacroCardPortExit, RedoGrowsTheRowBackInAndDropsAnyGhostOfIt) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    dropAndLand(c);
    ASSERT_TRUE(c.undo.undo());
    c.card().stepPortExitForTest(50.0);
    ASSERT_EQ(c.card().getLeavingPortRowCountForTest(), 1);
    ASSERT_TRUE(c.undo.redo()); // a new edit mid-fade
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0) << "the ghost of a port that is back is dropped";
    const auto port = c.macro().ports[0].nodeUuid;
    EXPECT_LT(c.card().getPortShown(port), 0.05f) << "redo grows the row in from nothing";
    c.card().stepPortEnterForTest(85.0);
    EXPECT_GT(c.card().getPortShown(port), 0.05f);
    EXPECT_LT(c.card().getPortShown(port), 1.0f);
    c.card().stepPortEnterForTest(170.0);
    EXPECT_FLOAT_EQ(c.card().getPortShown(port), 1.0f);
}

TEST(MacroCardPortExit, TheMinusButtonsRemovalFadesTheSameWay) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    dropAndLand(c);
    c.editor.getMacroController().deleteBottomMacroPort(c.macroId, true);
    ASSERT_TRUE(c.macro().ports.empty());
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 1);
    c.card().stepPortExitForTest(170.0);
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0);
}

TEST(MacroCardPortExit, ARowWithRowsBelowItIsNotKeptSoNothingDrawsThroughIt) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    auto& controller = c.editor.getMacroController();
    const auto first =
        controller.addMacroPort(c.macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "A");
    controller.addMacroPort(c.macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "B");
    c.card().stepPortEnterForTest(170.0);
    controller.deleteMacroPortManually(c.macroId, first);
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0) << "B moves up into A's row: A pops instead of overlapping";
}

TEST(MacroCardPortExit, ExpandingTheMacroMidFadeDropsTheGhostAndAGhostOutlivesNoCard) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    {
        SlideCanvas c;
        dropAndLand(c);
        ASSERT_TRUE(c.undo.undo());
        ASSERT_EQ(c.card().getLeavingPortRowCountForTest(), 1);
        c.editor.getMacroController().setMacroCollapsed(c.macroId, false);
        EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0) << "an expanded macro shows no card, so no ghost";
    }
    {
        SlideCanvas c; // the card is destroyed with the editor while a ghost fades: nothing runs after it
        dropAndLand(c);
        ASSERT_TRUE(c.undo.undo());
        ASSERT_EQ(c.card().getLeavingPortRowCountForTest(), 1);
    }
    SUCCEED();
}

TEST(MacroCardPortExit, UnderReduceMotionTheRowFadesOverTheShortFadeAndUnderOffItIsGoneAtOnce) {
    {
        MotionMode reduced(synth::ui::AnimationMode::reduced);
        FadesOffScreen fades;
        SlideCanvas c;
        dropAndLand(c);
        ASSERT_TRUE(c.undo.undo());
        EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 1);
        c.card().stepPortExitForTest(synth::ui::ShownFade::kReducedMs);
        EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0);
    }
    MotionMode off(synth::ui::AnimationMode::off);
    FadesOffScreen fades;
    SlideCanvas c;
    dropAndLand(c);
    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0);
}

TEST(MacroCardPortExit, APortThatArrivesWhileTheCardIsHiddenGrowsAndFadesNothing) {
    MotionMode full(synth::ui::AnimationMode::full);
    FadesOffScreen fades;
    SlideCanvas c;
    auto& controller = c.editor.getMacroController();
    controller.setMacroCollapsed(c.macroId, false);
    controller.addMacroPort(c.macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    controller.setMacroCollapsed(c.macroId, true); // the card comes back shown, row and all
    ASSERT_EQ(c.macro().ports.size(), 1u);
    EXPECT_FLOAT_EQ(c.card().getPortShown(c.macro().ports[0].nodeUuid), 1.0f);
    EXPECT_EQ(c.card().getLeavingPortRowCountForTest(), 0);
}
