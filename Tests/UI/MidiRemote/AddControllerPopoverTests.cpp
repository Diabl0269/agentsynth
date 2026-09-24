// AddControllerPopoverTests.cpp -- FRO134 (docs/control/midi-remote-ui.md#add-controller): the
// "+ Add controller" popover as a pure view over an injected device list -- deterministic, no
// juce::MidiInput. Suite names contain "MidiRemote" per the ship-task --gtest_filter convention.
#include "UI/MidiRemote/AddController/AddControllerPopover.h"

#include <gtest/gtest.h>

using synth::ui::AddControllerPopover;

namespace {

juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findById(*child, id))
            return found;
    return nullptr;
}

std::vector<AddControllerPopover::DeviceRow> devices() {
    return {{"id-a", "Launchkey Mini", "Launchkey"}, {"id-b", "nanoKONTROL2", ""}, {"id-c", "Arturia KeyStep", ""}};
}

std::vector<synth::midi::TemplateInfo> templates() {
    return {{"template-8-knobs", "8 knobs"}, {"template-transport-strip", "Transport strip"}};
}

} // namespace

TEST(MidiRemoteAddControllerPopoverTest, DevicesWithAProfileAreGreyedAndNamedByTheirProfile) {
    AddControllerPopover popover(devices(), templates());
    auto* combo = dynamic_cast<juce::ComboBox*>(findById(popover, "addControllerDeviceCombo"));
    ASSERT_NE(combo, nullptr);

    EXPECT_EQ(combo->getNumItems(), 3);
    EXPECT_EQ(combo->getItemText(0), "Launchkey Mini (Launchkey)");
    EXPECT_FALSE(combo->isItemEnabled(1)) << "already has a profile";
    EXPECT_TRUE(combo->isItemEnabled(2));
    EXPECT_TRUE(combo->isItemEnabled(3));
}

TEST(MidiRemoteAddControllerPopoverTest, FirstFreeDeviceIsPreselectedAndItsNamePrefilled) {
    AddControllerPopover popover(devices(), templates());
    auto* name = dynamic_cast<juce::TextEditor*>(findById(popover, "addControllerNameEditor"));
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->getText(), "nanoKONTROL2");
    EXPECT_TRUE(popover.canConfirm());
    EXPECT_EQ(popover.getChoice().deviceIdentifier, "id-b");
}

TEST(MidiRemoteAddControllerPopoverTest, StartWithDefaultsToDetectAndOffersTemplatesAndEmpty) {
    AddControllerPopover popover(devices(), templates());
    auto* start = dynamic_cast<juce::ComboBox*>(findById(popover, "addControllerStartCombo"));
    ASSERT_NE(start, nullptr);

    EXPECT_EQ(popover.getChoice().startWith, AddControllerPopover::StartWith::detect);
    EXPECT_EQ(start->getNumItems(), 4) << "Detect, two templates, Empty";
    EXPECT_EQ(start->getItemText(0), "Detect controls now");

    start->setSelectedItemIndex(2, juce::sendNotificationSync);
    auto choice = popover.getChoice();
    EXPECT_EQ(choice.startWith, AddControllerPopover::StartWith::templateLayout);
    EXPECT_EQ(choice.templateId, "template-transport-strip");

    start->setSelectedItemIndex(3, juce::sendNotificationSync);
    EXPECT_EQ(popover.getChoice().startWith, AddControllerPopover::StartWith::empty);
}

TEST(MidiRemoteAddControllerPopoverTest, ChangingDeviceReprefillsTheNameUntilTheUserTypesOwn) {
    AddControllerPopover popover(devices(), templates());
    auto* combo = dynamic_cast<juce::ComboBox*>(findById(popover, "addControllerDeviceCombo"));
    auto* name = dynamic_cast<juce::TextEditor*>(findById(popover, "addControllerNameEditor"));

    combo->setSelectedId(3, juce::sendNotificationSync);
    EXPECT_EQ(name->getText(), "Arturia KeyStep");

    name->setText("My keys", false);
    name->onTextChange(); // TextEditor posts its change notification asynchronously; drive it directly
    combo->setSelectedId(2, juce::sendNotificationSync);
    EXPECT_EQ(name->getText(), "My keys") << "a typed name is not clobbered";
    EXPECT_EQ(popover.getChoice().deviceIdentifier, "id-b");
    EXPECT_EQ(popover.getChoice().profileName, "My keys");
}

TEST(MidiRemoteAddControllerPopoverTest, OkIsDisabledWithoutADeviceOrAName) {
    AddControllerPopover none({}, templates());
    EXPECT_FALSE(none.canConfirm()) << "no MIDI input devices";

    AddControllerPopover popover(devices(), templates());
    auto* name = dynamic_cast<juce::TextEditor*>(findById(popover, "addControllerNameEditor"));
    auto* ok = dynamic_cast<juce::TextButton*>(findById(popover, "addControllerOk"));
    ASSERT_NE(ok, nullptr);
    EXPECT_TRUE(ok->isEnabled());
    name->setText("   ", false);
    name->onTextChange();
    EXPECT_FALSE(popover.canConfirm());
    EXPECT_FALSE(ok->isEnabled());
}

TEST(MidiRemoteAddControllerPopoverTest, OkReportsTheChoiceAndCancelReportsCancel) {
    AddControllerPopover popover(devices(), templates());
    std::vector<AddControllerPopover::Choice> confirmed;
    int cancelled = 0;
    popover.onConfirmed = [&](const AddControllerPopover::Choice& c) { confirmed.push_back(c); };
    popover.onCancelled = [&] { ++cancelled; };

    dynamic_cast<juce::TextButton*>(findById(popover, "addControllerOk"))->onClick();
    ASSERT_EQ(confirmed.size(), 1u);
    EXPECT_EQ(confirmed[0].deviceIdentifier, "id-b");
    EXPECT_EQ(confirmed[0].deviceName, "nanoKONTROL2");
    EXPECT_EQ(confirmed[0].profileName, "nanoKONTROL2");
    EXPECT_EQ(confirmed[0].startWith, AddControllerPopover::StartWith::detect);

    dynamic_cast<juce::TextButton*>(findById(popover, "addControllerCancel"))->onClick();
    EXPECT_EQ(cancelled, 1);
    EXPECT_EQ(confirmed.size(), 1u);
}
