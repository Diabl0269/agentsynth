#pragma once

#include "AudioEngine.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

namespace synth {

/**
    VST3 / AU / Standalone wrapper around the same AudioEngine the desktop app runs.

    The engine is constructed in HostMode::Hosted, so it never opens an audio device or a MIDI
    input of its own — this processor forwards the host's clock (prepareToPlay / processBlock /
    releaseResources) and the host's MIDI straight through to the module graph.

    Ownership note: ThemeManager and AppLookAndFeel live here, not in the editor. A host may
    create and destroy the editor many times over a session (every time the plugin window is
    closed and reopened), and the LookAndFeel must outlive every Component that points at it —
    the same shutdown-order guard Main.cpp relies on for the standalone app.

    The hosted-plugin scan list is here for the same reason, one step further: a DAW restoring a
    session never has to open our window at all, so an identity resolver that only existed while the
    editor did would leave every Hosted Plugin node in the restored patch an unresolved placeholder
    until the user happened to look at it. This processor restores the list the standalone app
    persisted and installs it on the process-wide backend itself. It never SCANS — inside a host the
    host owns plugin discovery (MainComponent::startPluginScan refuses outright).
*/
class AgentSynthAudioProcessor : public juce::AudioProcessor {
public:
    AgentSynthAudioProcessor();
    ~AgentSynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // The module graph is the program; there are no host-visible programs on top of it.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    AudioEngine& getAudioEngine() noexcept { return engine; }
    theme::ThemeManager& getThemeManager() noexcept { return themeManager; }
    theme::AppLookAndFeel& getLookAndFeel() noexcept { return lookAndFeel; }

    /** The session's identity resolver: restored from the persisted scan list in the constructor and
     *  installed on the process-wide backend for as long as this processor lives. An editor built on
     *  this engine adopts it rather than installing one of its own. */
    PluginScanService& getPluginScanService() noexcept { return pluginScanService; }

    // Editor size, persisted in the plugin state so a reopened window keeps the user's layout.
    juce::Point<int> getSavedEditorSize() const noexcept { return savedEditorSize; }
    void setSavedEditorSize(juce::Point<int> size) noexcept { savedEditorSize = size; }

private:
    /** Installs our scan service on the process-wide backend if that slot is currently empty.
     *  Covers the one case the constructor cannot: a sibling plugin instance in the same host
     *  installed its service over ours and has since been deleted, which vacates the slot. A
     *  sibling's service that is still installed is left alone — it was restored from the same
     *  settings file, so it resolves the same identities. */
    void ensureScanServiceInstalled();

    // Declaration order is the shutdown-order guard: everything here is declared BEFORE engine so it
    // is destroyed AFTER it (and after any editor, which the base class tears down first). Mirrors
    // AppApplication's member ordering in Main.cpp. The scan service in particular must outlive the
    // graph: a hosted-plugin node resolves its identity through it.
    juce::ApplicationProperties appProperties;
    PluginScanService pluginScanService;

    theme::ThemeManager themeManager;
    theme::AppLookAndFeel lookAndFeel;

    AudioEngine engine{AudioEngine::HostMode::Hosted};

    juce::Point<int> savedEditorSize{1600, 900};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgentSynthAudioProcessor)
};

} // namespace synth
