#pragma once

#include "MixerColumnHeader.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerDirectColumn.h -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): Direct's column. Not a channel --
// name fixed to "Direct", no colour/fader/pan/insert list/M-S (docs/mixer/panel.md#what-the-mixer-shows) -- just a
// "Make channel" button for whatever feeds Master's Direct bus today, reusing the P9-3d "Make channel" flow verbatim
// (MainComponent::makeChannelForNode).
namespace synth::ui {

class MixerDirectColumn : public juce::Component {
public:
    MixerDirectColumn();

    void configure(juce::AudioProcessorGraph& graph);

    /** Fires with the resolved chain source (synth::resolveChannelSource against whatever feeds
     *  Master's Direct input) when "Make channel" is clicked and a source resolves; never fires
     *  on an invalid/ambiguous resolution (the button itself is disabled then). */
    std::function<void(juce::AudioProcessorGraph::NodeID)> onMakeChannelRequested;

    /** Re-checks whether anything currently feeds Direct -- call after every graph change. */
    void refreshEnablement();

    /** FRO18: Direct's own leaf-level keyboard-focus outline -- Left/Right can land here just
     *  like any other column, so it needs the same visual as MixerColumnComponent/
     *  MixerMasterColumn (setKeyboardFocused), painted in paintOverChildren. */
    void setKeyboardFocused(bool focused);
    bool isKeyboardFocusedForTest() const noexcept { return keyboardFocused_; }

    /** FRO18 review fix: same contract as MixerColumnComponent::grabAccessibilityFocus() --
     *  Direct has no fader of its own, so this targets the column itself (setTitle("Direct") in
     *  the ctor is what its default, unspecified-role AccessibilityHandler reads). */
    void grabAccessibilityFocus() {
        if (auto* handler = getAccessibilityHandler())
            handler->grabFocus();
    }
    juce::Component& getAccessibilityFocusTargetForTest() noexcept { return *this; }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioProcessorGraph::NodeID resolveDirectFeeder() const;

    juce::AudioProcessorGraph* graph_ = nullptr;
    MixerColumnHeader header_;
    juce::TextButton makeChannelButton_{"Make channel"};
    bool keyboardFocused_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerDirectColumn)
};

} // namespace synth::ui
