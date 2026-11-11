// The machine-readable module catalog: coverage of the modules the hosted AI invents parameters for.
#include "AI/AIStateMapper/AIStateMapper.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

juce::var parseCatalog() {
    const juce::var catalog = juce::JSON::parse(synth::AIStateMapper::getModuleCatalogJson());
    EXPECT_TRUE(catalog.isObject());
    return catalog;
}

juce::var findParam(const juce::var& moduleEntry, const juce::String& id) {
    const auto* params = moduleEntry.getProperty("params", {}).getArray();
    if (params != nullptr)
        for (const auto& p : *params)
            if (p.getProperty("id", {}).toString() == id)
                return p;
    return {};
}

} // namespace

TEST(AIStateMapperCatalogTest, ListsEveryAuthorableModuleAndOnlyThose) {
    const auto catalog = parseCatalog();
    EXPECT_EQ((int)catalog.getProperty("version", 0), 1);
    auto* modules = catalog.getProperty("modules", {}).getDynamicObject();
    ASSERT_NE(modules, nullptr);

    const auto authorable = synth::AIStateMapper::authorableModuleTypes();
    EXPECT_EQ(modules->getProperties().size(), authorable.size());
    for (const auto& type : authorable)
        EXPECT_TRUE(modules->hasProperty(juce::Identifier(type))) << type;
    EXPECT_FALSE(modules->hasProperty("Track In"));
    EXPECT_FALSE(modules->hasProperty("Master"));
}

TEST(AIStateMapperCatalogTest, FilterHasAChoiceParamWithChoices) {
    const auto catalog = parseCatalog();
    const auto filter = catalog.getProperty("modules", {}).getProperty("Filter", {});
    ASSERT_TRUE(filter.isObject());

    bool sawChoice = false;
    for (const auto& p : *filter.getProperty("params", {}).getArray()) {
        if (p.getProperty("kind", {}).toString() != "choice")
            continue;
        sawChoice = true;
        const auto* choices = p.getProperty("choices", {}).getArray();
        ASSERT_NE(choices, nullptr);
        EXPECT_FALSE(choices->isEmpty());
        EXPECT_TRUE(choices->contains(p.getProperty("default", {})));
    }
    EXPECT_TRUE(sawChoice);

    const auto cutoff = findParam(filter, "cutoff");
    ASSERT_TRUE(cutoff.isObject());
    EXPECT_EQ(cutoff.getProperty("kind", {}).toString(), "float");
    EXPECT_TRUE((bool)cutoff.getProperty("modulatable", false));
    EXPECT_FALSE(findParam(filter, "bypassed").hasProperty("modulatable"));
    EXPECT_LT((double)cutoff.getProperty("min", 0), (double)cutoff.getProperty("max", 0));
}

TEST(AIStateMapperCatalogTest, CoversEffectsLfoAndDrumKit) {
    const auto modules = parseCatalog().getProperty("modules", {});
    for (const char* type : {"LFO", "Reverb", "Chorus", "Delay", "Drum Kit", "Pluck", "Mallet"}) {
        const auto entry = modules.getProperty(type, {});
        ASSERT_TRUE(entry.isObject()) << type;
        const auto* params = entry.getProperty("params", {}).getArray();
        ASSERT_NE(params, nullptr) << type;
        EXPECT_FALSE(params->isEmpty()) << type;
        EXPECT_TRUE(entry.hasProperty("audioIn")) << type;
        EXPECT_TRUE(entry.hasProperty("audioOut")) << type;
    }
    // The ids the model used to invent must not exist.
    EXPECT_FALSE(findParam(modules.getProperty("Chorus", {}), "rateHz").isObject());
    EXPECT_FALSE(findParam(modules.getProperty("Drum Kit", {}), "snareTone").isObject());
}

// Regression test for FRO1359: the instruments' Level took no mod cable, so the catalog listed it as not modulatable.
TEST(AIStateMapperCatalogTest, InstrumentLevelIsModulatableWithoutAnAudioInput) {
    const auto modules = parseCatalog().getProperty("modules", {});
    for (const char* type : {"Drum Kit", "Pluck", "Mallet"}) {
        const auto entry = modules.getProperty(type, {});
        ASSERT_TRUE(entry.isObject()) << type;
        EXPECT_FALSE((bool)entry.getProperty("audioIn", true)) << type << ": a knob's cable is no audio input";
        EXPECT_TRUE((bool)findParam(entry, "level").getProperty("modulatable", false)) << type;
    }
    EXPECT_TRUE((bool)findParam(modules.getProperty("Pluck", {}), "body").getProperty("modulatable", false));
    EXPECT_TRUE((bool)findParam(modules.getProperty("Mallet", {}), "tremolo").getProperty("modulatable", false));
    EXPECT_TRUE((bool)modules.getProperty("Delay", {}).getProperty("audioIn", false))
        << "a module with audio inputs beside a knob-only one keeps audioIn";
}

TEST(AIStateMapperCatalogTest, DescribesInstrumentTracksAndTheirEnvelope) {
    const auto tracks = parseCatalog().getProperty("instrumentTracks", {});
    auto* instruments = tracks.getProperty("instruments", {}).getDynamicObject();
    ASSERT_NE(instruments, nullptr);
    for (const char* type : {"Oscillator", "Wavetable", "Sampler", "Drum Kit", "Pluck", "Mallet", "Noise"})
        EXPECT_TRUE(instruments->hasProperty(type)) << type;
    EXPECT_TRUE((bool)instruments->getProperty("Oscillator").getProperty("hasEnvelope", false));
    // Noise gets the ADSR + VCA like a raw Oscillator, but a Noise track is always mono.
    EXPECT_TRUE((bool)instruments->getProperty("Noise").getProperty("hasEnvelope", false));
    EXPECT_FALSE((bool)instruments->getProperty("Noise").getProperty("poly", true));
    for (const char* type : {"Drum Kit", "Pluck", "Mallet"}) {
        EXPECT_FALSE((bool)instruments->getProperty(type).getProperty("hasEnvelope", true)) << type;
        EXPECT_FALSE((bool)instruments->getProperty(type).getProperty("poly", true)) << type;
    }

    const auto envelope = tracks.getProperty("envelope", {});
    EXPECT_TRUE(findParam(envelope, "sustain").isObject());
}

TEST(AIStateMapperCatalogTest, OutputIsDeterministic) {
    EXPECT_EQ(synth::AIStateMapper::getModuleCatalogJson(), synth::AIStateMapper::getModuleCatalogJson());
}

// Regression test for FRO1696: the catalog names every one of the EQ's 24 bands, each with its type and cut slope.
TEST(AIStateMapperCatalogTest, ParametricEqListsTwentyFourBandsWithTypesAndSlopes) {
    const auto eq = parseCatalog().getProperty("modules", {}).getProperty("Parametric EQ", {});
    ASSERT_TRUE(eq.isObject());
    for (int band = 1; band <= 24; ++band) {
        const juce::String prefix = "band" + juce::String(band);
        for (const char* field : {"On", "Freq", "Gain", "Q", "Type", "Slope"})
            EXPECT_TRUE(findParam(eq, prefix + field).isObject()) << prefix << field;
        const auto type = findParam(eq, prefix + "Type");
        const auto slope = findParam(eq, prefix + "Slope");
        ASSERT_NE(type.getProperty("choices", {}).getArray(), nullptr) << prefix;
        EXPECT_EQ(type.getProperty("kind", {}).toString(), "choice");
        EXPECT_EQ(slope.getProperty("kind", {}).toString(), "choice");
        const auto* types = type.getProperty("choices", {}).getArray();
        EXPECT_EQ(types->size(), 8);
        for (const char* name :
             {"Low Shelf", "Bell", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"})
            EXPECT_TRUE(types->contains(juce::var(name))) << prefix << " " << name;
        const auto* slopes = slope.getProperty("choices", {}).getArray();
        ASSERT_NE(slopes, nullptr);
        EXPECT_EQ(slopes->size(), 9);
        EXPECT_EQ(slopes->getFirst().toString(), "6 dB/oct");
        EXPECT_EQ(slopes->getLast().toString(), "96 dB/oct");
    }
    EXPECT_FALSE(findParam(eq, "band25On").isObject());
    EXPECT_EQ(findParam(eq, "band1Type").getProperty("default", {}).toString(), "Low Shelf");
    EXPECT_EQ(findParam(eq, "band5Type").getProperty("default", {}).toString(), "Bell");
}
