// Concern: the test-mode audio pump. With no audio device the graph never renders; the pump renders it on the message
// thread at the wall clock's pace, the way a bounce renders a live app's patch (BounceSession: nothing else clocks the
// graph, prepareForHost, then processHostBlock per block), so the displays fed by the audio thread move as they do
// with a device.

#include "TestMode/TestModeAudioPump.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/MidiKeyboardModule.h"
#include "TestMode/TestModeComponents.h"
#include "TestMode/TestModeLevel.h"
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

    void addNote(int note, bool down, int velocity = 100) {
        pendingMidi.addEvent(down ? juce::MidiMessage::noteOn(1, note, (juce::uint8)juce::jlimit(1, 127, velocity))
                                  : juce::MidiMessage::noteOff(1, note),
                             0);
    }

    /** Renders `ms` of audio now (not at the wall clock's pace); returns its level. `onFirstBlock` runs just before
     * the first block (it plays the note). */
    template <typename Fn>
    LevelReading renderNow(int ms, Fn onFirstBlock) {
        LevelMeter meter;
        auto& engine = owner->getAudioEngine();
        const int blocks = std::max(1, (int)std::ceil(ms * kSampleRate / 1000.0 / kBlockSize));
        for (int b = 0; b < blocks; ++b) {
            if (b == 0)
                onFirstBlock();
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
            meter.add(buffer);
        }
        return meter.reading();
    }

    /** Plays `note` at each velocity for `holdMs`, measuring that window, then lets it ring out for `releaseMs`. The
     * note goes in through every MIDI Keyboard module's key state (the module a user cables into Poly MIDI), which is
     * how a live note reaches the graph: the host MIDI buffer feeds no module. Returns an empty list with no MIDI
     * Keyboard in the graph. */
    std::vector<LevelReading> measureVelocities(int note, const std::vector<int>& velocities, int holdMs,
                                                int releaseMs) {
        std::vector<MidiKeyboardModule*> keyboards;
        for (auto* node : owner->getAudioEngine().getGraph().getNodes())
            if (auto* keyboard = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
                keyboards.push_back(keyboard);
        std::vector<LevelReading> out;
        if (keyboards.empty())
            return out;
        for (int velocity : velocities) {
            const float level = (float)juce::jlimit(1, 127, velocity) / 127.0f;
            out.push_back(renderNow(holdMs, [&] {
                for (auto* k : keyboards)
                    k->getKeyboardState().noteOn(1, note, level);
            }));
            renderNow(releaseMs, [&] {
                for (auto* k : keyboards)
                    k->getKeyboardState().noteOff(1, note, 0.0f);
            });
        }
        return out;
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

// {levels: {note, velocities, ms, releaseMs, requireRising, floor}} plays the note at each velocity on the running pump
// and reports the output level of each; with requireRising it fails unless every level is above `floor` and each is
// louder than the one before.
StepResult cmdLevels(const juce::var& spec) {
    if (pump() == nullptr)
        return StepResult::failed("the pump is not running: send {run: true} first");
    std::vector<int> velocities;
    if (auto* list = spec.getProperty("velocities", {}).getArray())
        for (const auto& v : *list)
            velocities.push_back(juce::jlimit(1, 127, (int)v));
    if (velocities.empty())
        return StepResult::failed("levels needs a non-empty `velocities` list");
    const auto readings = pump()->measureVelocities(juce::jlimit(0, 127, (int)spec.getProperty("note", 60)), velocities,
                                                    juce::jlimit(10, 5000, (int)spec.getProperty("ms", 300)),
                                                    juce::jlimit(0, 5000, (int)spec.getProperty("releaseMs", 500)));
    if (readings.empty())
        return StepResult::failed("levels plays through a MIDI Keyboard module: add one and cable it to Poly MIDI");
    juce::Array<juce::var> rows;
    std::vector<double> rms;
    for (size_t i = 0; i < readings.size(); ++i) {
        auto* row = new juce::DynamicObject();
        row->setProperty("velocity", velocities[i]);
        row->setProperty("rms", readings[i].rms);
        row->setProperty("peak", readings[i].peak);
        rows.add(juce::var(row));
        rms.push_back(readings[i].rms);
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("levels", rows);
    if ((bool)spec.getProperty("requireRising", false)) {
        const auto verdict = risingVerdict(velocities, rms, (double)spec.getProperty("floor", 1.0e-4));
        if (!verdict.empty()) {
            juce::String all;
            for (size_t i = 0; i < rms.size(); ++i)
                all << (i ? ", " : "") << velocities[i] << ": rms " << juce::String(rms[i], 6) << " peak "
                    << juce::String(readings[i].peak, 6);
            return StepResult::failed(juce::String(verdict) + " [" + all + "]");
        }
    }
    return StepResult::done(juce::var(o));
}

// {run: true|false} starts or stops the pump; {note: N, down: true|false, velocity?: 1-127 (100)} sends a note to the
// running pump.
StepResult cmdAudioPump(const juce::var& args, Scratch&, int) {
    auto* mc = pumpMainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    if (args.hasProperty("levels"))
        return cmdLevels(args["levels"]);
    if (args.hasProperty("note")) {
        if (pump() == nullptr)
            return StepResult::failed("the pump is not running: send {run: true} first");
        pump()->addNote(juce::jlimit(0, 127, (int)args["note"]), (bool)args.getProperty("down", true),
                        (int)args.getProperty("velocity", 100));
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

bool audioPumpRunning() { return pump() != nullptr; }

void registerAudioPumpCommands(CommandTable& table) { table.add("audioPump", {cmdAudioPump}); }

} // namespace synth::testmode
