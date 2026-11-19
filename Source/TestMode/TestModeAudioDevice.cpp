// Concern: the test-mode `audioDevice` command. The device is SilentAudioIODevice; rate and block go through the same
// AudioDeviceManager::setAudioDeviceSetup call the Audio settings tab uses, a restart is closeAudioDevice +
// restartLastAudioDevice, and `require` compares the device's numbers with what the engine took from it.

#include "TestMode/TestModeAudioDevice.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SilentAudioDevice.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"

namespace synth::testmode {

namespace {

int restartCount = 0;

MainComponent* deviceMainComponent() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        return dynamic_cast<MainComponent*>(window->getContentComponent());
    return nullptr;
}

SilentAudioIODevice* silentDevice(AudioEngine& engine) {
    return dynamic_cast<SilentAudioIODevice*>(engine.getDeviceManager().getCurrentAudioDevice());
}

SilentAudioDeviceType* silentType(AudioEngine& engine) {
    for (auto* type : engine.getDeviceManager().getAvailableDeviceTypes())
        if (auto* silent = dynamic_cast<SilentAudioDeviceType*>(type))
            return silent;
    return nullptr;
}

struct Numbers {
    double rate = 0.0;
    int block = 0;
    int latency = 0;
    double engineRate = 0.0;
    int engineBlock = 0;
    int engineLatency = 0;
    std::int64_t blocks = 0;
};

Numbers read(AudioEngine& engine) {
    Numbers n;
    if (auto* device = silentDevice(engine)) {
        n.rate = device->getCurrentSampleRate();
        n.block = device->getCurrentBufferSizeSamples();
        n.latency = device->getOutputLatencyInSamples();
        n.blocks = device->blocksRendered();
    }
    n.engineRate = engine.getGraph().getSampleRate();
    n.engineBlock = engine.getGraph().getBlockSize();
    n.engineLatency = engine.getOutputLatencySamples();
    return n;
}

juce::var reply(const Numbers& n) {
    auto* o = new juce::DynamicObject();
    o->setProperty("rate", n.rate);
    o->setProperty("block", n.block);
    o->setProperty("latency", n.latency);
    o->setProperty("engineRate", n.engineRate);
    o->setProperty("engineBlock", n.engineBlock);
    o->setProperty("engineLatency", n.engineLatency);
    o->setProperty("blocks", (juce::int64)n.blocks);
    o->setProperty("restarts", restartCount);
    return juce::var(o);
}

bool isPositiveNumber(const juce::var& v) { return (v.isInt() || v.isInt64() || v.isDouble()) && (double)v > 0.0; }

// Empty when `spec` has only the keys in `allowed`, else the failure text.
juce::String unknownKey(const juce::var& spec, std::initializer_list<const char*> allowed, const char* where) {
    if (auto* object = spec.getDynamicObject())
        for (const auto& property : object->getProperties()) {
            bool known = false;
            for (const char* name : allowed)
                known = known || property.name.toString() == name;
            if (!known)
                return juce::String("audioDevice ") + where + " has no `" + property.name.toString() + "`";
        }
    return {};
}

// Compares one `require` entry with the device's and the engine's view; empty when both match.
juce::String mismatch(const char* name, double wanted, double device, double engine) {
    if (device == wanted && engine == wanted)
        return {};
    return juce::String(name) + " wanted " + juce::String(wanted) + ", device has " + juce::String(device) +
           ", engine has " + juce::String(engine);
}

juce::String checkRequire(const juce::var& require, const Numbers& n) {
    juce::StringArray problems;
    auto add = [&](juce::String p) {
        if (p.isNotEmpty())
            problems.add(p);
    };
    if (require.hasProperty("rate"))
        add(mismatch("rate", (double)require["rate"], n.rate, n.engineRate));
    if (require.hasProperty("block"))
        add(mismatch("block", (double)require["block"], n.block, n.engineBlock));
    if (require.hasProperty("latency"))
        add(mismatch("latency", (double)require["latency"], n.latency, n.engineLatency));
    return problems.joinIntoString("; ");
}

// Restarts the way JUCE does for a device that went away and came back: closes it, then reopens the last setup.
void restartDevice(AudioEngine& engine) {
    engine.getDeviceManager().closeAudioDevice();
    engine.getDeviceManager().restartLastAudioDevice();
    ++restartCount;
}

// {latency?: samples, rate?: Hz, block?: samples, restart?: true, require?: {rate?, block?, latency?}}. All optional;
// the reply always has the device's and the engine's numbers.
StepResult cmdAudioDevice(const juce::var& args, Scratch&, int) {
    auto* mc = deviceMainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    auto& engine = mc->getAudioEngine();
    if (!engine.usesSilentAudioDevice())
        return StepResult::failed("this launch has no silent audio device");
    if (const auto bad = unknownKey(args, {"latency", "rate", "block", "restart", "require"}, "takes only");
        bad.isNotEmpty())
        return StepResult::failed(bad);
    for (const char* name : {"rate", "block"})
        if (args.hasProperty(name) && !isPositiveNumber(args[name]))
            return StepResult::failed(juce::String("audioDevice `") + name + "` must be a positive number");
    if (args.hasProperty("latency") &&
        !((args["latency"].isInt() || args["latency"].isDouble()) && (double)args["latency"] >= 0.0))
        return StepResult::failed("audioDevice `latency` must be a number of samples, 0 or more");
    if (args.hasProperty("require"))
        if (const auto bad = unknownKey(args["require"], {"rate", "block", "latency"}, "`require` takes only");
            bad.isNotEmpty())
            return StepResult::failed(bad);

    auto& manager = engine.getDeviceManager();
    if (silentDevice(engine) == nullptr)
        return StepResult::failed("the silent audio device is not open");

    bool needsRestart = (bool)args.getProperty("restart", false);
    if (args.hasProperty("latency")) {
        const int latency = (int)args["latency"];
        silentDevice(engine)->setOutputLatency(latency);
        if (auto* type = silentType(engine))
            type->setOutputLatency(latency); // later devices keep it
        needsRestart = true;                 // the engine reads it at the next start, as with a driver
    }
    if (args.hasProperty("rate") || args.hasProperty("block")) {
        auto setup = manager.getAudioDeviceSetup();
        if (args.hasProperty("rate"))
            setup.sampleRate = (double)args["rate"];
        if (args.hasProperty("block"))
            setup.bufferSize = (int)args["block"];
        if (const auto error = manager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
            return StepResult::failed("setAudioDeviceSetup failed: " + error);
    }
    if (needsRestart)
        restartDevice(engine);

    const auto numbers = read(engine);
    if (args.hasProperty("require"))
        if (const auto bad = checkRequire(args["require"], numbers); bad.isNotEmpty())
            return StepResult::failed("require not met: " + bad);
    return StepResult::done(reply(numbers));
}

} // namespace

void registerAudioDeviceCommands(CommandTable& table) { table.add("audioDevice", {cmdAudioDevice}); }

} // namespace synth::testmode
