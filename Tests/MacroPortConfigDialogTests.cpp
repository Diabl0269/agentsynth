// synth::ui::MacroPortConfigDialog — the "Configure I/O" modal for one Macro (P8-15b, T140).
// Pure UI, exactly like ExportAudioDialog: no graph/synth::MacroSet reference of its own, only a
// PortRow snapshot handed in and intent callbacks fired out, so these tests drive the real
// controls and read back what each button would send, with no GraphEditor/AudioEngine/message
// loop involved at all. GraphEditor-side wiring (promptConfigureMacroIO) and the port-mutation
// API these callbacks are meant to reach are covered separately in Tests/MacroPortFlowTests.cpp.

#include "../Source/UI/MacroPortConfigDialog.h"
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

TEST(MacroPortConfigDialogTest, MoveUpAndMoveDownFireTheCorrectDirection) {
    MacroPortConfigDialog dialog("My Macro", twoPorts());

    juce::String capturedUuid;
    bool capturedMoveUp = false;
    dialog.onReorderPort = [&](const juce::String& uuid, bool moveUp) {
        capturedUuid = uuid;
        capturedMoveUp = moveUp;
    };

    dialog.triggerRowMoveUpForTest(0);
    EXPECT_EQ(capturedUuid, "uuid-in");
    EXPECT_TRUE(capturedMoveUp);

    dialog.triggerRowMoveDownForTest(1);
    EXPECT_EQ(capturedUuid, "uuid-out");
    EXPECT_FALSE(capturedMoveUp);
}

// F1 founder-review fix: the per-row "Apply Shape" button is gone — picking a new shape in the
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
// port's node — minting a fresh nodeUuid for nothing, in the one subsystem (docs/macros.md §5.2)
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

// ============================================================================
// MacroAutoPortPromptDialog (founder-review fix F5, docs/macros.md §7 item 6.2)
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
