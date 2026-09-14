// Concern: keyPressMatches' shifted-symbol normalization (the macOS peer bug it rescues from),
// its use in getActionForKeyPress/getActionsForKeyPress, and the zoom/surface-default/display tests
// that ride alongside it.
#include "ShortcutManagerTestFixture.h"

TEST_F(ShortcutManagerTest, KeyPressMatchesRescuesShiftChordedSymbolsFromTheMacPeer) {
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    const juce::KeyPress storedCtrlShift1('1', juce::ModifierKeys(ctrlShift), 0);
    ASSERT_EQ(manager.getBinding("snapSetWhole"), storedCtrlShift1) << "precondition: the stored form is the digit";

    // What the peer actually delivers.
    const juce::KeyPress pressedBang('!', juce::ModifierKeys(ctrlShift), '!');
    EXPECT_FALSE(storedCtrlShift1 == pressedBang) << "precondition: plain KeyPress equality is what was broken";
    EXPECT_TRUE(ShortcutManager::keyPressMatches(storedCtrlShift1, pressedBang));

    // The zoom pair is the same bug on punctuation: Cmd+Shift+'=' arrives as '+'.
    const int cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const auto storedZoomInVertical = manager.getBinding("zoomInVertical");
    ASSERT_EQ(storedZoomInVertical, juce::KeyPress('=', juce::ModifierKeys(cmdShift), 0));
    EXPECT_TRUE(
        ShortcutManager::keyPressMatches(storedZoomInVertical, juce::KeyPress('+', juce::ModifierKeys(cmdShift), '+')));
    // ...and Cmd+Shift+'-' as '_'.
    EXPECT_TRUE(ShortcutManager::keyPressMatches(manager.getBinding("zoomOutVertical"),
                                                 juce::KeyPress('_', juce::ModifierKeys(cmdShift), '_')));

    // BIDIRECTIONAL: a binding persisted WITH the shifted glyph (which is what the Settings tab
    // captured on macOS for as long as this bug was live) still fires on the base character.
    EXPECT_TRUE(ShortcutManager::keyPressMatches(juce::KeyPress('+', juce::ModifierKeys(cmdShift), 0),
                                                 juce::KeyPress('=', juce::ModifierKeys(cmdShift), 0)));
    EXPECT_TRUE(ShortcutManager::keyPressMatches(juce::KeyPress('!', juce::ModifierKeys(ctrlShift), 0),
                                                 juce::KeyPress('1', juce::ModifierKeys(ctrlShift), 0)));
}

// The normalization is gated on Shift being down on BOTH sides, and on the modifier sets being
// otherwise identical. Without that gate the bare tool digits would start answering to the shifted
// glyphs, and Ctrl+Shift+1 could reach a bare 1.
TEST_F(ShortcutManagerTest, KeyPressMatchesNormalizesOnlyWhenBothSidesCarryShift) {
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    const juce::KeyPress bare1('1', juce::ModifierKeys::noModifiers, 0);
    const juce::KeyPress bare7('7', juce::ModifierKeys::noModifiers, 0);

    // No shift on either side: '!' is simply a different key from '1'.
    EXPECT_FALSE(ShortcutManager::keyPressMatches(bare1, juce::KeyPress('!', juce::ModifierKeys::noModifiers, '!')));
    // Shift on the PRESS only — the bare tool digit must not answer to Shift+7's '&'.
    EXPECT_FALSE(ShortcutManager::keyPressMatches(bare7, juce::KeyPress('&', juce::ModifierKeys::shiftModifier, '&')));
    // Shift on the BINDING only.
    EXPECT_FALSE(ShortcutManager::keyPressMatches(juce::KeyPress('&', juce::ModifierKeys::shiftModifier, 0), bare7));
    // Modifiers still have to agree exactly, normalization or not: Ctrl+Shift+'&' is nobody's
    // binding, and certainly never the Mute tool's bare 7.
    EXPECT_FALSE(ShortcutManager::keyPressMatches(bare7, juce::KeyPress('&', juce::ModifierKeys(ctrlShift), '&')));
    // And an invalid binding (an action the user cleared) matches nothing at all.
    EXPECT_FALSE(
        ShortcutManager::keyPressMatches(juce::KeyPress(), juce::KeyPress('!', juce::ModifierKeys(ctrlShift), '!')));
}

// Letters need no map entry: the peer upper-cases them, and key-code comparison is already
// case-insensitive. Pinned so nobody "completes" the table with letter rows.
TEST_F(ShortcutManagerTest, KeyPressMatchesLeavesLettersAndArrowsAlone) {
    const int cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const auto redo = manager.getBinding("redo"); // Cmd+Shift+Z, stored lower-case
    EXPECT_TRUE(ShortcutManager::keyPressMatches(redo, juce::KeyPress('Z', juce::ModifierKeys(cmdShift), 'Z')));
    EXPECT_TRUE(ShortcutManager::keyPressMatches(redo, juce::KeyPress('z', juce::ModifierKeys(cmdShift), 'z')));
    EXPECT_FALSE(ShortcutManager::keyPressMatches(redo, juce::KeyPress('a', juce::ModifierKeys(cmdShift), 'a')));

    // Extended keys live above 0x10000 and are not characters — Shift+Up stays Shift+Up.
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_TRUE(
        ShortcutManager::keyPressMatches(manager.getBinding("snapCyclePrev"),
                                         juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(ctrlShift), 0)));
    EXPECT_TRUE(
        ShortcutManager::keyPressMatches(manager.getBinding("pianoRollTransposeOctaveUp"),
                                         juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)));
}

// The whole point of the matcher: table lookup, not just the predicate, resolves the peer's event.
TEST_F(ShortcutManagerTest, GetActionForKeyPressResolvesAShiftedSymbolEvent) {
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('!', juce::ModifierKeys(ctrlShift), '!')), "snapSetWhole");
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('^', juce::ModifierKeys(ctrlShift), '^')),
              "snapSetThirtySecond");
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('&', juce::ModifierKeys(ctrlShift), '&')),
              "snapSetSixtyFourth");
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('*', juce::ModifierKeys(ctrlShift), '*')),
              "snapSetHundredTwentyEighth");

    // Exactly ONE action per shifted event — the normalization must not make a keystroke ambiguous.
    EXPECT_EQ(manager.getActionsForKeyPress(juce::KeyPress('&', juce::ModifierKeys(ctrlShift), '&')).size(), 1);

    // The locator pair, on plain Option+digit, needs no rescue at all: Option IS ignored by
    // charactersIgnoringModifiers, so a real Option+2 arrives carrying '2' and matches directly.
    // That immunity is half of why the pair was moved there.
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('1', juce::ModifierKeys::altModifier, '1')),
              "timelineJumpToLocator1");
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('2', juce::ModifierKeys::altModifier, '2')),
              "timelineJumpToLocator2");

    // And the bare tool digit is untouched by any of it.
    EXPECT_EQ(manager.getActionForKeyPress(juce::KeyPress('7', juce::ModifierKeys::noModifiers, '7')),
              "timelineToolMute");
}

// Conflict detection stays BINDING-vs-BINDING exact. Normalizing there would report two deliberately
// different stored chords as a collision, and the Settings tab's auto-swap would then steal one.
TEST_F(ShortcutManagerTest, ConflictDetectionStaysExactAndDoesNotNormalizeShiftedSymbols) {
    const int cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    ASSERT_EQ(manager.getBinding("zoomInVertical"), juce::KeyPress('=', juce::ModifierKeys(cmdShift), 0));

    // '+' is the same physical key as '=' for DISPATCH, but as a stored binding it is its own chord.
    EXPECT_TRUE(
        manager.getConflictingAction("openSettings", juce::KeyPress('+', juce::ModifierKeys(cmdShift), 0)).isEmpty());
    // The identical chord is of course still a conflict.
    EXPECT_EQ(manager.getConflictingAction("openSettings", juce::KeyPress('=', juce::ModifierKeys(cmdShift), 0)),
              "zoomInVertical");

    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_TRUE(
        manager.getConflictingAction("snapCycleNext", juce::KeyPress('!', juce::ModifierKeys(ctrlShift), 0)).isEmpty())
        << "Ctrl+Shift+'!' stored is not Ctrl+Shift+'1' stored";
    EXPECT_EQ(manager.getConflictingAction("snapCycleNext", juce::KeyPress('1', juce::ModifierKeys(ctrlShift), 0)),
              "snapSetWhole");

    // ...and the locator pair's own chord reports itself, on its own modifier set.
    EXPECT_EQ(manager.getConflictingAction("snapCycleNext", juce::KeyPress('2', juce::ModifierKeys::altModifier, 0)),
              "timelineJumpToLocator2");
}

TEST_F(ShortcutManagerTest, ZoomCommandsUseTheCommandModifierZoomPair) {
    EXPECT_EQ(manager.getBinding("zoomInHorizontal").getKeyCode(), '=');
    EXPECT_EQ(manager.getBinding("zoomOutHorizontal").getKeyCode(), '-');
    for (const char* actionId : {"zoomInHorizontal", "zoomOutHorizontal", "zoomInVertical", "zoomOutVertical"}) {
        const auto kp = manager.getBinding(actionId);
        EXPECT_TRUE(kp.getModifiers().isCommandDown()) << actionId;
    }
    // Shift is the vertical axis, mirroring the wheel bindings.
    EXPECT_FALSE(manager.getBinding("zoomInHorizontal").getModifiers().isShiftDown());
    EXPECT_TRUE(manager.getBinding("zoomInVertical").getModifiers().isShiftDown());
    EXPECT_TRUE(manager.getBinding("zoomOutVertical").getModifiers().isShiftDown());
}

TEST_F(ShortcutManagerTest, SurfaceDefaultsAreExactlyTheComponentsHardcodedFallbacks) {
    // These are the keys PianoRollComponent/TimelinePanelComponent fall back to with NO manager
    // installed. If a default here drifted from its fallback, installing a manager would silently
    // MOVE a key the user had already learned.
    // Snap is J (Cubase's snap key), NOT Q — Q is Cubase's quantise, which is what the roll uses it
    // for, so one letter used to mean two verbs depending on which surface had focus.
    EXPECT_EQ(manager.getBinding("timelineSnapToggle"), juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("timelineToggleLoop"), juce::KeyPress('l', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("timelineLoopSelection"), juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0));
    // BARE Q is quantise in the roll (Cubase parity). Snap has no piano-roll action of its own: the
    // roll resolves the SHARED "timelineSnapToggle" above, which is J — two rebindable "Toggle Snap"
    // actions on the same key flipping the same flag would be a Settings list nobody could reason
    // about. Pitch quantise keeps Option+Shift+Q, stored as key code + modifiers rather than the
    // Unicode glyph macOS delivers for Option+letter.
    EXPECT_EQ(manager.getBinding("pianoRollQuantise"), juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("pianoRollToggleScaleFilter"),
              juce::KeyPress('s', juce::ModifierKeys::altModifier, 0));
    EXPECT_EQ(manager.getBinding("pianoRollQuantisePitches"),
              juce::KeyPress(
                  'q', juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier), 0));
    EXPECT_EQ(manager.getBinding("pianoRollNudgeLeft"),
              juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("pianoRollNavPrevNote"),
              juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0));
    EXPECT_EQ(manager.getBinding("pianoRollTransposeOctaveUp"),
              juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    EXPECT_EQ(manager.getBinding("timelineToolMute"), juce::KeyPress('7', juce::ModifierKeys::noModifiers, 0));
}

// The extended keys are not characters (JUCE encodes them above 0x10000), so without explicit cases
// the Settings list would show a stray glyph for eight actions — and the tab's search matches
// against this very string.
TEST_F(ShortcutManagerTest, ArrowKeysGetReadableDisplayStrings) {
    EXPECT_EQ(ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollNudgeLeft")), "Left");
    EXPECT_EQ(ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollNudgeRight")), "Right");
    EXPECT_EQ(ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollTransposeUp")), "Up");
    EXPECT_TRUE(
        ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollTransposeOctaveDown")).contains("Down"));
    EXPECT_TRUE(
        ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollTransposeOctaveDown")).contains("Shift"));
}

// One keypress can now name more than one action (different categories). Command dispatch depends on
// getActionsForKeyPress returning ALL of them so MainComponent can pick the one with a command.
TEST_F(ShortcutManagerTest, GetActionsForKeyPressReportsEveryMatch) {
    // Bare Left: the piano roll's nudge and nothing else, and NOT a command.
    const juce::KeyPress bareLeft(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);
    const auto matches = manager.getActionsForKeyPress(bareLeft);
    EXPECT_EQ(matches.size(), 1);
    EXPECT_TRUE(matches.contains("pianoRollNudgeLeft"));
    EXPECT_EQ(AppCommands::getCommandForAction(matches[0]), AppCommands::kNoCommand);

    // Deliberately put a command action on the same key in another category and check both surface.
    manager.setBinding("autoArrange", bareLeft);
    const auto both = manager.getActionsForKeyPress(bareLeft);
    EXPECT_EQ(both.size(), 2);
    EXPECT_TRUE(both.contains("pianoRollNudgeLeft"));
    EXPECT_TRUE(both.contains("autoArrange"));
}
