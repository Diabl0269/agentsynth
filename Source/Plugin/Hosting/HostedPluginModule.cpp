#include "HostedPluginModule.h"
#include <algorithm>

namespace synth {

namespace {
/** A retired instance is freed once the audio thread has started this many later blocks. It
 *  re-reads activeInstance_ at the top of every block and never caches it, so one completed block
 *  after the swap already proves it let go; two is the cheap margin. */
constexpr std::uint64_t kBlocksBeforeReap = 2;
} // namespace

HostedPluginModule::HostedPluginModule()
    : ModuleBase("Hosted Plugin", kMaxPluginChannels, kMaxPluginChannels) {
    // 16 in / 16 out, for the node's whole lifetime, whatever plugin (if any) it ends up hosting.
    // See the class comment: JUCE fixes the bus layout here and renegotiating it would drop every
    // connection the node has.
    addMuteParameter();
}

HostedPluginModule::~HostedPluginModule() {
    // The node is off the graph by now, so nothing can be mid-processBlock. Drop the audio-visible
    // pointer first anyway, then free on this (message) thread.
    activeInstance_.store(nullptr, std::memory_order_release);
    if (ownedInstance_ != nullptr)
        ownedInstance_->releaseResources();
    ownedInstance_.reset();
    retired_.clear();
}

//==============================================================================
// Loading (message thread only)
//==============================================================================

void HostedPluginModule::loadPlugin(const juce::PluginDescription& description, HostedPluginBackend& backend) {
    identity_ = PluginIdentity::fromDescription(description);
    statusMessage_.clear();
    loading_ = true;

    const int generation = ++loadGeneration_;
    juce::WeakReference<HostedPluginModule> self(this);

    backend.createInstanceAsync(
        description, currentSampleRate_, currentBlockSize_,
        [self, generation](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable {
            auto* module = self.get();
            if (module == nullptr)
                return; // the module outlived by nothing — nothing to publish into
            if (module->loadGeneration_ != generation)
                return; // superseded by a later load

            module->loading_ = false;

            if (instance == nullptr) {
                module->statusMessage_ = error.isNotEmpty() ? error : juce::String("Plugin failed to load.");
                return;
            }

            module->publishInstance(std::move(instance));
        });
}

void HostedPluginModule::loadPlugin(const PluginIdentity& identity, HostedPluginBackend& backend) {
    identity_ = identity;
    statusMessage_.clear();
    loading_ = true;

    const int generation = ++loadGeneration_;
    juce::WeakReference<HostedPluginModule> self(this);

    backend.createInstanceAsync(
        identity, currentSampleRate_, currentBlockSize_,
        [self, generation](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) mutable {
            auto* module = self.get();
            if (module == nullptr)
                return;
            if (module->loadGeneration_ != generation)
                return;

            module->loading_ = false;

            if (instance == nullptr) {
                // The identity is deliberately KEPT: a placeholder that still
                // names its plugin is a valid state (TL7-3).
                module->statusMessage_ = error.isNotEmpty() ? error : juce::String("Plugin failed to load.");
                return;
            }

            module->publishInstance(std::move(instance));
        });
}

void HostedPluginModule::unloadPlugin() {
    ++loadGeneration_; // cancel anything in flight
    loading_ = false;
    retireActiveInstance();
    identity_ = {};
    pendingBlob_.reset();
    statusMessage_.clear();
    setLatencySamples(0);
}

void HostedPluginModule::prepareInstance(juce::AudioPluginInstance& instance) const {
    instance.setPlayConfigDetails(instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels(),
                                  currentSampleRate_, currentBlockSize_);
    instance.prepareToPlay(currentSampleRate_, currentBlockSize_);
}

void HostedPluginModule::publishInstance(std::unique_ptr<juce::AudioPluginInstance> instance) {
    if (instance == nullptr)
        return;

    // Take whatever bus layout the instance reports as its default — we ask it what it is rather
    // than imposing one, because a plugin's preferred layout is the one it is guaranteed to render
    // correctly. TL7-5 exposes layout choice to the user.
    const int instanceInputs = instance->getTotalNumInputChannels();
    const int instanceOutputs = instance->getTotalNumOutputChannels();

    if (instanceInputs > kMaxPluginChannels || instanceOutputs > kMaxPluginChannels) {
        // Refused, not truncated. Truncating a surround plugin down to 16 would silently drop
        // channels with nothing to tell the user; this leaves the module passing audio through and
        // says why.
        statusMessage_ = identity_.name + " needs " + juce::String(instanceInputs) + " in / " +
                         juce::String(instanceOutputs) + " out channels; a Hosted Plugin module carries at most " +
                         juce::String(kMaxPluginChannels) + ".";
        instance.reset(); // message thread — never the audio thread
        return;
    }

    prepareInstance(*instance);

    // The plugin's own state, if we are restoring a patch. Applied BEFORE publication so the audio
    // thread never renders one block of the plugin's factory default.
    if (pendingBlob_.getSize() > 0)
        instance->setStateInformation(pendingBlob_.getData(), static_cast<int>(pendingBlob_.getSize()));

    if (identity_.name.isEmpty())
        identity_ = PluginIdentity::fromDescription(instance->getPluginDescription());

    retireActiveInstance();

    auto* raw = instance.get();
    ownedInstance_ = std::move(instance);

    visibleInputs_.store(instanceInputs, std::memory_order_relaxed);
    visibleOutputs_.store(instanceOutputs, std::memory_order_relaxed);
    setLatencySamples(raw->getLatencySamples());

    // Release: everything above (prepareToPlay, the state blob, the port counts) must be visible to
    // the audio thread's acquire-load before it can see the pointer.
    activeInstance_.store(raw, std::memory_order_release);

    statusMessage_.clear();
}

void HostedPluginModule::retireActiveInstance() {
    activeInstance_.store(nullptr, std::memory_order_release);

    if (ownedInstance_ != nullptr)
        retired_.push_back({std::move(ownedInstance_), blockCounter_.load(std::memory_order_acquire)});

    visibleInputs_.store(1, std::memory_order_relaxed);
    visibleOutputs_.store(1, std::memory_order_relaxed);

    reapRetired();
}

void HostedPluginModule::reapRetired() {
    if (retired_.empty())
        return;

    const std::uint64_t now = blockCounter_.load(std::memory_order_acquire);
    // Not rendering at all (never prepared, or released): nothing can be inside a processBlock, so
    // there is nothing to wait for. Otherwise wait for the audio thread to visibly move on.
    const bool idle = !prepared_;

    auto reapable = [&](const RetiredInstance& entry) {
        return idle || now >= entry.retiredAtBlock + kBlocksBeforeReap;
    };

    for (auto& entry : retired_)
        if (reapable(entry) && entry.instance != nullptr)
            entry.instance->releaseResources();

    // Destruction happens here, on the message thread — the audio thread never frees an instance.
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), reapable), retired_.end());
}

//==============================================================================
// AudioProcessor
//==============================================================================

void HostedPluginModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize_ = samplesPerBlock > 0 ? samplesPerBlock : 512;
    prepared_ = true;

    // A live instance follows the host's stream format. The graph prepares its nodes with the audio
    // callback stopped, so re-preparing in place is safe and keeps the published pointer valid.
    if (auto* instance = activeInstance_.load(std::memory_order_acquire)) {
        prepareInstance(*instance);
        setLatencySamples(instance->getLatencySamples());
    }
}

void HostedPluginModule::releaseResources() {
    prepared_ = false;

    if (auto* instance = activeInstance_.load(std::memory_order_acquire))
        instance->releaseResources();

    reapRetired();
}

void HostedPluginModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Every exit path bumps the counter, including the ones below this guard would skip — see
    // kBlocksBeforeReap. Done first so an early return cannot forget it.
    struct BlockTick {
        std::atomic<std::uint64_t>& counter;
        ~BlockTick() { counter.fetch_add(1, std::memory_order_release); }
    } tick{blockCounter_};

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (isMuted()) {
        buffer.clear();
        return;
    }

    auto* instance = activeInstance_.load(std::memory_order_acquire);

    // Bypass and "still loading / not installed" are the same path: hand the audio straight back.
    // The module's inputs and outputs are the same 16 channels of the same buffer, so a dry
    // pass-through is literally nothing — and per the bypass/mute contract we must not touch them.
    if (instance == nullptr || isBypassed())
        return;

    const int instanceInputs = instance->getTotalNumInputChannels();
    const int instanceOutputs = instance->getTotalNumOutputChannels();
    const int instanceChannels = std::max(instanceInputs, instanceOutputs);

    // Cannot happen on the graph (the node always has kMaxPluginChannels and publish refuses
    // anything wider), but a bare unit-test render could hand us a narrower buffer. Passing through
    // beats scribbling past the end of it.
    if (instanceChannels > numChannels)
        return;

    // JUCE's processBlock convention: channels above the input count are output-only and arrive as
    // garbage. AudioProcessorGraph clears them for its own nodes; we do the same for ours, which is
    // also what stops our upstream audio leaking into an instrument's output.
    for (int channel = instanceInputs; channel < instanceChannels; ++channel)
        buffer.clear(channel, 0, numSamples);

    // A non-owning view over the first instanceChannels channels — no allocation, no copy.
    juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), instanceChannels, numSamples);

    instance->setPlayHead(getPlayHead());
    instance->processBlock(view, midiMessages);

    // Hidden channels are silenced every block — the other half of the fixed-maximum/visible-count
    // pattern (the Macro rule). Without it, whatever the upstream node put on channel 9 of a stereo
    // plugin's node would sail on downstream.
    for (int channel = instanceOutputs; channel < numChannels; ++channel)
        buffer.clear(channel, 0, numSamples);
}

//==============================================================================
// Port model
//==============================================================================

juce::String HostedPluginModule::getInputPortLabel(int channelIndex) const {
    const int visible = getVisibleInputPortCount();
    if (visible == 2)
        return channelIndex == 0 ? "In L" : "In R";
    return "In " + juce::String(channelIndex + 1);
}

juce::String HostedPluginModule::getOutputPortLabel(int channelIndex) const {
    const int visible = getVisibleOutputPortCount();
    if (visible == 2)
        return channelIndex == 0 ? "Out L" : "Out R";
    return "Out " + juce::String(channelIndex + 1);
}

LogicalPort HostedPluginModule::mapInputChannel(int rawChannel) const {
    LogicalPort port;
    const int visible = getVisibleInputPortCount();
    port.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
    port.role = PortRole::Audio;
    port.isPolyGroupHead = (rawChannel < visible);
    port.polyVoiceSpan = 1;
    return port;
}

LogicalPort HostedPluginModule::mapOutputChannel(int rawChannel) const {
    LogicalPort port;
    const int visible = getVisibleOutputPortCount();
    port.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
    port.role = PortRole::Audio;
    port.isPolyGroupHead = (rawChannel < visible);
    port.polyVoiceSpan = 1;
    return port;
}

//==============================================================================
// Non-parameter state — trusted path ONLY
//==============================================================================

juce::var HostedPluginModule::getExtraState() const {
    if (!identity_.isValid())
        return {};

    auto state = identity_.toVar();
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return {};

    // The live instance's state if we have one, otherwise the blob we were restored with — so
    // re-saving a patch on a machine that lacks the plugin preserves its settings rather than
    // silently resetting them to nothing.
    juce::MemoryBlock blob;
    if (auto* instance = activeInstance_.load(std::memory_order_acquire)) {
        // Message thread, on an instance this module owns. (The atomic hands back a non-const
        // pointer even from this const method, so getStateInformation's non-constness is moot.)
        instance->getStateInformation(blob);
    } else {
        blob = pendingBlob_;
    }

    if (blob.getSize() > 0)
        object->setProperty("pluginState", blob.toBase64Encoding());

    // NOTE: no path is written here, ever — not the plugin binary's, not anything else. The identity
    // is format + uniqueId + name (see PluginIdentity). HostedPluginTests asserts it over the
    // serialized patch.
    return state;
}

void HostedPluginModule::setExtraState(const juce::var& state) {
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return;

    const PluginIdentity identity = PluginIdentity::fromVar(state);

    pendingBlob_.reset();
    const juce::String encoded = object->getProperty("pluginState").toString();
    if (encoded.isNotEmpty())
        pendingBlob_.fromBase64Encoding(encoded);

    if (!identity.isValid()) {
        unloadPlugin();
        return;
    }

    // The backend seam: state restore happens deep inside AIStateMapper::applyJSONToGraph, which has
    // no backend to pass down, so we reach for the process-wide default. Tests install a stub for
    // the duration of a scope via HostedPluginBackend::ScopedDefault.
    loadPlugin(identity, HostedPluginBackend::getDefault());
}

} // namespace synth
