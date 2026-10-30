// DrumKitPadShownTests.cpp
// The Drum Kit card's Shown switch: a hidden pad leaves the grid and the pads after it move up, the caption counts
// them and offers Show all, H flips the picked pad, every change is one undo step, a hidden pad still plays, hiding a
// drum that has hits asks first, and the pads slide.

#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Layout/FadeVisibility.h"
#include <optional>

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
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    ~Scene() { synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    DrumPadGrid& grid() { return pads().grid(); }

    void pickQuietly(int pad) {
        grid().mouseDown(makeModuleClickWithMods(
            grid(), grid().padBounds(pad).getCentre(),
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
    }
    // A click: the toggle flips, then its handler runs (Button::triggerClick is asynchronous).
    void flipSwitch() {
        auto& sw = pads().controls().shownSwitch();
        sw.setToggleState(!sw.getToggleState(), juce::dontSendNotification);
        sw.onClick();
    }
    bool undo() {
        const bool ok = canvas.undo.undo();
        card().timerCallback();
        return ok;
    }
    // The pad the audio thread strikes for `note`, or -1.
    int playedForNote(int note) {
        auto& k = kit();
        k.prepareToPlay(44100.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, note, 1.0f), 0);
        k.processBlock(buffer, midi);
        return k.lastPlayedPad();
    }
};

} // namespace

TEST(DrumKitPadShown, TheSwitchTakesAPadOffTheGridAndThePadsAfterItMoveUp) {
    Scene scene;
    auto& grid = scene.grid();
    const auto seventhSpot = grid.padBounds(6);
    scene.pickQuietly(6);
    auto& shown = scene.pads().controls().shownSwitch();
    EXPECT_TRUE(shown.getToggleState());
    EXPECT_EQ(shown.getTitle(), "Shown: Cymbal");
    EXPECT_FALSE(shown.getTooltip().isEmpty());

    scene.flipSwitch();
    EXPECT_FALSE(scene.kit().padShown(6));
    EXPECT_EQ(grid.visibleCount(), 7);
    EXPECT_FALSE(grid.padCell(6).isVisible());
    EXPECT_TRUE(grid.padBounds(6).isEmpty());
    EXPECT_EQ(grid.padBounds(7), seventhSpot) << "the cowbell moved up into the cymbal's place";
    EXPECT_EQ(grid.padTitle(7), "Cowbell pad, built-in sound, 7 of 7");
    EXPECT_EQ(grid.padAt(seventhSpot.getCentre()), 7);
    EXPECT_FALSE(shown.getToggleState());
    EXPECT_EQ(scene.pads().caption().getText(), juce::String::fromUTF8("1 hidden \xC2\xB7"));
    EXPECT_TRUE(scene.pads().showAllLink().isVisible());
    EXPECT_EQ(scene.pads().controls().boundPad(), 6) << "the hidden pad stays picked, so it can be shown again";
}

TEST(DrumKitPadShown, AHiddenPadStillPlays) {
    Scene scene;
    scene.pickQuietly(6);
    scene.flipSwitch();
    ASSERT_FALSE(scene.kit().padShown(6));
    EXPECT_EQ(scene.playedForNote(49), 6) << "its note still strikes it";
}

TEST(DrumKitPadShown, HFlipsThePickedPadAndTheSwitchTurnsItBackOn) {
    Scene scene;
    auto& grid = scene.grid();
    scene.pickQuietly(2);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress('h', juce::ModifierKeys(), 'h')));
    EXPECT_FALSE(scene.kit().padShown(2));
    EXPECT_EQ(grid.picked(), 2);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress('H', juce::ModifierKeys::shiftModifier, 'H')));
    EXPECT_TRUE(scene.kit().padShown(2)) << "H again shows it";
    EXPECT_FALSE(grid.keyPressed(juce::KeyPress('h', juce::ModifierKeys::commandModifier, 'h')))
        << "Cmd+H passes on to the app";

    scene.flipSwitch();
    EXPECT_FALSE(scene.kit().padShown(2));
    scene.flipSwitch();
    EXPECT_TRUE(scene.kit().padShown(2));
    EXPECT_EQ(grid.visibleCount(), 8);
}

TEST(DrumKitPadShown, ArrowsSkipHiddenPadsAndStartFromAHiddenPick) {
    Scene scene;
    auto& grid = scene.grid();
    scene.kit().setPadShown(2, false);
    scene.kit().setPadShown(3, false);
    scene.pads().poll();
    scene.pickQuietly(1);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(grid.picked(), 4) << "the pads between are hidden";
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(grid.picked(), 1);
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_EQ(grid.picked(), 7);
    scene.kit().setPadShown(7, false);
    scene.pads().poll();
    EXPECT_EQ(grid.picked(), 7) << "a hidden pick stays picked";
    EXPECT_TRUE(grid.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(grid.picked(), 6);
}

TEST(DrumKitPadShown, ShowAllIsOneUndoStepAndAnUndoHidesThemAgain) {
    Scene scene;
    scene.pickQuietly(1);
    scene.flipSwitch();
    scene.pickQuietly(4);
    scene.flipSwitch();
    ASSERT_EQ(scene.kit().hiddenPadCount(), 2);
    EXPECT_EQ(scene.pads().caption().getText(), juce::String::fromUTF8("2 hidden \xC2\xB7"));

    scene.pads().showAllLink().onClick();
    EXPECT_EQ(scene.kit().hiddenPadCount(), 0);
    EXPECT_EQ(scene.grid().visibleCount(), 8);
    EXPECT_FALSE(scene.pads().showAllLink().isVisible());
    ASSERT_TRUE(scene.undo());
    EXPECT_EQ(scene.kit().hiddenPadCount(), 2) << "one step brought both back, one undo hides both";
    EXPECT_EQ(scene.grid().visibleCount(), 6);
    EXPECT_TRUE(scene.pads().showAllLink().isVisible());
}

TEST(DrumKitPadShown, EachSwitchIsOneUndoStepAndRedoFollows) {
    Scene scene;
    scene.pickQuietly(5);
    scene.flipSwitch();
    ASSERT_FALSE(scene.kit().padShown(5));
    ASSERT_TRUE(scene.undo());
    EXPECT_TRUE(scene.kit().padShown(5));
    EXPECT_EQ(scene.grid().visibleCount(), 8);
    EXPECT_TRUE(scene.grid().padCell(5).isVisible());
    ASSERT_TRUE(scene.canvas.undo.redo());
    scene.card().timerCallback();
    EXPECT_FALSE(scene.kit().padShown(5));
    EXPECT_EQ(scene.grid().visibleCount(), 7);
}

TEST(DrumKitPadShown, TheCardShrinksWithTheRowsAndGrowsBack) {
    Scene scene;
    const int height = scene.card().getHeight();
    for (int pad = 4; pad < 8; ++pad) {
        scene.pickQuietly(pad);
        scene.flipSwitch();
    }
    EXPECT_EQ(scene.grid().visibleCount(), 4);
    EXPECT_EQ(scene.grid().getHeight(), DrumPadGrid::heightFor(4));
    EXPECT_LT(scene.card().getHeight(), height) << "one row of pads, not two";
    scene.pads().showAllLink().onClick();
    EXPECT_EQ(scene.card().getHeight(), height);
}

TEST(DrumKitPadShown, HidingADrumWithHitsAsksFirst) {
    Scene scene;
    std::vector<std::pair<int, juce::String>> asked;
    std::function<void()> held;
    scene.canvas.editor.drumKit.confirmHidePad = [&](NodeID, int note, const juce::String& name,
                                                     std::function<void()> proceed) {
        asked.emplace_back(note, name);
        held = std::move(proceed);
    };
    scene.pickQuietly(7);
    scene.flipSwitch();
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0].first, 56);
    EXPECT_EQ(asked[0].second, "Cowbell");
    EXPECT_TRUE(scene.kit().padShown(7)) << "nothing changes until the person says yes";

    held = nullptr; // a No
    scene.pads().poll();
    EXPECT_TRUE(scene.kit().padShown(7));
    EXPECT_TRUE(scene.pads().controls().shownSwitch().getToggleState() == false ||
                scene.kit().padShown(7)); // the switch is back in step with the pad at the next refresh

    scene.pads().setPadShown(7, false);
    ASSERT_TRUE(static_cast<bool>(held));
    held(); // a Yes
    EXPECT_FALSE(scene.kit().padShown(7));
    EXPECT_EQ(scene.grid().visibleCount(), 7);

    asked.clear();
    scene.pads().setPadShown(7, true);
    EXPECT_TRUE(asked.empty()) << "showing never asks";
}

TEST(DrumKitPadShown, AnAnswerAfterTheCardIsGoneChangesNothing) {
    std::function<void()> held;
    {
        Scene scene;
        scene.canvas.editor.drumKit.confirmHidePad = [&](NodeID, int, const juce::String&,
                                                         std::function<void()> proceed) { held = std::move(proceed); };
        scene.pads().setPadShown(7, false);
        ASSERT_TRUE(static_cast<bool>(held));
    }
    EXPECT_NO_THROW(held()) << "the card and its kit are gone";
}

TEST(DrumKitPadShown, TheQuestionNamesTheDrumAndItsHits) {
    EXPECT_EQ(synth::ui::hideDrumConfirmText("Cowbell", 1).title, "Cowbell has 1 hit; hide it anyway?");
    EXPECT_EQ(synth::ui::hideDrumConfirmText("Kick", 12).title, "Kick has 12 hits; hide it anyway?");
}

TEST(DrumKitPadShown, TheSwitchAndTheLinkHaveKeyboardPathsAndNames) {
    Scene scene;
    auto& shown = scene.pads().controls().shownSwitch();
    EXPECT_TRUE(shown.getWantsKeyboardFocus());
    EXPECT_FALSE(shown.getTitle().isEmpty());
    scene.pickQuietly(1);
    scene.flipSwitch();
    auto& link = scene.pads().showAllLink();
    EXPECT_TRUE(link.getWantsKeyboardFocus());
    EXPECT_EQ(link.getTitle(), "Show all hidden pads");
    EXPECT_FALSE(link.getTooltip().isEmpty());
    const auto tabbable = scene.card().createKeyboardFocusTraverser()->getAllComponents(&scene.card());
    EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), &link), tabbable.end());
    EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), &shown), tabbable.end());
}

TEST(DrumKitPadShown, ThePadsSlideToMakeRoomWhenAnimationsRun) {
    Scene scene;
    auto& grid = scene.grid();
    scene.pickQuietly(5); // before animations run, so the panel is on pad 6 at once
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    const auto cowbellBefore = grid.padCell(7).getBounds();
    scene.flipSwitch();
    ASSERT_TRUE(grid.isSliding());
    EXPECT_TRUE(grid.padCell(5).isVisible()) << "the pad is still on its way out";
    grid.slideDriverForTest().stepForTest(55.0);
    EXPECT_LT(grid.padCell(5).getWidth(), grid.padBounds(4).getWidth()) << "it shrinks";
    EXPECT_NE(grid.padCell(7).getBounds(), cowbellBefore);
    EXPECT_NE(grid.padCell(7).getBounds(), grid.padBounds(7)) << "the cowbell is mid-way";
    grid.slideDriverForTest().completeForTest();
    EXPECT_FALSE(grid.isSliding());
    EXPECT_FALSE(grid.padCell(5).isVisible());
    EXPECT_EQ(grid.padCell(7).getBounds(), grid.padBounds(7));
}

TEST(DrumKitPadShown, WithAnimationsOffThePadsMoveAtOnce) {
    Scene scene;
    scene.pickQuietly(5);
    scene.flipSwitch();
    EXPECT_FALSE(scene.grid().isSliding());
    EXPECT_EQ(scene.grid().padCell(7).getBounds(), scene.grid().padBounds(7));
}

// Regression test for FRO1304: the Shown switch took the pad the panel was still fading from, so a click within the
// 80 ms cross-fade after a pick hid the previous pad.
TEST(DrumKitPadShown, TheSwitchActsOnTheNewPickBeforeThePanelFinishesFading) {
    Scene scene;
    scene.pickQuietly(0);
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    scene.pickQuietly(3); // the panel starts fading from pad 1 to pad 4
    ASSERT_NE(scene.pads().controls().boundPad(), 3) << "the fade has not re-bound the knobs yet";
    EXPECT_EQ(scene.pads().controls().shownSwitch().getTitle(), "Shown: Closed hat")
        << "the switch already speaks for it";
    scene.flipSwitch();
    EXPECT_FALSE(scene.kit().padShown(3)) << "the new pick changed";
    EXPECT_TRUE(scene.kit().padShown(0)) << "the previous pick did not";
}

namespace {

// Opens the pad menu of `pad` through the same path as a right-click and returns it.
juce::PopupMenu openPadMenu(Scene& scene, int pad, juce::PopupMenu& out) {
    scene.pads().showMenuForTest = [&out](juce::PopupMenu& menu, int, bool) { out = menu; };
    scene.pads().openMenu(pad, true, false);
    return out;
}

std::optional<juce::PopupMenu::Item> itemNamed(const juce::PopupMenu& menu, const juce::String& text) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().text == text)
            return it.getItem();
    return std::nullopt;
}

} // namespace

TEST(DrumKitPadShown, ThePadMenuHidesAPadAndShowsThemAll) {
    Scene scene;
    juce::PopupMenu menu;
    openPadMenu(scene, 3, menu);
    const auto hide = itemNamed(menu, "Hide pad");
    ASSERT_TRUE(hide);
    EXPECT_EQ(hide->shortcutKeyDescription, "H");
    auto all = itemNamed(menu, "Show all pads");
    ASSERT_TRUE(all);
    EXPECT_FALSE(all->isEnabled) << "nothing is hidden yet";

    scene.pads().applyMenuResult(3, hide->itemID);
    EXPECT_FALSE(scene.kit().padShown(3));
    EXPECT_TRUE(scene.undo());
    EXPECT_TRUE(scene.kit().padShown(3));
    scene.pads().applyMenuResult(3, hide->itemID);

    openPadMenu(scene, 3, menu);
    EXPECT_FALSE(itemNamed(menu, "Hide pad"));
    EXPECT_TRUE(itemNamed(menu, "Show pad"));
    all = itemNamed(menu, "Show all pads");
    ASSERT_TRUE(all);
    EXPECT_TRUE(all->isEnabled);
    scene.pads().applyMenuResult(3, all->itemID);
    EXPECT_EQ(scene.kit().hiddenPadCount(), 0);
    EXPECT_TRUE(scene.undo());
    EXPECT_FALSE(scene.kit().padShown(3));
}
