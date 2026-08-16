#pragma once

#include "../Source/Plugin/Hosting/HostedPluginBackend.h"
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <thread>
#include <vector>

// A fake third-party plugin, and a HostedPluginBackend that hands it out (TL7-2).
//
// There is no plugin binary we can check into this repo and load in CI — a real VST3 would be a
// platform-specific blob, a licence question, and a scan step. So everything worth pinning about
// hosting (publication, the 16-channel refusal, the state round-trip, the retained-instance
// discipline, latency) is exercised against an ordinary juce::AudioPluginInstance subclass that the
// stub backend resolves ANY identity to.
//
// The stub deliberately matches the real formats' threading: createInstanceAsync never calls back
// re-entrantly, it posts through MessageManager::callAsync, so tests have to pump the message loop
// exactly as they would with a real plugin.

namespace synth::test {

/** A stable-id (VST3/AU/LV2-style) stub parameter — TL7-6. Implements
 *  juce::HostedAudioProcessorParameter, the real hierarchy hosted plugin parameters use (NOT
 *  RangedAudioParameter — see HostedPluginModule.h), so tests exercise the exact type the resolver
 *  branches on. */
class StubHostedParameter : public juce::HostedAudioProcessorParameter {
public:
    StubHostedParameter(juce::String paramId, juce::String name, float defaultValue = 0.0f)
        : paramId_(std::move(paramId))
        , name_(std::move(name))
        , value_(defaultValue)
        , defaultValue_(defaultValue) {}

    juce::String getParameterID() const override { return paramId_; }

    float getValue() const override { return value_; }
    void setValue(float newValue) override { value_ = newValue; }
    float getDefaultValue() const override { return defaultValue_; }
    juce::String getName(int) const override { return name_; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override { return text.getFloatValue(); }

private:
    juce::String paramId_;
    juce::String name_;
    float value_;
    float defaultValue_;
};

/** A LEGACY stub parameter with NO stable id — the same shape JUCE's own VST2 wrapper produces for
 *  a plugin with no persistent parameter identity (TL7-6's "plugins without WithID parameters"
 *  case; see juce_VSTPluginFormat.cpp's own parameter class, whose getParameterID() likewise always
 *  returns an empty string). It still has to implement juce::HostedAudioProcessorParameter —
 *  juce::AudioPluginInstance's own addParameter is deliberately hidden private, and
 *  addHostedParameter refuses anything that is not one — so "no stable id" is expressed by
 *  returning an EMPTY string, not by opting out of the interface entirely. */
class StubLegacyParameter : public juce::HostedAudioProcessorParameter {
public:
    explicit StubLegacyParameter(juce::String name, float defaultValue = 0.0f)
        : name_(std::move(name))
        , value_(defaultValue)
        , defaultValue_(defaultValue) {}

    juce::String getParameterID() const override { return {}; }

    float getValue() const override { return value_; }
    void setValue(float newValue) override { value_ = newValue; }
    float getDefaultValue() const override { return defaultValue_; }
    juce::String getName(int) const override { return name_; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& text) const override { return text.getFloatValue(); }

private:
    juce::String name_;
    float value_;
    float defaultValue_;
};

/** One entry in the constructor's parameter list: a stable id + name builds a StubHostedParameter,
 *  an empty id + name builds a StubLegacyParameter — see StubPluginInstance's ctor. */
struct StubParamSpec {
    juce::String paramId; // empty => legacy/no-id parameter
    juce::String name;
    float defaultValue = 0.0f;
};

/** A juce::AudioPluginInstance that marks the audio it touches and round-trips a state blob.
 *
 *  - processBlock multiplies every output channel by `kGainMarker` — a value no other module in the
 *    graph produces, so "did the hosted instance actually render?" is a single sample comparison.
 *  - getStateInformation/setStateInformation round-trip an arbitrary juce::String payload.
 *  - The destructor records the thread it ran on, so a test can assert the audio thread never frees
 *    an instance. */
class StubPluginInstance : public juce::AudioPluginInstance {
public:
    /** The gain the stub applies to every output channel. Distinctive on purpose. */
    static constexpr float kGainMarker = 0.5f;

    /** Where the last StubPluginInstance destructor ran. Tests compare it against the thread that
     *  drove the render loop. Reset with clearDestructionRecord(). */
    static std::atomic<std::thread::id>& lastDestructionThread() {
        static std::atomic<std::thread::id> id{};
        return id;
    }
    static std::atomic<int>& destructionCount() {
        static std::atomic<int> count{0};
        return count;
    }
    static void clearDestructionRecord() {
        lastDestructionThread().store(std::thread::id{});
        destructionCount().store(0);
    }

    StubPluginInstance(int numInputs, int numOutputs, juce::String pluginName = "Stub Plugin", int uid = 0x5754424,
                       juce::String format = "VST3", std::vector<StubParamSpec> params = {})
        : juce::AudioPluginInstance(
              BusesProperties()
                  .withInput("Input", juce::AudioChannelSet::discreteChannels(juce::jmax(1, numInputs)), numInputs > 0)
                  .withOutput("Output", juce::AudioChannelSet::discreteChannels(juce::jmax(1, numOutputs)),
                              numOutputs > 0))
        , name_(std::move(pluginName))
        , format_(std::move(format))
        , uid_(uid) {
        // TL7-6: a stable id builds the VST3/AU-style stub, an empty one the no-id legacy stub.
        // addHostedParameter (not addParameter, which AudioPluginInstance hides private — every
        // hosted parameter must be a HostedAudioProcessorParameter) takes ownership, exactly like
        // juce::AudioProcessor::addParameter does for our own modules' parameters.
        for (const auto& spec : params) {
            if (spec.paramId.isNotEmpty())
                addHostedParameter(std::make_unique<StubHostedParameter>(spec.paramId, spec.name, spec.defaultValue));
            else
                addHostedParameter(std::make_unique<StubLegacyParameter>(spec.name, spec.defaultValue));
        }
    }

    ~StubPluginInstance() override {
        lastDestructionThread().store(std::this_thread::get_id());
        destructionCount().fetch_add(1);
    }

    //==============================================================================
    // AudioPluginInstance
    //==============================================================================

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = name_;
        description.pluginFormatName = format_;
        description.uniqueId = uid_;
        description.deprecatedUid = uid_;
        description.manufacturerName = "AgentSynth Tests";
        description.version = "1.0";
        description.isInstrument = getTotalNumInputChannels() == 0;
        description.numInputChannels = getTotalNumInputChannels();
        description.numOutputChannels = getTotalNumOutputChannels();
        // A real description carries a path here. Deliberately set, so a test asserting no path
        // leaks into the serialized patch is testing something.
        description.fileOrIdentifier = "/nonexistent/test/path/StubPlugin.vst3";
    }

    const juce::String getName() const override { return name_; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        preparedSampleRate = sampleRate;
        preparedBlockSize = samplesPerBlock;
        ++prepareCount;
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ignoreUnused(midi);
        const int numSamples = buffer.getNumSamples();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGain(channel, 0, numSamples, kGainMarker);
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override {
        destData.reset();
        destData.append(payload.toRawUTF8(), payload.getNumBytesAsUTF8());
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        payload = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    }

    /** The blob this instance round-trips. Set it, save, reload, compare. */
    juce::String payload;

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    int prepareCount = 0;

    using juce::AudioPluginInstance::setLatencySamples;

private:
    juce::String name_;
    juce::String format_;
    int uid_ = 0;
};

/** A backend that resolves ANY identity to a StubPluginInstance built by a factory the test owns.
 *
 *  Threading matches juce::AudioPluginFormat: the callback is posted with
 *  MessageManager::callAsync, never fired re-entrantly, so tests must pump the message loop
 *  (juce::MessageManager::getInstance()->runDispatchLoopUntil(...), as GraphEditorTests does). */
class StubBackend : public synth::HostedPluginBackend {
public:
    using Factory = std::function<std::unique_ptr<StubPluginInstance>()>;

    /** Default: a 2-in / 2-out stub. */
    StubBackend()
        : factory_([] { return std::make_unique<StubPluginInstance>(2, 2); }) {}

    explicit StubBackend(Factory factory)
        : factory_(std::move(factory)) {}

    using synth::HostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double sampleRate, int blockSize,
                             InstanceCallback callback) override {
        juce::ignoreUnused(description);
        ++createCount;
        lastSampleRate = sampleRate;
        lastBlockSize = blockSize;

        if (callback == nullptr)
            return;

        if (failWith.isNotEmpty()) {
            failAsync(std::move(callback), failWith);
            return;
        }

        // callAsync takes a std::function, which must be copy-constructible — so the move-only
        // instance and the callback both ride across in shared_ptrs rather than by capture.
        auto instance = std::make_shared<std::unique_ptr<juce::AudioPluginInstance>>(factory_());
        auto sharedCallback = std::make_shared<InstanceCallback>(std::move(callback));
        juce::MessageManager::callAsync(
            [instance, sharedCallback] { (*sharedCallback)(std::move(*instance), juce::String()); });
    }

    /** Any identity resolves — a stub backend has no scan list to miss. Copies the identity into the
     *  description so the module's own identity bookkeeping is exercised. */
    bool resolveIdentity(const synth::PluginIdentity& identity, juce::PluginDescription& out) const override {
        if (!identity.isValid() || !resolves)
            return false;
        out.name = identity.name;
        out.pluginFormatName = identity.format;
        out.uniqueId = identity.uid;
        out.deprecatedUid = identity.uid;
        return true;
    }

    void setFactory(Factory factory) { factory_ = std::move(factory); }

    /** When set, every create fails with this message instead of producing an instance. */
    juce::String failWith;
    /** When false, resolveIdentity() reports "not installed" — the unscanned-plugin placeholder. */
    bool resolves = true;

    int createCount = 0;
    double lastSampleRate = 0.0;
    int lastBlockSize = 0;

private:
    Factory factory_;
};

} // namespace synth::test
