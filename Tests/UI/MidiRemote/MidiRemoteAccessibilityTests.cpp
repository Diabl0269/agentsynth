// MidiRemoteAccessibilityTests.cpp -- what a screen reader hears from the MIDI Remote panel's
// Controllers list (a list whose value follows the selected controller) and from the side-pane
// toggle button, which is also a keyboard Tab stop. Headless: the handler is built directly with
// createAccessibilityHandler().
#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include "UI/MidiRemote/ControllersList/ControllersListComponent.h"
#include <gtest/gtest.h>

using synth::ui::ControllersListComponent;

namespace {

ControllersListComponent::RowModel
row(const juce::String& id, const juce::String& name,
    ControllersListComponent::RowState state = ControllersListComponent::RowState::present) {
    ControllersListComponent::RowModel model;
    model.profileId = id;
    model.name = name;
    model.state = state;
    return model;
}

} // namespace

TEST(MidiRemoteAccessibilityTest, ControllersListIsANamedListReadingTheSelectedController) {
    ControllersListComponent list;
    list.setSize(220, 300);
    list.setRows({row("a", "Launchpad"), row("b", "Keystep", ControllersListComponent::RowState::orphan)});
    EXPECT_EQ(list.getTitle(), "Controllers");

    const auto handler = list.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::list);
    auto* value = handler->getValueInterface();
    ASSERT_NE(value, nullptr);

    EXPECT_EQ(value->getCurrentValueAsString(), "2 controllers");
    list.setSelectedProfileId("b");
    EXPECT_EQ(value->getCurrentValueAsString(), "Keystep (not on this machine), 2 of 2");
    list.setSelectedProfileId("a");
    EXPECT_EQ(value->getCurrentValueAsString(), "Launchpad, 1 of 2");
}

TEST(MidiRemoteAccessibilityTest, SidePaneToggleIsANamedTooltippedTabStop) {
    synth::ui::SidePaneToggleButton button;
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(button.getTitle(), "Side pane");
    EXPECT_TRUE(button.getTooltip().isNotEmpty());
}
