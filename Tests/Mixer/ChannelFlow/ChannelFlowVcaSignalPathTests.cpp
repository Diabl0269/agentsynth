// ChannelFlowVcaSignalPathTests.cpp
//
// Kinds: jack-raw-mismatch (the instrument track's right leg stopped at the VCA)
//
// What a user hears from "Add Track -> Instrument -> Oscillator" (Track In -> Oscillator -> ADSR/VCA ->
// Gate ...) while they unplug cables. Every case drives the REAL engine through the fake device with
// a note held (Track In's audition path) and reads the Audio Output level.
//
// The bug these pin: with the default preference (collapsed jacks) the chain still wired the
// Oscillator -> VCA and VCA -> Gate RIGHT legs, which have no jack on a collapsed split-block module.
// The cables were invisible and could not be unplugged, so removing the one cable the user could see
// left the sound playing through the hidden one.

#include "../../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "Modules/VCAModule.h"
#include "Timeline/TimelineOps.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>

namespace {

struct Levels {
    float left = 0.0f;
    float right = 0.0f;
};

// One started device with a note held; level() renders more blocks and reports the output RMS.
struct Rig {
    synth::test::FakeAudioIODevice fake{0, 2};
    AudioEngine& engine;
    Rig(AudioEngine& e, TimelineMidiSourceModule& trackIn)
        : engine(e) {
        engine.audioDeviceAboutToStart(&fake);
        trackIn.pushAuditionNote(60, 100, true);
    }
    ~Rig() { engine.audioDeviceStopped(); }
    Levels level(int blocks = 40) {
        const int n = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((size_t)n), right((size_t)n);
        float* outs[] = {left.data(), right.data()};
        Levels sum;
        for (int b = 0; b < blocks; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, n, {});
            if (b >= blocks - 4)
                for (int i = 0; i < n; ++i) {
                    sum.left += left[(size_t)i] * left[(size_t)i];
                    sum.right += right[(size_t)i] * right[(size_t)i];
                }
        }
        return {std::sqrt(sum.left / (4.0f * (float)n)), std::sqrt(sum.right / (4.0f * (float)n))};
    }
};

struct Track {
    MainComponent mc{std::make_unique<MockProviderCFT>()};
    juce::AudioProcessorGraph::Node* vca = nullptr;
    TimelineMidiSourceModule* trackIn = nullptr;

    Track(ChannelFlowTest& fixture, bool splitJacks) {
        (void)fixture;
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getGraphEditor().setDefaultDualIOForNewModules(splitJacks);
        mc.getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        auto& graph = mc.getAudioEngine().getGraph();
        const auto& macro = mc.getGraphEditor().getMacros().getAll().front();
        vca = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
        if (auto* n = findMacroMemberOfTypeCFT(graph, macro, ModuleType::TimelineMidiSource))
            trackIn = dynamic_cast<TimelineMidiSourceModule*>(n->getProcessor());
    }

    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }

    // The audio cables on the VCA's input (isInput) or output side, excluding its Gain CV input (ch1).
    std::vector<juce::AudioProcessorGraph::Connection> audioCables(bool isInput) {
        std::vector<juce::AudioProcessorGraph::Connection> found;
        for (const auto& c : graph().getConnections()) {
            if (c.source.isMIDI())
                continue;
            if (isInput && c.destination.nodeID == vca->nodeID && c.destination.channelIndex != 1)
                found.push_back(c);
            if (!isInput && c.source.nodeID == vca->nodeID)
                found.push_back(c);
        }
        return found;
    }
};

} // namespace

// Collapsed jacks (the default): the VCA shows one Audio in and one Audio out, each owning both legs (ch0 and
// kRightBase), so every cable on the VCA belongs to a jack the user can see and unplug. Unplugging them stops the
// sound.
TEST_F(ChannelFlowTest, CollapsedInstrumentTrackHasNoCableOnAHiddenRightJack) {
    Track t(*this, /*splitJacks=*/false);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    auto* vca = dynamic_cast<VCAModule*>(t.vca->getProcessor());
    ASSERT_NE(vca, nullptr);
    ASSERT_FALSE(vca->isDualIO());
    for (const bool input : {true, false}) {
        const auto head = input ? vca->mapInputChannel(0) : vca->mapOutputChannel(0);
        EXPECT_EQ(head.polyVoiceSpan, 2) << "the one jack owns both legs";
        EXPECT_EQ(head.voiceStride, VCAModule::kRightBase);
    }

    for (const auto& c : t.graph().getConnections()) {
        if (c.source.isMIDI())
            continue;
        if (c.source.nodeID == t.vca->nodeID)
            EXPECT_TRUE(c.source.channelIndex == 0 || c.source.channelIndex == VCAModule::kRightBase)
                << "VCA output cable off its joined jack";
        if (c.destination.nodeID == t.vca->nodeID)
            EXPECT_TRUE(c.destination.channelIndex == 0 || c.destination.channelIndex == 1 ||
                        c.destination.channelIndex == VCAModule::kRightBase)
                << "VCA input cable off its joined jack (ch1 is the gain CV)";
    }
    EXPECT_EQ(t.audioCables(true).size(), 2u) << "the Oscillator's two legs on the joined jack";
    EXPECT_EQ(t.audioCables(false).size(), 2u) << "the VCA's two legs onto the Gate's collapsed pair";

    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    const auto playing = rig.level();
    EXPECT_GT(playing.left, 0.1f);
    EXPECT_GT(playing.right, 0.1f);

    // Unplug the one Oscillator -> VCA cable the user can see: silence.
    for (const auto& c : t.audioCables(true))
        t.graph().removeConnection(c);
    const auto silent = rig.level();
    EXPECT_FLOAT_EQ(silent.left, 0.0f);
    EXPECT_FLOAT_EQ(silent.right, 0.0f);
}

TEST_F(ChannelFlowTest, CollapsedInstrumentTrackStopsWhenTheVcaOutputIsUnplugged) {
    Track t(*this, /*splitJacks=*/false);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    ASSERT_GT(rig.level().left, 0.1f);

    for (const auto& c : t.audioCables(false))
        t.graph().removeConnection(c);
    const auto silent = rig.level();
    EXPECT_FLOAT_EQ(silent.left, 0.0f);
    EXPECT_FLOAT_EQ(silent.right, 0.0f);
}

// Split jacks: each leg has its own cable, so unplugging one leaves exactly the other leg sounding.
// This is the way to hear Left/Right working: pull the VCA's Audio L (or R) output cable.
TEST_F(ChannelFlowTest, SplitInstrumentTrackPlaysOnlyTheLegThatIsStillPlugged) {
    Track t(*this, /*splitJacks=*/true);
    ASSERT_NE(t.vca, nullptr);
    ASSERT_NE(t.trackIn, nullptr);
    Rig rig(t.mc.getAudioEngine(), *t.trackIn);
    const auto both = rig.level();
    ASSERT_GT(both.left, 0.1f);
    ASSERT_GT(both.right, 0.1f);

    const auto outs = t.audioCables(false);
    ASSERT_EQ(outs.size(), 2u);
    for (const auto& c : outs)
        if (c.source.channelIndex == 0)
            t.graph().removeConnection(c);
    const auto rightOnly = rig.level();
    EXPECT_FLOAT_EQ(rightOnly.left, 0.0f);
    EXPECT_GT(rightOnly.right, 0.1f);
}

// ============================================================================
// The stereo image of an instrument track (Kinds: jack-raw-mismatch)
//
// The Oscillator's / Wavetable's right leg (unison spread, Pan) has to reach the Audio Output through the VCA and
// the default channel, not stop at the VCA where the right leg used to be a copy of the left. Every case builds the
// track the way the Add Track menu does, holds a note through Track In and measures the two legs at the Audio Output.
// ============================================================================

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
struct StereoTrack {
    MainComponent mc{std::make_unique<MockProviderCFT>()};
    ModuleBase* instrument = nullptr;
    TimelineMidiSourceModule* trackIn = nullptr;

    StereoTrack(const juce::String& type, bool poly, bool splitJacks) {
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
// Regression test for FRO1899: a poly Oscillator/Wavetable track built with the collapsed jacks preference was mono
// (no right leg)
TEST_F(ChannelFlowTest, UnisonWidthSurvivesAnInstrumentTrack) {
    for (const char* type : kSources) {
        for (const bool poly : {false, true}) {
            for (const bool split : {false, true}) {
                for (const int unison : {3, 7}) {
                    const std::string n = std::string(type) + (poly ? " poly" : " mono") +
                                          (split ? " split" : " collapsed") + " unison " + std::to_string(unison);
                    StereoTrack t(type, poly, split);
                    ASSERT_NE(t.instrument, nullptr) << n;
                    ASSERT_NE(t.trackIn, nullptr) << n;
                    setParam(*t.instrument, "unison", (float)unison);
                    setParam(*t.instrument, "width", 1.0f);
                    const Legs legs = t.render();
                    ASSERT_GT(rms(legs.left), 0.01) << n << ": audible";
                    std::vector<float> mid(legs.left.size()), side(legs.left.size());
                    for (size_t i = 0; i < mid.size(); ++i) {
                        mid[i] = 0.5f * (legs.left[i] + legs.right[i]);
                        side[i] = 0.5f * (legs.left[i] - legs.right[i]);
                    }
                    std::printf("[measured] %s width 100%%: correlation %.3f, side/mid %.1f dB\n", n.c_str(),
                                correlation(legs), toDb(rms(side) / (rms(mid) + 1e-12)));
                    EXPECT_LT(correlation(legs), 0.9) << n << ": the track keeps the stack's spread";
                }
            }
        }
    }
}

// Regression test for FRO1899: a poly Oscillator/Wavetable track built with the collapsed jacks preference was mono
// (no right leg)
TEST_F(ChannelFlowTest, PanOnAnInstrumentTrackReachesOnlyItsSide) {
    for (const char* type : kSources) {
        for (const bool poly : {false, true}) {
            for (const bool split : {false, true}) {
                for (const float pan : {-1.0f, 1.0f}) {
                    const std::string n = std::string(type) + (poly ? " poly" : " mono") +
                                          (split ? " split" : " collapsed") + (pan < 0 ? " pan left" : " pan right");
                    StereoTrack t(type, poly, split);
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

// Regression test for FRO1899: a poly Oscillator/Wavetable track built with the collapsed jacks preference kept its
// jacks joined, so Width and Pan were greyed.
TEST_F(ChannelFlowTest, APolyInstrumentTrackSplitsItsJacksWhateverThePreferenceSays) {
    for (const char* type : kSources) {
        StereoTrack t(type, /*poly=*/true, /*splitJacks=*/false);
        ASSERT_NE(t.instrument, nullptr) << type;
        auto* dual = findParameterByID(t.instrument, "dualIO");
        ASSERT_NE(dual, nullptr) << type;
        EXPECT_GT(dual->getValue(), 0.5f) << type << ": dualIO on";
        EXPECT_GT(t.instrument->rightAudioLegChannel(), 0) << type << ": a right leg to spread and pan";
    }
}

namespace {

// The track header's Mono/Poly switch, as a click on it calls it.
void switchTrackPoly(StereoTrack& t, bool poly) {
    juce::AudioProcessorGraph::NodeID sourceId{};
    for (auto* node : t.mc.getAudioEngine().getGraph().getNodes())
        if (node->getProcessor() == t.trackIn)
            sourceId = node->nodeID;
    auto& chain = t.mc.getGraphEditor().getPolyChain();
    chain.trackPolyClicked(sourceId, poly);
    ASSERT_EQ(chain.trackPolyState(sourceId), poly ? synth::ui::TrackPolyState::poly : synth::ui::TrackPolyState::mono);
}

bool isDualIO(ModuleBase* m) { return m != nullptr && m->isDualIO(); }

// Unison 7 at full Width: both legs audible and spread.
void expectStereoSpread(StereoTrack& t, const std::string& n) {
    setParam(*t.instrument, "unison", 7.0f);
    setParam(*t.instrument, "width", 1.0f);
    const Legs legs = t.render();
    std::printf("[measured] %s: left rms %.3f, right rms %.3f, correlation %.3f\n", n.c_str(), rms(legs.left),
                rms(legs.right), correlation(legs));
    EXPECT_GT(rms(legs.left), 0.01) << n << ": the left leg is audible";
    EXPECT_GT(rms(legs.right), 0.01) << n << ": the right leg is audible";
    EXPECT_LT(correlation(legs), 0.9) << n << ": the stack's spread survives";
}

} // namespace

// Regression test for FRO2346: a mono track built with the collapsed preference, switched to Poly from the track
// header, played on the left only (right rms 0.000): the collapsed Oscillator, Wavetable and VCA became collapsed poly
// modules, whose one jack carries only the left voices.
TEST_F(ChannelFlowTest, SwitchingACollapsedMonoInstrumentTrackToPolyKeepsItsStereo) {
    for (const char* type : kSources) {
        const std::string n = std::string(type) + " collapsed, switched to poly";
        StereoTrack t(type, /*poly=*/false, /*splitJacks=*/false);
        ASSERT_NE(t.instrument, nullptr) << n;
        switchTrackPoly(t, true);
        EXPECT_TRUE(isDualIO(t.instrument)) << n << ": the instrument splits its jacks, as a track built poly has";
        expectStereoSpread(t, n);
    }
}

TEST_F(ChannelFlowTest, PanOnACollapsedTrackSwitchedToPolyReachesOnlyItsSide) {
    for (const char* type : kSources) {
        for (const float pan : {-1.0f, 1.0f}) {
            const std::string n = std::string(type) + " switched to poly" + (pan < 0 ? ", pan left" : ", pan right");
            StereoTrack t(type, /*poly=*/false, /*splitJacks=*/false);
            ASSERT_NE(t.instrument, nullptr) << n;
            switchTrackPoly(t, true);
            setParam(*t.instrument, "pan", pan);
            const Legs legs = t.render();
            const auto& heard = pan < 0 ? legs.left : legs.right;
            const auto& other = pan < 0 ? legs.right : legs.left;
            EXPECT_GT(toDb(peak(heard)), -30.0) << n << ": the panned side is audible";
            EXPECT_LT(toDb(peak(other)), -60.0) << n << ": nothing leaks to the other side";
        }
    }
}

// Poly off again lands on the layout the track started with (one joined jack each) and keeps both legs.
TEST_F(ChannelFlowTest, SwitchingItBackToMonoRejoinsTheJacksAndKeepsItsStereo) {
    for (const char* type : kSources) {
        const std::string n = std::string(type) + " collapsed, poly then mono";
        StereoTrack t(type, /*poly=*/false, /*splitJacks=*/false);
        ASSERT_NE(t.instrument, nullptr) << n;
        switchTrackPoly(t, true);
        switchTrackPoly(t, false);
        EXPECT_FALSE(isDualIO(t.instrument)) << n << ": joined again, as the preference builds a mono track";
        expectStereoSpread(t, n);
    }
}

// The switch, jacks and cables included, is one undo step each way.
TEST_F(ChannelFlowTest, UndoOfThePolySwitchRestoresTheJoinedMonoTrack) {
    for (const char* type : kSources) {
        const std::string n = std::string(type) + " collapsed, poly undone";
        StereoTrack t(type, /*poly=*/false, /*splitJacks=*/false);
        ASSERT_NE(t.instrument, nullptr) << n;
        switchTrackPoly(t, true);
        ASSERT_TRUE(t.mc.getUndoManager().undo()) << n;
        EXPECT_FALSE(synth::isProcessorPoly(t.instrument)) << n;
        EXPECT_FALSE(isDualIO(t.instrument)) << n << ": one undo puts the joined jack back";
        expectStereoSpread(t, n);
        ASSERT_TRUE(t.mc.getUndoManager().redo()) << n;
        EXPECT_TRUE(synth::isProcessorPoly(t.instrument)) << n;
        EXPECT_TRUE(isDualIO(t.instrument)) << n;
        expectStereoSpread(t, n + ", redone");
    }
}

TEST_F(ChannelFlowTest, CentredInstrumentTrackStaysIdenticalOnBothSides) {
    for (const char* type : kSources) {
        for (const bool split : {false, true}) {
            const std::string n = std::string(type) + (split ? " split" : " collapsed");
            {
                StereoTrack t(type, false, split);
                ASSERT_NE(t.instrument, nullptr) << n;
                const Legs unisonOne = t.render();
                ASSERT_GT(rms(unisonOne.left), 0.01) << n;
                EXPECT_EQ(unisonOne.left, unisonOne.right) << n << ": Unison 1, centred";
            }
            {
                StereoTrack t(type, false, split);
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
            StereoTrack t(type, false, split);
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
