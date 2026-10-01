#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A juce::Label whose inline editor leaves the rest of the window accessible. Use it for every label edited
 *  in place (`setEditable`): a plain juce::Label goes modal while editing, and VoiceOver then sees only the
 *  window's title bar. Committing on focus loss, Return and Escape behave as for any Label. */
class NonModalLabel : public juce::Label {
public:
    using juce::Label::Label;

protected:
    void editorShown(juce::TextEditor* editor) override;
};

} // namespace synth::ui
