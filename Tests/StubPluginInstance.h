#pragma once

#include "../Source/Plugin/Hosting/HostedPluginBackend.h"
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <thread>
#include <vector>

// A fake third-party plugin, and a HostedPluginBackend that hands it out.
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

/** A trivial, resizable dummy editor for StubPluginInstance. Exists purely so
 *  HostedPluginEditorWindowTests has a real, non-generic juce::AudioProcessorEditor to open and
 *  resize — it draws nothing meaningful and is never actually shown on screen. */
class StubPluginEditor : public juce::AudioProcessorEditor {
public:
    explicit StubPluginEditor(juce::AudioProcessor& proc)
        : juce::AudioProcessorEditor(proc) {
        setResizable(true, true);
        setSize(320, 240);
    }
    ~StubPluginEditor() override = default;

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StubPluginEditor)
};

/** A stable-id (VST3/AU/LV2-style) stub parameter. Implements
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
 *  a plugin with no persistent parameter identity ("plugins without WithID parameters" case; see
 *  juce_VSTPluginFormat.cpp's own parameter class, whose getParameterID() likewise always
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
 *  - ...and then DELAYS it by exactly the latency it reports. A plugin that claims 256
 *    samples of lookahead but renders in place would make a PDC test pass while proving nothing:
 *    the dry parallel path would be delayed by 256 and the "compensated" one would not be, and the
 *    misalignment the compensation exists to fix would never appear. So the stub is honest — the
 *    delay line is the thing under test as much as the graph's compensation is.
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

    // reportsEditor: when true, hasEditor() and createEditor() report and build a real
    // StubPluginEditor instead of the base default (no editor) — HostedPluginEditorWindowTests uses
    // this to exercise both the custom-editor and the GenericAudioProcessorEditor-fallback paths.
    //
    // initialLatency is deliberately LAST: every existing call site names its arguments
    // positionally, so a new parameter anywhere else would have to touch all of them.
    StubPluginInstance(int numInputs, int numOutputs, juce::String pluginName = "Stub Plugin", int uid = 0x5754424,
                       juce::String format = "VST3", std::vector<StubParamSpec> params = {}, bool reportsEditor = false,
                       int initialLatency = 0)
        : juce::AudioPluginInstance(
              BusesProperties()
                  .withInput("Input", juce::AudioChannelSet::discreteChannels(juce::jmax(1, numInputs)), numInputs > 0)
                  .withOutput("Output", juce::AudioChannelSet::discreteChannels(juce::jmax(1, numOutputs)),
                              numOutputs > 0))
        , name_(std::move(pluginName))
        , format_(std::move(format))
        , uid_(uid)
        , reportsEditor_(reportsEditor) {
        // A stable id builds the VST3/AU-style stub, an empty one the no-id legacy stub.
        // addHostedParameter (not addParameter, which AudioPluginInstance hides private — every
        // hosted parameter must be a HostedAudioProcessorParameter) takes ownership, exactly like
        // juce::AudioProcessor::addParameter does for our own modules' parameters.
        for (const auto& spec : params) {
            if (spec.paramId.isNotEmpty())
                addHostedParameter(std::make_unique<StubHostedParameter>(spec.paramId, spec.name, spec.defaultValue));
            else
                addHostedParameter(std::make_unique<StubLegacyParameter>(spec.name, spec.defaultValue));
        }

        if (initialLatency > 0)
            setReportedLatency(initialLatency);
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
        resizeDelayLine();
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ignoreUnused(midi);
        const int numSamples = buffer.getNumSamples();
        const int latency = getLatencySamples();

        // No reported latency (or a line we were never prepared for): apply gain only, no delay.
        if (latency <= 0 || delayLine.getNumSamples() < latency) {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.applyGain(channel, 0, numSamples, kGainMarker);
            return;
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            if (channel >= delayLine.getNumChannels()) {
                buffer.applyGain(channel, 0, numSamples, kGainMarker); // undelayed, but still marked
                continue;
            }

            auto* line = delayLine.getWritePointer(channel);
            auto* data = buffer.getWritePointer(channel);
            int writeIndex = delayWritePos;

            for (int sample = 0; sample < numSamples; ++sample) {
                const float marked = data[sample] * kGainMarker;
                data[sample] = line[writeIndex];
                line[writeIndex] = marked;
                if (++writeIndex >= latency)
                    writeIndex = 0;
            }
        }

        delayWritePos = (delayWritePos + numSamples) % latency;
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return reportsEditor_; }
    juce::AudioProcessorEditor* createEditor() override {
        return reportsEditor_ ? new StubPluginEditor(*this) : nullptr;
    }

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

    /** "The user flipped a lookahead mode in the plugin's own editor", message thread.
     *  juce::AudioProcessor::setLatencySamples is what fires audioProcessorChanged(...
     *  withLatencyChanged(true)) on every listener, so this IS the notification path under test; the
     *  delay line is resized to match so the instance stays honest about what it reports.
     *
     *  Not thread-safe against a concurrent processBlock (it reallocates), which is fine for the
     *  block-by-block, single-threaded harness these tests drive — a real plugin would swap a
     *  pre-allocated line instead. */
    void setReportedLatency(int samples) {
        setLatencySamples(juce::jmax(0, samples));
        resizeDelayLine();
    }

private:
    /** Sizes (and clears) the delay line for the currently reported latency. */
    void resizeDelayLine() {
        const int latency = getLatencySamples();
        const int channels = juce::jmax(1, getTotalNumInputChannels(), getTotalNumOutputChannels());
        delayLine.setSize(channels, juce::jmax(1, latency), false, true, true);
        delayLine.clear();
        delayWritePos = 0;
    }

    juce::String name_;
    juce::String format_;
    int uid_ = 0;
    bool reportsEditor_ = false;

    // The honest-latency delay line — see the class comment. One slot per reported sample, per
    // channel; the first getLatencySamples() samples out of a freshly sized line are silence,
    // exactly like a real lookahead buffer's priming.
    juce::AudioBuffer<float> delayLine;
    int delayWritePos = 0;
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
