#include "PluginEditor.h"

namespace synth {

namespace {
// Matches Main.cpp's DocumentWindow floor — the layout tokens assume at least this much room.
constexpr int kMinWidth = 480;
constexpr int kMinHeight = 400;
constexpr int kMaxWidth = 8192;
constexpr int kMaxHeight = 8192;
} // namespace

AgentSynthPluginEditor::AgentSynthPluginEditor(AgentSynthAudioProcessor& p)
    : juce::AudioProcessorEditor(&p)
    , processor(p)
    , mainComponent(p.getThemeManager(), p.getLookAndFeel(), p.getAudioEngine()) {
    // Scope the LookAndFeel to this editor's subtree. Children resolve it through the normal
    // Component lookup chain, so MainComponent sees the themed LnF without us touching the
    // process-wide Desktop default that the host and sibling plugins also read.
    setLookAndFeel(&p.getLookAndFeel());

    addAndMakeVisible(mainComponent);

    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

    const auto saved = processor.getSavedEditorSize();
    setSize(juce::jlimit(kMinWidth, kMaxWidth, saved.x), juce::jlimit(kMinHeight, kMaxHeight, saved.y));
}

AgentSynthPluginEditor::~AgentSynthPluginEditor() {
    // Clear before the LnF reference goes out of scope of this subtree. The LnF itself is owned
    // by the processor and outlives us, but JUCE requires no Component still points at an LnF
    // when that Component is destroyed.
    setLookAndFeel(nullptr);
}

void AgentSynthPluginEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AgentSynthPluginEditor::resized() {
    mainComponent.setBounds(getLocalBounds());
    // Persist into the processor so the size survives this editor being closed and reopened,
    // and gets written into the host session by getStateInformation.
    processor.setSavedEditorSize({getWidth(), getHeight()});
}

void AgentSynthPluginEditor::prepareForGraphReplacement() {
    mainComponent.getGraphEditor().detachAllModuleComponents();
}

void AgentSynthPluginEditor::refreshAfterGraphReplacement() { mainComponent.getGraphEditor().updateComponents(); }

} // namespace synth
