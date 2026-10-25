#pragma once

// The "Control Style..." panel of a module card: the knob style and the fader style for this module alone, or
// for every module of its type, each row with a leading Follow that clears that level. Changes apply live.
// docs/layout/theming.md#per-module-control-style.

#include "UI/Layout/SpaceClaimingButton.h"
#include "UI/Settings/KnobStylePicker.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class GraphEditor;

namespace synth::ui {

class ControlStylePanel : public juce::Component {
public:
    enum class Scope { Module, Type };

    // `typeName` is the module's factory type name (the key of the per-type setting).
    ControlStylePanel(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId, const juce::String& typeName);

    static constexpr int kWidth = 548;
    static constexpr int kHeight = 232;

    // Message thread only. Re-reads the scope's current style into the previews.
    void setScope(Scope scope);
    Scope getScope() const noexcept { return scope_; }

    void focusFirstControl() { moduleButton_.grabKeyboardFocus(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

    KnobStylePicker& getPickerForTest() { return picker_; }
    juce::Button& getScopeButtonForTest(Scope scope) { return scope == Scope::Module ? moduleButton_ : typeButton_; }

private:
    void write(std::optional<synth::theme::KnobStyle> knob, std::optional<synth::theme::KnobStyle> fader);
    void refreshPicker();
    void refreshTheme();

    GraphEditor& editor_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::String typeName_;
    Scope scope_ = Scope::Module;
    juce::Label title_{{}, "Control style"};
    SpaceClaimingButton<juce::TextButton> moduleButton_{"This module"};
    SpaceClaimingButton<juce::TextButton> typeButton_;
    KnobStylePicker picker_{KnobStylePicker::Mode::Override};
};

} // namespace synth::ui
