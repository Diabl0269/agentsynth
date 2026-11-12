// ParametricEQSavedModelTests.cpp
//
// The 24-slot saved model: a patch saved by the four-band module (Tests/fixtures/parametric-eq/) loads
// with every value it had, every slot's type is the one the four-band module gave it, and it renders
// exactly what the four-band engine rendered. The reference render below re-derives the four-band
// module's biquads from its own formulas, independently of the module's code. Also: the band model's
// slot API (findFreeBand / addBand), that every band is exposed, that a snapshot restore puts an added
// band back off, and the goldens of what the AI and a save see (at the end).

#include "AI/AIStateMapper/AIStateMapper.h"
#include "ParametricEQGoldenFixtures.h"
#include "ParametricEQTestHelpers.h"

using namespace eqtest;

namespace {

float paramValue(juce::AudioProcessor& module, const juce::String& id) {
    for (auto* p : module.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged->convertFrom0to1(ranged->getValue());
    ADD_FAILURE() << "missing parameter " << id.toStdString();
    return 0.0f;
}

juce::RangedAudioParameter* paramById(juce::AudioProcessor& module, const juce::String& id) {
    for (auto* p : module.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}

void expectLegacyValues(ParametricEQModule& eq) {
    for (const auto& edit : eqgolden::legacyEdits())
        EXPECT_NEAR(paramValue(eq, edit.id), edit.value, std::max(1.0e-3f, std::abs(edit.value) * 1.0e-4f)) << edit.id;
    const BandType expected[] = {BandType::LowShelf, BandType::Peak, BandType::Peak, BandType::HighShelf};
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        EXPECT_EQ(eq.getBandType(b), expected[b]) << "slot " << b + 1;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        EXPECT_EQ(eq.getBandSlope(b), 12) << "slot " << b + 1;
    for (int b = ParametricEQModule::kOriginalBands; b < ParametricEQModule::kMaxBands; ++b) {
        EXPECT_FALSE(eq.isBandEnabled(b)) << "slot " << b + 1;
        EXPECT_EQ(eq.getBandType(b), BandType::Peak) << "slot " << b + 1;
        EXPECT_FLOAT_EQ(paramValue(eq, bandId(b, "Gain")), 0.0f);
    }
}

// The four-band module's writeBiquad for its three types, verbatim, so the reference does not share code with
// the module under test.
void legacyBiquad(int slot, float centreHz, float gainDb, float q, double fs, float* dest) {
    const double f0 = juce::jlimit(1.0, fs * 0.49, static_cast<double>(centreHz));
    const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double Q = std::max(0.05, static_cast<double>(q));
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double sqrtA = std::sqrt(A);
    double b0, b1, b2, a0, a1, a2;
    if (slot == 0) {
        b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
        a0 = (A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
        a2 = (A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
    } else if (slot == 3) {
        b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
        a0 = (A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
        a2 = (A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
    } else {
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosW0;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha / A;
    }
    const double inv = 1.0 / a0;
    dest[0] = static_cast<float>(b0 * inv);
    dest[1] = static_cast<float>(b1 * inv);
    dest[2] = static_cast<float>(b2 * inv);
    dest[3] = static_cast<float>(a1 * inv);
    dest[4] = static_cast<float>(a2 * inv);
}

constexpr int kRenderBlocks = 24;

// A stereo test signal: two tones a side plus a little deterministic noise, so every band's region carries energy.
void fillTestSignal(juce::AudioBuffer<float>& buffer, int block, juce::Random& random) {
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(block * kBlockSize + i) / static_cast<float>(kSampleRate);
        const float twoPi = juce::MathConstants<float>::twoPi;
        buffer.setSample(0, i,
                         0.3f * std::sin(twoPi * 90.0f * t) + 0.2f * std::sin(twoPi * 7000.0f * t) +
                             0.05f * (random.nextFloat() - 0.5f));
        buffer.setSample(1, i,
                         0.3f * std::sin(twoPi * 640.0f * t) + 0.2f * std::sin(twoPi * 11000.0f * t) +
                             0.05f * (random.nextFloat() - 0.5f));
    }
}

std::vector<float> renderModule(ParametricEQModule& eq) {
    eq.setPlayConfigDetails(17, 2, kSampleRate, kBlockSize);
    eq.prepareToPlay(kSampleRate, kBlockSize);
    juce::Random random(1234);
    juce::MidiBuffer midi;
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(17, kBlockSize);
    for (int block = 0; block < kRenderBlocks; ++block) {
        buffer.clear();
        fillTestSignal(buffer, block, random);
        eq.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.insert(out.end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + kBlockSize);
    }
    return out;
}

// The four-band engine's render loop: per block, per channel, each enabled band's single biquad over the block, then
// the output trim per sample (static knobs: every smoother sits on its target). The knob values are read from the
// loaded module, so the reference shares its inputs with the module but none of its maths.
std::vector<float> renderLegacyReference(ParametricEQModule& loaded) {
    auto knob = [&](const juce::String& id) { return floatParamById(loaded, id)->get(); };
    struct Band {
        int slot;
        float freq, gain, q;
    };
    Band enabled[3];
    int count = 0;
    for (const int slot : {0, 1, 3})
        enabled[count++] = {slot, knob(bandId(slot, "Freq")), knob(bandId(slot, "Gain")), knob(bandId(slot, "Q"))};
    EXPECT_FALSE(loaded.isBandEnabled(2)) << "the fixture has slot 3 off";
    juce::dsp::ProcessSpec spec{kSampleRate, (juce::uint32)kBlockSize, 1};
    std::array<std::array<juce::dsp::IIR::Filter<float>, 3>, 2> filters;
    for (int k = 0; k < 3; ++k) {
        float raw[5];
        legacyBiquad(enabled[k].slot, enabled[k].freq, enabled[k].gain, enabled[k].q, kSampleRate, raw);
        auto coefs = new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        std::copy(raw, raw + 5, coefs->getRawCoefficients());
        for (auto& channel : filters) {
            channel[(size_t)k].prepare(spec);
            channel[(size_t)k].coefficients = coefs;
            channel[(size_t)k].reset();
        }
    }
    const float trim = juce::Decibels::decibelsToGain(knob("outputGain"));
    juce::Random random(1234);
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    for (int block = 0; block < kRenderBlocks; ++block) {
        fillTestSignal(buffer, block, random);
        for (int ch = 0; ch < 2; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (auto& filter : filters[(size_t)ch]) {
                for (int i = 0; i < kBlockSize; ++i)
                    data[i] = filter.processSample(data[i]);
                filter.snapToZero();
            }
            for (int i = 0; i < kBlockSize; ++i)
                data[i] *= trim;
        }
        for (int ch = 0; ch < 2; ++ch)
            out.insert(out.end(), buffer.getReadPointer(ch), buffer.getReadPointer(ch) + kBlockSize);
    }
    return out;
}

// An EQ in a graph, so a trusted load goes through the same node-preserving restore an undo or project load uses.
struct GraphEQ {
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr node;
    ParametricEQModule& eq() { return *dynamic_cast<ParametricEQModule*>(node->getProcessor()); }

    explicit GraphEQ(const juce::String& uuid) {
        node = graph.addNode(synth::AIStateMapper::createModule("Parametric EQ"));
        node->properties.set("uuid", uuid);
    }

    bool restore(const juce::var& nodeJson) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("nodes", juce::Array<juce::var>{nodeJson});
        root->setProperty("connections", juce::Array<juce::var>{});
        return synth::AIStateMapper::applySnapshotPreservingNodes(juce::var(root.get()), graph);
    }
};

juce::var legacyNode() { return juce::JSON::parse(eqgolden::goldenFile("node-legacy.json")); }

const juce::String kLegacyUuid = "00000000-0000-4000-8000-0000000000e0";

} // namespace

TEST(ParametricEQSavedModel, AFourBandSavedStateLoadsEveryValueAndTheSlotsKeepTheirTypes) {
    const auto xml = juce::parseXML(eqgolden::goldenFile("state-legacy.xml"));
    ASSERT_NE(xml, nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*xml, block);
    ParametricEQModule eq;
    eq.setStateInformation(block.getData(), (int)block.getSize());
    expectLegacyValues(eq);
}

TEST(ParametricEQSavedModel, AFourBandNodeJsonLoadsEveryValueAndTheSlotsKeepTheirTypes) {
    GraphEQ trusted(kLegacyUuid);
    ASSERT_TRUE(trusted.restore(legacyNode()));
    expectLegacyValues(trusted.eq());

    ParametricEQModule untrusted;
    synth::AIStateMapper::applyUntrustedParams(&untrusted, legacyNode().getProperty("params", {}).getDynamicObject());
    expectLegacyValues(untrusted);
}

// Fails if a slot's type default moved (slot 1 as a bell, say), if a stage were added to a legacy band, or if the
// render loop's order changed: the comparison is bit for bit.
TEST(ParametricEQSavedModel, AFourBandPatchRendersExactlyAsTheFourBandEngine) {
    GraphEQ loaded(kLegacyUuid);
    ASSERT_TRUE(loaded.restore(legacyNode()));
    auto& eq = loaded.eq();
    // Mix and Pan belong to the shared output stage, which this change does not touch; centre them so the
    // reference needs only the EQ's own render.
    paramById(eq, "mix")->setValueNotifyingHost(1.0f);
    paramById(eq, "pan")->setValueNotifyingHost(paramById(eq, "pan")->convertTo0to1(0.0f));
    const auto actual = renderModule(eq);
    const auto expected = renderLegacyReference(eq);
    ASSERT_EQ(actual.size(), expected.size());
    int firstDiff = -1;
    for (size_t i = 0; i < actual.size() && firstDiff < 0; ++i)
        if (actual[i] != expected[i])
            firstDiff = (int)i;
    EXPECT_EQ(firstDiff, -1) << "first differing sample " << firstDiff;
}

TEST(ParametricEQSavedModel, ParametersAreAppendedAfterPanSoOldIndicesHold) {
    ParametricEQModule eq;
    const auto& params = eq.getParameters();
    juce::StringArray ids;
    for (auto* p : params)
        ids.add(dynamic_cast<juce::AudioProcessorParameterWithID*>(p)->paramID);
    const int pan = ids.indexOf("pan");
    ASSERT_GE(pan, 0);
    EXPECT_EQ(ids.indexOf("band4Q"), ids.indexOf("outputGain") - 1);
    EXPECT_LT(ids.indexOf("muted"), pan);
    EXPECT_EQ(ids.indexOf("band5On"), pan + 1);
    EXPECT_EQ(ids.indexOf("band24Q"), pan + 80);
    EXPECT_EQ(ids.indexOf("band1Type"), pan + 81);
    EXPECT_EQ(ids.indexOf("band1Slope"), pan + 82);
    EXPECT_EQ(ids.indexOf("band24Slope"), ids.size() - 1);
    EXPECT_EQ(ids.size(), pan + 1 + 80 + 48);
}

TEST(ParametricEQSavedModel, EveryBandParameterIsExposedOnEverySurface) {
    ParametricEQModule eq;
    for (int b = 1; b <= ParametricEQModule::kMaxBands; ++b)
        for (const char* suffix : {"On", "Freq", "Gain", "Q", "Type", "Slope"})
            EXPECT_TRUE(eq.isParameterExposed("band" + juce::String(b) + suffix)) << b << suffix;
}

TEST(ParametricEQSlots, FindFreeBandIsTheLowestOffSlotOfAllTwentyFour) {
    ParametricEQModule eq;
    EXPECT_EQ(eq.findFreeBand(), 0);
    eq.setBandEnabled(0, true);
    eq.setBandEnabled(1, true);
    EXPECT_EQ(eq.findFreeBand(), 2);
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        eq.setBandEnabled(b, true);
    EXPECT_EQ(eq.findFreeBand(), 4) << "past the four shown slots";
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        eq.setBandEnabled(b, true);
    EXPECT_EQ(eq.findFreeBand(), -1);
    eq.setBandEnabled(17, false);
    EXPECT_EQ(eq.findFreeBand(), 17);
}

TEST(ParametricEQSlots, AddBandTurnsTheLowestFreeSlotOnWithItsTypeAndFrequency) {
    ParametricEQModule eq;
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        eq.setBandEnabled(b, true);
    eq.setBandGain(4, 9.0f); // a leftover setting is reset
    const int band = eq.addBand(250.0f, BandType::HighCut);
    ASSERT_EQ(band, 4);
    EXPECT_TRUE(eq.isBandEnabled(4));
    EXPECT_EQ(eq.getBandType(4), BandType::HighCut);
    const auto snap = eq.getBandSnapshots()[4];
    EXPECT_NEAR(snap.freqHz, 250.0f, 0.5f);
    EXPECT_FLOAT_EQ(snap.gainDb, 0.0f);
    EXPECT_NEAR(snap.q, ParametricEQModule::kDefaultQ, 1.0e-4f);
    EXPECT_EQ(snap.type, BandType::HighCut);
    for (int b = 5; b < ParametricEQModule::kMaxBands; ++b)
        ASSERT_GE(eq.addBand(1000.0f, BandType::Peak), 0);
    EXPECT_EQ(eq.addBand(1000.0f, BandType::Peak), -1) << "every slot is on";
}

TEST(ParametricEQSlots, NewPointsStillOnlyLandOnTheOriginalFourSlots) {
    ParametricEQModule eq;
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        eq.setBandEnabled(b, true);
    EXPECT_EQ(eq.findBandForNewPoint(1000.0f), -1) << "slots 5-24 are free but are picked by findFreeBand";
}

TEST(ParametricEQSlots, TypeAndSlopeSettersRoundTripAndSnap) {
    ParametricEQModule eq;
    eq.setBandType(9, BandType::Tilt);
    EXPECT_EQ(eq.getBandType(9), BandType::Tilt);
    eq.setBandSlope(9, 96);
    EXPECT_EQ(eq.getBandSlope(9), 96);
    eq.setBandSlope(9, 40);
    EXPECT_EQ(eq.getBandSlope(9), 36) << "snaps to the nearest listed slope";
    eq.setBandSlope(9, 60);
    EXPECT_EQ(eq.getBandSlope(9), 48);
    eq.setBandType(99, BandType::Notch); // ignored
    const auto snap = eq.getBandSnapshots()[9];
    EXPECT_EQ(snap.slopeDbPerOct, 48);
    EXPECT_EQ(snap.type, BandType::Tilt);
}

// An old patch's JSON names only the original four bands, so a trusted restore of it must put every other band back
// to its default, and undoing an added band must turn it off again.
TEST(ParametricEQSavedModel, ATrustedRestoreTurnsAnAddedBandBackOff) {
    GraphEQ graphEq("00000000-0000-4000-8000-0000000000e1");
    const auto before = synth::AIStateMapper::nodeToJSON(*graphEq.node);
    auto& eq = graphEq.eq();
    for (int b = 0; b < ParametricEQModule::kOriginalBands; ++b)
        eq.setBandEnabled(b, true);
    ASSERT_EQ(eq.addBand(300.0f, BandType::LowCut), 4);
    eq.setBandSlope(4, 48);

    const auto after = synth::AIStateMapper::nodeToJSON(*graphEq.node);
    auto* afterParams = after.getProperty("params", {}).getDynamicObject();
    ASSERT_NE(afterParams, nullptr);
    for (const char* id : {"band5On", "band5Type", "band5Slope", "band6On", "band24Q"})
        EXPECT_TRUE(afterParams->hasProperty(id)) << id << ": every band is saved";

    ASSERT_TRUE(graphEq.restore(before));
    EXPECT_FALSE(eq.isBandEnabled(4));
    EXPECT_EQ(eq.getBandType(4), BandType::Peak);
    EXPECT_EQ(eq.getBandSlope(4), 12);

    // ...and the saved edit loads back.
    ASSERT_TRUE(graphEq.restore(after));
    EXPECT_TRUE(eq.isBandEnabled(4));
    EXPECT_EQ(eq.getBandType(4), BandType::LowCut);
    EXPECT_EQ(eq.getBandSlope(4), 48);
}

// A model-authored patch reaches every band now, the same way it reaches the first four.
TEST(ParametricEQSavedModel, AnUntrustedPatchSetsAnyBand) {
    ParametricEQModule eq;
    eq.setBandEnabled(6, true);
    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    params->setProperty("band1Gain", 3.0);
    params->setProperty("band9On", true);
    params->setProperty("band9Freq", 1800.0);
    params->setProperty("band2Type", "Notch");
    synth::AIStateMapper::applyUntrustedParams(&eq, params.get());
    EXPECT_NEAR(paramValue(eq, "band1Gain"), 3.0f, 1.0e-3f);
    EXPECT_TRUE(eq.isBandEnabled(8));
    EXPECT_NEAR(paramValue(eq, "band9Freq"), 1800.0f, 2.0f);
    EXPECT_EQ(eq.getBandType(1), BandType::Notch);
    EXPECT_TRUE(eq.isBandEnabled(6)) << "a merge leaves a band it does not name alone";
}

// What the AI and the save format see of a Parametric EQ: the files in Tests/fixtures/parametric-eq/ were written by
// the four-band module (its node JSON all-default and with every original band edited, its module catalog entry, its
// schema table and its saved parameter state). The module now offers 24 bands with a Type and Slope each, so the AI's
// node JSON, catalog entry and schema grow by exactly those entries; everything the four-band module wrote is still
// there, unchanged. A diff here means an old patch's JSON, or the AI's view of one of the original parameters, moved.

namespace {

// True when `actual` holds everything `expected` does: objects by key, arrays of objects with an "id" by that id, other
// arrays element by element, numbers within float noise.
bool jsonContains(const juce::var& actual, const juce::var& expected) {
    if (auto* e = expected.getDynamicObject()) {
        auto* a = actual.getDynamicObject();
        if (a == nullptr)
            return false;
        for (const auto& prop : e->getProperties())
            if (!a->hasProperty(prop.name) || !jsonContains(a->getProperty(prop.name), prop.value))
                return false;
        return true;
    }
    if (auto* e = expected.getArray()) {
        auto* a = actual.getArray();
        if (a == nullptr)
            return false;
        const bool keyed = !e->isEmpty() && e->getFirst().getProperty("id", {}).isString();
        for (int i = 0; i < e->size(); ++i) {
            if (!keyed) {
                if (i >= a->size() || !jsonContains((*a)[i], (*e)[i]))
                    return false;
                continue;
            }
            bool found = false;
            for (const auto& candidate : *a)
                found = found || (candidate.getProperty("id", {}) == (*e)[i].getProperty("id", {}) &&
                                  jsonContains(candidate, (*e)[i]));
            if (!found)
                return false;
        }
        return true;
    }
    if (expected.isDouble() || expected.isInt() || expected.isInt64())
        return std::abs((double)actual - (double)expected) <= 1.0e-5 * std::max(1.0, std::abs((double)expected));
    return actual == expected;
}

void expectContainsGoldenJson(const juce::String& name, const juce::var& actual) {
    const auto golden = eqgolden::goldenFile(name);
    ASSERT_TRUE(golden.existsAsFile()) << "missing golden " << name.toStdString();
    EXPECT_TRUE(jsonContains(actual, juce::JSON::parse(golden)))
        << name.toStdString() << " lost or changed an entry the four-band module wrote";
}

} // namespace

TEST(ParametricEQGolden, DefaultNodeJsonKeepsEverythingTheFourBandModuleWrote) {
    juce::AudioProcessorGraph graph;
    const auto json = eqgolden::eqNodeJson(graph, false);
    expectContainsGoldenJson("node-default.json", json);
    auto* params = json.getProperty("params", {}).getDynamicObject();
    ASSERT_NE(params, nullptr);
    EXPECT_TRUE(params->hasProperty("band24Q")) << "the other bands are saved too";
    EXPECT_TRUE(params->hasProperty("band1Type"));
    EXPECT_TRUE(params->hasProperty("band24Slope"));
}

TEST(ParametricEQGolden, EditedNodeJsonKeepsEverythingTheFourBandModuleWrote) {
    juce::AudioProcessorGraph graph;
    expectContainsGoldenJson("node-legacy.json", eqgolden::eqNodeJson(graph, true));
}

TEST(ParametricEQGolden, CatalogEntryKeepsEverythingTheFourBandModuleListed) {
    const auto entry = eqgolden::catalogEntry();
    expectContainsGoldenJson("catalog-entry.json", entry);
    juce::StringArray ids;
    for (const auto& param : *entry.getProperty("params", {}).getArray())
        ids.add(param.getProperty("id", {}).toString());
    for (const char* id : {"band5On", "band24Gain", "band1Type", "band24Slope"})
        EXPECT_TRUE(ids.contains(id)) << id;
}

TEST(ParametricEQGolden, SchemaSectionKeepsEveryRowTheFourBandModuleHad) {
    const auto actual = eqgolden::schemaSection();
    const auto golden = eqgolden::goldenFile("schema-section.golden").loadFileAsString().replace("\r\n", "\n");
    // The four bands' rows are listed once as the `bandN...` pattern (the catalog still carries every id).
    for (const auto& line : juce::StringArray::fromLines(golden))
        if (line.isNotEmpty() && !line.startsWith("| `band"))
            EXPECT_TRUE(actual.contains(line)) << "missing row: " << line.toStdString();
    for (const char* field : {"On", "Freq", "Gain", "Q", "Type", "Slope"})
        EXPECT_TRUE(actual.contains("| `bandN" + juce::String(field) + "` |")) << field;
}

TEST(ParametricEQGolden, SavedStateOfTheFourBandModuleIsRecorded) {
    // The saved blob grows with the parameter list, so it is only written here (from the four-band
    // module) and read back above; nothing compares the new module's blob.
    const auto golden = eqgolden::goldenFile("state-legacy.xml");
    if (eqgolden::writeRequested()) {
        ParametricEQModule eq;
        eqgolden::applyLegacyEdits(eq);
        juce::MemoryBlock block;
        eq.getStateInformation(block);
        auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int)block.getSize());
        ASSERT_NE(xml, nullptr);
        golden.getParentDirectory().createDirectory();
        ASSERT_TRUE(golden.replaceWithText(xml->toString(), false, false, "\n"));
    }
    EXPECT_TRUE(golden.existsAsFile());
}
