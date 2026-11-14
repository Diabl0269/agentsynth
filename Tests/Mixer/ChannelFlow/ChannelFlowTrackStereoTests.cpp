// ChannelFlowTrackStereoTests.cpp
//
// Kinds: jack-raw-mismatch
//
// The stereo image of an instrument must survive "Add Track -> Instrument": the Oscillator's / Wavetable's right
// leg (unison spread, Pan) has to reach the Audio Output through the VCA and the default channel, not stop at the
// VCA where the right leg used to be a copy of the left. Every case builds the track the way the Add Track menu
// does, holds a note through Track In and measures the two legs at the Audio Output.

#include "../../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Timeline/TimelineOps.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

namespace {

struct Legs {
    std::vector<float> left, right;
};

double rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v)
        s += (double)x * x;
    return std::sqrt(s / (double)std::max<size_t>(1, v.size()));
}

double peak(const std::vector<float>& v) {
    float p = 0;
    for (float x : v)
        p = std::max(p, std::abs(x));
    return p;
}

double correlation(const Legs& l) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < l.left.size(); ++i) {
        ab += (double)l.left[i] * l.right[i];
        aa += (double)l.left[i] * l.left[i];
        bb += (double)l.right[i] * l.right[i];
    }
    return ab / std::sqrt(aa * bb + 1e-30);
}

double toDb(double x) { return 20.0 * std::log10(x + 1e-12); }

void setParam(juce::AudioProcessor& m, const char* id, float value) {
    if (auto* p = findParameterByID(&m, id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

int menuIdFor(const juce::String& type, bool poly) {
    using P = synth::ui::TimelinePanelComponent;
    if (type == "Oscillator")
        return poly ? P::kAddInstrumentOscillatorPolyMenuId : P::kAddInstrumentOscillatorMenuId;
    if (type == "Wavetable")
        return poly ? P::kAddInstrumentWavetablePolyMenuId : P::kAddInstrumentWavetableMenuId;
    if (type == "Sampler")
        return P::kAddInstrumentSamplerMenuId;
    if (type == "Drum Kit")
        return P::kAddInstrumentDrumKitMenuId;
    if (type == "Pluck")
        return P::kAddInstrumentPluckMenuId;
    if (type == "Mallet")
        return P::kAddInstrumentMalletMenuId;
    return P::kAddInstrumentNoiseMenuId;
}

// One instrument track built through the real menu path, played through the real engine.
struct Track {
    MainComponent mc{std::make_unique<MockProviderCFT>()};
    ModuleBase* instrument = nullptr;
    TimelineMidiSourceModule* trackIn = nullptr;

    Track(const juce::String& type, bool poly, bool splitJacks) {
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getGraphEditor().setDefaultDualIOForNewModules(splitJacks);
        mc.getTimelinePanel().applyAddTrackMenuChoice(menuIdFor(type, poly));
        auto& graph = mc.getAudioEngine().getGraph();
        const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
        for (auto* node : graph.getNodes())
            if (auto* m = dynamic_cast<ModuleBase*>(node->getProcessor());
                m != nullptr && macro.hasMember(nodeUuid(node))) {
                if (m->getName() == type)
                    instrument = m;
                if (auto* t = dynamic_cast<TimelineMidiSourceModule*>(m))
                    trackIn = t;
            }
    }

    // Plays `note` and returns the Audio Output legs once the stack and its ramps have settled.
    Legs render(int note = 57, int blocks = 80) {
        synth::test::FakeAudioIODevice fake{0, 2};
        auto& engine = mc.getAudioEngine();
        engine.audioDeviceAboutToStart(&fake);
        trackIn->pushAuditionNote(note, 100, true);
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((size_t)n), right((size_t)n);
        float* outs[] = {left.data(), right.data()};
        Legs out;
        for (int b = 0; b < blocks; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
            if (b < blocks / 2)
                continue;
            out.left.insert(out.left.end(), left.begin(), left.end());
            out.right.insert(out.right.end(), right.begin(), right.end());
        }
        engine.audioDeviceStopped();
        return out;
    }
};

const char* const kSources[] = {"Oscillator", "Wavetable"};

} // namespace

// Regression test for FRO1486: an Oscillator at Unison 7 rendered through a real track came out exactly mono
// (correlation 1.000) because the VCA's right leg was a copy of its left.
TEST_F(ChannelFlowTest, UnisonWidthSurvivesAnInstrumentTrack) {
    for (const char* type : kSources) {
        for (const bool poly : {false, true}) {
            // A collapsed poly jack carries the left voices only (its Width and Pan stay greyed), so only the
            // split poly chain has a right leg to keep.
            for (const bool split : {false, true}) {
                if (poly && !split)
                    continue;
                const std::string n =
                    std::string(type) + (poly ? " poly" : " mono") + (split ? " split" : " collapsed");
                Track t(type, poly, split);
                ASSERT_NE(t.instrument, nullptr) << n;
                ASSERT_NE(t.trackIn, nullptr) << n;
                setParam(*t.instrument, "unison", 7.0f);
                setParam(*t.instrument, "width", 1.0f);
                const Legs legs = t.render();
                ASSERT_GT(rms(legs.left), 0.01) << n << ": audible";
                std::printf("[measured] %s unison 7 width 100%%: correlation %.3f\n", n.c_str(), correlation(legs));
                EXPECT_LT(correlation(legs), 0.9) << n << ": the track keeps the stack's spread";
            }
        }
    }
}

TEST_F(ChannelFlowTest, PanOnAnInstrumentTrackReachesOnlyItsSide) {
    for (const char* type : kSources) {
        for (const bool poly : {false, true}) {
            for (const bool split : {false, true}) {
                if (poly && !split)
                    continue;
                for (const float pan : {-1.0f, 1.0f}) {
                    const std::string n = std::string(type) + (poly ? " poly" : " mono") +
                                          (split ? " split" : " collapsed") + (pan < 0 ? " pan left" : " pan right");
                    Track t(type, poly, split);
                    ASSERT_NE(t.instrument, nullptr) << n;
                    setParam(*t.instrument, "pan", pan);
                    const Legs legs = t.render();
                    const auto& heard = pan < 0 ? legs.left : legs.right;
                    const auto& other = pan < 0 ? legs.right : legs.left;
                    std::printf("[measured] %s: heard %.1f dB, other %.1f dB\n", n.c_str(), toDb(peak(heard)),
                                toDb(peak(other)));
                    EXPECT_GT(toDb(peak(heard)), -30.0) << n << ": the panned side is audible";
                    EXPECT_LT(toDb(peak(other)), -60.0) << n << ": nothing leaks to the other side";
                }
            }
        }
    }
}

TEST_F(ChannelFlowTest, CentredInstrumentTrackStaysIdenticalOnBothSides) {
    for (const char* type : kSources) {
        for (const bool split : {false, true}) {
            const std::string n = std::string(type) + (split ? " split" : " collapsed");
            {
                Track t(type, false, split);
                ASSERT_NE(t.instrument, nullptr) << n;
                const Legs unisonOne = t.render();
                ASSERT_GT(rms(unisonOne.left), 0.01) << n;
                EXPECT_EQ(unisonOne.left, unisonOne.right) << n << ": Unison 1, centred";
            }
            {
                Track t(type, false, split);
                setParam(*t.instrument, "unison", 7.0f);
                setParam(*t.instrument, "width", 0.0f);
                const Legs narrow = t.render();
                ASSERT_GT(rms(narrow.left), 0.01) << n;
                EXPECT_EQ(narrow.left, narrow.right) << n << ": Width 0, centred";
            }
        }
    }
}

// The guard for the kind: every instrument a track can be built from, played through the track body. A type with
// a Width control must keep its spread; one without must still put sound on both sides.
TEST_F(ChannelFlowTest, EveryAuthorableInstrumentKeepsItsStereoThroughTheTrackBody) {
    for (const char* type : synth::TimelineOps::kAuthorableInstrumentTypes) {
        for (const bool split : {false, true}) {
            const std::string n = std::string(type) + (split ? " split" : " collapsed");
            Track t(type, false, split);
            ASSERT_NE(t.instrument, nullptr) << n;
            const bool hasWidth = findParameterByID(t.instrument, "width") != nullptr;
            if (hasWidth) {
                setParam(*t.instrument, "unison", 7.0f);
                setParam(*t.instrument, "width", 1.0f);
            }
            const Legs legs = t.render();
            if (rms(legs.left) < 1e-4 && rms(legs.right) < 1e-4)
                continue; // a Sampler or Drum Kit with no sample loaded has nothing to measure
            std::printf("[measured] %s: correlation %.3f, rms %.4f / %.4f\n", n.c_str(), correlation(legs),
                        rms(legs.left), rms(legs.right));
            if (hasWidth)
                EXPECT_LT(correlation(legs), 0.9) << n << ": the spread reaches the output";
            else
                EXPECT_GT(rms(legs.right), 0.5 * rms(legs.left)) << n << ": nothing is lost on the right";
        }
    }
}
