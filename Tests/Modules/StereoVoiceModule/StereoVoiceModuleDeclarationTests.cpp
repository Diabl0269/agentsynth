// Dual I/O inheritance and state-compatibility tests for the L/R stereo split on voice modules
// (issue #219); see StereoVoiceModuleTestHelpers.h for the shared render/measurement helpers.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "StereoVoiceModuleTestHelpers.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Dual I/O is INHERITED, not registered (ModuleBase::StereoAudio)
//
// The toggle used to be a per-module `addDualIOParameter()` call, and the Ring Modulator shipped a
// stereo output pair without one — no header control, no Preferences row, nothing red. The base
// constructor now decides from the module's channel shape, so the only per-module decision left is
// the exception. These tests are what make an exception impossible to take silently: they sweep the
// whole factory and compare each module against the shape rule plus the two tables below.
// ---------------------------------------------------------------------------

namespace {

// Modules whose channel shape matches StereoAudio::Auto (>= 2 in, exactly 2 out) but which are NOT
// stereo — each passes StereoAudio::None, and each needs a reason recorded here.
const std::vector<std::pair<juce::String, const char*>> kDualIOOptOuts = {
    {"Comparator", "in ch0/ch1 are Signal + Threshold CV and out ch0/ch1 are Gate + inverted Gate — "
                   "two unrelated CV jacks per side, and no audio output at all"},
    {"Rec Tap", "a hidden recording tap: its two channels are the take's capture pair, wired by the "
                "record flow rather than patched, and it has no card to put a jack toggle on"},
    {"Master", "the mix bus: its four inputs are two stereo BLOCKS (Mix L/R and Direct L/R), not an FX "
               "pair plus CV, so a collapsed 'Audio' jack over ch0/ch1 would misdescribe it"},
};

// Modules that declare a second audio leg the shape rule cannot see (their own kRightBase block, or
// a ch0/ch1 pair alongside further outputs). Each passes StereoAudio::Declared and ships SPLIT.
const std::vector<juce::String> kDualIODeclared = {"Oscillator", "Wavetable", "Filter", "VCA", "Sampler"};

bool containsName(const std::vector<juce::String>& v, const juce::String& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool isDualIOOptOut(const juce::String& name) {
    for (const auto& entry : kDualIOOptOuts)
        if (entry.first == name)
            return true;
    return false;
}

// A brand-new module: the FX channel shape and NOTHING else. No Dual I/O registration, no jack
// maps, no port labels. Whatever this class can do, a module author gets for free.
class ShapeOnlyStereoModule : public ModuleBase {
public:
    ShapeOnlyStereoModule()
        : ModuleBase("Shape Only", 4, 2) {} // 2 audio + 2 CV in, stereo out — the Distortion shape

    void prepareToPlay(double, int) override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    ModuleType getModuleType() const override { return ModuleType::Distortion; }
};

} // namespace

TEST(StereoDeclaration, EveryFactoryModuleFollowsTheShapeRuleOrADocumentedException) {
    // The qualification rule, applied to every module the factory can build. A new stereo FX passes
    // this the moment it exists; a new module that needs an exception fails until it is listed above
    // with a reason.
    int autoQualified = 0;
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(name.toStdString());
        auto probe = synth::AIStateMapper::createModule(name);
        ASSERT_NE(probe, nullptr);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        if (mb == nullptr)
            continue; // graph I/O nodes are not modules

        const bool shapeMatches =
            ModuleBase::hasStereoOutputPairShape(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels());
        const bool declared = containsName(kDualIODeclared, name);

        if (declared) {
            EXPECT_TRUE(mb->hasDualIOParameter()) << "a Declared module must carry the toggle";
            EXPECT_TRUE(mb->isDualIO()) << "StereoAudio::Declared ships SPLIT — five saved-patch defaults "
                                           "depend on this";
        } else if (shapeMatches && !isDualIOOptOut(name)) {
            ++autoQualified;
            EXPECT_TRUE(mb->hasDualIOParameter())
                << "the channel shape says stereo, so the base must have added the toggle with no help "
                   "from the module";
            EXPECT_FALSE(mb->isDualIO()) << "StereoAudio::Auto ships COLLAPSED";
        } else {
            EXPECT_FALSE(mb->hasDualIOParameter())
                << (isDualIOOptOut(name) ? "this module opts out" : "this module's shape is not a stereo pair");
        }
    }
    EXPECT_GT(autoQualified, 10) << "expected the whole FX family to qualify by shape alone";
}

TEST(StereoDeclaration, TheExceptionTablesHaveNoStaleEntries) {
    // A renamed or deleted module must not leave a silent exception behind.
    const auto factory = synth::AIStateMapper::moduleFactoryTypeNames();
    for (const auto& entry : kDualIOOptOuts) {
        EXPECT_TRUE(factory.contains(entry.first)) << entry.first << " is no longer a factory module";
        EXPECT_GT(juce::String(entry.second).length(), 20) << entry.first << " needs a real reason recorded";
        auto probe = synth::AIStateMapper::createModule(entry.first);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        ASSERT_NE(mb, nullptr);
        EXPECT_TRUE(
            ModuleBase::hasStereoOutputPairShape(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels()))
            << entry.first << " no longer matches the Auto shape, so it does not need an opt-out";
    }
    for (const auto& name : kDualIODeclared)
        EXPECT_TRUE(factory.contains(name)) << name << " is no longer a factory module";
}

TEST(StereoDeclaration, ANewModuleWithTheStereoShapeInheritsTheWholeToggle) {
    // The claim in one test: a module that writes no Dual I/O code at all still gets the parameter
    // AND the collapsing output jack.
    ShapeOnlyStereoModule fresh;
    ASSERT_TRUE(fresh.hasDualIOParameter()) << "the base constructor must add the toggle from the shape alone";
    EXPECT_FALSE(fresh.isDualIO());
    EXPECT_TRUE(fresh.hasCollapsibleOutputPair());

    EXPECT_EQ(fresh.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(fresh.getOutputPortLabel(0), "Audio");
    const auto collapsedHead = fresh.mapOutputChannel(0);
    EXPECT_TRUE(collapsedHead.isPolyGroupHead);
    EXPECT_EQ(collapsedHead.polyVoiceSpan, 2) << "the collapsed jack owns both raw legs";
    EXPECT_EQ(collapsedHead.role, PortRole::Audio);
    EXPECT_FALSE(fresh.mapOutputChannel(1).isPolyGroupHead);

    setBoolParam(fresh, "dualIO", true);
    EXPECT_EQ(fresh.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(fresh.getOutputPortLabel(0), "Left");
    EXPECT_EQ(fresh.getOutputPortLabel(1), "Right");
    EXPECT_TRUE(fresh.mapOutputChannel(1).isPolyGroupHead);
    EXPECT_EQ(fresh.rightAudioLegChannel(), 1);

    // The INPUT side is deliberately NOT inferred — ch0/ch1 being an input pair is not knowable from
    // the shape (Voice Mixer's are voice inputs, the Ring Modulator's are Carrier + Modulator), so
    // the inherited input map leaves every input jack alone.
    EXPECT_EQ(fresh.getVisibleInputPortCount(), 4);
    EXPECT_NE(fresh.mapInputChannel(1).role, PortRole::Audio);
}

TEST(StereoDeclaration, RingModulatorGetsTheTogglePurelyByInheritance) {
    // RingModulatorModule's constructor contains no Dual I/O registration and the class overrides
    // none of the three output-side hooks — this is the module the bug was reported on.
    RingModulatorModule ringMod;
    ASSERT_TRUE(ringMod.hasDualIOParameter());
    EXPECT_FALSE(ringMod.isDualIO()) << "collapsed by default, like every other FX";
    EXPECT_TRUE(ringMod.hasCollapsibleOutputPair());
    EXPECT_EQ(ringMod.getVisibleOutputPortCount(), 1);
    EXPECT_EQ(ringMod.getOutputPortLabel(0), "Audio");
    ASSERT_EQ(ringMod.getJackTargets(0, /*isInput=*/false).size(), 1u);
    EXPECT_EQ(ringMod.getJackTargets(0, false)[0].voiceSpan, 2);

    setBoolParam(ringMod, "dualIO", true);
    EXPECT_EQ(ringMod.getVisibleOutputPortCount(), 2);
    EXPECT_EQ(ringMod.getOutputPortLabel(0), "Left");
    EXPECT_EQ(ringMod.getOutputPortLabel(1), "Right");

    // ...and its five input jacks are still its own, in both states.
    for (bool dual : {false, true}) {
        setBoolParam(ringMod, "dualIO", dual);
        EXPECT_EQ(ringMod.getVisibleInputPortCount(), 5) << "dual=" << dual;
        EXPECT_EQ(ringMod.getInputPortLabel(0), "Carrier") << "dual=" << dual;
        EXPECT_EQ(ringMod.getInputPortLabel(1), "Modulator") << "dual=" << dual;
        EXPECT_NE(ringMod.mapInputChannel(1).role, PortRole::Audio) << "dual=" << dual;
    }
}

TEST(StereoDeclaration, TheRegistryReportsEveryInheritedToggle) {
    // The dynamic registry asks hasDualIOParameter(), so inherited toggles have to show up in it —
    // that is what carries them into the Preferences popup and the global default.
    const auto& registry = synth::AIStateMapper::dualIOCapableModuleTypes();
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(name.toStdString());
        auto probe = synth::AIStateMapper::createModule(name);
        auto* mb = dynamic_cast<ModuleBase*>(probe.get());
        const bool expected = mb != nullptr && mb->hasDualIOParameter();
        EXPECT_EQ(registry.contains(name), expected);
    }
    EXPECT_TRUE(registry.contains("Ring Modulator"));
    EXPECT_FALSE(registry.contains("Comparator"));
    EXPECT_FALSE(registry.contains("Rec Tap"));
}

TEST(StereoDeclaration, MuteAndCVClearHoldForEveryDualIOCapableModuleInBothStates) {
    // The mute and CV-clear invariants, swept over the registry rather than spot-checked per module,
    // because the set now grows by inheritance.
    //
    // Mute is asserted for EVERY module in the registry: it clears the whole buffer, whatever the
    // channel layout. The CV-clear half is asserted only for the modules whose stereo pair the base
    // INFERRED (hasCollapsibleOutputPair() — audio on ch0/ch1, so every CV channel is >= 2, which is
    // what docs/architecture/module-base.md#bypassmute-contract is written against). It is deliberately
    // NOT asserted for the split-block modules: on those, ch0 is a pitch CV *input* and the Audio L
    // *output* at the same time, and ch1 is a CV input below the audio pair — "the CV channels are
    // zero after a block" is not a statement about them. Their clears are pinned per module by
    // FilterStereo.CVClearDoesNotEraseAudioR / VCAStereo.ClearsDoNotEraseAudioR and the Oscillator's
    // own poly-clear tests.
    //
    // Bypass's AUDIO behaviour is also not asserted here — it splits by family (dry pass-through vs.
    // clear for a pure source) and each module pins its own.
    for (const auto& name : synth::AIStateMapper::dualIOCapableModuleTypes()) {
        for (bool dual : {false, true}) {
            SCOPED_TRACE(name.toStdString() + (dual ? " [dual]" : " [collapsed]"));
            auto probe = synth::AIStateMapper::createModule(name);
            auto* mb = dynamic_cast<ModuleBase*>(probe.get());
            ASSERT_NE(mb, nullptr);
            setBoolParam(*mb, "dualIO", dual);
            mb->prepareToPlay(kSampleRate, kBlockSize);

            const int channels = std::max(mb->getTotalNumInputChannels(), mb->getTotalNumOutputChannels());
            auto fill = [&](juce::AudioBuffer<float>& b) {
                b.clear();
                for (int ch = 0; ch < std::min(2, channels); ++ch)
                    fillTone(b, ch, 1000.0f);
                for (const auto& t : mb->getModulationTargets())
                    if (t.channelIndex < channels)
                        for (int i = 0; i < kBlockSize; ++i)
                            b.setSample(t.channelIndex, i, 0.5f);
            };

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer(channels, kBlockSize);

            // Mute clears every channel — for the modules that HAVE a mute. Mute is opt-in like the
            // level stage (Voice Mixer is a summing utility and never took one), and setMuted() on a
            // module without the parameter dereferences a null, so ask first.
            if (findParameterByID(mb, "muted") != nullptr) {
                fill(buffer);
                mb->setMuted(true);
                mb->processBlock(buffer, midi);
                for (int ch = 0; ch < channels; ++ch)
                    EXPECT_LT(rmsOf(buffer, ch), 1.0e-9f) << "mute left channel " << ch << " audible";
                mb->setMuted(false);
            }

            // A normal block and a bypassed block both leave the CV inputs cleared — for the
            // inferred-pair family only (see the header comment).
            if (mb->hasCollapsibleOutputPair()) {
                for (bool bypassed : {false, true}) {
                    mb->setBypassed(bypassed);
                    fill(buffer);
                    mb->processBlock(buffer, midi);
                    for (const auto& t : mb->getModulationTargets()) {
                        ASSERT_GE(t.channelIndex, 2) << "an inferred stereo pair owns ch0/ch1, so a CV jack "
                                                        "cannot sit below ch2";
                        if (t.channelIndex < channels)
                            EXPECT_LT(rmsOf(buffer, t.channelIndex), 1.0e-6f)
                                << "CV channel " << t.channelIndex << " (" << t.name
                                << ") leaked, bypassed=" << bypassed;
                    }
                }
                mb->setBypassed(false);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// State compatibility: a patch saved BEFORE Dual I/O moved into the base
// ---------------------------------------------------------------------------

TEST(StereoDeclaration, APatchSavedBeforeTheMoveLoadsWithTheSameLayout) {
    // Authored by hand in the shape our own save path emitted before this change: parameters keyed
    // by ID (never by index), an explicit "dualIO" for the modules that had one, and NO "dualIO" for
    // the Ring Modulator, which had no such parameter then. The ids and defaults are unchanged, so
    // every module must come back with exactly the layout the patch describes — and the module that
    // gained the parameter must fall to its default rather than to something arbitrary.
    const juce::String legacyPatch = R"({
      "nodes": [
        {"id": 1, "type": "Oscillator", "params": {"waveform": "Saw", "dualIO": false}},
        {"id": 2, "type": "Filter", "params": {"cutoff": 800.0, "dualIO": true}},
        {"id": 3, "type": "Reverb", "params": {"roomSize": 0.7, "dualIO": true}},
        {"id": 4, "type": "Delay", "params": {"time": 250.0}},
        {"id": 5, "type": "Ring Modulator", "params": {"mix": 0.5}},
        {"id": 6, "type": "Audio Output"}
      ],
      "connections": [
        {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
        {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0},
        {"src": 3, "srcPort": 0, "dst": 6, "dstPort": 0}
      ]
    })";

    juce::AudioProcessorGraph graph;
    graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(juce::JSON::parse(legacyPatch), graph,
                                                       /*clearExisting=*/true, /*trusted=*/true));

    auto moduleNamed = [&graph](const juce::String& name) -> ModuleBase* {
        for (auto* node : graph.getNodes())
            if (node->getProcessor()->getName() == name)
                return dynamic_cast<ModuleBase*>(node->getProcessor());
        return nullptr;
    };

    auto* osc = moduleNamed("Oscillator");
    auto* filter = moduleNamed("Filter");
    auto* reverb = moduleNamed("Reverb");
    auto* delay = moduleNamed("Delay");
    auto* ringMod = moduleNamed("Ring Modulator");
    ASSERT_NE(osc, nullptr);
    ASSERT_NE(filter, nullptr);
    ASSERT_NE(reverb, nullptr);
    ASSERT_NE(delay, nullptr);
    ASSERT_NE(ringMod, nullptr);

    EXPECT_FALSE(osc->isDualIO()) << "an explicit false must survive the move to the base";
    EXPECT_EQ(osc->getVisibleOutputPortCount(), 1);
    EXPECT_TRUE(filter->isDualIO()) << "an explicit true must survive";
    EXPECT_EQ(filter->getVisibleInputPortCount(), 5);
    EXPECT_TRUE(reverb->isDualIO());
    EXPECT_EQ(reverb->getVisibleOutputPortCount(), 2);
    EXPECT_FALSE(delay->isDualIO()) << "an omitted dualIO must fall to the module's default (collapsed)";
    EXPECT_FALSE(ringMod->isDualIO()) << "a patch older than this module's toggle must open collapsed, not "
                                         "split — its default is what every other FX has";

    // ...and re-saving now carries the values forward unchanged.
    const auto resaved = synth::AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reloaded;
    reloaded.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(resaved, reloaded, /*clearExisting=*/true, /*trusted=*/true));

    auto dualOf = [](juce::AudioProcessorGraph& g, const juce::String& name) {
        for (auto* node : g.getNodes())
            if (node->getProcessor()->getName() == name)
                if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
                    return mb->isDualIO() ? 1 : 0;
        return -1;
    };
    for (const char* name : {"Oscillator", "Filter", "Reverb", "Delay", "Ring Modulator"})
        EXPECT_EQ(dualOf(reloaded, name), dualOf(graph, name)) << name << " changed layout across a save/load";
}

TEST(StereoDeclaration, ALegacyStateBlobWithNoDualIOPropertyLoadsAtTheModuleDefault) {
    // The other state path: ModuleBase::getStateInformation's XML, which the plugin's session state
    // and ProjectBundle use. Mirrors OutputLevelHelper's legacy-blob test — a blob written before the
    // parameter existed must leave the module at its own default, not at 0 or at whatever the
    // ValueTree happens to yield for a missing property.
    auto blobWithoutDualIO = [](ModuleBase& module) {
        juce::MemoryBlock full;
        module.getStateInformation(full);
        auto xml = juce::AudioProcessor::getXmlFromBinary(full.getData(), (int)full.getSize());
        // Make it look like a save from before the toggle existed.
        xml->removeAttribute("dualIO");
        juce::MemoryBlock legacy;
        juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
        return legacy;
    };

    // `setStateInformation` only writes the properties the blob carries, so the value a legacy blob
    // lands on is whatever the instance already held — and every real caller (the plugin's session
    // restore, AIStateMapper) constructs the module first and applies state second. So the case that
    // matters is a FRESHLY constructed node: it must keep its own default.
    {
        // Auto family: default collapsed. The Ring Modulator is the real case — patches predating
        // its toggle exist in the wild.
        RingModulatorModule source;
        setBoolParam(source, "dualIO", true); // a value the legacy blob will NOT carry
        const auto legacy = blobWithoutDualIO(source);

        RingModulatorModule target; // fresh, exactly as a load would create it
        target.setStateInformation(legacy.getData(), (int)legacy.getSize());
        EXPECT_FALSE(target.isDualIO()) << "a blob with no dualIO must leave an Auto module collapsed";
        EXPECT_EQ(target.getVisibleOutputPortCount(), 1);
    }

    {
        // Declared family: default split, and a legacy blob must not silently collapse a saved patch.
        FilterModule source;
        setBoolParam(source, "dualIO", false);
        const auto legacy = blobWithoutDualIO(source);

        FilterModule target; // fresh
        target.setStateInformation(legacy.getData(), (int)legacy.getSize());
        EXPECT_TRUE(target.isDualIO()) << "a blob with no dualIO must leave a Declared module split";
        EXPECT_EQ(target.getVisibleInputPortCount(), 5);
    }

    {
        // And a blob that DOES carry the value round-trips it in both directions.
        for (bool dual : {false, true}) {
            ReverbModule source;
            setBoolParam(source, "dualIO", dual);
            juce::MemoryBlock blob;
            source.getStateInformation(blob);

            ReverbModule target;
            setBoolParam(target, "dualIO", !dual);
            target.setStateInformation(blob.getData(), (int)blob.getSize());
            EXPECT_EQ(target.isDualIO(), dual) << "dualIO did not survive a state round-trip, dual=" << dual;
        }
    }
}
