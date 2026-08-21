#include "../Source/ShortcutManager.h"
#include "../Source/UI/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>

namespace {

// Every action id a COMPONENT resolves for itself, written out literally rather than derived from
// the manager. That is the whole point: this list is a transcription of what
// PianoRollComponent::keyPressed, TimelinePanelComponent::keyPressed and
// TimelineClipLaneArea::keyPressed actually ask for, so if one of them is missing from
// ShortcutManager::resetToDefaults() the test fails instead of the KEY silently going dead.
//
// The failure mode it guards is specific and invisible: with a manager installed, resolution is
// STRICT — getBinding() answers an unknown id with a default-constructed KeyPress, and every
// matchesAction() treats that as "this action has no key" rather than falling back to the hardcoded
// default. So a typo'd or unregistered id doesn't throw, doesn't warn, and doesn't fall back; the
// key just stops working.
const juce::StringArray& surfaceResolvedActionIds() {
    static const juce::StringArray ids{
        // PianoRollComponent::keyPressed
        "pianoRollQuantise",
        "pianoRollNavNextNote",
        "pianoRollNavPrevNote",
        "pianoRollNudgeRight",
        "pianoRollNudgeLeft",
        "pianoRollTransposeOctaveUp",
        "pianoRollTransposeOctaveDown",
        "pianoRollTransposeUp",
        "pianoRollTransposeDown",
        // TimelinePanelComponent::keyPressed (also consults timelineSnapToggle, shared with the roll)
        "timelineSnapToggle",
        "timelineToggleLoop",
        "timelineLoopSelection",
        "timelineToolSelect",
        "timelineToolSplit",
        "timelineToolGlue",
        "timelineToolErase",
        "timelineToolMute",
        "timelineToolDraw",
        // TimelineClipLaneArea::keyPressed (its P shares timelineLoopSelection with the panel)
    };
    return ids;
}

} // namespace

class ShortcutManagerTest : public ::testing::Test {
protected:
    ShortcutManager manager;
};

TEST_F(ShortcutManagerTest, DefaultBindingsCorrect) {
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), ',');
    EXPECT_EQ(manager.getBinding("savePreset").getKeyCode(), 's');
    EXPECT_EQ(manager.getBinding("openPreset").getKeyCode(), 'o');
    EXPECT_EQ(manager.getBinding("undo").getKeyCode(), 'z');
    EXPECT_EQ(manager.getBinding("redo").getKeyCode(), 'z');
    EXPECT_TRUE(manager.getBinding("redo").getModifiers().isShiftDown());
}

TEST_F(ShortcutManagerTest, CopyPasteDuplicateUseThePlatformStandardKeys) {
    EXPECT_EQ(manager.getBinding("copySelection").getKeyCode(), 'c');
    EXPECT_TRUE(manager.getBinding("copySelection").getModifiers().isCommandDown());
    EXPECT_FALSE(manager.getBinding("copySelection").getModifiers().isShiftDown());

    EXPECT_EQ(manager.getBinding("pasteSelection").getKeyCode(), 'v');
    EXPECT_TRUE(manager.getBinding("pasteSelection").getModifiers().isCommandDown());

    EXPECT_EQ(manager.getBinding("duplicateSelection").getKeyCode(), 'd');
    EXPECT_TRUE(manager.getBinding("duplicateSelection").getModifiers().isCommandDown());
}

TEST_F(ShortcutManagerTest, EveryDefaultBindingIsUnique) {
    // Adding an action with a binding that is already taken silently shadows one of the two, since
    // getActionForKeyPress returns whichever it reaches first.
    for (const auto& actionId : manager.getActionIds())
        EXPECT_TRUE(manager.getConflictingAction(actionId, manager.getBinding(actionId)).isEmpty())
            << actionId << " collides with " << manager.getConflictingAction(actionId, manager.getBinding(actionId));
}

// Every id needs a binding and a label. The COMMAND half of this invariant is no longer universal:
// surface actions (the timeline's own keys, the whole piano-roll block) are resolved by the
// component that owns the key, not dispatched through the command manager, so they map to
// AppCommands::kNoCommand by design — see the two tests below, which pin exactly which ids are
// allowed to do that.
TEST_F(ShortcutManagerTest, EveryActionIdHasABindingACategoryAndADescription) {
    for (const auto& actionId : manager.getActionIds()) {
        EXPECT_NE(manager.getBinding(actionId).getKeyCode(), 0) << actionId << " has no default binding";
        EXPECT_NE(ShortcutManager::getActionDescription(actionId), actionId)
            << actionId << " has no human-readable description";
        // getCategory() answers General for an id it has never heard of, so "has a category" is only
        // meaningful as "is IN the action table" — which is exactly what getActionIds() is built
        // from, so this asserts the round trip rather than the enum being non-empty.
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutManager::getCategory(actionId)).contains(actionId))
            << actionId << " is not filed under the category it reports";
    }
}

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

// The four sections must partition the id list exactly: every id in one of them, none in two, and
// nothing left over. The Settings tab draws one section per category and indexes its rows by
// getActionIds() order, so an id missing from every category would simply never be shown.
TEST_F(ShortcutManagerTest, CategoriesPartitionEveryActionId) {
    juce::StringArray seen;
    for (auto category : ShortcutManager::getCategoryOrder())
        for (const auto& actionId : ShortcutManager::getActionIdsInCategory(category)) {
            EXPECT_FALSE(seen.contains(actionId)) << actionId << " appears in two categories";
            seen.add(actionId);
        }

    EXPECT_EQ(seen.size(), manager.getActionIds().size());
    for (const auto& actionId : manager.getActionIds())
        EXPECT_TRUE(seen.contains(actionId)) << actionId << " is in no category";
}

// Each category's ids must be a CONTIGUOUS run of getActionIds(): ShortcutsSettingsTab emits one
// header per category and lays the rows out in getActionIds() order, so an id filed out of place
// would split its section into two headers with the same name.
TEST_F(ShortcutManagerTest, CategoriesAreContiguousInActionIdOrder) {
    std::vector<ShortcutCategory> runs;
    for (const auto& actionId : manager.getActionIds()) {
        const auto category = ShortcutManager::getCategory(actionId);
        if (runs.empty() || runs.back() != category)
            runs.push_back(category);
    }
    EXPECT_EQ(runs.size(), ShortcutManager::getCategoryOrder().size())
        << "a category's ids are split into more than one run in getActionIds() order";
    EXPECT_EQ(runs, ShortcutManager::getCategoryOrder()) << "sections are not in getCategoryOrder() order";
}

TEST_F(ShortcutManagerTest, CategoryAssignmentsAreWhatTheSectionsPromise) {
    EXPECT_EQ(ShortcutManager::getCategory("undo"), ShortcutCategory::General);
    // Routed per focused surface, so General rather than Graph — one key, whichever editor has focus.
    EXPECT_EQ(ShortcutManager::getCategory("cutSelection"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("selectAllModules"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("zoomInHorizontal"), ShortcutCategory::General);
    // The two verbs that mean nothing off the canvas.
    EXPECT_EQ(ShortcutManager::getCategory("autoArrange"), ShortcutCategory::Graph);
    EXPECT_EQ(ShortcutManager::getCategory("saveSnippet"), ShortcutCategory::Graph);
    EXPECT_EQ(ShortcutManager::getCategory("timelineToolSplit"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("snapSetEighth"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollNudgeLeft"), ShortcutCategory::PianoRoll);
    // An id this build has never heard of falls back to General — the widest conflict scope, so an
    // unknown id can never quietly duplicate a real app-wide binding.
    EXPECT_EQ(ShortcutManager::getCategory("nonsenseActionId"), ShortcutCategory::General);
}

// ---------------------------------------------------------------------------
// Command vs surface actions
// ---------------------------------------------------------------------------

// THE ordering tripwire (see surfaceResolvedActionIds): every id a component resolves through the
// installed manager must exist in the defaults table. Installing the manager makes resolution
// strict, so an id that is missing here is a key that does nothing at all, silently.
TEST_F(ShortcutManagerTest, EverySurfaceResolvedIdExistsInTheDefaultsTable) {
    for (const auto& actionId : surfaceResolvedActionIds()) {
        EXPECT_TRUE(manager.getActionIds().contains(actionId))
            << actionId
            << " is consulted by a component but is not a registered action - with a ShortcutManager "
               "installed that key is INERT";
        EXPECT_TRUE(manager.getBinding(actionId).isValid()) << actionId << " has no default binding";
    }
}

// The complement: a surface action must NOT resolve to a command, or MainComponent::keyPressed would
// try to dispatch one and the component's own handling would be bypassed.
TEST_F(ShortcutManagerTest, SurfaceActionsMapToNoCommand) {
    for (const auto& actionId : surfaceResolvedActionIds())
        EXPECT_EQ(AppCommands::getCommandForAction(actionId), AppCommands::kNoCommand)
            << actionId << " is resolved by a component but also claims a command id";

    // And the grid commands, which live in the same Timeline category, DO have one — the category is
    // about conflict scope and Settings grouping, never about how an action is dispatched.
    EXPECT_EQ(AppCommands::getCommandForAction("snapSetQuarter"), AppCommands::snapSetQuarter);
    EXPECT_EQ(AppCommands::getCommandForAction("snapCycleNext"), AppCommands::snapCycleNext);
    EXPECT_EQ(AppCommands::getCommandForAction("zoomInVertical"), AppCommands::zoomInVertical);
}

// Every id that is NOT surface-resolved must have a command, for the original reason: a rebindable
// key with no command behind it fires and nothing happens.
TEST_F(ShortcutManagerTest, EveryNonSurfaceActionHasACommand) {
    for (const auto& actionId : manager.getActionIds()) {
        if (surfaceResolvedActionIds().contains(actionId))
            continue;
        EXPECT_NE(AppCommands::getCommandForAction(actionId), AppCommands::kNoCommand)
            << actionId << " maps to no command";
    }
}

// ---------------------------------------------------------------------------
// Category-scoped conflicts
// ---------------------------------------------------------------------------

// The whole reason the scope narrowed: the bare-key DAW conventions (Q/L/P, the tool digits, the
// piano roll's arrows) live on surfaces that can never hold keyboard focus at once, so sharing a key
// across categories is legal and must not be reported.
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

// Modifier equality is EXACT, which is what keeps the bare tool digits clear of the Ctrl+Shift grid
// commands that share their key codes — the pair most likely to be "fixed" into a collision later.
TEST_F(ShortcutManagerTest, BareToolDigitsDoNotCollideWithTheCtrlShiftGridCommands) {
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
        EXPECT_FALSE(toolBinding == gridBinding);
        // Same category, and still no conflict, because the modifiers differ.
        EXPECT_TRUE(manager.getConflictingAction(pair.toolId, toolBinding).isEmpty());
    }
}

// ---------------------------------------------------------------------------
// New defaults
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, GridCommandsUseRealCtrlNotCommand) {
    // Deliberate: on macOS the Ctrl+digit space is free where Cmd+digit is not. On Windows/Linux
    // juce::ModifierKeys::commandModifier IS ctrlModifier, so this reads as Ctrl there either way.
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
// exactly why the whole grid block and both Cmd+Shift zoom keys were dead in the app and green here.
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
    // Modifiers still have to agree exactly, normalization or not: Ctrl+Shift+'&' is the 1/64 grid
    // command, never the Mute tool's bare 7.
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
    EXPECT_EQ(manager.getBinding("timelineSnapToggle"), juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("timelineToggleLoop"), juce::KeyPress('l', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("timelineLoopSelection"), juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(manager.getBinding("pianoRollQuantise"), juce::KeyPress('q', juce::ModifierKeys::shiftModifier, 0));
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

// ---------------------------------------------------------------------------
// ShortcutsSettingsTab pure helpers (no GUI needed)
// ---------------------------------------------------------------------------

TEST(ShortcutsSettingsFilterTest, RowMatchesDescriptionAndBindingTextCaseInsensitively) {
    // No query hides nothing — this answers "is the row visible", not "is this a highlight hit".
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("   ", "Undo", "Cmd + Z")) << "whitespace is not a filter";

    // Description, either case, and as a substring.
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("undo", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("UND", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("ose up", "Transpose Up a Semitone", "Up"));

    // Binding text — the commonest question is "what is on Shift+Q?", which description-only search
    // cannot answer.
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("cmd", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("shift", "Quantise Selected Notes", "Shift + Q"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("left", "Grid Coarser", "Ctrl + Shift + Left"));

    EXPECT_FALSE(ShortcutsSettingsTab::rowMatchesQuery("reverb", "Undo", "Cmd + Z"));
}

TEST(ShortcutsSettingsFilterTest, SectionVisibilityAndAutoExpandUnderAFilter) {
    // No filter: visibility is unconditional, and the fold is the user's own collapse flag.
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsVisible(/*filterActive=*/false, /*sectionHasMatch=*/false));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(false, false, /*collapsed=*/false));
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsExpanded(false, true, /*collapsed=*/true));

    // Filter active: a section with no matches disappears header and all, rather than leaving a
    // lone header over empty space.
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsVisible(true, false));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsVisible(true, true));

    // A matching section is FORCED open — including one the user had folded, so a match can never be
    // trapped inside a fold. The collapse flag itself is untouched, which is what lets clearing the
    // query restore exactly the folds they had.
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(true, true, /*collapsed=*/true));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(true, true, /*collapsed=*/false));
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsExpanded(true, false, /*collapsed=*/false));
}

TEST_F(ShortcutManagerTest, GetActionForKeyPress_Correct) {
    juce::KeyPress cmdS('s', juce::ModifierKeys::commandModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdS), "savePreset");

    juce::KeyPress cmdZ('z', juce::ModifierKeys::commandModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdZ), "undo");

    // Cmd+Shift+Z must match redo
    juce::KeyPress cmdShiftZ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdShiftZ), "redo");
}

TEST_F(ShortcutManagerTest, GetActionForKeyPress_UnknownReturnsEmpty) {
    // Cmd+J has no default binding (Cmd+X became cutSelection, so it no longer works as the
    // "unknown" sample here).
    juce::KeyPress cmdJ('j', juce::ModifierKeys::commandModifier, 0);
    EXPECT_TRUE(manager.getActionForKeyPress(cmdJ).isEmpty());
}

TEST_F(ShortcutManagerTest, SetBinding_Updates) {
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), 'k');
    EXPECT_EQ(manager.getActionForKeyPress(cmdK), "openSettings");
}

TEST_F(ShortcutManagerTest, SaveAndLoad_RoundTrips) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "ShortcutTest";
    opts.folderName = "ShortcutTest";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);

    // Set a custom binding and save
    manager.loadFromProperties(props);
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    manager.saveToProperties();

    // Create a new manager and load
    ShortcutManager manager2;
    manager2.loadFromProperties(props);
    EXPECT_EQ(manager2.getBinding("openSettings").getKeyCode(), 'k');

    // Cleanup
    if (auto* settings = props.getUserSettings())
        settings->clear();
}

TEST_F(ShortcutManagerTest, ResetToDefaults_Restores) {
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), 'k');

    manager.resetToDefaults();
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), ',');
}

TEST_F(ShortcutManagerTest, KeyPressToDisplayString_Formats) {
    juce::KeyPress cmdS('s', juce::ModifierKeys::commandModifier, 0);
    auto display = ShortcutManager::keyPressToDisplayString(cmdS);
#if JUCE_MAC
    EXPECT_TRUE(display.contains("Cmd"));
#endif
    EXPECT_TRUE(display.contains("S"));
}

TEST_F(ShortcutManagerTest, GetActionDescription_Works) {
    EXPECT_EQ(ShortcutManager::getActionDescription("openSettings"), "Open Settings");
    EXPECT_EQ(ShortcutManager::getActionDescription("savePreset"), "Save Preset");
    EXPECT_EQ(ShortcutManager::getActionDescription("openPreset"), "Open Preset");
    EXPECT_EQ(ShortcutManager::getActionDescription("undo"), "Undo");
    EXPECT_EQ(ShortcutManager::getActionDescription("redo"), "Redo");
}

// ---------------------------------------------------------------------------
// Minimap toggle (issue #159)
// ---------------------------------------------------------------------------

// Default binding for the minimap toggle must be Cmd+K, with no extra modifiers.
TEST_F(ShortcutManagerTest, ToggleMinimapDefaultBindingIsCmdK) {
    const auto kp = manager.getBinding("toggleMinimap");
    EXPECT_EQ(kp.getKeyCode(), 'k');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

// AppCommands::getCommandForAction must resolve "toggleMinimap" to the real command ID.
TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesToggleMinimap) {
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMinimap"), AppCommands::toggleMinimap);
}

// "toggleMinimap" must be a registered action id (drives Settings' shortcut list).
TEST_F(ShortcutManagerTest, ActionIds_ContainsToggleMinimap) {
    EXPECT_TRUE(manager.getActionIds().contains("toggleMinimap"));
}

// The action needs a human-readable, non-empty description for the Settings UI.
TEST_F(ShortcutManagerTest, GetActionDescription_ToggleMinimapIsNonEmpty) {
    const auto description = ShortcutManager::getActionDescription("toggleMinimap");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Toggle Minimap");
}
