// HostedPluginTitleTests.cpp
//
// A hosted plugin module is shown under the plugin's name ("Diva"), the second of the same plugin as
// "Diva 2". The type key (getName() / the factory name) stays "Hosted Plugin" throughout.

#include "../StubPluginInstance.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <gtest/gtest.h>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::test::StubBackend;

namespace {

juce::PluginDescription descriptionNamed(const juce::String& name, int uid) {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = "VST3";
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = "/nonexistent/test/path/" + name + ".vst3";
    return description;
}

HostedPluginModule* addHosted(AudioEngine& engine, const juce::String& name, int uid, StubBackend& backend) {
    auto module = std::make_unique<HostedPluginModule>();
    auto* raw = module.get();
    engine.getGraph().addNode(std::move(module));
    raw->loadPlugin(descriptionNamed(name, uid), backend);
    return raw;
}

class HostedPluginTitleTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.initialise();
        engine.getGraph().clear();
    }
    AudioEngine engine;
    StubBackend backend;
};

} // namespace

TEST_F(HostedPluginTitleTest, ABareModuleKeepsTheGenericTitle) {
    HostedPluginModule module;
    EXPECT_EQ(synth::moduleTitle(juce::NamedValueSet(), &module), "Hosted Plugin");
}

TEST_F(HostedPluginTitleTest, ALoneModuleShowsThePluginName) {
    auto* diva = addHosted(engine, "Diva", 1, backend);
    engine.updateModuleNames();
    EXPECT_EQ(synth::moduleTitle(juce::NamedValueSet(), diva), "Diva");
    EXPECT_EQ(diva->getName(), "Hosted Plugin") << "the type key must not change";
    EXPECT_EQ(synth::AIStateMapper::getFactoryTypeName(diva), "Hosted Plugin");
}

TEST_F(HostedPluginTitleTest, TwoOfTheSamePluginReadDivaAndDiva2) {
    auto* first = addHosted(engine, "Diva", 1, backend);
    auto* second = addHosted(engine, "Diva", 1, backend);
    auto* serum = addHosted(engine, "Serum", 2, backend);
    auto* third = addHosted(engine, "Diva", 1, backend);
    engine.updateModuleNames();

    EXPECT_EQ(first->getDefaultTitle(), "Diva");
    EXPECT_EQ(second->getDefaultTitle(), "Diva 2");
    EXPECT_EQ(third->getDefaultTitle(), "Diva 3");
    EXPECT_EQ(serum->getDefaultTitle(), "Serum") << "a different plugin is numbered on its own";
}

TEST_F(HostedPluginTitleTest, ACustomTitleStillWinsOverThePluginName) {
    auto* diva = addHosted(engine, "Diva", 1, backend);
    juce::NamedValueSet props;
    props.set("displayName", "Lead");
    EXPECT_EQ(synth::moduleTitle(props, diva), "Lead");
}

TEST_F(HostedPluginTitleTest, TheShownNamesSurviveASaveAndLoad) {
    addHosted(engine, "Diva", 1, backend);
    addHosted(engine, "Diva", 1, backend);
    engine.updateModuleNames();
    const juce::var json = synth::AIStateMapper::graphToJSON(engine.getGraph());

    HostedPluginBackend::ScopedDefault installed(&backend);
    AudioEngine other;
    other.initialise();
    other.getGraph().clear();
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, other.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));
    other.updateModuleNames();

    std::vector<juce::String> titles;
    for (auto* node : other.getGraph().getNodes())
        if (dynamic_cast<HostedPluginModule*>(node->getProcessor()) != nullptr)
            titles.push_back(synth::moduleTitle(*node));
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_TRUE((titles[0] == "Diva" && titles[1] == "Diva 2") || (titles[0] == "Diva 2" && titles[1] == "Diva"));
}
