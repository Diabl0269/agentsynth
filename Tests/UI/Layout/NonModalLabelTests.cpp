// Concern: a juce::Label goes modal while its inline editor is open, and on macOS a modal Label makes the rest of the
// window inaccessible (the accessibility tree collapses to the title bar). Every label edited in place is a
// NonModalLabel, which leaves modal state again right after the editor opens.
#include "UI/Layout/NonModalLabel.h"
#include "UI/Mixer/MixerColumnHeader.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include <gtest/gtest.h>

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

// The condition JUCE's own accessibility code keys on (juce_AccessibilityHandler.cpp): a component blocked by
// another modal component is treated as unavailable.
bool windowIsBlockedByAnEditor(juce::Component& bystander) {
    return juce::Component::getCurrentlyModalComponent() != nullptr ||
           bystander.isCurrentlyBlockedByAnotherModalComponent();
}

struct Host : juce::Component {
    Host() {
        addAndMakeVisible(bystander);
        addAndMakeVisible(label);
        setSize(300, 100);
        bystander.setBounds(0, 0, 100, 40);
        label.setBounds(120, 0, 120, 40);
        label.setText("120.0", juce::dontSendNotification);
    }
    juce::TextButton bystander{"other control"};
    synth::ui::NonModalLabel label;
};

} // namespace

// Documents the JUCE behaviour the class exists to undo, so a JUCE upgrade that changes it is noticed.
TEST(NonModalLabelTest, APlainLabelIsModalWhileItsEditorIsOpen) {
    juce::Component host;
    host.setSize(300, 100);
    juce::TextButton bystander("other control");
    juce::Label label;
    host.addAndMakeVisible(bystander);
    host.addAndMakeVisible(label);
    label.setEditable(false, true, false);
    label.showEditor();
    pump();

    EXPECT_TRUE(windowIsBlockedByAnEditor(bystander));
    label.hideEditor(true);
}

TEST(NonModalLabelTest, TheEditorOpensWithoutBlockingTheRestOfTheWindow) {
    Host host;
    host.label.setEditable(false, true, false);
    host.label.showEditor();
    pump();

    ASSERT_NE(host.label.getCurrentTextEditor(), nullptr) << "the editor itself must stay open";
    EXPECT_FALSE(windowIsBlockedByAnEditor(host.bystander));
    EXPECT_FALSE(host.label.isCurrentlyModal(false));

    host.label.hideEditor(true);
    EXPECT_EQ(host.label.getCurrentTextEditor(), nullptr);
    EXPECT_FALSE(windowIsBlockedByAnEditor(host.bystander));
}

TEST(NonModalLabelTest, CommittingStillAppliesTheEditedText) {
    Host host;
    host.label.setEditable(false, true, false);
    int changes = 0;
    host.label.onTextChange = [&changes] { ++changes; };

    host.label.showEditor();
    pump();
    // No text-change message: a headless editor has no real focus, so the Label would commit at once.
    host.label.getCurrentTextEditor()->setText("96.0", false);
    host.label.hideEditor(false); // false = commit
    EXPECT_EQ(host.label.getText(), "96.0");
    EXPECT_EQ(changes, 1);
}

// Discarding (what Escape asks for) leaves the text alone and no modal state behind. Not driven by a real Escape
// key press: JUCE 8.0.3's TextEditor::handleCommandMessage lets Label::hideEditor delete the editor while its
// listener list is still locked, and valgrind shows the unlock then writing into freed memory (it poisons a
// glibc tcache bin and crashes a later allocation on Linux).
TEST(NonModalLabelTest, DiscardingTheEditWithoutALingeringModalState) {
    Host host;
    host.label.setEditable(false, true, false);
    host.label.showEditor();
    pump();
    host.label.getCurrentTextEditor()->setText("nonsense", false);
    host.label.hideEditor(true); // true = discard
    pump();
    EXPECT_EQ(host.label.getText(), "120.0");
    EXPECT_EQ(host.label.getCurrentTextEditor(), nullptr);
    EXPECT_FALSE(windowIsBlockedByAnEditor(host.bystander));
}

TEST(NonModalLabelTest, ClosingTheEditorBeforeTheDeferredExitRunsIsSafe) {
    Host host;
    host.label.setEditable(false, true, false);
    host.label.showEditor();
    host.label.hideEditor(true); // before the message loop ever ran the deferred exit
    pump();
    EXPECT_FALSE(windowIsBlockedByAnEditor(host.bystander));
}

TEST(NonModalLabelTest, TransportBarLabelsDoNotBlockTheWindow) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 34);
    for (auto* label : {&bar.getBpmLabel(), &bar.getTimeSigLabel()}) {
        ASSERT_NE(dynamic_cast<synth::ui::NonModalLabel*>(label), nullptr);
        label->showEditor();
        pump();
        EXPECT_NE(label->getCurrentTextEditor(), nullptr);
        EXPECT_FALSE(windowIsBlockedByAnEditor(bar.getPlayStopButton())) << label->getComponentID();
        label->hideEditor(true);
    }
}

TEST(NonModalLabelTest, MixerColumnNameEditorDoesNotBlockTheWindow) {
    synth::ui::MixerColumnHeader header;
    header.setSize(120, 40);
    header.setRenameEnabled(true);

    auto& nameLabel = header.getNameLabelForTest();
    ASSERT_NE(dynamic_cast<synth::ui::NonModalLabel*>(&nameLabel), nullptr);
    nameLabel.showEditor();
    pump();
    ASSERT_NE(nameLabel.getCurrentTextEditor(), nullptr);
    EXPECT_FALSE(windowIsBlockedByAnEditor(header));
    nameLabel.hideEditor(true);
}
