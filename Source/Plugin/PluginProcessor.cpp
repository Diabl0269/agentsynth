#include "PluginProcessor.h"
#include "AI/AIStateMapper.h"
#include "Branding.h"
#include "PluginEditor.h"
#include "UserSettings.h"

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

    // The identity resolver, installed before anything in the graph can ask for a plugin. This is
    // the processor's job and not the editor's: setStateInformation runs whether or not the host
    // ever opens our window, and an identity that cannot resolve leaves a Hosted Plugin node a
    // placeholder for the rest of the session. The list is whatever the standalone app persisted
    // after its last scan — nothing here scans (see startPluginScan).
    appProperties.setStorageParameters(userSettingsOptions());
    if (auto savedScanList = juce::parseXML(appProperties.getUserSettings()->getValue(kPluginScanListSettingKey)))
        pluginScanService.loadFromXml(*savedScanList);
    if (auto* backend = dynamic_cast<DefaultHostedPluginBackend*>(&HostedPluginBackend::getDefault()))
        backend->setScanService(&pluginScanService);

    // Builds the default patch. In HostMode::Hosted this touches no audio device and opens no
    // MIDI input — see AudioEngine::initialise().
    engine.initialise();
}

AgentSynthAudioProcessor::~AgentSynthAudioProcessor() {
    // Unhook before the graph goes: the backend holds a bare pointer, and a hosted-plugin node
    // resolving an identity after this point would read freed memory. Guarded on "still ours" for
    // the same reason MainComponent's destructor is — a second plugin instance in the same process
    // will have installed its own service over this one.
    if (auto* backend = dynamic_cast<DefaultHostedPluginBackend*>(&HostedPluginBackend::getDefault()))
        if (backend->getScanService() == &pluginScanService)
            backend->setScanService(nullptr);

    engine.shutdown();
}

void AgentSynthAudioProcessor::ensureScanServiceInstalled() {
    if (auto* backend = dynamic_cast<DefaultHostedPluginBackend*>(&HostedPluginBackend::getDefault()))
        if (backend->getScanService() == nullptr)
            backend->setScanService(&pluginScanService);
}

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

    // The inner graph's latency (a hosted plugin's lookahead, compensated inside the graph) is
    // invisible to the DAW unless this wrapper reports it as its own.
    setLatencySamples(engine.getGraphLatencySamples());
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

    // A hosted plugin flipping its lookahead mid-session re-derives the inner graph's latency on
    // the message thread; this compare is how the new figure reaches the DAW without waiting for
    // the next prepareToPlay. setLatencySamples is allocation-free.
    const int graphLatency = engine.getGraphLatencySamples();
    if (graphLatency != getLatencySamples())
        setLatencySamples(graphLatency);
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

    // Before anything can apply a Hosted Plugin node's identity — see the helper for the one case
    // this covers that the constructor cannot.
    ensureScanServiceInstalled();

    // Tear down module components BEFORE the graph is cleared, so no ScopeComponent timer can
    // fire against a freed VisualBuffer. Same ordering contract as GraphEditor::loadPreset and
    // MainComponent::aiPatchAboutToApply — see docs/architecture.md.
    auto* editor = dynamic_cast<AgentSynthPluginEditor*>(getActiveEditor());
    if (editor != nullptr)
        editor->prepareForGraphReplacement();

    // Validate untrusted, then apply trusted — the SnippetManager::insertSnippet pairing from
    // docs/layout.md §12.5. Host session files travel between machines and users, so they must
    // pass the full validatePatch() boundary and be rejected whole if tampered with. But the
    // values themselves are our own graphToJSON output: applying them untrusted would run the
    // [0,1] rescale heuristic meant for sloppy model output and silently corrupt exact values
    // (a 0.5 Hz LFO rate on a 0.01–20 Hz range reloads as ~10 Hz). Trusted apply preserves them.
    //
    // allowInternalModuleTypes: the model-authorship restriction does not apply to our OWN saved
    // graph, which legitimately contains internal nodes (every mod routing is an Attenuverter, and
    // a timeline patch has a Track In per track). Everything else the gate checks still runs.
    if (!AIStateMapper::validatePatch(patch, engine.getGraph(), /*clearExisting=*/true, /*trusted=*/false,
                                      /*allowInternalModuleTypes=*/true)
             .ok) {
        if (editor != nullptr)
            editor->refreshAfterGraphReplacement();
        return;
    }
    AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), /*clearExisting=*/true, /*trusted=*/true);

    // Reconcile the view against whatever the graph now holds — including after a rejected
    // patch, where the graph is left untouched and the editor must rebuild what it just detached.
    if (editor != nullptr)
        editor->refreshAfterGraphReplacement();
}

} // namespace synth

// This creates new instances of the plugin — the entry point every format wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new synth::AgentSynthAudioProcessor(); }
