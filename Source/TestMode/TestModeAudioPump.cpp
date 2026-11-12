// Concern: the test-mode audio pump. With no audio device the graph never renders; the pump renders it on the message
// thread at the wall clock's pace, the way a bounce renders a live app's patch (BounceSession: nothing else clocks the
// graph, prepareForHost, then processHostBlock per block), so the displays fed by the audio thread move as they do
// with a device.

#include "TestMode/TestModeAudioPump.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"
#include <memory>

namespace synth::testmode {

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr int kMaxBlocksPerTick = 16; // a stalled message thread skips ahead instead of rendering a backlog

class AudioPump : private juce::Timer {
public:
    explicit AudioPump(MainComponent& ownerToPump)
        : owner(&ownerToPump) {
        buffer.setSize(2, kBlockSize);
        auto& engine = ownerToPump.getAudioEngine();
        engine.releaseFromHost();
        engine.prepareForHost(kSampleRate, kBlockSize, 0, 2);
        startMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(100);
    }

    ~AudioPump() override { stopTimer(); }

    void addNote(int note, bool down) {
        pendingMidi.addEvent(
            down ? juce::MidiMessage::noteOn(1, note, (juce::uint8)100) : juce::MidiMessage::noteOff(1, note), 0);
    }

    std::int64_t blocksRendered() const noexcept { return rendered; }

private:
    // The main window can close before the pump is dropped at shutdown, so the engine is looked up through it on
    // every tick and the pump stops once it is gone.
    void timerCallback() override {
        if (owner == nullptr) {
            stopTimer();
            return;
        }
        auto& engine = owner->getAudioEngine();
        const auto due =
            (std::int64_t)((juce::Time::getMillisecondCounterHiRes() - startMs) * kSampleRate / 1000.0 / kBlockSize);
        if (due - rendered > kMaxBlocksPerTick)
            rendered = due - kMaxBlocksPerTick;
        while (rendered < due) {
            buffer.clear();
            juce::MidiBuffer midi;
            midi.swapWith(pendingMidi);
            engine.processHostBlock(buffer, midi);
            ++rendered;
        }
    }

    juce::Component::SafePointer<MainComponent> owner;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer pendingMidi;
    double startMs = 0.0;
    std::int64_t rendered = 0;
};

// Held by a DeletedAtShutdown object, so the pump's timer goes before JUCE does (never a plain static: exit order).
struct PumpHolder : public juce::DeletedAtShutdown {
    std::unique_ptr<AudioPump> pump;
};

std::unique_ptr<AudioPump>& pump() {
    static PumpHolder* holder = new PumpHolder();
    return holder->pump;
}

MainComponent* pumpMainComponent() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        return dynamic_cast<MainComponent*>(window->getContentComponent());
    return nullptr;
}

juce::var result(bool running) {
    auto* o = new juce::DynamicObject();
    o->setProperty("running", running);
    o->setProperty("blocks", running ? (juce::int64)pump()->blocksRendered() : (juce::int64)0);
    return juce::var(o);
}

// {run: true|false} starts or stops the pump; {note: N, down: true|false} sends a note to the running pump.
StepResult cmdAudioPump(const juce::var& args, Scratch&, int) {
    auto* mc = pumpMainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    if (args.hasProperty("note")) {
        if (pump() == nullptr)
            return StepResult::failed("the pump is not running: send {run: true} first");
        pump()->addNote(juce::jlimit(0, 127, (int)args["note"]), (bool)args.getProperty("down", true));
        return StepResult::done(result(true));
    }
    if (!args.hasProperty("run"))
        return StepResult::failed("audioPump takes {run: true|false} or {note: N, down: true|false}");
    if (!(bool)args["run"]) {
        pump().reset();
        return StepResult::done(result(false));
    }
    if (mc->getAudioEngine().isReceivingDeviceCallbacks())
        return StepResult::failed("an audio device is running: the pump is only for launches without one");
    if (pump() == nullptr)
        pump() = std::make_unique<AudioPump>(*mc);
    return StepResult::done(result(true));
}

} // namespace

void registerAudioPumpCommands(CommandTable& table) { table.add("audioPump", {cmdAudioPump}); }

} // namespace synth::testmode
