// Concern: category-scoped conflict detection, the bare tool digits vs the Ctrl+Shift grid,
// locator jumps, and the grid command family.
#include "ShortcutManagerTestFixture.h"

TEST_F(ShortcutManagerTest, SameKeyInTwoCategoriesIsNotAConflict) {
    const juce::KeyPress bareP('p', juce::ModifierKeys::noModifiers, 0);
    ASSERT_EQ(manager.getBinding("timelineLoopSelection"), bareP) << "precondition: P is the timeline's default";

    // A piano-roll action asking for the timeline's bare P: different category, no conflict.
    EXPECT_TRUE(manager.getConflictingAction("pianoRollQuantise", bareP).isEmpty());
    // A Graph action asking for it: also clear.
    EXPECT_TRUE(manager.getConflictingAction("autoArrange", bareP).isEmpty());
    // Another TIMELINE action asking for it is still a conflict.
    EXPECT_EQ(manager.getConflictingAction("timelineToggleLoop", bareP), "timelineLoopSelection");
}

TEST_F(ShortcutManagerTest, SameKeyWithinACategoryIsStillAConflictAndStillAutoSwaps) {
    const juce::KeyPress cmdX('x', juce::ModifierKeys::commandModifier, 0);
    ASSERT_EQ(manager.getBinding("cutSelection"), cmdX);

    // Both General: reported, and the Settings tab's swap then applies.
    EXPECT_EQ(manager.getConflictingAction("copySelection", cmdX), "cutSelection");

    // The swap itself, exactly as ShortcutsSettingsTab::keyPressed performs it.
    const auto oldCopyBinding = manager.getBinding("copySelection");
    manager.setBinding("cutSelection", oldCopyBinding);
    manager.setBinding("copySelection", cmdX);
    EXPECT_EQ(manager.getBinding("copySelection").getKeyCode(), 'x');
    EXPECT_EQ(manager.getBinding("cutSelection").getKeyCode(), 'c');
    // And the table is collision-free again inside General.
    EXPECT_TRUE(manager.getConflictingAction("copySelection", manager.getBinding("copySelection")).isEmpty());
    EXPECT_TRUE(manager.getConflictingAction("cutSelection", manager.getBinding("cutSelection")).isEmpty());
}

// Modifier equality is EXACT, which is what keeps the bare tool digits clear of the Ctrl+Alt grid
// commands that share their key codes — the pair most likely to be "fixed" into a collision later.
// Three Timeline families now share the digit row (bare = tools, Ctrl+Alt = set the grid,
// Ctrl+Shift+1/2 = jump to a locator) and only the modifier set separates them.
TEST_F(ShortcutManagerTest, BareToolDigitsDoNotCollideWithTheGridOrLocatorCommands) {
    struct Pair {
        const char* toolId;
        const char* gridId;
        int digit;
    };
    for (const auto& pair : std::vector<Pair>{{"timelineToolSelect", "snapSetWhole", '1'},
                                              {"timelineToolSplit", "snapSetQuarter", '3'},
                                              {"timelineToolGlue", "snapSetEighth", '4'},
                                              {"timelineToolErase", "snapSetSixteenth", '5'},
                                              // The two new pairs the 1/64 and 1/128 commands create.
                                              {"timelineToolMute", "snapSetSixtyFourth", '7'},
                                              {"timelineToolDraw", "snapSetHundredTwentyEighth", '8'}}) {
        const auto toolBinding = manager.getBinding(pair.toolId);
        const auto gridBinding = manager.getBinding(pair.gridId);
        EXPECT_EQ(toolBinding.getKeyCode(), pair.digit);
        EXPECT_EQ(gridBinding.getKeyCode(), pair.digit) << "same digit, different modifiers, by design";
        EXPECT_TRUE(toolBinding.getModifiers().getRawFlags() == 0);
        EXPECT_TRUE(gridBinding.getModifiers().isCtrlDown());
        EXPECT_TRUE(gridBinding.getModifiers().isShiftDown());
        EXPECT_FALSE(gridBinding.getModifiers().isAltDown()) << "Alt+digit belongs to the locator jumps";
        EXPECT_FALSE(toolBinding == gridBinding);
        // Same category, and still no conflict, because the modifiers differ.
        EXPECT_TRUE(manager.getConflictingAction(pair.toolId, toolBinding).isEmpty());
    }

    // The third family on the same key code: all three are distinct stored chords on '1'.
    const auto tool1 = manager.getBinding("timelineToolSelect");
    const auto grid1 = manager.getBinding("snapSetWhole");
    const auto locator1 = manager.getBinding("timelineJumpToLocator1");
    EXPECT_EQ(tool1.getKeyCode(), '1');
    EXPECT_EQ(grid1.getKeyCode(), '1');
    EXPECT_EQ(locator1.getKeyCode(), '1');
    EXPECT_FALSE(tool1 == grid1);
    EXPECT_FALSE(grid1 == locator1);
    EXPECT_FALSE(tool1 == locator1);
    EXPECT_TRUE(manager.getConflictingAction("timelineJumpToLocator1", locator1).isEmpty());
}

// ---------------------------------------------------------------------------
// Locator jumps — the chord the grid-set family used to own
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, LocatorJumpsOwnOptionOneAndTwo) {
    EXPECT_EQ(manager.getBinding("timelineJumpToLocator1"), juce::KeyPress('1', juce::ModifierKeys::altModifier, 0));
    EXPECT_EQ(manager.getBinding("timelineJumpToLocator2"), juce::KeyPress('2', juce::ModifierKeys::altModifier, 0));

    // Timeline category, and SURFACE actions — the panel's keyPressed resolves them, so there must
    // be no command behind either one (MainComponent would otherwise try to dispatch one).
    EXPECT_EQ(ShortcutManager::getCategory("timelineJumpToLocator1"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("timelineJumpToLocator2"), ShortcutCategory::Timeline);
    EXPECT_EQ(AppCommands::getCommandForAction("timelineJumpToLocator1"), AppCommands::kNoCommand);
    EXPECT_EQ(AppCommands::getCommandForAction("timelineJumpToLocator2"), AppCommands::kNoCommand);

    EXPECT_EQ(ShortcutManager::getActionDescription("timelineJumpToLocator1"), "Jump to Locator 1");
    EXPECT_EQ(ShortcutManager::getActionDescription("timelineJumpToLocator2"), "Jump to Locator 2");

    // Exactly one action per chord, and NOT the grid: the grid-set family kept Ctrl+Shift+digit, so
    // these two had to land somewhere no shipped version ever bound. That is the whole point —
    // moving a DEFAULT does not migrate a user's PERSISTED binding, so a locator jump sharing a
    // chord with a grid COMMAND loses to it in MainComponent::keyPressed forever after.
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('1', juce::ModifierKeys::altModifier, 0)),
              "timelineJumpToLocator1");
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('2', juce::ModifierKeys::altModifier, 0)),
              "timelineJumpToLocator2");
    EXPECT_EQ(manager.getActionsForKeyPress(juce::KeyPress('1', juce::ModifierKeys::altModifier, 0)).size(), 1);
    EXPECT_EQ(manager.getActionsForKeyPress(juce::KeyPress('2', juce::ModifierKeys::altModifier, 0)).size(), 1);

    // The grid family is back on Ctrl+Shift+digit, where every existing install already has it.
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_EQ(manager.getBinding("snapSetWhole"), juce::KeyPress('1', juce::ModifierKeys(ctrlShift), 0));
    EXPECT_EQ(manager.getBinding("snapSetHalf"), juce::KeyPress('2', juce::ModifierKeys(ctrlShift), 0));
}

// THE root cause of "jump to locator 2 does nothing", pinned so the same shape cannot come back.
//
// `saveToProperties` writes EVERY action's binding, so one rebind of anything freezes the whole
// table on disk. A later build that moves a default therefore does NOT move the user's key — and if
// the chord it moved OFF still has a stale COMMAND on it, `MainComponent::keyPressed`'s "first
// action bound to this key that HAS a command" rule dispatches the command and the new surface
// action never runs.
TEST_F(ShortcutManagerTest, APersistedCommandBindingShadowsASurfaceActionOnTheSameChord) {
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    const juce::KeyPress chord('2', juce::ModifierKeys(ctrlShift), 0);

    // Simulate the collision the shipped defaults would have had: a surface action moved onto the
    // chord a command already owns.
    manager.setBinding("timelineJumpToLocator2", chord);
    const auto matches = manager.getActionsForKeyPress(chord);
    ASSERT_GE(matches.size(), 2) << "both ids answer to the chord";
    EXPECT_TRUE(matches.contains("timelineJumpToLocator2"));
    EXPECT_TRUE(matches.contains("snapSetHalf"));

    // MainComponent's rule, restated here: the COMMAND wins, whatever the table order.
    juce::String dispatched;
    for (const auto& action : matches)
        if (AppCommands::getCommandForAction(action) != AppCommands::kNoCommand) {
            dispatched = action;
            break;
        }
    EXPECT_EQ(dispatched, "snapSetHalf") << "the grid command swallows the chord; the locator jump never runs";

    // The shipped defaults avoid it by construction: Option+digit collides with nothing.
    ShortcutManager fresh;
    for (const char* id : {"timelineJumpToLocator1", "timelineJumpToLocator2"}) {
        const auto binding = fresh.getBinding(id);
        const auto all = fresh.getActionsForKeyPress(binding);
        EXPECT_EQ(all.size(), 1) << id << " must be the only action on its chord";
        for (const auto& other : all)
            EXPECT_EQ(AppCommands::getCommandForAction(other), AppCommands::kNoCommand)
                << id << " shares its chord with the command " << other;
    }
}

// ---------------------------------------------------------------------------
// New defaults
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, GridCommandsUseRealCtrlNotCommand) {
    // Deliberate: on macOS the Ctrl+digit space is free where Cmd+digit is not. On Windows/Linux
    // juce::ModifierKeys::commandModifier IS ctrlModifier, so this reads as Ctrl there either way.
    //
    // The eight ABSOLUTE grid commands are Ctrl+ALT+digit: Ctrl+Shift+1/2 now belongs to the locator
    // jumps, and moving the whole family kept it a single coherent block rather than splitting the
    // digit row across two modifier sets.
    for (const char* actionId :
         {"snapSetWhole", "snapSetHalf", "snapSetQuarter", "snapSetEighth", "snapSetSixteenth", "snapSetThirtySecond",
          "snapSetSixtyFourth", "snapSetHundredTwentyEighth", "snapCyclePrev", "snapCycleNext"}) {
        const auto kp = manager.getBinding(actionId);
        EXPECT_TRUE(kp.getModifiers().isCtrlDown()) << actionId;
        EXPECT_TRUE(kp.getModifiers().isShiftDown()) << actionId;
        EXPECT_FALSE(kp.getModifiers().isAltDown()) << actionId;
    }
    EXPECT_EQ(manager.getBinding("snapCyclePrev").getKeyCode(), juce::KeyPress::leftKey);
    EXPECT_EQ(manager.getBinding("snapCycleNext").getKeyCode(), juce::KeyPress::rightKey);
}

// The finer half of the grid row: one digit each, continuing 1..5 rather than moving to a second
// modifier family, and each one a real command (the Settings row and the key both come from here).
TEST_F(ShortcutManagerTest, FinerGridCommandsContinueTheDigitRow) {
    EXPECT_EQ(manager.getBinding("snapSetThirtySecond").getKeyCode(), '6');
    EXPECT_EQ(manager.getBinding("snapSetSixtyFourth").getKeyCode(), '7');
    EXPECT_EQ(manager.getBinding("snapSetHundredTwentyEighth").getKeyCode(), '8');

    EXPECT_EQ(AppCommands::getCommandForAction("snapSetThirtySecond"), AppCommands::snapSetThirtySecond);
    EXPECT_EQ(AppCommands::getCommandForAction("snapSetSixtyFourth"), AppCommands::snapSetSixtyFourth);
    EXPECT_EQ(AppCommands::getCommandForAction("snapSetHundredTwentyEighth"), AppCommands::snapSetHundredTwentyEighth);

    // Labelled with the note values the snap combo shows, like the five that came before them.
    EXPECT_EQ(ShortcutManager::getActionDescription("snapSetThirtySecond"), "Set Grid to 1/32");
    EXPECT_EQ(ShortcutManager::getActionDescription("snapSetSixtyFourth"), "Set Grid to 1/64");
    EXPECT_EQ(ShortcutManager::getActionDescription("snapSetHundredTwentyEighth"), "Set Grid to 1/128");

    EXPECT_EQ(ShortcutManager::getCategory("snapSetThirtySecond"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("snapSetHundredTwentyEighth"), ShortcutCategory::Timeline);
}

// ---------------------------------------------------------------------------
// keyPressMatches — the macOS shifted-symbol dispatch bug
// ---------------------------------------------------------------------------

// THE regression this function exists for. juce_NSViewComponentPeer_mac.mm's getKeyCodeFromEvent()
// builds the key code from [ev charactersIgnoringModifiers][0] and, as its own comment concedes,
// "charactersIgnoringModifiers does not ignore the shift key" — it only upper-cases LETTERS. So a
// real Ctrl+Shift+1 reaches keyPressed as KeyPress('!', ctrl|shift) and never equalled the stored
// '1'. Every test in this file used to construct the binding's own key code directly, which is
