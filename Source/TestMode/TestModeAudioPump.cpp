// Concern: the test-mode `audioPump` command. The audio thread belongs to SilentAudioIODevice, so this only keeps its
// callback attached, reports how many blocks it has rendered and hands notes to the engine's MIDI input seam.
// `levels` pauses that callback and renders offline, the way a bounce does.

#include "TestMode/TestModeAudioPump.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/SilentAudioDevice.h"
#include "MainComponent/MainComponent.h"
#include "Modules/MidiKeyboardModule.h"
#include "TestMode/TestModeComponents.h"
#include "TestMode/TestModeLevel.h"

namespace synth::testmode {

namespace {

constexpr const char* kNoteSource = "automation";
constexpr double kLevelSampleRate = 44100.0;
constexpr int kLevelBlockSize = 512;

MainComponent* pumpMainComponent() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        return dynamic_cast<MainComponent*>(window->getContentComponent());
    return nullptr;
}

juce::var result(AudioEngine& engine) {
    std::int64_t blocks = 0;
    if (auto* device = dynamic_cast<SilentAudioIODevice*>(engine.getDeviceManager().getCurrentAudioDevice()))
        blocks = device->blocksRendered();
    auto* o = new juce::DynamicObject();
    o->setProperty("running", true);
    o->setProperty("blocks", (juce::int64)blocks);
    return juce::var(o);
}

// Plays `note` at each velocity for `holdMs`, measuring that window, then lets it ring out for `releaseMs`. The note
// goes in through every MIDI Keyboard module's key state (the module a user cables into Poly MIDI), which is how a
// live note reaches the graph: the host MIDI buffer feeds no module. The silent device's callback is paused while this
// renders offline (not at the wall clock's pace) and re-attached after, the way BounceSession/StemSession do. Returns
// an empty list with no MIDI Keyboard in the graph.
std::vector<LevelReading> measureVelocities(AudioEngine& engine, int note, const std::vector<int>& velocities,
                                            int holdMs, int releaseMs) {
    std::vector<MidiKeyboardModule*> keyboards;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* keyboard = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            keyboards.push_back(keyboard);
    std::vector<LevelReading> out;
    if (keyboards.empty())
        return out;

    const auto& graph = engine.getGraph();
    const double previousRate = graph.getSampleRate();
    const int previousBlock = graph.getBlockSize();
    const int previousIn = graph.getTotalNumInputChannels();
    const int previousOut = graph.getTotalNumOutputChannels();
    const bool wasAttached = engine.suspendDeviceCallback();
    engine.prepareForHost(kLevelSampleRate, kLevelBlockSize, 0, 2);

    juce::AudioBuffer<float> buffer(2, kLevelBlockSize);
    auto renderNow = [&](int ms, auto onFirstBlock) {
        LevelMeter meter;
        const int blocks = std::max(1, (int)std::ceil(ms * kLevelSampleRate / 1000.0 / kLevelBlockSize));
        for (int b = 0; b < blocks; ++b) {
            if (b == 0)
                onFirstBlock();
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
            meter.add(buffer);
        }
        return meter.reading();
    };
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

    if (wasAttached)
        engine.resumeDeviceCallback(); // re-prepares at the device's rate and block size
    else if (previousRate > 0.0 && previousBlock > 0)
        engine.prepareForHost(previousRate, previousBlock, previousIn, previousOut);
    return out;
}

// {levels: {note, velocities, ms, releaseMs, requireRising, floor}} plays the note at each velocity and reports the
// output level of each; with requireRising it fails unless every level is above `floor` and each is louder than the
// one before.
StepResult cmdLevels(AudioEngine& engine, const juce::var& spec) {
    std::vector<int> velocities;
    if (auto* list = spec.getProperty("velocities", {}).getArray())
        for (const auto& v : *list)
            velocities.push_back(juce::jlimit(1, 127, (int)v));
    if (velocities.empty())
        return StepResult::failed("levels needs a non-empty `velocities` list");
    const auto readings = measureVelocities(engine, juce::jlimit(0, 127, (int)spec.getProperty("note", 60)), velocities,
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

// {run: true|false} reports the device's block counter (true also re-attaches a callback feedVisual detached);
// {note: N, down: true|false, velocity?: 1-127 (100)} sends a note through handleIncomingMidiMessageFromSource, as a
// MIDI input would.
StepResult cmdAudioPump(const juce::var& args, Scratch&, int) {
    auto* mc = pumpMainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    auto& engine = mc->getAudioEngine();
    if (!engine.usesSilentAudioDevice())
        return StepResult::failed("this launch has no silent audio device");
    if (args.hasProperty("levels"))
        return cmdLevels(engine, args["levels"]);
    if (args.hasProperty("note")) {
        const int note = juce::jlimit(0, 127, (int)args["note"]);
        auto message = (bool)args.getProperty("down", true)
                           ? juce::MidiMessage::noteOn(
                                 1, note, (juce::uint8)juce::jlimit(1, 127, (int)args.getProperty("velocity", 100)))
                           : juce::MidiMessage::noteOff(1, note);
        message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        engine.handleIncomingMidiMessageFromSource(kNoteSource, message);
        return StepResult::done(result(engine));
    }
    if (!args.hasProperty("run"))
        return StepResult::failed("audioPump takes {run: true|false}, {note: N, down: true|false} or {levels: {...}}");
    if ((bool)args["run"])
        engine.resumeDeviceCallback();
    return StepResult::done(result(engine));
}

} // namespace

void registerAudioPumpCommands(CommandTable& table) { table.add("audioPump", {cmdAudioPump}); }

} // namespace synth::testmode
