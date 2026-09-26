#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// ControllerSurfaceToolbar.h -- FRO134 (docs/control/midi-remote-ui.md#the-midi-remote-panel): the
// row above the surface grid: [Detect] [Assign...] [Templates] [...], plus the Detect hint row while
// Detect is on. Knows nothing about profiles -- MidiRemotePanelComponent supplies the menus' contents.
// [Assign...] (FRO135) starts the panel-side assign flow for the selected control.
namespace synth::ui {

class ControllerSurfaceToolbar : public juce::Component {
public:
    ControllerSurfaceToolbar();
    ~ControllerSurfaceToolbar() override;

    /** Templates/Detect/... need a controller to act on. */
    void setProfileSelected(bool selected);
    /** Assign... needs a selected control. */
    void setControlSelected(bool selected);
    /** Reflects Detect's state (button toggle + hint row) without firing onDetectToggled. */
    void setDetectOn(bool on);
    bool isDetectOn() const noexcept { return detectOn_; }

    /** FRO273: the right-aligned undo cue on the button row; empty hides it. */
    void setUndoHint(const juce::String& text);
    juce::String getUndoHint() const { return undoHintLabel_.getText(); }

    /** Height this toolbar wants right now (the hint row adds to it). */
    int getPreferredHeight() const noexcept;

    std::function<void(bool on)> onDetectToggled;
    /** The anchor is the button the popup menu should hang from. */
    std::function<void(juce::Component& anchor)> onAssignRequested;
    std::function<void(juce::Component& anchor)> onTemplatesRequested;
    std::function<void(juce::Component& anchor)> onMoreRequested;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kRowHeight = 30;
    static constexpr int kHintHeight = 34;

private:
    bool detectOn_ = false;
    juce::TextButton detectButton_;
    juce::TextButton assignButton_;
    juce::TextButton templatesButton_;
    juce::TextButton moreButton_;
    juce::Label hintLabel_;
    juce::Label undoHintLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfaceToolbar)
};

} // namespace synth::ui
