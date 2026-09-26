// ControlInspectorTests.cpp -- FRO131 (docs/control/midi-remote-ui.md#inspector-right):
// Source/UI/MidiRemote/Inspector/ControlInspectorComponent.{h,cpp}, the MIDI Remote panel's right
// region. Headless, "test the real mouse path" convention (docs/control/midi-remote-ui.md's Tests
// section, Source/UI/CLAUDE.md): a click on the "Drives" label is driven through a real
// juce::MouseEvent delivered to the label's own mouseUp(), the same realChildMouseEvent()/
// mouseUp() idiom Tests/UI/Mixer/MixerColumnMidiLearnTests.cpp and
// Tests/UI/Graph/ModuleComponent/ModuleComponentMidiLearnTests.cpp use. A ComboBox selection is
// driven via setSelectedId(id, juce::sendNotificationSync) and a TextEditor commit via
// onReturnKey() -- both the established substitutes used throughout Tests/UI (see
// PianoRollScaleAssistTests.cpp / TimelinePanelMarkerTests.cpp) for gestures a real OS click/
// keypress can't be synthesized for headlessly. A Button click (Forget, Invert) is driven by
// calling its own onClick() directly, same as TimelinePanelZoomSnapScrollTests.cpp's
// getSnapToggleButton().onClick() and MixerColumnMidiLearnTests.cpp's
// getSoloButtonForTest().onClick() -- juce::Button's real click completion gates on
// Component::isMouseOver(), which reads the real desktop cursor position, never true headlessly.
//
// ControlInspectorComponent.h is locked by the orchestrating session and declares no test
// accessors, so every widget here is reached by componentID through the real Component tree
// (findComponentWithID() below, recursive since AssignmentRow's own sub-widgets are grandchildren
// of the inspector, not direct children) rather than a dedicated getter.

#include "UI/MidiRemote/Inspector/ControlInspectorComponent.h"

#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>
#include <vector>

using synth::ui::ControlInspectorComponent;

namespace {

// Recursive componentID search -- juce::Component::findChildWithID only looks at DIRECT children,
// which is not enough here: an assignment row's own widgets (the takeover combo, the range
// editors, ...) are children of the row, which is itself a child of the inspector.
juce::Component* findComponentWithID(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findComponentWithID(*child, id))
            return found;
    return nullptr;
}

juce::MouseEvent realClickEvent(juce::Component& comp) {
    const auto pos = comp.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
}

synth::Control makeControl() {
    synth::Control control;
    control.id = "control-1";
    control.name = "Knob 1";
    control.kind = synth::ControlKind::knob;
    control.message = {synth::MessageType::cc, 1, 21};
    control.encoding = synth::Encoding::abs7;
    control.buttonMode = synth::ButtonMode::momentary;
    return control;
}

synth::Assignment makeParameterAssignment() {
    synth::Assignment assignment;
    assignment.id = "assign-project";
    assignment.control.profileId = "profile-1";
    assignment.control.controlId = "control-1";
    assignment.target.kind = synth::Target::Kind::parameter;
    assignment.target.parameter.nodeUuid = "node-uuid-1";
    assignment.target.parameter.paramId = "cutoff";
    assignment.takeover = synth::Takeover::useDefault;
    assignment.range.min = 0.0;
    assignment.range.max = 1.0;
    return assignment;
}

synth::Assignment makeActionAssignment() {
    synth::Assignment assignment;
    assignment.id = "assign-global";
    assignment.control.profileId = "profile-1";
    assignment.control.controlId = "control-1";
    assignment.target.kind = synth::Target::Kind::action;
    assignment.target.action.actionId = "togglePlayback";
    assignment.takeover = synth::Takeover::useDefault;
    assignment.range.min = 0.0;
    assignment.range.max = 1.0;
    return assignment;
}

ControlInspectorComponent::AssignmentRowModel makeProjectRow() {
    ControlInspectorComponent::AssignmentRowModel row;
    row.assignment = makeParameterAssignment();
    row.scopeLabel = "Project";
    row.drivesLabel = "Filter . Cutoff";
    row.nodeUuid = "node-uuid-1";
    row.isOrphaned = false;
    return row;
}

synth::Assignment makeNodeCommandAssignment() {
    synth::Assignment assignment;
    assignment.id = "assign-solo";
    assignment.control.profileId = "profile-1";
    assignment.control.controlId = "control-1";
    assignment.target.kind = synth::Target::Kind::nodeCommand;
    assignment.target.nodeCommand.nodeUuid = "node-uuid-2";
    assignment.target.nodeCommand.command = synth::NodeCommandKind::toggleSolo;
    assignment.takeover = synth::Takeover::useDefault;
    assignment.range.min = 0.0;
    assignment.range.max = 1.0;
    return assignment;
}

// FRO253's node command target lives in the PROJECT doc alongside parameter targets (never
// Global), so it carries the same "Project" scope tag -- MidiRemotePanelComponent's own
// refreshInspectorForSelection() branches on target.kind only for the drivesLabel/nodeUuid, same
// as ControlInspectorComponent::isTakeoverEditable() branching on it for the enabled state below.
ControlInspectorComponent::AssignmentRowModel makeNodeCommandRow() {
    ControlInspectorComponent::AssignmentRowModel row;
    row.assignment = makeNodeCommandAssignment();
    row.scopeLabel = "Project";
    row.drivesLabel = "Kick . Solo";
    row.nodeUuid = "node-uuid-2";
    row.isOrphaned = false;
    return row;
}

ControlInspectorComponent::AssignmentRowModel makeGlobalRow() {
    ControlInspectorComponent::AssignmentRowModel row;
    row.assignment = makeActionAssignment();
    row.scopeLabel = "Global";
    row.drivesLabel = "Play";
    row.nodeUuid = ""; // action target -- no node to locate
    row.isOrphaned = false;
    return row;
}

ControlInspectorComponent::ControlModel makeModel(std::vector<ControlInspectorComponent::AssignmentRowModel> rows) {
    ControlInspectorComponent::ControlModel model;
    model.hasControl = true;
    model.control = makeControl();
    model.assignments = std::move(rows);
    return model;
}

struct AssignmentEditRecorder {
    int callCount = 0;
    synth::Assignment last;

    std::function<void(const synth::Assignment&)> callback() {
        return [this](const synth::Assignment& a) {
            ++callCount;
            last = a;
        };
    }
};

} // namespace

class ControlInspectorComponentTest : public ::testing::Test {
protected:
    void SetUp() override { inspector.setSize(360, 700); }

    ControlInspectorComponent inspector;
};

//==============================================================================
// Empty state
//==============================================================================

TEST_F(ControlInspectorComponentTest, EmptyStateShowsPlaceholderAndHidesEverythingElse) {
    ControlInspectorComponent::ControlModel model; // hasControl == false by default
    inspector.setControl(model);

    auto* nameLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "controlNameLabel"));
    ASSERT_NE(nameLabel, nullptr);
    EXPECT_EQ(nameLabel->getText(), "No control selected");
    EXPECT_TRUE(nameLabel->isVisible());

    auto* kindCombo = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "controlKindCombo"));
    ASSERT_NE(kindCombo, nullptr);
    EXPECT_FALSE(kindCombo->isVisible());
    auto* messageLabel = findComponentWithID(inspector, "controlMessageSpecLabel");
    ASSERT_NE(messageLabel, nullptr);
    EXPECT_FALSE(messageLabel->isVisible());
    auto* buttonModeLabel = findComponentWithID(inspector, "buttonModeLabel");
    ASSERT_NE(buttonModeLabel, nullptr);
    EXPECT_FALSE(buttonModeLabel->isVisible());
    auto* relearnButton = findComponentWithID(inspector, "relearnButton");
    auto* encodingCombo = findComponentWithID(inspector, "encodingCombo");
    auto* autoDetectButton = findComponentWithID(inspector, "autoDetectButton");
    ASSERT_NE(relearnButton, nullptr);
    ASSERT_NE(encodingCombo, nullptr);
    ASSERT_NE(autoDetectButton, nullptr);
    EXPECT_FALSE(relearnButton->isVisible());
    EXPECT_FALSE(encodingCombo->isVisible());
    EXPECT_FALSE(autoDetectButton->isVisible());

    EXPECT_EQ(findComponentWithID(inspector, "assignmentRow0"), nullptr) << "no assignment rows in the empty state";
}

//==============================================================================
// Message spec / button mode display (docs/control/midi-remote.md's MessageSpec example)
//==============================================================================

TEST_F(ControlInspectorComponentTest, MessageSpecAndButtonModeRenderReadableText) {
    inspector.setControl(makeModel({}));

    auto* messageLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "controlMessageSpecLabel"));
    ASSERT_NE(messageLabel, nullptr);
    EXPECT_EQ(messageLabel->getText(), "CC 21 ch 1");

    auto* modeLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "buttonModeLabel"));
    ASSERT_NE(modeLabel, nullptr);
    EXPECT_EQ(modeLabel->getText(), "Momentary");
}

//==============================================================================
// Project (parameter-target) row: scope tag, drives label, working takeover/range/invert.
//==============================================================================

TEST_F(ControlInspectorComponentTest, ProjectRowRendersScopeAndDrivesAndLocatesOnClick) {
    inspector.setControl(makeModel({makeProjectRow()}));

    auto* scopeLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentScopeLabel0"));
    ASSERT_NE(scopeLabel, nullptr);
    EXPECT_EQ(scopeLabel->getText(), "Project");

    auto* drivesLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentDrivesLabel0"));
    ASSERT_NE(drivesLabel, nullptr);
    EXPECT_EQ(drivesLabel->getText(), "Filter . Cutoff");

    juce::String locatedUuid;
    int locateCount = 0;
    inspector.onLocateRequested = [&](const juce::String& uuid) {
        locatedUuid = uuid;
        ++locateCount;
    };

    // juce::Label::mouseUp is protected; call through the juce::Component base (public there),
    // relying on virtual dispatch to still reach ClickableLabel's real override -- the "test the
    // real mouse path" convention, not a direct private-method call.
    static_cast<juce::Component*>(drivesLabel)->mouseUp(realClickEvent(*drivesLabel));

    EXPECT_EQ(locateCount, 1);
    EXPECT_EQ(locatedUuid, "node-uuid-1");
}

TEST_F(ControlInspectorComponentTest, ProjectRowTakeoverRangeAndInvertAreEnabled) {
    inspector.setControl(makeModel({makeProjectRow()}));

    auto* takeoverCombo = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo0"));
    ASSERT_NE(takeoverCombo, nullptr);
    EXPECT_TRUE(takeoverCombo->isEnabled());

    auto* rangeMin = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMinEditor0"));
    auto* rangeMax = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMaxEditor0"));
    ASSERT_NE(rangeMin, nullptr);
    ASSERT_NE(rangeMax, nullptr);
    EXPECT_TRUE(rangeMin->isEnabled());
    EXPECT_TRUE(rangeMax->isEnabled());

    auto* invertToggle = dynamic_cast<juce::ToggleButton*>(findComponentWithID(inspector, "assignmentInvertToggle0"));
    ASSERT_NE(invertToggle, nullptr);
    EXPECT_TRUE(invertToggle->isEnabled());
}

// FRO136: the Default item names what the Preferences default currently is, and follows it.
TEST_F(ControlInspectorComponentTest, DefaultTakeoverItemNamesThePreferencesDefaultAndFollowsIt) {
    auto takeoverCombo = [&] {
        return dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo0"));
    };
    inspector.setControl(makeModel({makeProjectRow()}));
    ASSERT_NE(takeoverCombo(), nullptr);
    EXPECT_EQ(takeoverCombo()->getItemText(0), "Default (Scale)");

    inspector.setDefaultTakeover(synth::Takeover::jump);
    ASSERT_NE(takeoverCombo(), nullptr);
    EXPECT_EQ(takeoverCombo()->getItemText(0), "Default (Jump)");
    EXPECT_EQ(takeoverCombo()->getSelectedId(), 1) << "the assignment is still on Default";

    inspector.setDefaultTakeover(synth::Takeover::pickup);
    EXPECT_EQ(takeoverCombo()->getItemText(0), "Default (Pick-up)");
}

//==============================================================================
// Global (action-target) row: takeover/range/invert DISABLED, Forget still fires.
//==============================================================================

TEST_F(ControlInspectorComponentTest, GlobalRowDisablesTakeoverRangeInvertButForgetStillWorks) {
    inspector.setControl(makeModel({makeGlobalRow()}));

    auto* scopeLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentScopeLabel0"));
    ASSERT_NE(scopeLabel, nullptr);
    EXPECT_EQ(scopeLabel->getText(), "Global");

    auto* takeoverCombo = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo0"));
    auto* rangeMin = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMinEditor0"));
    auto* rangeMax = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMaxEditor0"));
    auto* invertToggle = dynamic_cast<juce::ToggleButton*>(findComponentWithID(inspector, "assignmentInvertToggle0"));
    ASSERT_NE(takeoverCombo, nullptr);
    ASSERT_NE(rangeMin, nullptr);
    ASSERT_NE(rangeMax, nullptr);
    ASSERT_NE(invertToggle, nullptr);
    EXPECT_FALSE(takeoverCombo->isEnabled());
    EXPECT_FALSE(rangeMin->isEnabled());
    EXPECT_FALSE(rangeMax->isEnabled());
    EXPECT_FALSE(invertToggle->isEnabled());

    auto* forgetButton = dynamic_cast<juce::TextButton*>(findComponentWithID(inspector, "assignmentForgetButton0"));
    ASSERT_NE(forgetButton, nullptr);
    EXPECT_TRUE(forgetButton->isEnabled()) << "Forget must still work on a global row";

    juce::String forgottenId;
    int forgetCount = 0;
    inspector.onForgetRequested = [&](const juce::String& id) {
        forgottenId = id;
        ++forgetCount;
    };
    forgetButton->onClick();

    EXPECT_EQ(forgetCount, 1);
    EXPECT_EQ(forgottenId, "assign-global");
}

// FRO253's Solo (node command) row: Project scope like a parameter row, but takeover/range/invert
// disabled like a Global row (isTakeoverEditable() is parameter-only) and Forget still works.
TEST_F(ControlInspectorComponentTest, NodeCommandRowIsProjectScopedButDisablesTakeoverLikeGlobal) {
    inspector.setControl(makeModel({makeNodeCommandRow()}));

    auto* scopeLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentScopeLabel0"));
    ASSERT_NE(scopeLabel, nullptr);
    EXPECT_EQ(scopeLabel->getText(), "Project");

    auto* drivesLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentDrivesLabel0"));
    ASSERT_NE(drivesLabel, nullptr);
    EXPECT_EQ(drivesLabel->getText(), "Kick . Solo");

    auto* takeoverCombo = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo0"));
    auto* rangeMin = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMinEditor0"));
    auto* rangeMax = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMaxEditor0"));
    auto* invertToggle = dynamic_cast<juce::ToggleButton*>(findComponentWithID(inspector, "assignmentInvertToggle0"));
    ASSERT_NE(takeoverCombo, nullptr);
    ASSERT_NE(rangeMin, nullptr);
    ASSERT_NE(rangeMax, nullptr);
    ASSERT_NE(invertToggle, nullptr);
    EXPECT_FALSE(takeoverCombo->isEnabled());
    EXPECT_FALSE(rangeMin->isEnabled());
    EXPECT_FALSE(rangeMax->isEnabled());
    EXPECT_FALSE(invertToggle->isEnabled());

    auto* forgetButton = dynamic_cast<juce::TextButton*>(findComponentWithID(inspector, "assignmentForgetButton0"));
    ASSERT_NE(forgetButton, nullptr);
    EXPECT_TRUE(forgetButton->isEnabled()) << "Forget must still work on a node command row";

    juce::String forgottenId;
    int forgetCount = 0;
    inspector.onForgetRequested = [&](const juce::String& id) {
        forgottenId = id;
        ++forgetCount;
    };
    forgetButton->onClick();

    EXPECT_EQ(forgetCount, 1);
    EXPECT_EQ(forgottenId, "assign-solo");

    juce::String locatedUuid;
    inspector.onLocateRequested = [&](const juce::String& uuid) { locatedUuid = uuid; };
    static_cast<juce::Component*>(drivesLabel)->mouseUp(realClickEvent(*drivesLabel));
    EXPECT_EQ(locatedUuid, "node-uuid-2") << "click-to-locate works the same as a parameter row";
}

//==============================================================================
// Both a Project and a Global assignment: two distinct blocks, each in its own scope.
//==============================================================================

TEST_F(ControlInspectorComponentTest, ProjectAndGlobalRenderAsTwoDistinctBlocks) {
    inspector.setControl(makeModel({makeProjectRow(), makeGlobalRow()}));

    auto* scope0 = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentScopeLabel0"));
    auto* scope1 = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "assignmentScopeLabel1"));
    ASSERT_NE(scope0, nullptr);
    ASSERT_NE(scope1, nullptr);
    EXPECT_EQ(scope0->getText(), "Project");
    EXPECT_EQ(scope1->getText(), "Global");

    auto* row0 = findComponentWithID(inspector, "assignmentRow0");
    auto* row1 = findComponentWithID(inspector, "assignmentRow1");
    ASSERT_NE(row0, nullptr);
    ASSERT_NE(row1, nullptr);
    EXPECT_NE(row0, row1);

    auto* takeover1 = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo1"));
    ASSERT_NE(takeover1, nullptr);
    EXPECT_FALSE(takeover1->isEnabled()) << "the Global row's takeover stays disabled even alongside a Project row";
}

//==============================================================================
// Editing takeover fires onAssignmentEdited with everything else unchanged.
//==============================================================================

TEST_F(ControlInspectorComponentTest, EditingTakeoverFiresOnAssignmentEditedWithOnlyTakeoverChanged) {
    inspector.setControl(makeModel({makeProjectRow()}));

    AssignmentEditRecorder recorder;
    inspector.onAssignmentEdited = recorder.callback();

    auto* takeoverCombo = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "assignmentTakeoverCombo0"));
    ASSERT_NE(takeoverCombo, nullptr);
    takeoverCombo->setSelectedId(3 /* Pick-up */, juce::sendNotificationSync);

    ASSERT_EQ(recorder.callCount, 1);
    EXPECT_EQ(recorder.last.takeover, synth::Takeover::pickup);
    EXPECT_EQ(recorder.last.id, "assign-project");
    EXPECT_DOUBLE_EQ(recorder.last.range.min, 0.0);
    EXPECT_DOUBLE_EQ(recorder.last.range.max, 1.0);
    EXPECT_EQ(recorder.last.target.kind, synth::Target::Kind::parameter);
    EXPECT_EQ(recorder.last.target.parameter.nodeUuid, "node-uuid-1");
}

//==============================================================================
// Toggling invert fires onAssignmentEdited with range.min > range.max.
//==============================================================================

TEST_F(ControlInspectorComponentTest, TogglingInvertFiresOnAssignmentEditedWithMinAboveMax) {
    inspector.setControl(makeModel({makeProjectRow()})); // range starts min=0.0, max=1.0

    AssignmentEditRecorder recorder;
    inspector.onAssignmentEdited = recorder.callback();

    auto* invertToggle = dynamic_cast<juce::ToggleButton*>(findComponentWithID(inspector, "assignmentInvertToggle0"));
    ASSERT_NE(invertToggle, nullptr);
    invertToggle->onClick();

    ASSERT_EQ(recorder.callCount, 1);
    EXPECT_GT(recorder.last.range.min, recorder.last.range.max);
    EXPECT_DOUBLE_EQ(recorder.last.range.min, 1.0);
    EXPECT_DOUBLE_EQ(recorder.last.range.max, 0.0);
    EXPECT_EQ(recorder.last.id, "assign-project");
}

//==============================================================================
// Editing range min/max (Enter commit) fires onAssignmentEdited with the new values.
//==============================================================================

TEST_F(ControlInspectorComponentTest, CommittingRangeEditorsFiresOnAssignmentEditedWithNewRange) {
    inspector.setControl(makeModel({makeProjectRow()}));

    AssignmentEditRecorder recorder;
    inspector.onAssignmentEdited = recorder.callback();

    auto* rangeMin = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMinEditor0"));
    auto* rangeMax = dynamic_cast<juce::TextEditor*>(findComponentWithID(inspector, "assignmentRangeMaxEditor0"));
    ASSERT_NE(rangeMin, nullptr);
    ASSERT_NE(rangeMax, nullptr);

    rangeMin->setText("25", juce::dontSendNotification);
    rangeMin->onReturnKey();

    ASSERT_EQ(recorder.callCount, 1);
    EXPECT_NEAR(recorder.last.range.min, 0.25, 1e-9);
    EXPECT_DOUBLE_EQ(recorder.last.range.max, 1.0);

    rangeMax->setText("75", juce::dontSendNotification);
    rangeMax->onReturnKey();

    ASSERT_EQ(recorder.callCount, 2);
    EXPECT_NEAR(recorder.last.range.min, 0.25, 1e-9);
    EXPECT_NEAR(recorder.last.range.max, 0.75, 1e-9);
}

//==============================================================================
// Relearn stays inert; FRO134 made name, kind, encoding and Auto-detect... live.
//==============================================================================

TEST_F(ControlInspectorComponentTest, RelearnIsStillDisabled) {
    inspector.setControl(makeModel({}));
    auto* relearn = dynamic_cast<juce::TextButton*>(findComponentWithID(inspector, "relearnButton"));
    ASSERT_NE(relearn, nullptr);
    EXPECT_FALSE(relearn->isEnabled());
}

TEST_F(ControlInspectorComponentTest, EncodingComboEditsTheControlThroughOnControlEdited) {
    inspector.setControl(makeModel({}));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);
    EXPECT_TRUE(encoding->isEnabled());
    encoding->setSelectedId(3, juce::sendNotificationSync); // relBinOffset (declaration order + 1)

    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].encoding, synth::Encoding::relBinOffset);
    EXPECT_EQ(edits[0].id, "control-1");

    encoding->setSelectedId(3, juce::sendNotificationSync);
    EXPECT_EQ(edits.size(), 1u) << "re-selecting the current encoding is not an edit";
}

TEST_F(ControlInspectorComponentTest, KindComboRetypesTheControl) {
    inspector.setControl(makeModel({}));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* kind = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "controlKindCombo"));
    ASSERT_NE(kind, nullptr);
    EXPECT_TRUE(kind->isVisible());
    kind->setSelectedId(static_cast<int>(synth::ControlKind::fader) + 1, juce::sendNotificationSync);

    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].kind, synth::ControlKind::fader);
}

// FRO141 (docs/control/midi-remote.md#focus-bank): the "Follow selection (focus bank)" toggle.
TEST_F(ControlInspectorComponentTest, FocusBankToggleFlipsTheControlAndFiresOnControlEdited) {
    inspector.setControl(makeModel({}));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* toggle = dynamic_cast<juce::ToggleButton*>(findComponentWithID(inspector, "focusBankToggle"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_FALSE(toggle->getToggleState()) << "the fixture's control starts with focusBank == false";

    // A real click can't be completed headlessly (the same reason AssignmentRow's own Invert
    // toggle is driven this way, per this file's header comment) -- call the button's own onClick
    // directly.
    toggle->onClick();

    ASSERT_EQ(edits.size(), 1u);
    EXPECT_TRUE(edits[0].focusBank);
    EXPECT_EQ(edits[0].id, "control-1");
    EXPECT_TRUE(toggle->getToggleState()) << "the widget itself reflects the new state too";

    toggle->onClick();
    ASSERT_EQ(edits.size(), 2u);
    EXPECT_FALSE(edits[1].focusBank) << "a second click flips it back off";
}

TEST_F(ControlInspectorComponentTest, NameLabelCommitsARenameButRejectsAnEmptyName) {
    inspector.setControl(makeModel({}));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* name = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "controlNameLabel"));
    ASSERT_NE(name, nullptr);
    EXPECT_TRUE(name->isEditable());

    name->setText("  Cutoff  ", juce::sendNotificationSync);
    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].name, "Cutoff");

    name->setText("   ", juce::sendNotificationSync);
    EXPECT_EQ(edits.size(), 1u);
    EXPECT_EQ(name->getText(), "Cutoff") << "an empty rename reverts";
}

TEST_F(ControlInspectorComponentTest, AutoDetectIsEnabledOnlyForACcKnobOrEncoder) {
    auto* autoDetect = dynamic_cast<juce::TextButton*>(findComponentWithID(inspector, "autoDetectButton"));
    ASSERT_NE(autoDetect, nullptr);

    inspector.setControl(makeModel({}));
    EXPECT_TRUE(autoDetect->isEnabled()) << "a CC knob";

    ControlInspectorComponent::ControlModel pad;
    pad.hasControl = true;
    pad.control = makeControl();
    pad.control.kind = synth::ControlKind::pad;
    inspector.setControl(pad);
    EXPECT_FALSE(autoDetect->isEnabled());

    ControlInspectorComponent::ControlModel note;
    note.hasControl = true;
    note.control = makeControl();
    note.control.message.type = synth::MessageType::note;
    inspector.setControl(note);
    EXPECT_FALSE(autoDetect->isEnabled()) << "a note is not an encoder";

    ControlInspectorComponent::ControlModel encoder;
    encoder.hasControl = true;
    encoder.control = makeControl();
    encoder.control.kind = synth::ControlKind::encoder;
    inspector.setControl(encoder);
    EXPECT_TRUE(autoDetect->isEnabled());

    int requested = 0;
    inspector.onAutoDetectRequested = [&](const synth::Control& c) {
        ++requested;
        EXPECT_EQ(c.id, "control-1");
    };
    autoDetect->onClick();
    EXPECT_EQ(requested, 1);
}

//==============================================================================
// 14-bit and NRPN encodings (FRO140)
//==============================================================================

namespace {

std::vector<std::pair<int, juce::String>> comboItems(const juce::ComboBox& combo) {
    std::vector<std::pair<int, juce::String>> items;
    for (int i = 0; i < combo.getNumItems(); ++i)
        items.emplace_back(combo.getItemId(i), combo.getItemText(i));
    return items;
}

ControlInspectorComponent::ControlModel modelFor(synth::MessageType type, int number, synth::Encoding encoding) {
    ControlInspectorComponent::ControlModel model;
    model.hasControl = true;
    model.control = makeControl();
    model.control.message = {type, 1, number};
    model.control.encoding = encoding;
    return model;
}

using ItemList = std::vector<std::pair<int, juce::String>>;

const ItemList kFourOriginalItems = {{1, "Absolute (7-bit)"},
                                     {2, "Relative (Two's Complement)"},
                                     {3, "Relative (Binary Offset)"},
                                     {4, "Relative (Sign Magnitude)"}};

} // namespace

TEST_F(ControlInspectorComponentTest, LowNumberedCcOffersTheTwoFourteenBitEncodings) {
    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs7));
    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);

    auto expected = kFourOriginalItems;
    expected.emplace_back(5, "Absolute (14-bit, MSB first)");
    expected.emplace_back(6, "Absolute (14-bit, LSB first)");
    EXPECT_EQ(comboItems(*encoding), expected);
    EXPECT_EQ(encoding->getSelectedId(), 1);
}

TEST_F(ControlInspectorComponentTest, CcAtOrAbove32OffersOnlyTheFourOriginalEncodings) {
    inspector.setControl(modelFor(synth::MessageType::cc, 32, synth::Encoding::abs7));
    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);
    EXPECT_EQ(comboItems(*encoding), kFourOriginalItems) << "CC 32 has no LSB partner at 64";

    inspector.setControl(modelFor(synth::MessageType::cc, 100, synth::Encoding::relTwos));
    EXPECT_EQ(comboItems(*encoding), kFourOriginalItems);
    EXPECT_EQ(encoding->getSelectedId(), 2);
}

TEST_F(ControlInspectorComponentTest, NonCcNonNrpnTypesOfferOnlyTheFourOriginalEncodings) {
    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);
    inspector.setControl(modelFor(synth::MessageType::note, 10, synth::Encoding::abs7));
    EXPECT_EQ(comboItems(*encoding), kFourOriginalItems);
    inspector.setControl(modelFor(synth::MessageType::pitchBend, 0, synth::Encoding::abs7));
    EXPECT_EQ(comboItems(*encoding), kFourOriginalItems);
}

TEST_F(ControlInspectorComponentTest, NrpnControlOffersOnlyNrpnNamedChoicesWithEnumDerivedIds) {
    inspector.setControl(modelFor(synth::MessageType::nrpn, 1024, synth::Encoding::abs14LsbFirst));
    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);

    const ItemList expected = {
        {1, "NRPN (7-bit, CC 6 only)"}, {5, "NRPN (14-bit, MSB first)"}, {6, "NRPN (14-bit, LSB first)"}};
    EXPECT_EQ(comboItems(*encoding), expected);
    EXPECT_EQ(encoding->getSelectedId(), 6);
}

TEST_F(ControlInspectorComponentTest, ComboIsRepopulatedWhenTheSelectionChangesBetweenControlKinds) {
    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);

    inspector.setControl(modelFor(synth::MessageType::nrpn, 1024, synth::Encoding::abs14));
    EXPECT_EQ(encoding->getNumItems(), 3);
    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs14));
    EXPECT_EQ(encoding->getNumItems(), 6);
    EXPECT_EQ(encoding->getSelectedId(), 5);
    inspector.setControl(modelFor(synth::MessageType::note, 60, synth::Encoding::abs7));
    EXPECT_EQ(encoding->getNumItems(), 4);
}

TEST_F(ControlInspectorComponentTest, NrpnMessageSpecLabelShowsTheAddressAndChannel) {
    inspector.setControl(modelFor(synth::MessageType::nrpn, 1024, synth::Encoding::abs14));
    auto* messageLabel = dynamic_cast<juce::Label*>(findComponentWithID(inspector, "controlMessageSpecLabel"));
    ASSERT_NE(messageLabel, nullptr);
    EXPECT_EQ(messageLabel->getText(), "NRPN 1024 ch 1");
}

TEST_F(ControlInspectorComponentTest, ChoosingFourteenBitMsbFirstFiresOnControlEditedWithAbs14) {
    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs7));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);
    int msbFirstId = 0;
    for (const auto& [id, text] : comboItems(*encoding))
        if (text == "Absolute (14-bit, MSB first)")
            msbFirstId = id;
    ASSERT_NE(msbFirstId, 0);
    encoding->setSelectedId(msbFirstId, juce::sendNotificationSync);

    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].encoding, synth::Encoding::abs14);
    EXPECT_EQ(edits[0].message.number, 21) << "only the encoding changes";
    EXPECT_EQ(edits[0].id, "control-1");
}

TEST_F(ControlInspectorComponentTest, ChoosingNrpnLsbFirstFiresOnControlEditedWithAbs14LsbFirst) {
    inspector.setControl(modelFor(synth::MessageType::nrpn, 1024, synth::Encoding::abs14));
    std::vector<synth::Control> edits;
    inspector.onControlEdited = [&](const synth::Control& c) { edits.push_back(c); };

    auto* encoding = dynamic_cast<juce::ComboBox*>(findComponentWithID(inspector, "encodingCombo"));
    ASSERT_NE(encoding, nullptr);
    encoding->setSelectedId(6, juce::sendNotificationSync);

    ASSERT_EQ(edits.size(), 1u);
    EXPECT_EQ(edits[0].encoding, synth::Encoding::abs14LsbFirst);
    EXPECT_EQ(edits[0].message.type, synth::MessageType::nrpn);
}

TEST_F(ControlInspectorComponentTest, AutoDetectIsDisabledForAPairedEncodingControl) {
    auto* autoDetect = dynamic_cast<juce::TextButton*>(findComponentWithID(inspector, "autoDetectButton"));
    ASSERT_NE(autoDetect, nullptr);

    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs7));
    EXPECT_TRUE(autoDetect->isEnabled());
    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs14));
    EXPECT_FALSE(autoDetect->isEnabled());
    inspector.setControl(modelFor(synth::MessageType::cc, 21, synth::Encoding::abs14LsbFirst));
    EXPECT_FALSE(autoDetect->isEnabled());
}
