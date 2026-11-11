// DrumKitPadGlideTests.cpp
// Picking another Drum Kit pad never fades the controls panel: the text changes at once and the four knobs glide from
// the old pad's values to the new pad's, in a window that is really showing. A drag or a second pick mid-glide goes on
// from the value drawn, an undo or redo of a knob change glides, and Reduce Motion and Animations Off keep to their
// rules. Also: the pad's cable ring follows the glide instead of popping.
//
// Kinds: motion-over-time (a pick or swap moves values, it never fades its container).

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../../Layout/NativeWindowTestGuard.h"
#include "../CardBody/CardCanvas.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"

namespace {

// A mouse event for `comp` at `position`, as a real peer would build it.
juce::MouseEvent makeModuleClickWithMods(juce::Component& comp, juce::Point<int> position, juce::ModifierKeys mods) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

} // namespace

using namespace cardbody_test;
using synth::ui::AnimationMode;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using synth::ui::DrumPadControls;
using PadParam = DrumKitModule::PadParam;

namespace {

constexpr int kSnare = 1;
constexpr int kKick = 0;
constexpr float kSnareLevel = 0.80f;
constexpr float kKickLevel = 0.20f;

// A Drum Kit card in a window that is on screen, with distinct Level, Tune and Decay on the Kick and the Snare.
struct GlideScene {
    CardCanvas canvas;
    NodeID id;
    bool onScreen = false;

    GlideScene() {
        id = canvas.add(std::make_unique<DrumKitModule>(), 40, 40);
        canvas.editor.updateComponents();
        canvas.editor.addToDesktop(juce::ComponentPeer::windowIsTemporary);
        canvas.editor.setVisible(true);
        onScreen = pads().controls().isShowing();
        for (const auto [pad, level] : {std::pair{kSnare, kSnareLevel}, std::pair{kKick, kKickLevel}}) {
            set(pad, PadParam::Level, level);
            set(pad, PadParam::Tune, level);
            set(pad, PadParam::Decay, 1.0f - level);
        }
        pads().controls().showPad(kSnare, false);
    }
    ~GlideScene() { canvas.editor.removeFromDesktop(); }

    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    DrumPadControls& panel() { return pads().controls(); }
    void set(int pad, PadParam which, float norm) { kit().padParameter(pad, which)->setValueNotifyingHost(norm); }
    float param(int pad, PadParam which) {
        const juce::AudioProcessorParameter* p = kit().padParameter(pad, which);
        return p->getValue();
    }
    float shown(PadParam which) { return (float)panel().shownProportion(which); }

    void pick(int pad) {
        auto& grid = pads().grid();
        grid.mouseDown(makeModuleClickWithMods(grid, grid.padBounds(pad).getCentre(),
                                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
        card().timerCallback();
    }
    // Drags the Level knob up: one change gesture, the way the mouse does it.
    void dragLevel(int dy) {
        auto& knob = panel().knob(PadParam::Level);
        const auto centre = knob.getLocalBounds().getCentre();
        const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
        knob.mouseDown(makeModuleClickWithMods(knob, centre, plain));
        knob.mouseDrag(makeModuleClickWithMods(knob, centre + juce::Point<int>(0, dy), plain));
        knob.mouseUp(makeModuleClickWithMods(knob, centre + juce::Point<int>(0, dy), plain));
    }
    void step(double ms) { panel().stepMotionForTest(ms); }
    // Everything that must stay still while the values move.
    void expectPanelStill() {
        EXPECT_FLOAT_EQ(panel().getAlpha(), 1.0f) << "the panel never fades";
        EXPECT_TRUE(panel().isVisible());
        for (auto* part :
             {static_cast<juce::Component*>(&panel().nameLabel()), static_cast<juce::Component*>(&panel().noteLabel()),
              static_cast<juce::Component*>(&panel().soundButton())})
            EXPECT_FLOAT_EQ(part->getAlpha(), 1.0f);
    }
};

#define REQUIRE_ON_SCREEN(scene)                                                                                       \
    if (nativeWindowsAbortHere() || !(scene).onScreen)                                                                 \
    GTEST_SKIP() << "this machine would not show the temporary window"

} // namespace

TEST(DrumKitPadGlide, APickKeepsThePanelStillAndGlidesEveryKnobFromTheOldPadsValues) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    scene.pick(kKick);
    ASSERT_EQ(scene.panel().boundPad(), kKick) << "the knobs are on the new pad at once";
    EXPECT_EQ(scene.panel().nameLabel().getText(), "Kick") << "the text changes at once";
    EXPECT_TRUE(scene.panel().soundButton().getTitle().contains("Kick"));
    EXPECT_TRUE(scene.panel().isMoving());
    EXPECT_NEAR(scene.param(kKick, PadParam::Level), kKickLevel, 1e-3f) << "the parameter is final at once";
    for (const auto which : {PadParam::Level, PadParam::Tune})
        EXPECT_NEAR(scene.shown(which), kSnareLevel, 0.01f) << "first frame: still the snare's value";
    EXPECT_NEAR(scene.shown(PadParam::Decay), 1.0f - kSnareLevel, 0.01f);
    scene.expectPanelStill();
    EXPECT_FLOAT_EQ(scene.panel().knob(PadParam::Level).getAlpha(), 1.0f) << "no knob blinks either";

    scene.step(60.0);
    scene.expectPanelStill();
    const float mid = scene.shown(PadParam::Level);
    EXPECT_LT(mid, kSnareLevel - 0.05f);
    EXPECT_GT(mid, kKickLevel + 0.05f);
    EXPECT_NE(scene.panel().knob(PadParam::Level).getTextFromValue(scene.panel().knob(PadParam::Level).getValue()),
              juce::String())
        << "the value box reads the drawn value";

    scene.step(80.0);
    scene.step(160.0);
    EXPECT_FALSE(scene.panel().isMoving());
    for (const auto which : {PadParam::Level, PadParam::Tune})
        EXPECT_NEAR(scene.shown(which), kKickLevel, 0.01f);
    EXPECT_NEAR(scene.shown(PadParam::Decay), 1.0f - kKickLevel, 0.01f);
    scene.expectPanelStill();
}

TEST(DrumKitPadGlide, ThePanelNeverFadesOnAnyFrameOfAPickOrAnArrowKeyStep) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    for (const int pad : {kKick, kSnare, kKick}) {
        scene.pick(pad);
        for (double ms = 0.0; ms <= 180.0; ms += 20.0) {
            scene.step(ms);
            scene.expectPanelStill();
            EXPECT_FLOAT_EQ(scene.panel().knob(PadParam::Level).getAlpha(), 1.0f);
            EXPECT_TRUE(scene.panel().knob(PadParam::Level).isVisible());
        }
    }
    auto& grid = scene.pads().grid();
    grid.grabKeyboardFocus();
    ASSERT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    scene.card().timerCallback();
    EXPECT_NE(scene.panel().boundPad(), kKick) << "the arrow key picked the next pad";
    scene.expectPanelStill();
    EXPECT_TRUE(scene.panel().isMoving() ||
                scene.shown(PadParam::Level) == scene.param(scene.panel().boundPad(), PadParam::Level));
}

TEST(DrumKitPadGlide, ASecondPickMidGlideGoesOnFromTheValueDrawn) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    scene.pick(kKick);
    scene.step(50.0);
    const float drawn = scene.shown(PadParam::Level);
    ASSERT_LT(drawn, kSnareLevel - 0.02f);
    scene.pick(kSnare);
    EXPECT_NEAR(scene.shown(PadParam::Level), drawn, 0.01f) << "the glide starts from where the knob is drawn";
    scene.step(60.0);
    const float towards = scene.shown(PadParam::Level);
    EXPECT_GT(towards, drawn);
    EXPECT_LT(towards, kSnareLevel);
    scene.step(160.0);
    EXPECT_NEAR(scene.shown(PadParam::Level), kSnareLevel, 0.01f);
    EXPECT_FALSE(scene.panel().isMoving());
}

TEST(DrumKitPadGlide, ADragMidGlideTakesTheKnobWhereItIsDrawnAndNothingJumpsBack) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    scene.pick(kKick);
    scene.step(50.0);
    const float drawn = scene.shown(PadParam::Level);
    auto& knob = scene.panel().knob(PadParam::Level);
    ASSERT_TRUE(knob.onDragStart);
    knob.onDragStart();
    scene.step(160.0);
    EXPECT_NEAR(scene.shown(PadParam::Level), drawn, 0.01f) << "the glide let go of the knob without landing it";
    EXPECT_NEAR(scene.shown(PadParam::Tune), kKickLevel, 0.01f) << "the other knobs still arrive";
}

TEST(DrumKitPadGlide, AKnobChangeAndItsUndoAndRedoGlide) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    const float before = scene.param(kSnare, PadParam::Level);
    scene.dragLevel(-30);
    const float after = scene.param(kSnare, PadParam::Level);
    ASSERT_GT(std::abs(after - before), 0.05f);
    EXPECT_FALSE(scene.panel().isMoving()) << "a drag follows the pointer, it does not glide";
    EXPECT_NEAR(scene.shown(PadParam::Level), after, 0.01f);

    ASSERT_TRUE(scene.canvas.undo.undo());
    scene.card().timerCallback();
    EXPECT_NEAR(scene.param(kSnare, PadParam::Level), before, 1e-3f) << "the parameter is final at once";
    EXPECT_NEAR(scene.shown(PadParam::Level), after, 0.01f) << "undo: the first frame is the old drawn value";
    scene.step(60.0);
    const float mid = scene.shown(PadParam::Level);
    EXPECT_GT(std::abs(mid - after), 0.01f);
    EXPECT_GT(std::abs(mid - before), 0.01f);
    scene.step(160.0);
    EXPECT_NEAR(scene.shown(PadParam::Level), before, 0.01f);
    scene.expectPanelStill();

    ASSERT_TRUE(scene.canvas.undo.redo());
    scene.card().timerCallback();
    EXPECT_NEAR(scene.shown(PadParam::Level), before, 0.01f) << "redo: the first frame is where it was";
    scene.step(60.0);
    EXPECT_GT(std::abs(scene.shown(PadParam::Level) - before), 0.01f);
    scene.step(160.0);
    EXPECT_NEAR(scene.shown(PadParam::Level), after, 0.01f);
}

TEST(DrumKitPadGlide, AParameterMovedByAutomationKeepsToTheParameterFrameForFrame) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    scene.set(kSnare, PadParam::Level, 0.35f);
    EXPECT_NEAR(scene.shown(PadParam::Level), 0.35f, 0.01f);
    EXPECT_FALSE(scene.panel().isMoving());
}

TEST(DrumKitPadGlide, UnderReduceMotionTheValuesLandAtOnceAndTheKnobsFadeInPlace) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard(AnimationMode::reduced);
    scene.pick(kKick);
    EXPECT_NEAR(scene.shown(PadParam::Level), kKickLevel, 0.01f) << "no glide: the new value is shown at once";
    scene.expectPanelStill();
    const float alpha = scene.panel().knob(PadParam::Level).getAlpha();
    EXPECT_LT(alpha, 1.0f) << "the knob fades in over 80 ms instead";
    EXPECT_GT(alpha, 0.0f) << "and never blinks out";
    scene.step(40.0);
    const float later = scene.panel().knob(PadParam::Level).getAlpha();
    EXPECT_GT(later, alpha);
    scene.step(80.0);
    EXPECT_FLOAT_EQ(scene.panel().knob(PadParam::Level).getAlpha(), 1.0f);
    EXPECT_FALSE(scene.panel().isMoving());
    scene.expectPanelStill();
}

TEST(DrumKitPadGlide, WithAnimationsOffTheValuesLandAtOnce) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard(AnimationMode::off);
    scene.pick(kKick);
    EXPECT_NEAR(scene.shown(PadParam::Level), kKickLevel, 0.01f);
    EXPECT_FALSE(scene.panel().isMoving());
    EXPECT_FLOAT_EQ(scene.panel().knob(PadParam::Level).getAlpha(), 1.0f);
    scene.expectPanelStill();
}

namespace {

// Pad 1 plays a recording, so it has a fourth column (Start) and the other pads have three.
void makeKickAFilePad(GlideScene& scene, const juce::File& wav) {
    ASSERT_TRUE(scene.pads().setPadFileUndoably(kKick, wav));
    scene.card().timerCallback();
}

} // namespace

TEST(DrumKitPadGlide, StartFadesInPlaceAndTheColumnsSlideWhenThePadsKindMovesThem) {
    sampler_test::TempDir dir;
    const auto wav = dir.file("hit.wav");
    sampler_test::writeWav(wav, 4410, 1, 44100.0, [](int, int) { return 0.3f; });
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    makeKickAFilePad(scene, wav);
    FadeAnimateGuard guard;
    scene.pick(kKick);
    scene.step(160.0);
    auto& start = scene.panel().knob(PadParam::Start);
    ASSERT_TRUE(start.isVisible()) << "a recording has a Start knob";
    const auto fileLevel = scene.panel().knob(PadParam::Level).getBounds();

    scene.pick(kSnare);
    EXPECT_EQ(scene.panel().knob(PadParam::Level).getBounds(), fileLevel) << "first frame: the columns have not moved";
    EXPECT_TRUE(start.isVisible()) << "Start fades out where it stands";
    scene.step(60.0);
    scene.expectPanelStill();
    const auto mid = scene.panel().knob(PadParam::Level).getBounds();
    EXPECT_NE(mid, fileLevel);
    EXPECT_GT(scene.panel().knob(PadParam::Level).getWidth(), fileLevel.getWidth()) << "widening towards three columns";
    scene.panel().completeMotionForTest();
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_GT(scene.panel().knob(PadParam::Level).getWidth(), mid.getWidth() - 1);
    EXPECT_FALSE(start.isVisible());
    EXPECT_FALSE(scene.panel().isMoving());
}

TEST(DrumKitPadGlide, ThePicksRingFadesInOnTheKnobWhileTheLeavingPadsFadesOut) {
    GlideScene scene;
    REQUIRE_ON_SCREEN(scene);
    FadeAnimateGuard guard;
    scene.pick(kKick);
    const auto& panel = std::as_const(scene.panel());
    EXPECT_LT(panel.arrivingRingAlpha(), 0.05f) << "the new pad's ring starts clear";
    const auto leaving = panel.leavingRing(scene.kit().padParameter(kSnare, PadParam::Level)->paramID);
    ASSERT_TRUE(leaving.has_value()) << "the old pad's ring is still on its knob";
    EXPECT_GT(leaving->alpha, 0.95f);
    EXPECT_FALSE(panel.leavingRing(scene.kit().padParameter(kKick, PadParam::Level)->paramID).has_value());
    scene.step(60.0);
    EXPECT_GT(panel.arrivingRingAlpha(), 0.05f);
    EXPECT_LT(panel.arrivingRingAlpha(), 0.99f);
    scene.step(160.0);
    EXPECT_FLOAT_EQ(panel.arrivingRingAlpha(), 1.0f);
    EXPECT_FALSE(panel.leavingRing(scene.kit().padParameter(kSnare, PadParam::Level)->paramID).has_value());
}
