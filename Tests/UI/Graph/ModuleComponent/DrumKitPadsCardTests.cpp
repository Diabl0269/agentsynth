// DrumKitPadsCardTests.cpp
// The Drum Kit card's pads area (DrumKitPadsSection): the Pads stepper and the card height it sets, a click that plays
// and picks, Shift+click and the keys that pick quietly, the picked pad's controls and their names, no overlap at 8,
// 16 and 32 pads under every theme, and the one-step undo of a count change.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace cardbody_test;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using synth::ui::DrumPadGrid;
using PadParam = DrumKitModule::PadParam;

namespace {

struct Scene {
    CardCanvas canvas;
    NodeID id;

    Scene() {
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    DrumPadGrid& grid() { return pads().grid(); }

    void click(int pad, bool shift) {
        auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                       (shift ? juce::ModifierKeys::shiftModifier : 0));
        grid().mouseDown(makeModuleClickWithMods(grid(), grid().padBounds(pad).getCentre(), mods));
    }
    // The pad the audio thread strikes in the next block, or -1.
    int playedInNextBlock() {
        auto& k = kit();
        k.prepareToPlay(44100.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        const int before = k.lastPlayedPad();
        k.processBlock(buffer, midi);
        return k.lastPlayedPad() != before ? k.lastPlayedPad() : -1;
    }
};

void expectNoOverlap(Scene& scene) {
    auto& pads = scene.pads();
    const std::vector<juce::Component*> mine = {&pads.stepper(), &pads.grid(), &pads.caption(), &pads.controls()};
    const auto cardBounds = scene.card().getLocalBounds();
    for (auto* c : mine) {
        ASSERT_TRUE(c->isVisible());
        EXPECT_TRUE(cardBounds.contains(c->getBounds())) << c->getComponentID();
        for (auto* other : scene.card().getChildren())
            if (other != c && other->isVisible() && !other->getBounds().isEmpty())
                EXPECT_FALSE(c->getBounds().intersects(other->getBounds()))
                    << c->getComponentID() << " overlaps " << other->getTitle() << other->getComponentID();
    }
}

} // namespace

TEST(DrumKitPadsCard, AFreshKitShowsEightPadsInTwoRowsOfFourAndTheKickIsPicked) {
    Scene scene;
    auto& grid = scene.grid();
    EXPECT_EQ(scene.pads().stepper().getValueText(), "8");
    EXPECT_EQ(grid.picked(), 0);
    EXPECT_EQ(grid.getHeight(), 2 * DrumPadGrid::kTallPad + DrumPadGrid::kGap);
    EXPECT_EQ(grid.padBounds(0).getPosition(), juce::Point<int>(0, 0)) << "pad 1 is top left";
    EXPECT_EQ(grid.padBounds(3).getY(), 0);
    EXPECT_EQ(grid.padBounds(4).getX(), 0) << "four a row";
    EXPECT_EQ(grid.padBounds(3).getRight(), grid.getWidth());
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        EXPECT_EQ(grid.padCell(i).isVisible(), i < 8) << i;
}

TEST(DrumKitPadsCard, TheStepperChangesTheRowsAndTheCardHeightInStepsOfFour) {
    Scene scene;
    const int height8 = scene.card().getHeight();
    scene.pads().stepper().onStep(+1);
    EXPECT_EQ(scene.kit().padCount(), 12);
    EXPECT_EQ(scene.card().getHeight(), height8 + DrumPadGrid::kTallPad + DrumPadGrid::kGap);
    scene.pads().stepper().onStep(-1);
    scene.pads().stepper().onStep(-1);
    EXPECT_EQ(scene.kit().padCount(), 4);
    scene.pads().stepper().onStep(-1);
    EXPECT_EQ(scene.kit().padCount(), 4) << "four is the least";

    scene.pads().stepper().onTypedValue("30");
    EXPECT_EQ(scene.kit().padCount(), 32) << "a typed count rounds up to a multiple of four";
    EXPECT_EQ(scene.grid().getHeight(), DrumPadGrid::heightFor(32));
    EXPECT_EQ(scene.grid().padBounds(0).getHeight(), DrumPadGrid::kShortPad) << "short pads above sixteen";
    EXPECT_EQ(scene.pads().stepper().getTitle(), "Pads, 32 of 32 maximum");
    const auto estimate = synth::measureDataDrivenCardSize("Drum Kit");
    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(scene.card().getHeight(), estimate->y + DrumPadGrid::heightFor(32) - DrumPadGrid::heightFor(8));
}

TEST(DrumKitPadsCard, PlusAndMinusKeysOnTheStepperStepByFour) {
    Scene scene;
    auto& stepper = scene.pads().stepper();
    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress('+', juce::ModifierKeys::shiftModifier, '+')));
    EXPECT_EQ(scene.kit().padCount(), 12);
    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress('-', 0, '-')));
    EXPECT_EQ(scene.kit().padCount(), 8);
    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(scene.kit().padCount(), 12);
}

TEST(DrumKitPadsCard, LoweringTheCountHidesPadsAndKeepsTheirSettings) {
    Scene scene;
    scene.pads().setPadCountUndoably(16);
    scene.kit().padParameter(13, PadParam::Level)->setValueNotifyingHost(0.2f);
    scene.pads().setPadCountUndoably(8);
    EXPECT_FALSE(scene.grid().padCell(13).isVisible());
    EXPECT_NEAR(scene.kit().padParameter(13, PadParam::Level)->get(), 0.2f, 1.0e-6f);
    EXPECT_EQ(scene.pads().caption().getText(), "1 hidden pad keeps its sound");
    scene.pads().setPadCountUndoably(16);
    EXPECT_TRUE(scene.grid().padCell(13).isVisible());
}

TEST(DrumKitPadsCard, ACountChangeIsOneUndoStep) {
    Scene scene;
    const int height8 = scene.card().getHeight();
    scene.pads().setPadCountUndoably(16);
    ASSERT_EQ(scene.kit().padCount(), 16);
    ASSERT_TRUE(scene.canvas.undo.undo());
    scene.card().timerCallback();
    EXPECT_EQ(scene.kit().padCount(), 8);
    EXPECT_EQ(scene.card().getHeight(), height8);
    EXPECT_FALSE(scene.grid().padCell(8).isVisible());
    ASSERT_TRUE(scene.canvas.undo.redo());
    scene.card().timerCallback();
    EXPECT_EQ(scene.kit().padCount(), 16);
    EXPECT_TRUE(scene.grid().padCell(15).isVisible());
}

TEST(DrumKitPadsCard, AClickPlaysThePadAndPicksIt) {
    Scene scene;
    scene.click(4, false);
    EXPECT_EQ(scene.grid().picked(), 4);
    EXPECT_EQ(scene.playedInNextBlock(), 4);
    EXPECT_EQ(scene.pads().controls().boundPad(), 4) << "off screen, the controls switch at once";
    EXPECT_EQ(scene.pads().lastAnnouncement(), "Open hat picked");
}

TEST(DrumKitPadsCard, AShiftClickPicksWithoutASoundAndSaysSo) {
    Scene scene;
    scene.click(3, true);
    EXPECT_EQ(scene.grid().picked(), 3);
    EXPECT_EQ(scene.playedInNextBlock(), -1);
    EXPECT_EQ(scene.pads().caption().getText(), "Closed hat picked (no sound)");
}

TEST(DrumKitPadsCard, ArrowsHomeAndEndPickQuietlyAndSpacePlays) {
    Scene scene;
    auto& grid = scene.grid();
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(grid.picked(), 1);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(grid.picked(), 5);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(grid.picked(), 7) << "clamped to the last pad";
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_EQ(grid.picked(), 0);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_EQ(grid.picked(), 7);
    EXPECT_EQ(scene.playedInNextBlock(), -1);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_EQ(scene.playedInNextBlock(), 7);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(scene.playedInNextBlock(), 0);
    EXPECT_FALSE(grid.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')))
        << "app shortcuts pass through";
}

TEST(DrumKitPadsCard, ThePadsAndTheirControlsHaveNamesAndTooltips) {
    Scene scene;
    auto& grid = scene.grid();
    EXPECT_EQ(grid.getTitle(), "Drum pads");
    EXPECT_TRUE(grid.getWantsKeyboardFocus());
    EXPECT_FALSE(grid.getTooltip().isEmpty());
    EXPECT_EQ(grid.padCell(1).getTitle(), "Snare pad, built-in sound, 2 of 8");
    EXPECT_EQ(grid.padTooltip(3),
              "Closed hat: click to play, Shift+click to pick without sound, right-click for its sound");
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        EXPECT_FALSE(grid.padCell(i).getWantsKeyboardFocus()) << "the grid is one Tab stop";
    const auto tabbable = scene.card().createKeyboardFocusTraverser()->getAllComponents(&scene.card());
    const auto at = [&](juce::Component* c) { return std::find(tabbable.begin(), tabbable.end(), c); };
    EXPECT_NE(at(&grid), tabbable.end());
    EXPECT_LT(at(&scene.pads().stepper().getUpButton()), at(&grid)) << "the grid follows the stepper";
    EXPECT_FALSE(scene.pads().stepper().getTooltip().isEmpty());

    scene.click(1, true);
    auto& controls = scene.pads().controls();
    EXPECT_EQ(controls.getTitle(), "Snare controls");
    EXPECT_EQ(controls.noteLabel().getText(), "plays note D1 (38)");
    EXPECT_EQ(controls.knob(PadParam::Level).getTitle(), "Level: Snare");
    EXPECT_EQ(controls.knob(PadParam::Decay).getTooltip(), "Decay: Snare");
    EXPECT_TRUE(controls.knob(PadParam::Tune).getWantsKeyboardFocus());
    EXPECT_FALSE(controls.knob(PadParam::Start).isVisible()) << "Start is for a file pad";
    EXPECT_TRUE(controls.knob(PadParam::Level).keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
        << "Escape goes back to the pad";
}

TEST(DrumKitPadsCard, TheControlsDriveThePickedPadsOwnParameters) {
    Scene scene;
    const float kick = scene.kit().padParameter(0, PadParam::Level)->get();
    scene.click(2, true);
    auto& level = scene.pads().controls().knob(PadParam::Level);
    level.setValue(0.25, juce::sendNotificationSync);
    EXPECT_NEAR(scene.kit().padParameter(2, PadParam::Level)->get(), 0.25f, 1.0e-4f);
    EXPECT_NEAR(scene.kit().padParameter(0, PadParam::Level)->get(), kick, 1.0e-4f) << "the kick is untouched";
    scene.click(0, true);
    EXPECT_NEAR(level.getValue(), kick, 1.0e-4) << "re-bound to the kick";
}

TEST(DrumKitPadsCard, AFilePadShowsStartAndItsMark) {
    sampler_test::TempDir dir;
    const auto wav = dir.file("hit.wav");
    sampler_test::writeWav(wav, 4410, 1, 44100.0, [](int, int) { return 0.5f; });
    Scene scene;
    ASSERT_TRUE(scene.kit().setPadFile(2, wav));
    scene.card().timerCallback();
    scene.click(2, true);
    EXPECT_TRUE(scene.pads().controls().knob(PadParam::Start).isVisible());
    EXPECT_EQ(scene.grid().padCell(2).getTitle(), "hit pad, audio file, 3 of 8");
}

TEST(DrumKitPadsCard, NothingOverlapsAtEightSixteenAndThirtyTwoPads) {
    Scene scene;
    for (int count : {8, 16, 32}) {
        SCOPED_TRACE(count);
        scene.pads().setPadCountUndoably(count);
        expectNoOverlap(scene);
    }
}

// A translucent theme colour comes back from the snapshot through premultiplied alpha, a unit off per channel.
bool closeColour(juce::Colour a, juce::Colour b) {
    return std::abs(a.getAlpha() - b.getAlpha()) <= 2 && std::abs(a.getRed() - b.getRed()) <= 2 &&
           std::abs(a.getGreen() - b.getGreen()) <= 2 && std::abs(a.getBlue() - b.getBlue()) <= 2;
}

TEST(DrumKitPadsCard, PadsPaintInEveryThemesColours) {
    Scene scene;
    auto& grid = scene.grid();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SCOPED_TRACE(theme.name);
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        grid.setLookAndFeel(&lookAndFeel);
        const auto image = grid.createComponentSnapshot(grid.getLocalBounds(), true, 1.0f);
        const auto pad = grid.padBounds(1);
        const auto face = image.getPixelAt(pad.getCentreX(), pad.getCentreY());
        EXPECT_TRUE(closeColour(face, theme.colors.surfaceHi)) << "the pad face is the theme's raised surface";
        const auto ring = image.getPixelAt(1, grid.padBounds(0).getCentreY());
        EXPECT_TRUE(closeColour(ring, theme.colors.accent)) << "the picked pad's ring is the accent";
        grid.setLookAndFeel(nullptr);
    }
}
