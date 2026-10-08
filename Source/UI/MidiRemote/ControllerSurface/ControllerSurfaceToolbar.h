#pragma once

#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// ControllerSurfaceToolbar.h (docs/control/midi-remote-ui.md#the-controllers-panel): the
// row above the surface grid: [Detect] [Assign...] [Templates] [...], plus the Detect hint row while
// Detect is on. Knows nothing about profiles -- MidiRemotePanelComponent supplies the menus' contents.
// [Assign...] starts the panel-side assign flow for the selected control.
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

    /** The right-aligned undo cue on the button row; empty hides it. */
    void setUndoHint(const juce::String& text);
    juce::String getUndoHint() const { return undoFade_.isShown() ? undoHintLabel_.getText() : juce::String(); }

    /** The selected controller's
     *  handshake port-mismatch warning (MidiLearnController::getHandshakeIssueForProfile), shown as
     *  its own full-width row below the button row; empty hides it. Same idempotent contract as
     *  setUndoHint, except (unlike the undo cue) this row changes getPreferredHeight() -- the caller
     *  must re-layout afterwards, same as setDetectOn() (see
     * docs/control/midi-remote-device-handshake.md#device-handshake). */
    void setPortHint(const juce::String& text);
    juce::String getPortHint() const { return portFade_.isShown() ? portHintLabel_.getText() : juce::String(); }

    /** Height this toolbar wants right now: each hint row adds its height times its fade's progress, so the
     *  rows below slide while a hint fades in or out. */
    int getPreferredHeight() const noexcept;

    /** Fired on each frame of a hint row's fade, when getPreferredHeight() has changed: the owner lays out again. */
    std::function<void()> onPreferredHeightChanged;
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
    void relayoutForHintFade();
    bool detectOn_ = false;
    juce::TextButton detectButton_;
    juce::TextButton assignButton_;
    juce::TextButton templatesButton_;
    juce::TextButton moreButton_;
    juce::Label hintLabel_;
    juce::Label undoHintLabel_;
    juce::Label portHintLabel_;
    // The hint rows fade (FadeVisibility, docs/layout/animation.md#fading-things-in-and-out); the two full-width
    // rows also tween their height with the fade.
    FadeVisibility detectHintFade_{&hintLabel_};
    FadeVisibility undoFade_{&undoHintLabel_};
    FadeVisibility portFade_{&portHintLabel_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfaceToolbar)
};

} // namespace synth::ui
