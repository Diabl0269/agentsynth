// synth::ui::MacroPortConfigDialog — the "Configure I/O" modal for one Macro.
// Pure UI, exactly like ExportAudioDialog: no graph/synth::MacroSet reference of its own, only a
// PortRow snapshot handed in and intent callbacks fired out, so these tests drive the real
// controls and read back what each button would send, with no GraphEditor/AudioEngine/message
// loop involved at all. GraphEditor-side wiring (promptConfigureMacroIO) and the port-mutation
// API these callbacks are meant to reach are covered separately in Tests/MacroPortFlowTests.cpp.

#include "../UI/Layout/FadeVisibilityTestGuard.h"
#include "../UI/Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using synth::MacroPortKind;
using synth::ui::MacroPortConfigDialog;
using Row = MacroPortConfigDialog::PortRow;

namespace {
std::vector<Row> twoPorts() {
    Row in;
    in.nodeUuid = "uuid-in";
    in.isInput = true;
    in.name = "Pitch In";
    in.kind = MacroPortKind::AudioCV;
    in.shape = MacroPortShape::Mono;
    in.voiceCount = 1;

    Row out;
    out.nodeUuid = "uuid-out";
    out.isInput = false;
    out.name = "Wet Out";
    out.kind = MacroPortKind::AudioCV;
    out.shape = MacroPortShape::Stereo;
    out.voiceCount = 1;

    return {in, out};
}
} // namespace

TEST(MacroPortConfigDialogTest, ConstructsWithTheGivenRows) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    ASSERT_EQ(dialog.getRowCountForTest(), 2);
    EXPECT_EQ(dialog.getRowNodeUuidForTest(0), "uuid-in");
    EXPECT_EQ(dialog.getRowNameForTest(0), "Pitch In");
    EXPECT_TRUE(dialog.getRowIsInputForTest(0));
    EXPECT_EQ(dialog.getRowNodeUuidForTest(1), "uuid-out");
    EXPECT_FALSE(dialog.getRowIsInputForTest(1));
}

TEST(MacroPortConfigDialogTest, AddPortEmitsWhateverTheNewPortControlsHold) {
    MacroPortConfigDialog dialog("My Macro", {});

    bool fired = false;
    bool capturedIsInput = false;
    MacroPortKind capturedKind = MacroPortKind::Midi;
    MacroPortShape capturedShape = MacroPortShape::Mono;
    int capturedVoices = 0;
    juce::String capturedName;
    dialog.onAddPort = [&](bool isInput, MacroPortKind kind, MacroPortShape shape, int voices,
                           const juce::String& name) {
        fired = true;
        capturedIsInput = isInput;
        capturedKind = kind;
        capturedShape = shape;
        capturedVoices = voices;
        capturedName = name;
    };

    dialog.setNewPortDirectionForTest(false); // Output
    dialog.setNewPortKindForTest(MacroPortKind::AudioCV);
    dialog.setNewPortShapeForTest(MacroPortShape::Poly);
    dialog.setNewPortVoiceCountForTest(6);
    dialog.setNewPortNameForTest("Voice Out");
    dialog.triggerAddPortForTest();

    ASSERT_TRUE(fired);
    EXPECT_FALSE(capturedIsInput);
    EXPECT_EQ(capturedKind, MacroPortKind::AudioCV);
    EXPECT_EQ(capturedShape, MacroPortShape::Poly);
    EXPECT_EQ(capturedVoices, 6);
    EXPECT_EQ(capturedName, "Voice Out");
}

TEST(MacroPortConfigDialogTest, AddPortDefaultsToMonoInputAudioCV) {
    MacroPortConfigDialog dialog("My Macro", {});

    bool capturedIsInput = false;
    MacroPortKind capturedKind = MacroPortKind::Midi;
    MacroPortShape capturedShape = MacroPortShape::Stereo;
    dialog.onAddPort = [&](bool isInput, MacroPortKind kind, MacroPortShape shape, int, const juce::String&) {
        capturedIsInput = isInput;
        capturedKind = kind;
        capturedShape = shape;
    };
    dialog.triggerAddPortForTest();

    EXPECT_TRUE(capturedIsInput);
    EXPECT_EQ(capturedKind, MacroPortKind::AudioCV);
    EXPECT_EQ(capturedShape, MacroPortShape::Mono);
}

TEST(MacroPortConfigDialogTest, RenameCommitsWhateverTheEditorHolds) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid, capturedName;
    dialog.onRenamePort = [&](const juce::String& uuid, const juce::String& name) {
        capturedUuid = uuid;
        capturedName = name;
    };

    dialog.setRowNameForTest(0, "Cutoff In");
    dialog.commitRowNameForTest(0);

    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_EQ(capturedName, "Cutoff In");
}

TEST(MacroPortConfigDialogTest, DeleteButtonFiresWithTheRowsUuid) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    dialog.onDeletePort = [&](const juce::String& uuid) { capturedUuid = uuid; };
    dialog.triggerRowDeleteForTest(1);

    EXPECT_EQ(capturedUuid, "uuid-out");
}

// There is no per-row "Apply Shape" button — picking a new shape in the
// combo box commits immediately (still delete+re-add of the node as ONE undo step underneath,
// per GraphEditor::changeMacroPortShape; only the UI gesture collapsed to one step). So selecting
// Poly alone must fire onChangePortShape, with no separate "Apply" click.
TEST(MacroPortConfigDialogTest, SelectingANewShapeCommitsImmediately) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    bool fired = false;
    juce::String capturedUuid;
    MacroPortShape capturedShape = MacroPortShape::Mono;
    dialog.onChangePortShape = [&](const juce::String& uuid, MacroPortShape shape, int) {
        fired = true;
        capturedUuid = uuid;
        capturedShape = shape;
    };

    dialog.setRowShapeForTest(0, MacroPortShape::Poly);

    ASSERT_TRUE(fired);
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_EQ(capturedShape, MacroPortShape::Poly);
}

// StereoCollapsed is auto-derived only and never a choice this dialog's
// combo box offers, but an existing auto-created port can still show up here as a row — it
// genuinely IS a stereo pair, so it must display as "Stereo" (comboIndexFromShape), not fall back
// to "Mono". Because the combo can never itself produce StereoCollapsed, any real interaction with
// that row — even re-picking the same-looking "Stereo" entry — is a deliberate shape choice and
// correctly converts the port to the two-jack Stereo shape (a real change, not a no-op), which is
// exactly what maybeCommitShape's committedShape_ != newShape guard does here.
TEST(MacroPortConfigDialogTest, StereoCollapsedRowDisplaysAsStereoAndPickingStereoCommitsARealConversion) {
    Row collapsed;
    collapsed.nodeUuid = "uuid-collapsed";
    collapsed.isInput = false;
    collapsed.name = "Reverb Audio";
    collapsed.kind = MacroPortKind::AudioCV;
    collapsed.shape = MacroPortShape::StereoCollapsed;
    collapsed.voiceCount = 1;

    MacroPortConfigDialog dialog("My Macro", {collapsed});
    ASSERT_EQ(dialog.getRowCountForTest(), 1);

    bool fired = false;
    juce::String capturedUuid;
    MacroPortShape capturedShape = MacroPortShape::Mono;
    dialog.onChangePortShape = [&](const juce::String& uuid, MacroPortShape shape, int) {
        fired = true;
        capturedUuid = uuid;
        capturedShape = shape;
    };

    dialog.setRowShapeForTest(0, MacroPortShape::Stereo);

    ASSERT_TRUE(fired) << "picking Stereo on a StereoCollapsed row must commit, not no-op";
    EXPECT_EQ(capturedUuid, "uuid-collapsed");
    EXPECT_EQ(capturedShape, MacroPortShape::Stereo); // the dialog never emits StereoCollapsed itself
}

// The voice-count field is a separate commit gesture from the shape combo (typing a number and
// hitting Return/losing focus, the same idiom the row's name editor already uses) — it re-sends
// onChangePortShape with the row's CURRENT shape selection and whatever the field now holds.
TEST(MacroPortConfigDialogTest, VoiceCountCommitsWithTheRowsCurrentShape) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    MacroPortShape capturedShape = MacroPortShape::Mono;
    int capturedVoices = 0;
    dialog.onChangePortShape = [&](const juce::String& uuid, MacroPortShape shape, int voices) {
        capturedUuid = uuid;
        capturedShape = shape;
        capturedVoices = voices;
    };

    dialog.setRowShapeForTest(0, MacroPortShape::Poly); // commits once, with the default voice count
    dialog.setRowVoiceCountForTest(0, 3);               // just types into the field, no commit yet
    dialog.commitRowVoiceCountForTest(0);               // simulates Return / focus-lost

    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_EQ(capturedShape, MacroPortShape::Poly);
    EXPECT_EQ(capturedVoices, 3);
}

// A voice-count TextEditor's onFocusLost/onReturnKey fire on every transit through the field, not
// only on an actual edit (unlike a combo box, which only notifies on a real selection change) — so
// committing without ever changing the field (e.g. tabbing past it, or pressing Close right after
// it) must be a no-op. Firing anyway would send onChangePortShape with the SAME (shape, voices)
// pair GraphEditor::changeMacroPortShape already has, which still deletes and re-creates the
// port's node — minting a fresh nodeUuid for nothing, in the one subsystem (docs/macros/ports.md#port-set-and-ordering)
// built entirely on uuid identity.
TEST(MacroPortConfigDialogTest, VoiceCountCommitDoesNothingWhenNothingChanged) {
    Row poly;
    poly.nodeUuid = "uuid-poly";
    poly.isInput = true;
    poly.name = "Voice In";
    poly.kind = MacroPortKind::AudioCV;
    poly.shape = MacroPortShape::Poly;
    poly.voiceCount = 4;

    MacroPortConfigDialog dialog("My Macro", {poly});

    bool fired = false;
    dialog.onChangePortShape = [&](const juce::String&, MacroPortShape, int) { fired = true; };

    // No setRowVoiceCountForTest before this - the field still holds exactly what the row was
    // constructed with.
    dialog.commitRowVoiceCountForTest(0);

    EXPECT_FALSE(fired);
}

TEST(MacroPortConfigDialogTest, RefreshPortsReplacesTheRowListAndResizesRowControls) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    ASSERT_EQ(dialog.getRowCountForTest(), 2);

    Row solo;
    solo.nodeUuid = "uuid-solo";
    solo.isInput = true;
    solo.name = "Solo In";
    dialog.refreshPorts({solo});

    ASSERT_EQ(dialog.getRowCountForTest(), 1);
    EXPECT_EQ(dialog.getRowNodeUuidForTest(0), "uuid-solo");
    EXPECT_EQ(dialog.getRowNameForTest(0), "Solo In");
}

TEST(MacroPortConfigDialogTest, CloseButtonFiresOnRequestClose) {
    MacroPortConfigDialog dialog("My Macro", {});
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.triggerCloseForTest();

    EXPECT_TRUE(closed);
}

// Renaming a port then closing the dialog without any intervening focus-loss/Return must apply
// the new name. juce::TextEditor::focusLost() posts an async command message rather than calling
// onFocusLost synchronously, and Close's own mouseDown grabs keyboard focus away from the name
// editor (queuing that async commit) before onRequestClose tears the dialog down — so the commit
// would either never land or land too late. requestClose() therefore forces every row's rename
// (and shape/voice) edit to commit synchronously before onRequestClose fires. setRowNameForTest
// deliberately does NOT simulate focus-loss/Return first — going straight to Close is the exact
// repro.
TEST(MacroPortConfigDialogTest, RenameFollowedImmediatelyByCloseAppliesExactlyOnce) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    int renameCallCount = 0;
    juce::String capturedUuid, capturedName;
    dialog.onRenamePort = [&](const juce::String& uuid, const juce::String& name) {
        ++renameCallCount;
        capturedUuid = uuid;
        capturedName = name;
    };
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.setRowNameForTest(0, "Cutoff In");
    dialog.triggerCloseForTest();

    EXPECT_EQ(renameCallCount, 1);
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_EQ(capturedName, "Cutoff In");
    EXPECT_TRUE(closed);

    // A second close (nothing changed since the first commit) must not re-fire the rename against
    // the same unchanged text.
    dialog.triggerCloseForTest();
    EXPECT_EQ(renameCallCount, 1);
}

// Regression guard: requestClose()'s per-row commit sweep must NOT re-derive a row's shape
// from shapeBox and diff it against committedShape_ the way maybeCommitShape() does everywhere
// else. comboIndexFromShape maps StereoCollapsed to the same combo item id as plain Stereo (the
// combo has no separate entry for it), so shapeFromComboIndex(shapeBox.getSelectedId()) always
// reads back Stereo for a StereoCollapsed row, even with nothing touched. Simply opening and
// closing Configure I/O on an untouched StereoCollapsed port must never fire onChangePortShape —
// that would silently convert it to a real two-jack Stereo port (delete+recreate, fresh uuid,
// dropped cables) via GraphEditor::changeMacroPortShape.
TEST(MacroPortConfigDialogTest, ClosingWithAnUntouchedStereoCollapsedRowNeverChangesItsShape) {
    Row collapsed;
    collapsed.nodeUuid = "uuid-collapsed";
    collapsed.isInput = false;
    collapsed.name = "Reverb Audio";
    collapsed.kind = MacroPortKind::AudioCV;
    collapsed.shape = MacroPortShape::StereoCollapsed;
    collapsed.voiceCount = 1;

    MacroPortConfigDialog dialog("My Macro", {collapsed});

    bool shapeChanged = false;
    dialog.onChangePortShape = [&](const juce::String&, MacroPortShape, int) { shapeChanged = true; };
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.triggerCloseForTest();

    EXPECT_FALSE(shapeChanged);
    EXPECT_TRUE(closed);
}

// ============================================================================
// Drag-to-reorder — the constraint under test is structural: onReorderPortTo's
// index is scoped to the dragged row's OWN direction group, so there is no argument that could
// ever mean "become an output" — GraphEditor::reorderMacroPortToIndex (MacroPortFlowTests.cpp)
// covers the other half: that the resulting `order` values never touch the opposite group.
// ============================================================================

TEST(MacroPortConfigDialogTest, DragReorderFiresOnReorderPortToWithTheRowsUuidAndTargetIndex) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    int capturedIndex = -1;
    dialog.onReorderPortTo = [&](const juce::String& uuid, int index) {
        capturedUuid = uuid;
        capturedIndex = index;
    };

    dialog.dragRowToIndexInGroupForTest(1, 0); // drag "uuid-out" (the lone output) to index 0

    EXPECT_EQ(capturedUuid, "uuid-out");
    EXPECT_EQ(capturedIndex, 0);
}

TEST(MacroPortConfigDialogTest, DraggingAnInputRowNeverFiresForTheOutputRowAndViceVersa) {
    MacroPortConfigDialog dialog("My Macro", twoPorts()); // row 0 = input, row 1 = output

    std::vector<juce::String> capturedUuids;
    dialog.onReorderPortTo = [&](const juce::String& uuid, int) { capturedUuids.push_back(uuid); };

    dialog.dragRowToIndexInGroupForTest(0, 0); // the input row's own (single-member) group

    ASSERT_EQ(capturedUuids.size(), 1u);
    EXPECT_EQ(capturedUuids[0], "uuid-in") << "dragging the input row must never report the output row's uuid";
}

// ---- The real mouse path: lift, glide aside, one commit on release, Esc cancels -----------------
// Headless, so the dialog is not showing and the animator lands every glide instantly: a row's bounds
// are exactly where the animator says they are.

namespace {
std::vector<Row> threeInputsOneOutput() {
    std::vector<Row> rows;
    for (const char* name : {"A", "B", "C"}) {
        Row in;
        in.nodeUuid = juce::String("uuid-") + name;
        in.isInput = true;
        in.name = name;
        rows.push_back(in);
    }
    Row out;
    out.nodeUuid = "uuid-out";
    out.isInput = false;
    out.name = "Out";
    rows.push_back(out);
    return rows;
}

// Hand-built events on a row's grab handle. The list position is turned into handle-local coordinates
// at every event, because the handle moves with its row while the row is dragged.
struct HandleDrag {
    MacroPortConfigDialog& dialog;
    int row;
    float pressY;

    juce::Component& handle() const { return *dialog.getRowDragHandleForTest(row); }
    juce::Point<float> local(float listY) const {
        auto& content = dialog.getRowsContentForTest();
        const float x = content.getLocalPoint(&handle(), handle().getLocalBounds().toFloat().getCentre()).x;
        return handle().getLocalPoint(&content, juce::Point<float>(x, listY));
    }
    void down() { handle().mouseDown(makeClickEvent(handle(), local(pressY))); }
    void dragBy(float dy) { handle().mouseDrag(makeDragEvent(handle(), local(pressY + dy), local(pressY))); }
    void up(float dy) { handle().mouseUp(makeClickEvent(handle(), local(pressY + dy))); }
};

HandleDrag handleDragOf(MacroPortConfigDialog& dialog, int row) {
    return {dialog, row, (float)dialog.getRowBoundsForTest(row).getCentreY()};
}

constexpr float kRowStride = 36.0f; // row height 32 + gap 4
} // namespace

TEST(MacroPortConfigDialogTest, DraggingAcrossTwoRowsCommitsOnceOnReleaseWithTheFinalIndex) {
    MacroPortConfigDialog dialog("My Macro", threeInputsOneOutput());
    std::vector<std::pair<juce::String, int>> commits;
    dialog.onReorderPortTo = [&](const juce::String& uuid, int index) { commits.emplace_back(uuid, index); };

    auto drag = handleDragOf(dialog, 0);
    drag.down();
    drag.dragBy(kRowStride);
    drag.dragBy(2.0f * kRowStride + 4.0f);
    EXPECT_TRUE(commits.empty()) << "nothing is committed while the row is still held";
    drag.up(2.0f * kRowStride + 4.0f);

    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].first, "uuid-A");
    EXPECT_EQ(commits[0].second, 2) << "same final index the old line-and-release drag produced";
}

TEST(MacroPortConfigDialogTest, TheDraggedRowFollowsThePointerWhileItsNeighboursGlideAside) {
    MacroPortConfigDialog dialog("My Macro", threeInputsOneOutput());
    int commits = 0;
    dialog.onReorderPortTo = [&](const juce::String&, int) { ++commits; };
    const auto a = dialog.getRowBoundsForTest(0);
    const auto c = dialog.getRowBoundsForTest(2);

    auto drag = handleDragOf(dialog, 0);
    drag.down();
    drag.dragBy(40.0f); // past B's midpoint, short of C's

    EXPECT_EQ(commits, 0);
    EXPECT_TRUE(dialog.isRowDragActiveForTest());
    EXPECT_EQ(dialog.getRowBoundsForTest(0).getY(), a.getY() + 40) << "the lifted row sits under the pointer";
    EXPECT_EQ(dialog.getRowBoundsForTest(1).getY(), a.getY()) << "B moved up into the vacated slot";
    EXPECT_EQ(dialog.getRowBoundsForTest(2).getY(), c.getY()) << "C stays put";

    drag.dragBy(2.0f * kRowStride + 4.0f);
    EXPECT_EQ(dialog.getRowBoundsForTest(0).getY(), c.getY()) << "held inside the group, at its last slot";
    EXPECT_EQ(dialog.getRowBoundsForTest(2).getY(), dialog.getRowBoundsForTest(1).getY() + (int)kRowStride)
        << "C moved up one slot";

    drag.up(2.0f * kRowStride + 4.0f);
    EXPECT_EQ(commits, 1);
}

TEST(MacroPortConfigDialogTest, ADraggedInputNeverLeavesItsOwnGroup) {
    MacroPortConfigDialog dialog("My Macro", threeInputsOneOutput());
    int capturedIndex = -1;
    dialog.onReorderPortTo = [&](const juce::String&, int index) { capturedIndex = index; };
    const int outputY = dialog.getRowBoundsForTest(3).getY();

    auto drag = handleDragOf(dialog, 0);
    drag.down();
    drag.dragBy(500.0f); // far below the output row
    EXPECT_LT(dialog.getRowBoundsForTest(0).getY(), outputY) << "the lifted input stops at the end of the inputs";
    drag.up(500.0f);

    EXPECT_EQ(capturedIndex, 2) << "the index is the last slot of the input group";
}

TEST(MacroPortConfigDialogTest, EscapeMidDragCommitsNothingAndPutsEveryRowBack) {
    MacroPortConfigDialog dialog("My Macro", threeInputsOneOutput());
    int commits = 0;
    dialog.onReorderPortTo = [&](const juce::String&, int) { ++commits; };
    std::vector<int> before;
    for (int i = 0; i < 4; ++i)
        before.push_back(dialog.getRowBoundsForTest(i).getY());

    auto drag = handleDragOf(dialog, 0);
    drag.down();
    drag.dragBy(2.0f * kRowStride + 4.0f);
    ASSERT_TRUE(dialog.isRowDragActiveForTest());
    EXPECT_TRUE(dialog.sendEscapeToRowDragForTest());
    drag.up(2.0f * kRowStride + 4.0f);

    EXPECT_EQ(commits, 0);
    EXPECT_FALSE(dialog.isRowDragActiveForTest());
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(dialog.getRowBoundsForTest(i).getY(), before[(size_t)i]) << "row " << i;
}

TEST(MacroPortConfigDialogTest, EscapeMidDragCancelsTheDragInsteadOfClosingTheDialog) {
    MacroPortConfigDialog dialog("My Macro", threeInputsOneOutput());
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };
    auto drag = handleDragOf(dialog, 0);
    drag.down();
    drag.dragBy(2.0f * kRowStride);

    dialog.simulateEscapeKeyForTest();
    EXPECT_FALSE(closed);
    dialog.simulateEscapeKeyForTest();
    EXPECT_TRUE(closed) << "with no drag live, Escape closes the dialog as before";
}

TEST(MacroPortConfigDialogTest, TheGrabHandleShowsTheGrabCursor) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    ASSERT_NE(dialog.getRowDragHandleForTest(0), nullptr);
    EXPECT_TRUE(dialog.getRowDragHandleForTest(0)->getMouseCursor() == synth::ui::dragGrabCursor());
}

// ============================================================================
// Per-port colour
// ============================================================================

TEST(MacroPortConfigDialogTest, RowWithNoColourDisplaysTheKindTintByDefault) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    EXPECT_FALSE(dialog.getRowHasCustomColourForTest(0));
    // The exact tint colour depends on the live theme/LookAndFeel (absent here, so the fallback
    // theme applies) — the load-bearing assertion is "no custom colour", covered above; toVar/
    // fromVar's own round-trip tests (MacroSetTests.cpp) pin the persisted representation.
}

TEST(MacroPortConfigDialogTest, SettingAColourFiresOnChangePortColourAndUpdatesTheSwatch) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    std::optional<juce::Colour> capturedColour;
    dialog.onChangePortColour = [&](const juce::String& uuid, std::optional<juce::Colour> c) {
        capturedUuid = uuid;
        capturedColour = c;
    };

    dialog.setRowColourForTest(0, juce::Colour(0xffff0000));

    ASSERT_TRUE(capturedColour.has_value());
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_EQ(*capturedColour, juce::Colour(0xffff0000));
    EXPECT_TRUE(dialog.getRowHasCustomColourForTest(0));
    EXPECT_EQ(dialog.getRowDisplayColourForTest(0), juce::Colour(0xffff0000));
}

TEST(MacroPortConfigDialogTest, ResettingAColourFiresOnChangePortColourWithNulloptAndFallsBackToTheTint) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    dialog.setRowColourForTest(0, juce::Colour(0xffff0000));
    const juce::Colour tintBeforeReset = [&] {
        // Capture what the tint fallback actually is by resetting and reading the swatch back.
        dialog.resetRowColourForTest(0);
        return dialog.getRowDisplayColourForTest(0);
    }();
    EXPECT_FALSE(dialog.getRowHasCustomColourForTest(0));

    bool fired = false;
    std::optional<juce::Colour> capturedColour = juce::Colour(0xff123456);
    dialog.onChangePortColour = [&](const juce::String&, std::optional<juce::Colour> c) {
        fired = true;
        capturedColour = c;
    };
    dialog.setRowColourForTest(0, juce::Colour(0xff00ff00));
    dialog.resetRowColourForTest(0);

    ASSERT_TRUE(fired);
    EXPECT_FALSE(capturedColour.has_value());
    EXPECT_FALSE(dialog.getRowHasCustomColourForTest(0));
    EXPECT_EQ(dialog.getRowDisplayColourForTest(0), tintBeforeReset);
}

// Regression test for a lifetime bug caught in review: the popup buildColourPicker() returns
// outlives a single click dispatch (a real one lives inside a juce::CallOutBox until the user
// closes it), and any OTHER row-mutating callback firing while it's open (a rename's onFocusLost,
// a shape change, ...) rebuilds every row component out from under it via refreshPorts() ->
// rebuildRowComponents() -> rowControls_.clear(). The picker's onCommit callback must still land
// the user's pick on the dialog (never crash by reaching into the now-destroyed row) even when
// that race happens between opening the picker and closing it.
TEST(MacroPortConfigDialogTest, ColourPickerCommitSurvivesTheRowBeingRebuiltWhileItIsOpen) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    auto picker = dialog.createRowColourPickerForTest(0);
    ASSERT_NE(picker, nullptr);

    // Simulate the async rebuild any other GraphEditor callback triggers while the popup is still
    // open: refreshPorts() destroys and recreates every PortRowComponent, including row 0's, whose
    // `this` the still-open picker's callbacks must not touch.
    dialog.refreshPorts(twoPorts());

    juce::String capturedUuid;
    std::optional<juce::Colour> capturedColour;
    dialog.onChangePortColour = [&](const juce::String& uuid, std::optional<juce::Colour> c) {
        capturedUuid = uuid;
        capturedColour = c;
    };

    // Must not crash, and the pick must still land on the dialog via the surviving row uuid —
    // even though the row that opened the picker is long gone.
    picker->commitForTest();

    ASSERT_TRUE(capturedColour.has_value());
    EXPECT_EQ(capturedUuid, "uuid-in");
}

// ============================================================================
// Keyboard accessibility
// ============================================================================

TEST(MacroPortConfigDialogTest, EscapeClosesTheDialogWhenNothingHasFocus) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.simulateEscapeKeyForTest();

    EXPECT_TRUE(closed);
}

// TextEditor consumes Escape itself before it ever bubbles to the dialog's own keyPressed() —
// every row's name field is wired with its OWN onEscapeKey for exactly this reason (see the
// class comment's section), and this drives that wired lambda directly (the same idiom
// commitRowNameForTest already uses for onFocusLost).
TEST(MacroPortConfigDialogTest, EscapeFromARowsNameFieldAlsoClosesTheDialog) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.simulateRowNameEscapeForTest(0);

    EXPECT_TRUE(closed);
}

TEST(MacroPortConfigDialogTest, EscapeFromTheNewPortNameFieldAlsoClosesTheDialog) {
    MacroPortConfigDialog dialog("My Macro", {});
    bool closed = false;
    dialog.onRequestClose = [&] { closed = true; };

    dialog.simulateNewPortNameEscapeForTest();

    EXPECT_TRUE(closed);
}

// Return in the "Add a port" name field is the keyboard equivalent of clicking Add.
TEST(MacroPortConfigDialogTest, ReturnInTheNewPortNameFieldCommitsAnAdd) {
    MacroPortConfigDialog dialog("My Macro", {});
    bool fired = false;
    dialog.onAddPort = [&](bool, MacroPortKind, MacroPortShape, int, const juce::String&) { fired = true; };

    dialog.setNewPortNameForTest("Return Test");
    dialog.simulateNewPortNameReturnForTest();

    EXPECT_TRUE(fired);
}

TEST(MacroPortConfigDialogTest, ArrowDownNavigationTargetsTheNextRowAndStopsAtTheEnd) {
    Row a = twoPorts()[0];
    Row b = twoPorts()[1];
    Row c;
    c.nodeUuid = "uuid-out2";
    c.isInput = false;
    c.name = "Wet Out 2";
    MacroPortConfigDialog dialog("My Macro", {a, b, c});
    ASSERT_EQ(dialog.getRowCountForTest(), 3);

    EXPECT_EQ(dialog.computeArrowNavigationTargetRowForTest(0, /*moveDown=*/true), 1);
    EXPECT_EQ(dialog.computeArrowNavigationTargetRowForTest(1, /*moveDown=*/true), 2);
    EXPECT_EQ(dialog.computeArrowNavigationTargetRowForTest(2, /*moveDown=*/true), -1)
        << "no wraparound past the last row";
    EXPECT_EQ(dialog.computeArrowNavigationTargetRowForTest(0, /*moveDown=*/false), -1)
        << "no wraparound past the first row";
}

TEST(MacroPortConfigDialogTest, ArrowKeyOnARowsDeleteButtonIsConsumedByTheButtonItself) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    // Consumed (returns true) means GlyphButton's own keyPressed() override handled it via
    // onVerticalArrow rather than falling through to Button::keyPressed (which would only react
    // to Return/Space) — proving the wiring in the constructor actually reaches the override.
    EXPECT_TRUE(
        dialog.simulateRowControlArrowKeyForTest(0, MacroPortConfigDialog::RowControl::Delete, /*moveDown=*/true));
}

// ============================================================================
// MacroAutoPortPromptDialog (docs/macros/auto-ports.md#auto-creating-ports-when-grouping)
// ============================================================================

using synth::ui::MacroAutoPortPromptDialog;

TEST(MacroAutoPortPromptDialogTest, RememberDefaultsToOn) {
    MacroAutoPortPromptDialog dialog(3);
    EXPECT_TRUE(dialog.getRememberChoiceForTest());
}

TEST(MacroAutoPortPromptDialogTest, CreatePortsFiresTrueWithTheRememberState) {
    MacroAutoPortPromptDialog dialog(2);

    bool fired = false;
    bool capturedCreate = false;
    bool capturedRemember = true;
    dialog.onChoice = [&](bool createPorts, bool remember) {
        fired = true;
        capturedCreate = createPorts;
        capturedRemember = remember;
    };

    dialog.setRememberChoiceForTest(false);
    dialog.triggerCreatePortsForTest();

    ASSERT_TRUE(fired);
    EXPECT_TRUE(capturedCreate);
    EXPECT_FALSE(capturedRemember);
}

TEST(MacroAutoPortPromptDialogTest, LeaveCablesAsIsFiresFalseWithTheRememberState) {
    MacroAutoPortPromptDialog dialog(1);

    bool fired = false;
    bool capturedCreate = true;
    bool capturedRemember = false;
    dialog.onChoice = [&](bool createPorts, bool remember) {
        fired = true;
        capturedCreate = createPorts;
        capturedRemember = remember;
    };

    dialog.setRememberChoiceForTest(true);
    dialog.triggerLeaveCablesAsIsForTest();

    ASSERT_TRUE(fired);
    EXPECT_FALSE(capturedCreate);
    EXPECT_TRUE(capturedRemember);
}

TEST(MacroAutoPortPromptDialogTest, PaintAndResizeDoNotCrash) {
    MacroAutoPortPromptDialog dialog(4);
    dialog.setBounds(0, 0, 420, 190);
    juce::Image image(juce::Image::ARGB, 420, 190, true);
    juce::Graphics g(image);
    dialog.paint(g);
    SUCCEED();
}

// ============================================================================
// Keyboard reorder (Cmd+Up/Cmd+Down, no Up/Down buttons); keyboard-focus visibility; "Add a port"
// panel layout.
// ============================================================================

namespace {
bool imagesHaveIdenticalPixels(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}
} // namespace

// Diagnosed via Component::createComponentSnapshot rendered to a PNG (docs/development/
// `createComponentSnapshot` smoke-test pattern): the "Add a port" panel's addBlockArea budgeted
// height for only 2 of its 3 rows (section label + newRow1), so newRow2 — the name field AND the
// Add button — was squeezed to zero height and effectively vanished: a button that
// "doesn't appear" but still works via Tab+Return. Root cause was the layout
// arithmetic in resized(), not colour/contrast.
TEST(MacroPortConfigDialogTest, AddButtonHasNonTrivialVisibleBounds) {
    MacroPortConfigDialog dialog("My Macro", {});
    auto bounds = dialog.getAddButtonBoundsForTest();
    EXPECT_GT(bounds.getWidth(), 20);
    // The zero-height regression this guards against would fail this at height == 0; 20px leaves
    // headroom below the real ~26px row height without hardcoding the dialog's private constant.
    EXPECT_GT(bounds.getHeight(), 20);
}

TEST(MacroPortConfigDialogTest, AddButtonRegionPaintsVisibleContentInFullDialogSnapshot) {
    MacroPortConfigDialog dialog("My Macro", {});
    synth::theme::AppLookAndFeel lf;
    dialog.setLookAndFeel(&lf);
    dialog.setBounds(0, 0, dialog.getWidth(), dialog.getHeight());

    auto img = dialog.createComponentSnapshot(dialog.getLocalBounds());
    auto bounds = dialog.getAddButtonBoundsForTest();
    ASSERT_GT(bounds.getHeight(), 0);

    // The button's fill/border/text must actually differ from the dialog's own background at the
    // button's own centre — proving it paints something there, not merely that it occupies space.
    auto centre = bounds.getCentre();
    auto buttonPixel = img.getPixelAt(centre.x, centre.y);
    auto bgPixel = img.getPixelAt(2, 2); // top-left corner, outside every control
    EXPECT_NE(buttonPixel, bgPixel);

    dialog.setLookAndFeel(nullptr);
}

// GlyphButton/PortColourSwatch::paintButton only ever receives isOver()/isDown() from
// juce::Button::paint() (confirmed by reading juce_Button.cpp), never keyboard-focus state, so
// nothing distinguished a focused row control from an unfocused one. grabKeyboardFocus() can't be
// exercised headlessly (Component::isShowing() gates it), so this drives the forced-flag seam
// that bypasses real focus tracking and exercises the same paint path.
TEST(MacroPortConfigDialogTest, FocusedRowControlPaintsVisiblyDifferentlyFromUnfocused) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    // The real theme, not GlyphButton's fallback Colors{}/Metrics{} struct defaults — a fallback
    // where accent happened to equal textMuted would let this pass on ring GEOMETRY alone, never
    // catching a same-colour regression in the real app.
    synth::theme::AppLookAndFeel lf;
    dialog.setLookAndFeel(&lf);

    auto unfocused = dialog.renderRowDeleteButtonForTest(0);
    ASSERT_GT(unfocused.getWidth(), 0);

    dialog.setRowFocusRingForcedForTest(0, true);
    auto focused = dialog.renderRowDeleteButtonForTest(0);
    ASSERT_GT(focused.getWidth(), 0);

    EXPECT_FALSE(imagesHaveIdenticalPixels(unfocused, focused));

    dialog.setRowFocusRingForcedForTest(0, false);
    auto unfocusedAgain = dialog.renderRowDeleteButtonForTest(0);
    EXPECT_TRUE(imagesHaveIdenticalPixels(unfocused, unfocusedAgain));

    dialog.setLookAndFeel(nullptr);
}

// The per-row Up/Down glyph buttons are gone (drag-to-reorder is confirmed working, so they were
// pure clutter); Cmd+Up/Cmd+Down on the row's remaining controls (colour swatch, Delete button) is
// the keyboard-accessible replacement, firing the same onReorderPort the buttons used to.
TEST(MacroPortConfigDialogTest, CmdArrowOnDeleteButtonFiresReorderWithTheCorrectDirection) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    bool capturedMoveUp = false;
    dialog.onReorderPort = [&](const juce::String& uuid, bool moveUp) {
        capturedUuid = uuid;
        capturedMoveUp = moveUp;
    };

    EXPECT_TRUE(dialog.simulateRowControlArrowKeyForTest(0, MacroPortConfigDialog::RowControl::Delete,
                                                         /*moveDown=*/false, /*withCommandModifier=*/true));
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_TRUE(capturedMoveUp);

    EXPECT_TRUE(dialog.simulateRowControlArrowKeyForTest(1, MacroPortConfigDialog::RowControl::Delete,
                                                         /*moveDown=*/true, /*withCommandModifier=*/true));
    EXPECT_EQ(capturedUuid, "uuid-out");
    EXPECT_FALSE(capturedMoveUp);
}

TEST(MacroPortConfigDialogTest, CmdArrowOnColourSwatchAlsoFiresReorder) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    bool capturedMoveUp = false;
    dialog.onReorderPort = [&](const juce::String& uuid, bool moveUp) {
        capturedUuid = uuid;
        capturedMoveUp = moveUp;
    };

    EXPECT_TRUE(dialog.simulateRowControlArrowKeyForTest(0, MacroPortConfigDialog::RowControl::Colour,
                                                         /*moveDown=*/false, /*withCommandModifier=*/true));
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_TRUE(capturedMoveUp);
}

// A bare (unmodified) Up/Down on a row control must keep doing row-FOCUS navigation, not reorder —
// bare arrows already mean "move focus to the row above/below" (moveRowFocus), so the command
// modifier is what disambiguates the two meanings, never the bare key alone.
TEST(MacroPortConfigDialogTest, BareArrowOnARowControlDoesRowNavigationNotReorder) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    bool reorderFired = false;
    dialog.onReorderPort = [&](const juce::String&, bool) { reorderFired = true; };

    EXPECT_TRUE(dialog.simulateRowControlArrowKeyForTest(0, MacroPortConfigDialog::RowControl::Delete,
                                                         /*moveDown=*/true, /*withCommandModifier=*/false));
    EXPECT_FALSE(reorderFired);
}

// ---- Fades (docs/layout/animation.md, "Fading things in and out") ----------------------------------------------

// The "Add a port" shape box fades out for a MIDI port (it has no shape) and back in for Audio / CV.
TEST(MacroPortConfigDialogTest, TheNewPortShapeBoxFadesWithTheKind) {
    MacroPortConfigDialog dialog("My Macro", {});
    auto& box = dialog.getNewShapeBoxForTest();
    ASSERT_TRUE(box.isVisible());

    FadeAnimateGuard guard;
    dialog.setNewPortKindForTest(MacroPortKind::Midi);
    EXPECT_TRUE(box.isVisible()) << "stays on screen while it fades out";
    EXPECT_FALSE(interceptsClicks(box));
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(box.getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(box.isVisible());

    dialog.setNewPortKindForTest(MacroPortKind::AudioCV);
    EXPECT_TRUE(box.isVisible());
    EXPECT_FLOAT_EQ(box.getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(box.getAlpha(), 1.0f);
}

TEST(MacroPortConfigDialogTest, TheNewPortVoicesEditorFadesInForPolyAndGetsItsPlace) {
    MacroPortConfigDialog dialog("My Macro", {});
    auto& voices = dialog.getNewVoicesEditorForTest();
    ASSERT_FALSE(voices.isVisible());

    FadeAnimateGuard guard;
    dialog.setNewPortShapeForTest(MacroPortShape::Poly);
    EXPECT_TRUE(voices.isVisible());
    EXPECT_FLOAT_EQ(voices.getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(voices.getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(voices.getAlpha(), 1.0f);
    EXPECT_FALSE(voices.getBounds().isEmpty()) << "a revealed editor has a place in the row";

    dialog.setNewPortShapeForTest(MacroPortShape::Mono);
    EXPECT_TRUE(voices.isVisible());
    EXPECT_FALSE(interceptsClicks(voices));
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(voices.isVisible());
}

// A row's voices editor fades in when its shape becomes Poly and out when it stops being Poly.
TEST(MacroPortConfigDialogTest, ARowsVoicesEditorFadesWithItsShape) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());
    auto* voices = dialog.getRowVoicesEditorForTest(0);
    auto* shape = dialog.getRowShapeBoxForTest(0);
    ASSERT_TRUE(voices != nullptr && shape != nullptr);
    ASSERT_FALSE(voices->isVisible()) << "a Mono row starts without it, and without a fade";
    const int shapeRightBefore = shape->getRight();

    FadeAnimateGuard guard;
    dialog.setRowShapeForTest(0, MacroPortShape::Poly);
    EXPECT_TRUE(voices->isVisible());
    EXPECT_FLOAT_EQ(voices->getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(voices->getAlpha(), 0.5f, 0.01f);
    const int shapeRightMid = shape->getRight();
    EXPECT_LT(shapeRightMid, shapeRightBefore) << "the shape box slides over as the voices editor takes its place";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(voices->getAlpha(), 1.0f);
    EXPECT_LT(shape->getRight(), shapeRightMid);
}

// Close commits a pending voice count only while the editor is really there; one that is fading out (the shape is
// no longer Poly) must not recommit the shape.
TEST(MacroPortConfigDialogTest, CloseDoesNotRecommitFromAVoicesEditorThatIsFadingOut) {
    Row poly;
    poly.nodeUuid = "uuid-poly";
    poly.isInput = true;
    poly.name = "Voices In";
    poly.kind = MacroPortKind::AudioCV;
    poly.shape = MacroPortShape::Poly;
    poly.voiceCount = 3;
    MacroPortConfigDialog dialog("My Macro", {poly});

    int commits = 0;
    dialog.onChangePortShape = [&](const juce::String&, MacroPortShape, int) { ++commits; };

    FadeAnimateGuard guard;
    dialog.setRowShapeForTest(0, MacroPortShape::Mono);
    ASSERT_EQ(commits, 1);
    ASSERT_TRUE(dialog.getRowVoicesEditorForTest(0)->isVisible()) << "still fading out";

    dialog.setRowVoiceCountForTest(0, 7);
    dialog.triggerCloseForTest();
    EXPECT_EQ(commits, 1) << "closing must not send the shape again from the fading editor";
}

// An empty section's hint fades out when its first port arrives, and the section below slides up with it.
TEST(MacroPortConfigDialogTest, TheEmptyHintFadesOutAsAPortArrivesAndTheSectionBelowSlides) {
    MacroPortConfigDialog dialog("My Macro", {});
    auto& inputsHint = dialog.getInputsEmptyHintForTest();
    auto& outputsHint = dialog.getOutputsEmptyHintForTest();
    ASSERT_TRUE(inputsHint.isVisible());
    ASSERT_TRUE(outputsHint.isVisible());

    auto twoPortsWithAnInput = twoPorts();
    twoPortsWithAnInput.pop_back(); // one input, no outputs

    FadeAnimateGuard guard;
    dialog.refreshPorts(twoPortsWithAnInput);
    EXPECT_TRUE(inputsHint.isVisible()) << "stays while it fades";
    const int fullHeight = inputsHint.getHeight();
    ASSERT_GT(fullHeight, 0);
    const int outputsHintYStart = outputsHint.getY();

    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(inputsHint.getAlpha(), 0.5f, 0.01f);
    EXPECT_LT(inputsHint.getHeight(), fullHeight);
    EXPECT_GT(inputsHint.getHeight(), 0);
    EXPECT_LT(outputsHint.getY(), outputsHintYStart) << "the outputs section slides up";
    EXPECT_TRUE(outputsHint.isVisible()) << "the outputs section is still empty";

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(inputsHint.isVisible());
    EXPECT_EQ(outputsHint.getY(), outputsHintYStart - fullHeight);
}
