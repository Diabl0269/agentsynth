#include "MacroPortConfigDialog.h"

namespace synth::ui {

// Concern: MacroAutoPortPromptDialog (founder-review fix F5), the small companion "Create
// Ports?" modal declared alongside MacroPortConfigDialog in the same header.

MacroAutoPortPromptDialog::MacroAutoPortPromptDialog(int crossingPortCount) {
    titleLabel_.setText("Macro has boundary cables", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    const juce::String plural = crossingPortCount == 1 ? juce::String() : juce::String("s");
    messageLabel_.setText("This macro will need " + juce::String(crossingPortCount) + " port" + plural +
                              " for the cables crossing its boundary. Create the port" + plural +
                              ", or leave the cables exactly as they are?",
                          juce::dontSendNotification);
    messageLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    messageLabel_.setJustificationType(juce::Justification::topLeft);
    messageLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(messageLabel_);

    // Opt-out, not opt-in: most users making this choice want it applied from now on, and "always
    // ask" stays one click away in Preferences for anyone who wants to reconsider every time.
    rememberToggle_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(rememberToggle_);

    createPortsButton_.onClick = [this] { triggerCreatePortsForTest(); };
    addAndMakeVisible(createPortsButton_);

    leaveAsIsButton_.onClick = [this] { triggerLeaveCablesAsIsForTest(); };
    addAndMakeVisible(leaveAsIsButton_);

    setSize(kWidth, 190);
}

MacroAutoPortPromptDialog::~MacroAutoPortPromptDialog() = default;

// T153: see the class comment for the decision — Escape == "Leave Cables As Is", remember forced
// to false regardless of the toggle's current state.
bool MacroAutoPortPromptDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onChoice)
            onChoice(/*createPorts=*/false, /*remember=*/false);
        return true;
    }
    return false;
}

void MacroAutoPortPromptDialog::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void MacroAutoPortPromptDialog::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    messageLabel_.setBounds(area.removeFromTop(64));
    area.removeFromTop(10);
    rememberToggle_.setBounds(area.removeFromTop(22));
    area.removeFromTop(14);

    auto buttonRow = area.removeFromTop(30);
    leaveAsIsButton_.setBounds(buttonRow.removeFromRight(150));
    buttonRow.removeFromRight(8);
    createPortsButton_.setBounds(buttonRow.removeFromRight(130));
}

void MacroAutoPortPromptDialog::setRememberChoiceForTest(bool remember) {
    rememberToggle_.setToggleState(remember, juce::dontSendNotification);
}

bool MacroAutoPortPromptDialog::getRememberChoiceForTest() const { return rememberToggle_.getToggleState(); }

void MacroAutoPortPromptDialog::triggerCreatePortsForTest() {
    if (onChoice)
        onChoice(true, rememberToggle_.getToggleState());
}

void MacroAutoPortPromptDialog::triggerLeaveCablesAsIsForTest() {
    if (onChoice)
        onChoice(false, rememberToggle_.getToggleState());
}

} // namespace synth::ui
