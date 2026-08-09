#include "PluginProcessor.h"
#include "AI/AIStateMapper.h"
#include "Branding.h"
#include "PluginEditor.h"

namespace synth {

namespace {
// State wrapper keys. The patch itself is stored in exactly the same shape a .json preset file
// uses (AIStateMapper::graphToJSON), so plugin state and preset files stay interchangeable; the
// wrapper only adds host-session extras like the editor size.
constexpr const char* kStateVersionKey = "stateVersion";
constexpr const char* kPatchKey = "patch";
constexpr const char* kEditorWidthKey = "editorWidth";
constexpr const char* kEditorHeightKey = "editorHeight";
constexpr int kStateVersion = 1;
} // namespace

AgentSynthAudioProcessor::AgentSynthAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // Apply the default theme before any Component exists, exactly as Main.cpp does for the
    // standalone app — the editor is created later (and possibly many times) and must never find
    // an unthemed LnF. MainComponent calls themeManager.initialise(&appProperties) once it owns
    // the properties file, which restores the user's persisted theme over this default.
    lookAndFeel.applyTheme(themeManager.getActiveTheme());

    // Builds the default patch. In HostMode::Hosted this touches no audio device and opens no
    // MIDI input — see AudioEngine::initialise().
    engine.initialise();
}

AgentSynthAudioProcessor::~AgentSynthAudioProcessor() { engine.shutdown(); }

const juce::String AgentSynthAudioProcessor::getName() const { return synth::branding::kProductName; }

double AgentSynthAudioProcessor::getTailLengthSeconds() const {
    // A modular patch has no knowable tail — the user can wire an arbitrary reverb/delay chain.
    // Report a fixed, generous value so hosts don't truncate the default patch's reverb on
    // offline render, without pinning the plugin open for an unreasonable time.
    return 4.0;
}

const juce::String AgentSynthAudioProcessor::getProgramName(int index) {
    juce::ignoreUnused(index);
    return {};
}

void AgentSynthAudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void AgentSynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepareForHost(sampleRate, samplesPerBlock, getTotalNumInputChannels(), getTotalNumOutputChannels());
}

void AgentSynthAudioProcessor::releaseResources() { engine.releaseFromHost(); }

bool AgentSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Instrument: no input bus, mono or stereo out. Mirrors the standalone app, which opens the
    // default device with 0 inputs and 2 outputs.
    if (layouts.getMainInputChannels() != 0)
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AgentSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Zero the output BEFORE the graph runs — the standalone device callback does the same, and
    // the graph only adds into the buffer, so a stale host buffer would leak through as noise.
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    engine.processHostBlock(buffer, midiMessages);

    // producesMidi() is false, so anything the graph left in the buffer (e.g. a Sequencer's note
    // stream driving the internal oscillators) must not escape to the host.
    midiMessages.clear();
}

juce::AudioProcessorEditor* AgentSynthAudioProcessor::createEditor() { return new AgentSynthPluginEditor(*this); }

void AgentSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto* state = new juce::DynamicObject();
    state->setProperty(kStateVersionKey, kStateVersion);
    state->setProperty(kPatchKey, AIStateMapper::graphToJSON(engine.getGraph()));
    state->setProperty(kEditorWidthKey, savedEditorSize.x);
    state->setProperty(kEditorHeightKey, savedEditorSize.y);

    const auto json = juce::JSON::toString(juce::var(state));
    destData.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void AgentSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto parsed = juce::JSON::parse(juce::String::createStringFromData(data, sizeInBytes));
    if (!parsed.isObject())
        return;

    // Accept both the wrapper written by getStateInformation and a bare patch object, so a raw
    // preset JSON dropped into the host's state slot still loads.
    juce::var patch = parsed;
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty(kPatchKey))
            patch = obj->getProperty(kPatchKey);

        const int w = obj->getProperty(kEditorWidthKey);
        const int h = obj->getProperty(kEditorHeightKey);
        if (w > 0 && h > 0)
            savedEditorSize = {w, h};
    }

    if (!patch.isObject())
        return;

    // Tear down module components BEFORE the graph is cleared, so no ScopeComponent timer can
    // fire against a freed VisualBuffer. Same ordering contract as GraphEditor::loadPreset and
    // MainComponent::aiPatchAboutToApply — see docs/architecture.md.
    auto* editor = dynamic_cast<AgentSynthPluginEditor*>(getActiveEditor());
    if (editor != nullptr)
        editor->prepareForGraphReplacement();

    // trusted=false on purpose. Host session files travel between machines and users — they are
    // not the same trust class as a preset the user just picked out of their own filesystem — so
    // this goes through the full validatePatch() boundary and is rejected outright, never
    // partially applied, if it doesn't check out. See docs/AI_Engine.md.
    AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true, /*trusted=*/false);

    // Reconcile the view against whatever the graph now holds — including after a rejected
    // patch, where the graph is left untouched and the editor must rebuild what it just detached.
    if (editor != nullptr)
        editor->refreshAfterGraphReplacement();
}

} // namespace synth

// This creates new instances of the plugin — the entry point every format wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new synth::AgentSynthAudioProcessor(); }
