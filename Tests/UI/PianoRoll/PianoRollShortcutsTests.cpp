// PianoRoll rebindable-shortcut tests: the same actions resolved through a ShortcutManager
// instead of the hardcoded defaults, including the "an unbound action has no key" rule and the
// two keys (Escape, Delete) that stay fixed on purpose.
// Shared PianoRollFixture and helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

// ============================================================================
// 9. Rebindable surface keys (ShortcutManager-resolved)
// ============================================================================

namespace {

juce::KeyPress plainPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress shiftPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::shiftModifier, 0); }
juce::KeyPress ctrlPress(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::ctrlModifier, 0); }

// Every action id the roll resolves. A test pins ALL of them to an explicitly invalid KeyPress
// first, then spells out only the one or two it cares about — so nothing here can be rescued (or
// broken) by whatever ShortcutManager::resetToDefaults() happens to know about these ids in any
// given build. That matters because the defaults land in ShortcutManager in a separate phase.
const char* const kRollActionIds[] = {
    "pianoRollNudgeLeft",         "pianoRollNudgeRight",          "pianoRollTransposeUp", "pianoRollTransposeDown",
    "pianoRollTransposeOctaveUp", "pianoRollTransposeOctaveDown", "pianoRollNavPrevNote", "pianoRollNavNextNote",
    "pianoRollQuantise",          "pianoRollQuantisePitches",     "timelineSnapToggle",   "pianoRollToggleScalePanel",
    "pianoRollToggleScaleFilter"};

void clearRollBindings(ShortcutManager& mgr) {
    for (const char* id : kRollActionIds)
        mgr.setBinding(id, juce::KeyPress());
}

// One selected note at beat 2 of a 16-beat clip, snap pinned to quarters — the bed every key test
// below nudges, transposes or navigates from.
struct KeyBed {
    ClipId clipId;
    NoteId note;
};

KeyBed makeKeyBed(PianoRollFixture& f, double startBeat = 2.0) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    KeyBed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.note = f.doc.addNote(bed.clipId, makeNote(startBeat, 60, 1.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    f.roll.getSelectionForTest().setSelection({bed.note});
    return bed;
}

} // namespace

// The no-manager path is the one every other test in this file exercises implicitly; asserted here
// explicitly so a regression in matchesAction's fallback branch fails with an obvious name.
TEST(PianoRollShortcutTest, WithNoShortcutManagerTheHardcodedDefaultsStillApply) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);
    ASSERT_EQ(f.roll.getShortcutManager(), nullptr);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 3.0);
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 61);
    EXPECT_TRUE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 73) << "Shift+Up is still the octave";
    EXPECT_TRUE(f.roll.keyPressed(shiftPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::downKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);

    ASSERT_TRUE(f.state.snapEnabled);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('j'))) << "bare J is the snap toggle";
    EXPECT_FALSE(f.state.snapEnabled);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('q'))) << "and bare Q is quantise";
    EXPECT_FALSE(f.state.snapEnabled) << "quantise never flips the switch";
}

// Rebinding one action moves the gesture WHOLESALE: the factory key stops doing anything (it is no
// longer bound to anything the roll asks about) and the new key does the nudge.
TEST(PianoRollShortcutTest, RebindingNudgeRightMovesTheGestureToTheNewKey) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollNudgeRight", plainPress('l'));
    f.roll.setShortcutManager(&mgr);
    EXPECT_EQ(f.roll.getShortcutManager(), &mgr);

    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)))
        << "the old key is not bound to this action any more, so it must fall through";
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);
    EXPECT_FALSE(f.undo.canUndo()) << "and it must not have written a document edit either";

    EXPECT_TRUE(f.roll.keyPressed(plainPress('l')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 3.0);
    EXPECT_TRUE(f.undo.canUndo());

    // Dropping the manager restores the defaults — the fallback is not a one-time decision.
    f.roll.setShortcutManager(nullptr);
    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 4.0);
}

// With a manager installed, an action whose binding is unset/invalid has NO key at all — it must
// not quietly fall back to its hardcoded default (that would resurrect a key the user cleared).
// The same rule covers an id this ShortcutManager has never heard of, which is what getBinding
// answers with an invalid KeyPress.
TEST(PianoRollShortcutTest, AnUnboundActionHasNoKeyAndNeverFallsBackToItsDefault) {
    EXPECT_FALSE(ShortcutManager().getBinding("someActionNobodyRegistered").isValid())
        << "the contract this test depends on: an unknown id resolves to an INVALID KeyPress";

    PianoRollFixture f;
    const auto bed = makeKeyBed(f);
    const bool snapBefore = f.state.snapEnabled;

    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.roll.keyPressed(plainPress('q')));
    EXPECT_FALSE(f.roll.keyPressed(shiftPress('q')));

    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 2.0);
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);
    EXPECT_EQ(f.state.snapEnabled, snapBefore);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_FALSE(f.roll.isQuantiseFlashingForTest()) << "an unbound Q never even flashes the button";

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope — see PR
    // #381's fix for TimelinePanelToolStripTests.cpp for the idiom this follows.
    f.roll.setShortcutManager(nullptr);
}

// The snap toggle and the one-shot quantise are two independent PianoRoll-category bindings — the
// roll resolves its OWN "pianoRollSnapToggle" and no longer consults the timeline's, so rebinding
// either one moves only that one.
TEST(PianoRollShortcutTest, SnapToggleAndQuantiseFollowTheirOwnBindings) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f, /*startBeat*/ 1.1); // off-grid, so a quantise is observable
    f.roll.getSelectionForTest().clear();              // quantise-all, no selection needed

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("timelineSnapToggle", plainPress('g'));
    mgr.setBinding("pianoRollQuantise", shiftPress('g'));
    f.roll.setShortcutManager(&mgr);

    int toggles = 0;
    f.roll.onSnapToggled = [&] { ++toggles; };
    ASSERT_TRUE(f.state.snapEnabled);

    EXPECT_FALSE(f.roll.keyPressed(plainPress('q'))) << "Q is no longer either of these actions";
    EXPECT_FALSE(f.roll.keyPressed(plainPress('j'))) << "nor is J";
    EXPECT_EQ(toggles, 0);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.1);

    EXPECT_TRUE(f.roll.keyPressed(plainPress('g')));
    EXPECT_FALSE(f.state.snapEnabled);
    EXPECT_EQ(toggles, 1);
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.1) << "a snap toggle never moves a note";

    EXPECT_TRUE(f.roll.keyPressed(shiftPress('g')));
    EXPECT_DOUBLE_EQ(f.doc.getNote(bed.note)->startBeat, 1.0) << "Shift+G quantised, from the RAW division";
    EXPECT_FALSE(f.state.snapEnabled) << "and never flipped the switch";
    EXPECT_EQ(toggles, 1);

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope.
    f.roll.setShortcutManager(nullptr);
}

// Rebinding the octave transpose alone must not shadow (or be shadowed by) the semitone one — the
// two are separate actions, and Shift+Up is only "the octave" because that is its DEFAULT.
TEST(PianoRollShortcutTest, RebindingTheOctaveTransposeLeavesTheSemitoneOneAlone) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollTransposeUp", plainPress(juce::KeyPress::upKey));
    mgr.setBinding("pianoRollTransposeOctaveUp", plainPress('u'));
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(shiftPress(juce::KeyPress::upKey)))
        << "Shift+Up is bound to nothing now, and must NOT decay into the plain transpose";
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 60);

    EXPECT_TRUE(f.roll.keyPressed(plainPress(juce::KeyPress::upKey)));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 61);
    EXPECT_TRUE(f.roll.keyPressed(plainPress('u')));
    EXPECT_EQ(f.doc.getNote(bed.note)->pitch, 73);

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope.
    f.roll.setShortcutManager(nullptr);
}

// Alt+Left/Right stay resolvable too — and navigation is still selection-only, never an undo step.
TEST(PianoRollShortcutTest, NoteNavigationIsRebindableAndStillNeverTouchesTheDoc) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    const auto first = f.doc.addNote(clipId, makeNote(0.0, 60, 1.0));
    const auto second = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(clipId);
    f.roll.getSelectionForTest().setSelection({first});

    ShortcutManager mgr;
    clearRollBindings(mgr);
    mgr.setBinding("pianoRollNavNextNote", plainPress(']'));
    mgr.setBinding("pianoRollNavPrevNote", plainPress('['));
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(altArrow(juce::KeyPress::rightKey)));
    EXPECT_TRUE(onlySelected(f, first));

    EXPECT_TRUE(f.roll.keyPressed(plainPress(']')));
    EXPECT_TRUE(onlySelected(f, second));
    EXPECT_TRUE(f.roll.keyPressed(plainPress('[')));
    EXPECT_TRUE(onlySelected(f, first));
    EXPECT_FALSE(f.undo.canUndo()) << "navigation is selection-only";

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope.
    f.roll.setShortcutManager(nullptr);
}

// Escape and Delete/Backspace are platform conventions, not app shortcuts: they answer identically
// with a manager installed and every roll action explicitly unbound.
TEST(PianoRollShortcutTest, EscapeAndDeleteStayFixedRegardlessOfTheManager) {
    PianoRollFixture f;
    const auto bed = makeKeyBed(f);

    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    bool closeRequested = false;
    f.roll.onCloseRequested = [&] { closeRequested = true; };

    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(f.roll.getSelectionForTest().isEmpty()) << "first Escape clears the selection";
    EXPECT_TRUE(f.roll.isOpen());
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.roll.isOpen());
    EXPECT_TRUE(closeRequested);

    f.roll.openClip(bed.clipId);
    f.roll.getSelectionForTest().setSelection({bed.note});
    EXPECT_TRUE(f.roll.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getNote(bed.note), nullptr);
    EXPECT_TRUE(f.undo.canUndo()) << "and it is still one undo step";

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope.
    f.roll.setShortcutManager(nullptr);
}

// Ctrl+S (the actual Control key — never Cmd, which is Save Preset) toggles the scale-assist
// panel, with no ShortcutManager installed at all.
TEST(PianoRollShortcutTest, DefaultCtrlSTogglesTheScalePanel) {
    PianoRollFixture f;
    ASSERT_EQ(f.roll.getShortcutManager(), nullptr);
    ASSERT_FALSE(f.roll.getScaleAssistPanel().isVisible());

    EXPECT_TRUE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_TRUE(f.roll.getScaleAssistPanel().isVisible());

    EXPECT_TRUE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
}

// Plain S is bound to nothing here (Ctrl is required), so it must fall straight through.
TEST(PianoRollShortcutTest, PlainSDoesNothingToTheScalePanel) {
    PianoRollFixture f;
    ASSERT_FALSE(f.roll.getScaleAssistPanel().isVisible());
    EXPECT_FALSE(f.roll.keyPressed(plainPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());
}

// The strict no-fallback contract (setShortcutManager's class comment) applies here exactly like
// every other roll action: with a manager installed and this binding explicitly cleared, Ctrl+S
// has NO key at all — it must not quietly resurrect its hardcoded default.
TEST(PianoRollShortcutTest, WithManagerInstalledAndBindingClearedCtrlSDoesNothing) {
    PianoRollFixture f;
    ShortcutManager mgr;
    clearRollBindings(mgr);
    f.roll.setShortcutManager(&mgr);

    EXPECT_FALSE(f.roll.keyPressed(ctrlPress('s')));
    EXPECT_FALSE(f.roll.getScaleAssistPanel().isVisible());

    // Detach before `mgr` (declared after `f`, so destroyed first) goes out of scope.
    f.roll.setShortcutManager(nullptr);
}
