// The timeline half of the click guard: the notes of a timeline track are played through the transport, with the
// loop on for four passes, and every note start and end is checked for a tick (ClickDetector.h), so the states a
// note is only ever in on the timeline are covered: the first pass, a pass after the loop wrapped, a clip that
// starts later in the loop and a note that is held across the loop end. SourceClickGuardTests.cpp plays notes
// straight into one module and cannot reach those.
//
// The synthetic chain (Track In -> Poly MIDI -> Oscillator -> ADSR -> VCA -> output) always runs. The same check on
// the real projects of the person testing runs only when AGENTSYNTH_REAL_PROJECT_DIR names the folder that holds
// them, and prints each click with the strip, the pass and its distance to the nearest note edge.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioRenderingTestHelpers.h"
#include "ClickDetector.h"
#include "MacroSet.h"
#include "Modules/ADSRModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/GateEdges.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/OfflineTransportDriver.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static_assert(PolyMidiModule::kPitchStepGateLevel == GateEdges::kThreshold,
              "a sounding voice must change pitch where the Oscillator restarts the note");

namespace {

constexpr double kTlRate = 48000.0;
constexpr int kTlBlock = 512;
constexpr double kTlSamplesPerBeat = 24000.0; // 120 bpm
constexpr int kTlPasses = 4;
constexpr const char* kTlTrackUuid = "c1c1c1c1-0000-0000-0000-000000000001";

// A note's span inside one loop pass, in samples from the loop start.
struct TlSpan {
    int start = 0;
    int end = 0;
    int pitch = 0;
};

struct TlNote {
    double clipStart;
    double start; // clip-relative
    double length;
    int pitch;
};

// Two clips in an 8 beat loop: the first at the start, the second starting in bar 2. The notes touch each other,
// change pitch every time, one is short, and the last is held across the loop end.
std::vector<TlNote> tlNotes() {
    return {{0.0, 0.0, 1.0, 57}, {0.0, 1.0, 1.0, 64}, {0.0, 2.0, 0.5, 60}, {0.0, 3.0, 0.75, 69},
            {4.0, 0.0, 1.0, 62}, {4.0, 1.0, 1.0, 67}, {4.0, 2.0, 2.5, 55}};
}

std::vector<TlSpan> tlSpans(const std::vector<TlNote>& notes, double loopBeats) {
    std::vector<TlSpan> spans;
    for (const auto& n : notes) {
        const double on = (n.clipStart + n.start) * kTlSamplesPerBeat;
        const double off =
            std::min((n.clipStart + n.start + n.length) * kTlSamplesPerBeat, loopBeats * kTlSamplesPerBeat);
        spans.push_back({(int)std::lround(on), (int)std::lround(off), n.pitch});
    }
    return spans;
}

// Where a click at `pos` (samples into its pass) sits against the note edges, as text.
std::string tlWhere(int pos, const std::vector<TlSpan>& spans, int loopSamples) {
    int bestDist = 1 << 30;
    std::string text = "away from every note edge";
    for (size_t i = 0; i < spans.size(); ++i) {
        for (const bool isStart : {true, false}) {
            const int edge = isStart ? spans[i].start : spans[i].end;
            const int d = pos - edge;
            if (d >= -96 && d < 9600 && std::abs(d) < std::abs(bestDist)) {
                bestDist = d;
                text = "note " + std::to_string(i) + " (pitch " + std::to_string(spans[i].pitch) + ") " +
                       (isStart ? "start" : "end") + " " + (d >= 0 ? "+" : "") +
                       std::to_string((double)d * 1000.0 / kTlRate).substr(0, 5) + " ms";
            }
        }
    }
    (void)loopSamples;
    return text;
}

// Every click in `x`, one line each, with its pass and its place against the note edges.
std::vector<std::string> tlClickLines(const std::string& label, const std::vector<float>& x, int loopSamples,
                                      const std::vector<TlSpan>& spans) {
    std::vector<std::string> lines;
    for (const auto& c : clickdetector::findClicks(x)) {
        const int pass = c.index / loopSamples;
        const int pos = c.index % loopSamples;
        lines.push_back(label + " pass " + std::to_string(pass) + " sample " + std::to_string(pos) + " size " +
                        std::to_string(c.size).substr(0, 7) + " ratio " + std::to_string(c.ratio).substr(0, 5) + ": " +
                        tlWhere(pos, spans, loopSamples));
    }
    return lines;
}

void tlSetParam(juce::AudioProcessor& p, const char* id, float value) { setParamValue(&p, id, value); }

// Click sizes are given relative to the signal peak, so they read as a fraction of the sound's level.
constexpr float kTlMaxRelativeClick = 0.015f;

enum class TlStage { Source, AfterEnvelope };

struct TlCase {
    float phase;
    int unison;
    int waveform; // 0 Sine, 2 Saw
    TlStage stage;
};

// Track In -> Poly MIDI -> Oscillator [-> ADSR -> VCA] -> Audio Output, built node by node.
struct TlChain {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    synth::TimelineDoc doc;
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    ~TlChain() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }

    bool build(const TlCase& c, const std::vector<TlNote>& notes) {
        engine.initialise();
        auto& graph = engine.getGraph();
        const auto add = [&](const char* type) { return graph.addNode(synth::AIStateMapper::createModule(type)); };
        auto trackIn = add("Track In");
        auto poly = add("Poly MIDI");
        auto osc = add("Oscillator");
        auto adsr = add("ADSR");
        auto vca = add("VCA");
        auto out = add("Audio Output");
        if (!trackIn || !poly || !osc || !adsr || !vca || !out)
            return false;
        trackIn->properties.set("uuid", kTlTrackUuid);
        if (auto* m = dynamic_cast<ModuleBase*>(trackIn->getProcessor()))
            m->setNodeUuid(kTlTrackUuid);

        auto& o = *osc->getProcessor();
        tlSetParam(o, "poly", 1.0f);
        tlSetParam(o, "phase", c.phase);
        tlSetParam(o, "unison", (float)c.unison);
        tlSetParam(o, "detune", 68.6f);
        setChoiceParam(&o, "waveform", c.waveform);
        for (auto* p : {adsr.get(), vca.get()}) {
            auto& proc = *p->getProcessor();
            tlSetParam(proc, "poly", 1.0f);
        }
        tlSetParam(*adsr->getProcessor(), "attack", 0.001f);
        tlSetParam(*adsr->getProcessor(), "decay", 0.0f);
        tlSetParam(*adsr->getProcessor(), "release", 0.015f);
        tlSetParam(*adsr->getProcessor(), "sustain", 1.0f);
        tlSetParam(*vca->getProcessor(), "gain", 0.5f);

        constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;
        graph.addConnection({{trackIn->nodeID, kMidi}, {poly->nodeID, kMidi}});
        graph.addConnection({{trackIn->nodeID, kMidi}, {osc->nodeID, kMidi}});
        for (int v = 0; v < 8; ++v) {
            graph.addConnection({{poly->nodeID, v}, {osc->nodeID, v}});
            graph.addConnection({{poly->nodeID, 8 + v}, {osc->nodeID, OscillatorModule::kGateBase + v}});
            if (c.stage == TlStage::Source) {
                graph.addConnection({{osc->nodeID, v}, {out->nodeID, 0}});
                continue;
            }
            graph.addConnection({{poly->nodeID, 8 + v}, {adsr->nodeID, v}});
            graph.addConnection({{osc->nodeID, v}, {vca->nodeID, v}});
            graph.addConnection({{adsr->nodeID, v}, {vca->nodeID, 8 + v}});
            graph.addConnection({{vca->nodeID, v}, {out->nodeID, 0}});
        }

        const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
        doc.setTrackBinding(trackId, kTlTrackUuid);
        std::vector<double> clipStarts;
        for (const auto& n : notes)
            if (std::find(clipStarts.begin(), clipStarts.end(), n.clipStart) == clipStarts.end())
                clipStarts.push_back(n.clipStart);
        for (const double clipStart : clipStarts) {
            const auto clip = doc.addClip(trackId, clipStart, 4.0, "Clip");
            for (const auto& n : notes) {
                if (n.clipStart != clipStart)
                    continue;
                synth::MidiNote note;
                note.startBeat = n.start;
                note.lengthBeats = n.length;
                note.pitch = n.pitch;
                note.velocity = 100;
                if (!doc.addNote(clip, note).isValid())
                    return false;
            }
        }
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kTlRate, kTlBlock, 2);
        engine.publishTimeline(doc);
        return true;
    }
};

std::string tlCaseName(const TlCase& c) {
    return std::string(c.stage == TlStage::Source ? "source" : "after VCA") + " phase " + std::to_string((int)c.phase) +
           " unison " + std::to_string(c.unison) + (c.waveform == 0 ? " sine" : " saw");
}

} // namespace

class TimelineClickGuard : public ::testing::TestWithParam<TlCase> {};

// Regression test for FRO1228: a timeline note start ticked on the first pass and on later passes because the
// voice's pitch was still sliding while the envelope's attack began, and the attack ended in a corner.
TEST_P(TimelineClickGuard, EveryNoteEdgeOfEveryPassIsClean) {
    const auto c = GetParam();
    const auto notes = tlNotes();
    constexpr double kLoopBeats = 8.0;
    const int loopSamples = (int)(kLoopBeats * kTlSamplesPerBeat);

    TlChain chain;
    ASSERT_TRUE(chain.build(c, notes));
    ASSERT_TRUE(chain.driver->getTransport().setLoop(0.0, kLoopBeats, true));
    ASSERT_TRUE(chain.driver->getTransport().play());

    std::vector<float> out;
    std::int64_t firstStart = -1;
    chain.driver->streamBlocks(kTlPasses * loopSamples / kTlBlock + 1,
                               [&](const juce::AudioBuffer<float>& b, const synth::BlockTimeInfo& info) {
                                   if (firstStart < 0)
                                       firstStart = info.blockStartSample;
                                   out.insert(out.end(), b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
                               });
    ASSERT_EQ(firstStart, 0);
    out.resize((size_t)kTlPasses * (size_t)loopSamples);

    float peak = 0.0f;
    for (float v : out)
        peak = std::max(peak, std::abs(v));
    ASSERT_GT(peak, 0.05f) << tlCaseName(c) << " played nothing";

    const auto lines = tlClickLines(tlCaseName(c), out, loopSamples, tlSpans(notes, kLoopBeats));
    if (c.waveform == 0)
        for (const auto& l : lines)
            std::cout << "[click] " << l << std::endl;
    // A saw's own wrap is a step every cycle, which the detector reads as a click, so a saw is only reported.
    if (c.waveform != 0) {
        std::cout << "[saw] " << tlCaseName(c) << ": " << lines.size() << " detector hits (reported, not asserted)"
                  << std::endl;
        return;
    }
    // The raw oscillator has no envelope, so a sine from it never ticks. After the VCA the only things left are the
    // envelope's own release corner and a retrigger kink, which stay under the limit.
    if (c.stage == TlStage::Source) {
        EXPECT_TRUE(lines.empty()) << lines.size() << " clicks, first: " << lines.front();
        return;
    }
    for (const auto& click : clickdetector::findClicks(out))
        EXPECT_LE(click.size / peak, kTlMaxRelativeClick)
            << tlCaseName(c) << " sample " << click.index << " relative size " << click.size / peak;
}

namespace {
std::vector<TlCase> tlCases() {
    std::vector<TlCase> cases;
    for (const auto stage : {TlStage::Source, TlStage::AfterEnvelope})
        for (const int wave : {0, 2})
            for (const int unison : {1, 7})
                for (const float phase : {0.0f, 90.0f, 250.0f, 274.0f})
                    cases.push_back({phase, unison, wave, stage});
    return cases;
}
} // namespace

INSTANTIATE_TEST_SUITE_P(EveryChain, TimelineClickGuard, ::testing::ValuesIn(tlCases()),
                         [](const ::testing::TestParamInfo<TlCase>& info) {
                             auto s = tlCaseName(info.param);
                             std::replace(s.begin(), s.end(), ' ', '_');
                             return s;
                         });

namespace {

// Sixteen consecutive pitches (MIDI 60-75), each 200 ms long, 300 ms apart, as notes of timeline clips.
std::vector<TlNote> tlSixteenPitchNotes() {
    std::vector<TlNote> notes;
    for (int i = 0; i < 16; ++i) {
        const double startBeat = i * 0.6; // 300 ms at 120 bpm
        const double clipStart = std::floor(startBeat / 4.0) * 4.0;
        notes.push_back({clipStart, startBeat - clipStart, 0.4, 60 + i});
    }
    return notes;
}

// Plays `chain` once from the start (no loop) for `samples` samples and returns the left channel.
std::vector<float> tlPlayOnce(TlChain& chain, int samples) {
    std::vector<float> out;
    if (!chain.driver->getTransport().play())
        return out;
    chain.driver->streamBlocks(samples / kTlBlock + 1,
                               [&](const juce::AudioBuffer<float>& b, const synth::BlockTimeInfo&) {
                                   out.insert(out.end(), b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
                               });
    out.resize((size_t)samples);
    return out;
}

float tlPeak(const std::vector<float>& x) {
    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    return peak;
}

} // namespace

// Regression test for FRO1228: a 1 ms attack ended in a corner that ticked on most notes (12 of 16 flagged, 0.02-0.05
// of the peak). The attack now lands softly, so no note start or end of a sine ticks above 0.015 of its peak, at any
// phase the oscillator can start in.
TEST(TimelineClickGuardAttack, SixteenPitchesNeverTickAboveTheLimit) {
    const auto notes = tlSixteenPitchNotes();
    int total = 0;
    for (const float phase : {0.0f, 90.0f, 250.0f, 274.0f}) {
        TlChain chain;
        ASSERT_TRUE(chain.build({phase, 1, 0, TlStage::AfterEnvelope}, notes));
        const auto out = tlPlayOnce(chain, (int)(16 * 0.6 * kTlSamplesPerBeat) + 24000);
        const float peak = tlPeak(out);
        ASSERT_GT(peak, 0.05f) << "phase " << phase << " played nothing";
        const auto found = clickdetector::findClicks(out);
        std::cout << "[attack] phase " << phase << ": " << found.size() << " clicks" << std::endl;
        total += (int)found.size();
        for (const auto& c : found)
            EXPECT_LE(c.size / peak, kTlMaxRelativeClick) << "phase " << phase << " sample " << c.index
                                                          << " relative size " << c.size / peak << " ratio " << c.ratio;
    }
    std::cout << "[attack] total flagged " << total << std::endl;
}

// Regression test for FRO1228: after a note retriggered on the same voice, the tail of the previous note's restart
// hand-over took the new pitch for a moment, a small tick about 1.5 ms into the new note. The tail now keeps the old
// pitch, so nothing ticks in the window after each touching note's start.
TEST(TimelineClickGuardAttack, NoTickShortlyAfterARetrigger) {
    const auto notes = tlNotes();
    constexpr int kLoopSamples = (int)(8.0 * kTlSamplesPerBeat);
    TlChain chain;
    ASSERT_TRUE(chain.build({0.0f, 1, 0, TlStage::AfterEnvelope}, notes));
    const auto out = tlPlayOnce(chain, kLoopSamples);
    const auto spans = tlSpans(notes, 8.0);
    const auto found = clickdetector::findClicks(out);
    for (size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].start != spans[i - 1].end)
            continue; // not touching, so no retrigger hand-over
        for (const auto& c : found) {
            const double ms = (c.index - spans[i].start) * 1000.0 / kTlRate;
            EXPECT_FALSE(ms >= 1.2 && ms <= 1.8)
                << "note " << i << " ticked " << ms << " ms after its start, size " << c.size / tlPeak(out);
        }
    }
}

// A note starts at its own pitch. A fresh voice steps to it on the note's own sample (it never slides in from
// 0 Hz). A voice that sounded before changes pitch on the sample its gate rises, never earlier (the tail of its
// previous note keeps its pitch) and never later: every note, whatever voice it lands on, is at its own pitch from the
// moment its gate is up.
TEST(TimelineClickGuardPitch, EveryNoteIsAtItsOwnPitchFromItsGateRise) {
    PolyMidiModule midi;
    midi.prepareToPlay(kTlRate, kTlBlock);
    const std::vector<int> pitches = {57, 64, 60, 69, 62};
    const int step = 6000;
    std::vector<float> pitchOut, gateOut;
    for (int pos = 0; pos < step * (int)pitches.size() + 4000; pos += kTlBlock) {
        juce::MidiBuffer in;
        for (size_t i = 0; i < pitches.size(); ++i) {
            const int on = (int)i * step;
            if (on >= pos && on < pos + kTlBlock)
                in.addEvent(juce::MidiMessage::noteOn(1, pitches[i], (juce::uint8)100), on - pos);
            if (on + step - 1 >= pos && on + step - 1 < pos + kTlBlock)
                in.addEvent(juce::MidiMessage::noteOff(1, pitches[i]), on + step - 1 - pos);
        }
        juce::AudioBuffer<float> fan(16, kTlBlock);
        fan.clear();
        midi.processBlock(fan, in);
        pitchOut.insert(pitchOut.end(), fan.getReadPointer(0), fan.getReadPointer(0) + kTlBlock);
        gateOut.insert(gateOut.end(), fan.getReadPointer(8), fan.getReadPointer(8) + kTlBlock);
    }
    for (size_t i = 0; i < pitches.size(); ++i) {
        const float hz = juce::MidiMessage::getMidiNoteInHertz(pitches[i]);
        const int on = (int)i * step;
        for (int s = on; s < on + step - 1; ++s) {
            if (gateOut[(size_t)s] >= 0.1f)
                EXPECT_FLOAT_EQ(pitchOut[(size_t)s], hz) << "note " << i << " sample " << s - on << " after its start";
            else if (i > 0)
                EXPECT_FLOAT_EQ(pitchOut[(size_t)s], juce::MidiMessage::getMidiNoteInHertz(pitches[i - 1]))
                    << "note " << i << " tail sample " << s - on;
        }
    }
}

// The real projects of the person testing: each is loaded, looped for four passes through the transport and
// every mixer strip's output is listed. Reports only; runs only when AGENTSYNTH_REAL_PROJECT_DIR names the folder
// holding them.
TEST(TimelineClickGuardRealProjects, EveryStripOfEveryProjectIsClean) {
    const auto dirText = juce::SystemStats::getEnvironmentVariable("AGENTSYNTH_REAL_PROJECT_DIR", {});
    if (dirText.isEmpty())
        GTEST_SKIP() << "AGENTSYNTH_REAL_PROJECT_DIR is not set";

    int totalClicks = 0;
    for (const char* name : {"FRO848 test.agsproj", "legato-quircks.agsproj"}) {
        const juce::File bundle = juce::File(dirText).getChildFile(name);
        if (!bundle.isDirectory()) {
            std::cout << "[real] " << name << " not found, skipped" << std::endl;
            continue;
        }
        AudioEngine engine(AudioEngine::HostMode::Hosted);
        engine.initialise();
        synth::TimelineDoc doc;
        synth::PatchDocument patchDoc;
        synth::MacroSet macros;
        synth::MidiRemoteProjectDoc midiRemote;
        synth::TransportDoc transportDoc;
        const auto loaded = synth::ProjectBundle::load(bundle, engine.getGraph(), doc, patchDoc, macros, midiRemote,
                                                       nullptr, nullptr, &transportDoc);
        ASSERT_TRUE(loaded.ok) << name << ": " << loaded.message;
        engine.publishTimeline(doc);

        // The loop covers every clip, so a clip that starts after the saved loop end is played too.
        const double loopEnd = std::max(transportDoc.loopEndBeat, doc.getArrangementEndBeat());
        const double samplesPerBeat = kTlRate * 60.0 / transportDoc.bpm;
        const int loopSamples = (int)std::lround(loopEnd * samplesPerBeat);

        std::vector<TlSpan> spans;
        for (const auto& track : doc.getTracks())
            for (const auto& clip : track.clips)
                for (const auto& n : clip.notes) {
                    const double on = (clip.startBeat + n.startBeat) * samplesPerBeat;
                    const double off =
                        std::min((clip.startBeat + n.startBeat + n.lengthBeats) * samplesPerBeat, (double)loopSamples);
                    spans.push_back({(int)std::lround(on), (int)std::lround(off), n.pitch});
                }

        std::vector<ChannelStripModule*> strips;
        std::vector<std::string> stripLabels;
        for (auto* node : engine.getGraph().getNodes())
            if (auto* s = dynamic_cast<ChannelStripModule*>(node->getProcessor())) {
                strips.push_back(s);
                stripLabels.push_back("node " + std::to_string((int)node->nodeID.uid) + " '" +
                                      s->getStripName().toStdString() + "'");
            }
        std::vector<juce::AudioBuffer<float>> taps(strips.size(), juce::AudioBuffer<float>(2, kTlBlock));
        std::vector<std::vector<float>> stems(strips.size());

        synth::OfflineTransportDriver driver(engine, kTlRate, kTlBlock, 2);
        ASSERT_TRUE(driver.getTransport().setBpm(transportDoc.bpm));
        ASSERT_TRUE(driver.getTransport().setLoop(0.0, loopEnd, true));
        ASSERT_TRUE(driver.getTransport().play());
        for (size_t i = 0; i < strips.size(); ++i)
            strips[i]->setStemTapBuffer(&taps[i]);
        driver.streamBlocks(
            kTlPasses * loopSamples / kTlBlock + 1, [&](const juce::AudioBuffer<float>&, const synth::BlockTimeInfo&) {
                for (size_t i = 0; i < strips.size(); ++i)
                    stems[i].insert(stems[i].end(), taps[i].getReadPointer(0), taps[i].getReadPointer(0) + kTlBlock);
            });
        for (auto* s : strips)
            s->setStemTapBuffer(nullptr);

        for (size_t i = 0; i < strips.size(); ++i) {
            stems[i].resize((size_t)kTlPasses * (size_t)loopSamples);
            const auto lines =
                tlClickLines(std::string(name) + " strip " + stripLabels[i], stems[i], loopSamples, spans);
            std::cout << "[real] " << name << " strip " << stripLabels[i] << ": " << lines.size() << " clicks"
                      << std::endl;
            for (const auto& l : lines)
                std::cout << "[click] " << l << std::endl;
            totalClicks += (int)lines.size();
        }
        engine.releaseFromHost();
        engine.shutdown();
    }
    std::cout << "[real] total clicks " << totalClicks << std::endl;
}
