#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// OrphanControllerComponent.h -- FRO135 (docs/control/midi-remote-ui.md#controllers-list-left): what the
// panel's right region shows instead of the control inspector when an orphan controller row is selected
// -- a project references a controller this machine does not have. Re-link points its assignments at a
// controller that is here; Recreate mints one from the assignments. A plain view: the panel supplies the
// text and turns the buttons into menus.
namespace synth::ui {

class OrphanControllerComponent : public juce::Component {
public:
    OrphanControllerComponent();
    ~OrphanControllerComponent() override;

    /** `assignmentCount` is how many of this project's assignments reference the controller;
     *  `canRecreate` is false when no MIDI input is free to recreate it on. */
    void setOrphan(const juce::String& name, int assignmentCount, bool canRecreate);
    /** Result line under the buttons ("2 assignments stay orphaned ..."); empty clears it. */
    void setStatusText(const juce::String& text);

    /** `anchor` is the button the menu should hang from. */
    std::function<void(juce::Component& anchor)> onRelinkRequested;
    std::function<void(juce::Component& anchor)> onRecreateRequested;

    juce::String getTitleForTest() const { return titleLabel_.getText(); }
    juce::String getStatusTextForTest() const { return statusLabel_.getText(); }
    juce::TextButton& getRelinkButtonForTest() { return relinkButton_; }
    juce::TextButton& getRecreateButtonForTest() { return recreateButton_; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::Label titleLabel_;
    juce::Label bodyLabel_;
    juce::TextButton relinkButton_{"Re-link..."};
    juce::TextButton recreateButton_{"Recreate"};
    juce::Label statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrphanControllerComponent)
};

} // namespace synth::ui
