#include "PluginEditor.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

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
    , mainComponentOwner_(std::make_unique<MainComponent>(p.getThemeManager(), p.getLookAndFeel(), p.getAudioEngine()))
    , mainComponent(*mainComponentOwner_) {
    // Scope the LookAndFeel to this editor's subtree. Children resolve it through the normal
    // Component lookup chain, so MainComponent sees the themed LnF without us touching the
    // process-wide Desktop default that the host and sibling plugins also read.
    setLookAndFeel(&p.getLookAndFeel());

    addAndMakeVisible(mainComponent);

    setResizable(true, true);
    setResizeLimits(kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

    const auto saved = processor.getSavedEditorSize();
    setSize(juce::jlimit(kMinWidth, kMaxWidth, saved.x), juce::jlimit(kMinHeight, kMaxHeight, saved.y));

    // FRO12 follow-up: this is the plugin's ONE construction site for MainComponent (no test
    // builds an AgentSynthPluginEditor directly) — opt both detach hosts into actually creating a
    // native window on detach, same as Main.cpp's MainWindow does for the standalone app. See
    // DetachablePanelHost::setCreatesNativeWindows()'s doc comment.
    mainComponent.getBottomDock().getTimelineHost().setCreatesNativeWindows(true);
    mainComponent.getBottomDock().getMixerHost().setCreatesNativeWindows(true);
    mainComponent.getBottomDock().getMidiRemoteHost().setCreatesNativeWindows(true);

    // FRO100: same reasoning, for the hosted-plugin "Open Editor" window. See
    // HostedPluginWindowManager::setCreatesNativeWindows()'s doc comment.
    mainComponent.getPluginWindowManager().setCreatesNativeWindows(true);
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
