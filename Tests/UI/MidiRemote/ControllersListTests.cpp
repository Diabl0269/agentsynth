// ControllersListTests.cpp -- FRO131 (docs/control/midi-remote-ui.md#controllers-list-left):
// headless tests for ControllersListComponent, driven through its REAL mouseDown() override with
// synthesized juce::MouseEvents -- the "test the real mouse path" convention (Source/UI/CLAUDE.md,
// Tests/UI/Mixer/MixerColumnComponentTests.cpp / Tests/UI/Graph/ModuleComponent/
// ModuleComponentMidiLearnTests.cpp's template) -- never a direct private-method call.
//
// The right-click menu and the Rename/Delete prompts route through three free-function test hooks
// (synth::ui::test_hooks::contextMenuHookForTest/renamePromptHookForTest/deleteConfirmHookForTest,
// defined with external linkage in ControllersListComponent.cpp) rather than hook MEMBERS --
// ControllersListComponent.h's contract is locked for this ticket, so this file forward-declares
// the same signatures instead of the class exposing them. contextMenuHookForTest mirrors
// ModuleComponent::setShowContextMenuHookForTest for the same reason: a real
// juce::PopupMenu::showMenuAsync() (and a real juce::AlertWindow) segfaults on a headless Linux CI
// runner with no display.
#include "UI/MidiRemote/ControllersList/ControllersListComponent.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <vector>

namespace synth::ui::test_hooks {
std::function<void(juce::PopupMenu&)>& contextMenuHookForTest();
std::function<void(const juce::String& currentName, std::function<void(const juce::String& resultText)> onChoice)>&
renamePromptHookForTest();
std::function<void(const juce::String& message, std::function<void(int result)> onChoice)>& deleteConfirmHookForTest();
} // namespace synth::ui::test_hooks

using synth::ui::ControllersListComponent;
using RowModel = ControllersListComponent::RowModel;
using RowState = ControllersListComponent::RowState;

namespace {

std::vector<RowModel> makeRows() {
    return {
        {"profile-a", "Launchkey Mini", RowState::present},
        {"profile-b", "nanoKONTROL2", RowState::absent},
        {"profile-c", "Old Controller", RowState::orphan},
    };
}

juce::MouseEvent mouseEventAt(juce::Component& component, juce::Point<int> position, juce::ModifierKeys mods) {
    const auto posF = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), posF, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &component, &component, juce::Time::getCurrentTime(), posF, juce::Time::getCurrentTime(), 1,
                            false);
}

juce::Point<int> rowPoint(int rowIndex) {
    return {10, rowIndex * ControllersListComponent::kRowHeight + ControllersListComponent::kRowHeight / 2};
}

// Real mouseDown() dispatch at a given row's on-screen position -- ControllersListComponent
// self-paints every row rather than parenting a child per row, so (unlike a per-row-child
// component) the real listener path IS just calling mouseDown() on the list itself.
void clickRow(ControllersListComponent& list, int rowIndex, bool rightClick) {
    const auto mods = juce::ModifierKeys(rightClick ? juce::ModifierKeys::rightButtonModifier
                                                    : juce::ModifierKeys::leftButtonModifier);
    list.mouseDown(mouseEventAt(list, rowPoint(rowIndex), mods));
}

std::vector<juce::String> menuItemTexts(const juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text.isNotEmpty())
            texts.push_back(it.getItem().text);
    return texts;
}

bool triggerMenuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

} // namespace

class ControllersListComponentTest : public ::testing::Test {
protected:
    void SetUp() override { clearHooks(); }
    void TearDown() override { clearHooks(); }

private:
    // Hooks are process-wide statics (see the file comment) -- clear them on both sides of every
    // test so a stub installed here can never leak into a later test sharing the process.
    static void clearHooks() {
        synth::ui::test_hooks::contextMenuHookForTest() = nullptr;
        synth::ui::test_hooks::renamePromptHookForTest() = nullptr;
        synth::ui::test_hooks::deleteConfirmHookForTest() = nullptr;
    }
};

// ============================================================================
// 1. setRows / setSelectedProfileId
// ============================================================================

TEST_F(ControllersListComponentTest, SetRowsRendersExactRowCountAndSelectionIsQueryable) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    list.setSelectedProfileId("profile-b");
    EXPECT_EQ(list.getSelectedProfileId(), "profile-b");

    // Row 2 (the last of the 3 setRows() provided) is clickable and resolves to profile-c; one
    // row further (index 3) is out of range and fires nothing -- proving exactly 3 rows exist.
    juce::String selected;
    list.onSelectProfile = [&](const juce::String& id) { selected = id; };
    clickRow(list, 2, false);
    EXPECT_EQ(selected, "profile-c");

    selected.clear();
    clickRow(list, 3, false);
    EXPECT_TRUE(selected.isEmpty());
}

// ============================================================================
// 2. Clicking a row fires onSelectProfile and highlights it
// ============================================================================

TEST_F(ControllersListComponentTest, ClickingRowFiresOnSelectProfileAndHighlightsIt) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::String selectedId;
    int callCount = 0;
    list.onSelectProfile = [&](const juce::String& id) {
        selectedId = id;
        ++callCount;
    };

    clickRow(list, 1, false);
    EXPECT_EQ(selectedId, "profile-b");
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(list.getSelectedProfileId(), "profile-b");
}

// An orphan row must still be selectable -- docs/control/midi-remote-ui.md's design and the
// header's own RowState comment: downstream code handles a profile that doesn't resolve.
TEST_F(ControllersListComponentTest, ClickingOrphanRowStillFiresOnSelectProfile) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::String selectedId;
    list.onSelectProfile = [&](const juce::String& id) { selectedId = id; };
    clickRow(list, 2, false); // profile-c is RowState::orphan
    EXPECT_EQ(selectedId, "profile-c");
}

// ============================================================================
// 3. setActivityLit
// ============================================================================

TEST_F(ControllersListComponentTest, SetActivityLitTogglesWithoutCrashing) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    list.setActivityLit("profile-a", true);
    list.setActivityLit("profile-a", false);
    list.setActivityLit("profile-a", false);      // repeat: the "no real change" branch, still no crash
    list.setActivityLit("unknown-profile", true); // unknown id: silently ignored, no crash

    // The component is still fully usable afterwards.
    juce::String selectedId;
    list.onSelectProfile = [&](const juce::String& id) { selectedId = id; };
    clickRow(list, 0, false);
    EXPECT_EQ(selectedId, "profile-a");
}

// ============================================================================
// 4. Right-click Rename / Export / Delete
// ============================================================================

TEST_F(ControllersListComponentTest, RightClickShowsRenameExportDeleteItems) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    clickRow(list, 0, true);
    const auto texts = menuItemTexts(captured);
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Rename"), texts.end());
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Export..."), texts.end());
    EXPECT_NE(std::find(texts.begin(), texts.end(), "Delete..."), texts.end());
}

TEST_F(ControllersListComponentTest, RightClickRenameCommitsThroughPromptHook) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    juce::String promptedCurrentName;
    synth::ui::test_hooks::renamePromptHookForTest() = [&](const juce::String& currentName,
                                                           std::function<void(const juce::String&)> onChoice) {
        promptedCurrentName = currentName;
        onChoice("Launchkey Mini MK3"); // simulate typing a new name and pressing Rename
    };

    juce::String renamedProfileId;
    juce::String renamedTo;
    list.onRenameRequested = [&](const juce::String& profileId, const juce::String& newName) {
        renamedProfileId = profileId;
        renamedTo = newName;
    };

    clickRow(list, 0, true); // row 0 == profile-a "Launchkey Mini"
    ASSERT_TRUE(triggerMenuItem(captured, "Rename"));

    EXPECT_EQ(promptedCurrentName, "Launchkey Mini");
    EXPECT_EQ(renamedProfileId, "profile-a");
    EXPECT_EQ(renamedTo, "Launchkey Mini MK3");
}

TEST_F(ControllersListComponentTest, RightClickRenameDoesNotFireForEmptyOrUnchangedName) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    bool fired = false;
    list.onRenameRequested = [&](const juce::String&, const juce::String&) { fired = true; };

    synth::ui::test_hooks::renamePromptHookForTest() = [](const juce::String& currentName,
                                                          std::function<void(const juce::String&)> onChoice) {
        onChoice(currentName); // "changed" it to the exact same name
    };
    clickRow(list, 0, true);
    ASSERT_TRUE(triggerMenuItem(captured, "Rename"));
    EXPECT_FALSE(fired);

    synth::ui::test_hooks::renamePromptHookForTest() = [](const juce::String&,
                                                          std::function<void(const juce::String&)> onChoice) {
        onChoice("   "); // whitespace-only
    };
    clickRow(list, 0, true);
    ASSERT_TRUE(triggerMenuItem(captured, "Rename"));
    EXPECT_FALSE(fired);
}

TEST_F(ControllersListComponentTest, RightClickExportFiresOnExportRequested) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    juce::String exportedProfileId;
    list.onExportRequested = [&](const juce::String& id) { exportedProfileId = id; };

    clickRow(list, 1, true); // row 1 == profile-b
    ASSERT_TRUE(triggerMenuItem(captured, "Export..."));
    EXPECT_EQ(exportedProfileId, "profile-b");
}

// ============================================================================
// 4 (delete) + 5. Delete confirms using countProjectAssignments, only fires once confirmed
// ============================================================================

TEST_F(ControllersListComponentTest, RightClickDeleteUsesAssignmentCountThenFiresOnceConfirmed) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    juce::String countedProfileId;
    list.countProjectAssignments = [&](const juce::String& id) {
        countedProfileId = id;
        return 3;
    };

    juce::String confirmMessage;
    synth::ui::test_hooks::deleteConfirmHookForTest() = [&](const juce::String& message,
                                                            std::function<void(int)> onChoice) {
        confirmMessage = message;
        onChoice(1); // simulate pressing "Delete"
    };

    juce::String deletedProfileId;
    list.onDeleteConfirmed = [&](const juce::String& id) { deletedProfileId = id; };

    clickRow(list, 0, true); // row 0 == profile-a "Launchkey Mini"
    ASSERT_TRUE(triggerMenuItem(captured, "Delete..."));

    EXPECT_EQ(countedProfileId, "profile-a"); // countProjectAssignments called with the right id
    EXPECT_NE(confirmMessage.indexOf(juce::String(3)), -1);
    EXPECT_NE(confirmMessage.indexOf("assignments"), -1); // plural for count 3
    EXPECT_EQ(deletedProfileId, "profile-a");             // fires only after confirming
}

TEST_F(ControllersListComponentTest, RightClickDeleteDoesNotFireWhenCancelled) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };
    list.countProjectAssignments = [](const juce::String&) { return 0; };
    synth::ui::test_hooks::deleteConfirmHookForTest() = [](const juce::String&, std::function<void(int)> onChoice) {
        onChoice(0); // Cancel
    };

    bool fired = false;
    list.onDeleteConfirmed = [&](const juce::String&) { fired = true; };

    clickRow(list, 0, true);
    ASSERT_TRUE(triggerMenuItem(captured, "Delete..."));
    EXPECT_FALSE(fired);
}

TEST_F(ControllersListComponentTest, DeleteConfirmMessageSingularAndZeroPhrasing) {
    ControllersListComponent list;
    list.setSize(240, 300);
    list.setRows(makeRows());

    juce::PopupMenu captured;
    synth::ui::test_hooks::contextMenuHookForTest() = [&](juce::PopupMenu& menu) { captured = menu; };

    juce::String message;
    synth::ui::test_hooks::deleteConfirmHookForTest() = [&](const juce::String& msg,
                                                            std::function<void(int)> onChoice) {
        message = msg;
        onChoice(0);
    };

    list.countProjectAssignments = [](const juce::String&) { return 1; };
    clickRow(list, 0, true);
    ASSERT_TRUE(triggerMenuItem(captured, "Delete..."));
    EXPECT_NE(message.indexOf("1 project assignment."), -1);
    EXPECT_EQ(message.indexOf("assignments"), -1); // singular, not plural

    list.countProjectAssignments = [](const juce::String&) { return 0; };
    clickRow(list, 0, true);
    ASSERT_TRUE(triggerMenuItem(captured, "Delete..."));
    EXPECT_NE(message.indexOf("no project assignments"), -1);
}
