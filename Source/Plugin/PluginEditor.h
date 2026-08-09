#pragma once

#include "MainComponent.h"
#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/**
    Plugin editor — a thin resizable frame around the same MainComponent the desktop app uses.

    The whole UI (graph editor, module library, AI chat, toolbar, status bar) is reused as-is;
    the only differences are that the AudioEngine is injected rather than owned, and the
    LookAndFeel is attached to this editor rather than to juce::Desktop. A plugin must never
    call Desktop::setDefaultLookAndFeel — that is global to the host process and would re-skin
    the host's own windows and every other plugin loaded alongside us.
*/
class AgentSynthPluginEditor : public juce::AudioProcessorEditor {
public:
    explicit AgentSynthPluginEditor(AgentSynthAudioProcessor&);
    ~AgentSynthPluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Called by the processor around a host state load. prepare... detaches module components
    // before the graph is cleared (ScopeComponent timer / VisualBuffer use-after-free guard);
    // refresh... reconciles the view against the new graph.
    void prepareForGraphReplacement();
    void refreshAfterGraphReplacement();

private:
    AgentSynthAudioProcessor& processor;
    MainComponent mainComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgentSynthPluginEditor)
};

} // namespace synth
