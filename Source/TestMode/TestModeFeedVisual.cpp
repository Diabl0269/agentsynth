// Concern: the test-mode `feedVisual` command. A module's displays read lock-free circular buffers (VisualBuffer)
// that the audio thread fills with pushBlock, one writer by contract. With no audio device nothing fills them, so
// this command fills the very same buffers through the same pushBlock call: no new lock, no new queue, nothing on the
// audio path changes. It refuses while the pump or a device could also write, so the single-writer contract holds.

#include "TestMode/TestModeFeedVisual.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/FilterModule.h"
#include "TestMode/TestModeAudioPump.h"
#include "TestMode/TestModeComponents.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <cmath>
#include <vector>

namespace synth::testmode {

namespace {

constexpr double kDefaultHz = 440.0;
constexpr float kDefaultAmp = 0.5f;
constexpr int kMaxSamples = 1 << 16;

std::vector<float> makeSignal(FeedKind kind, double hz, float amp, int numSamples, double sampleRate) {
    std::vector<float> out((size_t)numSamples, 0.0f);
    if (kind == FeedKind::silence)
        return out;
    juce::Random random(12345); // fixed seed: the same noise on every run
    const double step = juce::MathConstants<double>::twoPi * hz / sampleRate;
    for (int i = 0; i < numSamples; ++i)
        out[(size_t)i] =
            kind == FeedKind::sine ? amp * (float)std::sin(step * i) : amp * (random.nextFloat() * 2.0f - 1.0f);
    return out;
}

bool parseKind(const juce::String& text, FeedKind& kind) {
    if (text == "sine" || text.isEmpty())
        kind = FeedKind::sine;
    else if (text == "noise")
        kind = FeedKind::noise;
    else if (text == "silence")
        kind = FeedKind::silence;
    else
        return false;
    return true;
}

// {id, kind, hz, amp, ms}: `id` names a module card; `ms` (default 100) is how much signal to write.
StepResult cmdFeedVisual(const juce::var& args, Scratch&, int) {
    FeedKind kind{};
    if (!parseKind(args.getProperty("kind", {}).toString(), kind))
        return StepResult::failed("feedVisual kind is sine, noise or silence");
    const auto id = args.getProperty("id", {}).toString();
    if (id.isEmpty())
        return StepResult::failed("missing \"id\"");
    const auto found = findComponent(id);
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    auto* card = dynamic_cast<ModuleComponent*>(found.component);
    if (card == nullptr || card->getModule() == nullptr)
        return StepResult::failed("feedVisual needs a module card id such as \"card:Filter\"");
    if (audioPumpRunning())
        return StepResult::failed("the audio pump is running: send audioPump {run: false} before feedVisual");
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        if (auto* mc = dynamic_cast<MainComponent*>(window->getContentComponent()))
            if (mc->getAudioEngine().isReceivingDeviceCallbacks())
                return StepResult::failed("an audio device is running: feedVisual is only for launches without one");
    const double sampleRate = 44100.0;
    const int numSamples =
        juce::jlimit(1, kMaxSamples, (int)(sampleRate * (double)args.getProperty("ms", 100) / 1000.0));
    const double hz = args.hasProperty("hz") ? (double)args["hz"] : kDefaultHz;
    const float amp = args.hasProperty("amp") ? (float)(double)args["amp"] : kDefaultAmp;
    const int written = feedVisualBuffers(*card->getModule(), kind, hz, amp, numSamples, sampleRate);
    if (written == 0)
        return StepResult::failed("this module has no scope or spectrum buffer to feed");
    auto* o = new juce::DynamicObject();
    o->setProperty("buffers", written);
    o->setProperty("samples", numSamples);
    return StepResult::done(juce::var(o));
}

} // namespace

int feedVisualBuffers(juce::AudioProcessor& module, FeedKind kind, double hz, float amp, int numSamples,
                      double sampleRate) {
    const auto signal = makeSignal(kind, hz, amp, numSamples, sampleRate);
    int written = 0;
    if (auto* base = dynamic_cast<ModuleBase*>(&module)) {
        if (auto* vb = base->getVisualBuffer()) {
            vb->pushBlock(signal.data(), numSamples);
            ++written;
        }
    }
    if (auto* filter = dynamic_cast<FilterModule*>(&module)) {
        filter->getSpectrumBuffer().pushBlock(signal.data(), numSamples);
        ++written;
    }
    return written;
}

void registerFeedVisualCommands(CommandTable& table) { table.add("feedVisual", {cmdFeedVisual}); }

} // namespace synth::testmode
