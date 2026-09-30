// PianoRoll velocity-strip toolbar tests: the header's exact-value box (what it shows, Return with
// and without a selection, rejected input), Humanize with a seeded source (range, clamping,
// selection-else-all, one undo step), the Velocity chip and the "pianoRollToggleVelocityLane"
// shortcut toggling the strip, and the strip's visibility persisting through a PropertiesFile.

#include "PianoRollVelocityTestHelpers.h"

namespace {
juce::KeyPress returnKey() { return juce::KeyPress(juce::KeyPress::returnKey); }

// Types into the real box and presses Return. juce::TextEditor delivers Return to onReturnKey through
// a posted command message, so the message loop is pumped once for it to arrive.
bool typeIntoBox(PianoRollComponent& roll, const juce::String& text) {
    auto& box = roll.getVelocityValueBox();
    box.setText(text, juce::dontSendNotification);
    const bool consumed = box.keyPressed(returnKey());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    return consumed;
}
} // namespace

// ============================================================================
// Exact-value box
// ============================================================================

TEST(PianoRollVelocityBoxTest, ShowsTheSelectionsCommonVelocityBlankForNoneAndADashWhenMixed) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({90, 90, 40});
    auto& box = f.roll.getVelocityValueBox();
    EXPECT_EQ(box.getText(), "") << "nothing selected";
    f.roll.getSelectionForTest().setSelection({bed.notes[0], bed.notes[1]});
    EXPECT_EQ(box.getText(), "90");
    f.roll.getSelectionForTest().add(bed.notes[2]);
    EXPECT_EQ(box.getText(), juce::String::fromUTF8("\xE2\x80\x94"));
    f.roll.getSelectionForTest().clear();
    EXPECT_EQ(box.getText(), "");
}

TEST(PianoRollVelocityBoxTest, IsLabelledForAccessibility) {
    VelocityLaneFixture f;
    EXPECT_EQ(f.roll.getVelocityValueBox().getTitle(), "Velocity of selected notes");
    EXPECT_TRUE(f.roll.getVelocityValueBox().isVisible());
    EXPECT_TRUE(f.roll.getVelocityValueBox().getBounds().getBottom() <= PianoRollComponent::kToolbarHeight);
}

TEST(PianoRollVelocityBoxTest, ReturnSetsTheSelectedNotesAsOneUndoStep) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60, 80});
    f.roll.getSelectionForTest().setSelection({bed.notes[0], bed.notes[1]});
    EXPECT_TRUE(typeIntoBox(f.roll, "90"));
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 90);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 90);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 80);
    EXPECT_EQ(f.roll.getVelocityValueBox().getText(), "90");
    f.undo.undo();
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 60);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollVelocityBoxTest, ReturnWithNothingSelectedSetsEveryNoteInTheClip) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60, 80});
    typeIntoBox(f.roll, "33");
    for (const auto id : bed.notes)
        EXPECT_EQ(f.velocityOf(id), 33);
    f.undo.undo();
    EXPECT_FALSE(f.undo.canUndo()) << "one step for the whole clip";
}

TEST(PianoRollVelocityBoxTest, InvalidInputIsRejectedAndTheBoxReverts) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60});
    f.roll.getSelectionForTest().setSelection({bed.notes[0]});
    for (const char* bad : {"0", "128", "999", "abc", "", "-5", "1.5"}) {
        f.roll.getVelocityValueBox().setText(bad, juce::dontSendNotification);
        EXPECT_FALSE(f.roll.applyVelocityValueText(bad)) << bad;
        EXPECT_EQ(f.roll.getVelocityValueBox().getText(), "100") << "reverted after '" << bad << "'";
    }
    // And through the real Return key.
    typeIntoBox(f.roll, "200");
    EXPECT_EQ(f.roll.getVelocityValueBox().getText(), "100");
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 100);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 60);
    EXPECT_FALSE(f.undo.canUndo());
}

// ============================================================================
// Humanize
// ============================================================================

TEST(PianoRollHumanizeTest, OffsetsStayInRangeClampAndLandAsOneUndoStep) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({64, 2, 126});
    f.roll.setHumanizeRandomSeed(42);
    f.roll.humanizeVelocities(20);
    EXPECT_GE(f.velocityOf(bed.notes[0]), 44);
    EXPECT_LE(f.velocityOf(bed.notes[0]), 84);
    EXPECT_GE(f.velocityOf(bed.notes[1]), 1) << "clamped at the bottom";
    EXPECT_LE(f.velocityOf(bed.notes[1]), 22);
    EXPECT_GE(f.velocityOf(bed.notes[2]), 106);
    EXPECT_LE(f.velocityOf(bed.notes[2]), 127) << "clamped at the top";
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 64);
    EXPECT_EQ(f.velocityOf(bed.notes[1]), 2);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 126);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(PianoRollHumanizeTest, ASeededSourceGivesTheSameResultEveryTime) {
    std::vector<int> first, second;
    for (auto* out : {&first, &second}) {
        VelocityLaneFixture f;
        const auto bed = f.makeBed({50, 70, 90, 110});
        f.roll.setHumanizeRandomSeed(7);
        f.roll.humanizeVelocities(10);
        for (const auto id : bed.notes)
            out->push_back(f.velocityOf(id));
    }
    EXPECT_EQ(first, second);
}

TEST(PianoRollHumanizeTest, WithASelectionOnlySelectedNotesMove) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({64, 64, 64});
    f.roll.getSelectionForTest().setSelection({bed.notes[1]});
    f.roll.setHumanizeRandomSeed(3);
    for (int i = 0; i < 5; ++i)
        f.roll.humanizeVelocities(20);
    EXPECT_EQ(f.velocityOf(bed.notes[0]), 64);
    EXPECT_EQ(f.velocityOf(bed.notes[2]), 64);
}

// ============================================================================
// Velocity chip, shortcut, persistence
// ============================================================================

TEST(PianoRollVelocityChipTest, ClickingTheChipTogglesTheStrip) {
    VelocityLaneFixture f;
    f.makeBed({100});
    const auto chip = centreOf(f.roll.getVelocityChipBounds());
    f.roll.mouseDown(leftClick(f.roll, chip));
    f.roll.mouseUp(leftClick(f.roll, chip));
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());
    EXPECT_FALSE(f.lane().isVisible());
    f.roll.mouseDown(leftClick(f.roll, chip));
    f.roll.mouseUp(leftClick(f.roll, chip));
    EXPECT_TRUE(f.roll.isVelocityLaneVisible());
    EXPECT_TRUE(f.lane().isVisible());
}

TEST(PianoRollVelocityChipTest, TheNewChipsAndTheBoxNeverOverlapAnyOtherHeaderControl) {
    VelocityLaneFixture f;
    const std::vector<juce::Rectangle<int>> rects{f.roll.getBackButtonBounds(),
                                                  f.roll.getQuantiseButtonBounds(),
                                                  f.roll.getQuantiseLengthButtonBounds(),
                                                  f.roll.getQuantisePitchButtonBounds(),
                                                  f.roll.getScaleButtonBounds(),
                                                  f.roll.getScaleFilterButtonBounds(),
                                                  f.roll.getVelocityChipBounds(),
                                                  f.roll.getHumanizeChipBounds(),
                                                  f.roll.getVelocityValueBox().getBounds()};
    for (size_t i = 0; i < rects.size(); ++i) {
        EXPECT_FALSE(rects[i].isEmpty()) << i;
        for (size_t j = i + 1; j < rects.size(); ++j)
            EXPECT_FALSE(rects[i].intersects(rects[j])) << i << " vs " << j;
    }
}

// The value box belongs to the velocity group: it sits right of Humanize (with only the "Set"
// caption between), not stranded at the header's far end where nobody connects it to the strip.
TEST(PianoRollVelocityChipTest, TheValueBoxSitsRightAfterTheHumanizeChip) {
    VelocityLaneFixture f;
    const auto humanize = f.roll.getHumanizeChipBounds();
    const auto box = f.roll.getVelocityValueBox().getBounds();
    EXPECT_GT(box.getX(), humanize.getRight());
    EXPECT_LE(box.getX() - humanize.getRight(), 2 + f.roll.getVelocityCaptionBounds().getWidth());
    EXPECT_EQ(box.getCentreY(), humanize.getCentreY());
}

TEST(PianoRollVelocityChipTest, HoverLightsTheVelocityChipAndItsTooltipNamesTheShortcut) {
    VelocityLaneFixture f;
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getVelocityChipBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Velocity));
    f.roll.mouseMove(hover(f.roll, centreOf(f.roll.getHumanizeChipBounds())));
    EXPECT_TRUE(f.roll.isHeaderButtonHoveredForTest(PianoRollComponent::HeaderButtonId::Humanize));

    ShortcutManager mgr;
    mgr.setBinding("pianoRollToggleVelocityLane", juce::KeyPress('k', juce::ModifierKeys::altModifier, 0));
    f.roll.setShortcutManager(&mgr);
    const auto tip = f.roll.getTooltipFor(f.roll.getVelocityChipBounds().getCentre());
    EXPECT_TRUE(tip.startsWith("Velocity strip")) << tip;
    EXPECT_TRUE(tip.contains(ShortcutManager::keyPressToDisplayString(mgr.getBinding("pianoRollToggleVelocityLane"))))
        << tip;
    EXPECT_TRUE(f.roll.getTooltipFor(f.roll.getHumanizeChipBounds().getCentre()).startsWith("Humanize"));
}

TEST(PianoRollVelocityShortcutTest, TheRebindableActionTogglesTheStrip) {
    VelocityLaneFixture f;
    ShortcutManager mgr;
    const juce::KeyPress chord('v', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
    mgr.setBinding("pianoRollToggleVelocityLane", chord);
    f.roll.setShortcutManager(&mgr);
    EXPECT_TRUE(f.roll.keyPressed(chord));
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());
    EXPECT_TRUE(f.roll.keyPressed(chord));
    EXPECT_TRUE(f.roll.isVelocityLaneVisible());

    mgr.setBinding("pianoRollToggleVelocityLane", juce::KeyPress());
    EXPECT_FALSE(f.roll.keyPressed(chord)) << "an unbound action has no key";
}

TEST(PianoRollVelocityShortcutTest, TheDefaultBindingIsTheRealCtrlVOnMacAndCmdShiftVElsewhere) {
    ShortcutManager mgr;
    const auto binding = mgr.getBinding("pianoRollToggleVelocityLane");
#if JUCE_MAC
    EXPECT_EQ(binding, juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0));
#else
    EXPECT_EQ(binding, juce::KeyPress('v', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
#endif
    EXPECT_NE(binding, mgr.getBinding("pasteSelection")) << "never Paste wearing a different hat";
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollToggleVelocityLane"), ShortcutCategory::PianoRoll);
    EXPECT_EQ(ShortcutManager::getActionDescription("pianoRollToggleVelocityLane"), "Show or Hide Velocity Strip");

    // With no manager installed the roll falls back to that same default.
    VelocityLaneFixture f;
    EXPECT_TRUE(f.roll.keyPressed(binding));
    EXPECT_FALSE(f.roll.isVelocityLaneVisible());
}

TEST(PianoRollVelocityPersistenceTest, VisibilityRoundTripsThroughThePropertiesFile) {
    auto props = makeScaleAssistTestProps("PianoRollVelocityLanePersistence");
    {
        VelocityLaneFixture f;
        f.roll.setPropertiesFile(props.get());
        EXPECT_TRUE(f.roll.isVelocityLaneVisible()) << "an absent key means shown";
        f.roll.toggleVelocityLane();
        EXPECT_FALSE(props->getBoolValue("pianoRollVelocityLaneVisible", true));
    }
    {
        VelocityLaneFixture f;
        ASSERT_TRUE(f.roll.isVelocityLaneVisible());
        f.roll.setPropertiesFile(props.get());
        EXPECT_FALSE(f.roll.isVelocityLaneVisible()) << "restored hidden";
        EXPECT_EQ(f.roll.canvasBottom(), f.roll.getHeight());
        f.roll.toggleVelocityLane();
    }
    EXPECT_TRUE(props->getBoolValue("pianoRollVelocityLaneVisible", false));
    props->getFile().deleteFile();
}

TEST(PianoRollVelocityPersistenceTest, WithoutAPropertiesFileTheStripIsShownAndSessionOnly) {
    TimelineViewState state;
    PianoRollComponent roll{state};
    roll.setSize(900, 300);
    EXPECT_TRUE(roll.isVelocityLaneVisible());
    roll.setPropertiesFile(nullptr);
    EXPECT_TRUE(roll.isVelocityLaneVisible());
    roll.toggleVelocityLane();
    EXPECT_FALSE(roll.isVelocityLaneVisible());
}
