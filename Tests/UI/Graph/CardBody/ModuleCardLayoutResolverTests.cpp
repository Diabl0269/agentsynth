// ModuleCardLayoutResolverTests.cpp
//
// Source/UI/Graph/CardBody/ModuleCardLayoutResolver.h: instance override, then the type's stored
// default, then the code default, then automatic. Registries and stores are injected, so nothing
// touches the real settings folder.

#include "UI/Graph/CardBody/ModuleCardLayoutResolver.h"
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::DefaultCardLayouts;
using synth::ModuleCardLayoutStore;
using synth::resolveModuleCardLayout;
using Source = synth::ResolvedModuleCardLayout::Source;

namespace {

// A sections-form layout (it round-trips through JSON as itself) that hides `id`.
CardLayout hiding(const juce::String& id) {
    synth::CardSection section;
    section.id = "main";
    CardLayout layout;
    layout.sections = {section};
    layout.hidden.add(id);
    return layout;
}

CardLayout flat(const juce::String& id) {
    synth::CardSlot slot;
    slot.paramId = id;
    CardLayout layout;
    layout.slots.push_back(slot);
    return layout;
}

} // namespace

class ModuleCardLayoutResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-modulecardlayout-resolver-" + juce::Uuid().toString());
        defaults.add("Filter", hiding("code"), 4);
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
    DefaultCardLayouts defaults;
};

TEST_F(ModuleCardLayoutResolverTest, InstanceBeatsTypeDefaultBeatsCodeDefaultBeatsAutomatic) {
    ModuleCardLayoutStore store(root);

    auto resolved = resolveModuleCardLayout("Filter", hiding("instance").toVar(), &store, defaults);
    EXPECT_EQ(resolved.source, Source::Instance);
    EXPECT_EQ(resolved.layout, hiding("instance"));

    // Without an instance layout, a stored per-type default applies...
    ASSERT_TRUE(store.setDefault("Filter", hiding("user")));
    resolved = resolveModuleCardLayout("Filter", juce::var(), &store, defaults);
    EXPECT_EQ(resolved.source, Source::TypeDefault);
    EXPECT_EQ(resolved.layout, hiding("user"));

    // ...then the code default, with its revision...
    ASSERT_TRUE(store.clearDefault("Filter"));
    resolved = resolveModuleCardLayout("Filter", juce::var(), &store, defaults);
    EXPECT_EQ(resolved.source, Source::CodeDefault);
    EXPECT_EQ(resolved.layout, hiding("code"));
    EXPECT_EQ(resolved.defaultRevision, 4);

    // ...and a type with none is automatic.
    resolved = resolveModuleCardLayout("Oscillator", juce::var(), &store, defaults);
    EXPECT_EQ(resolved.source, Source::Automatic);
    EXPECT_FALSE(resolved.layout.has_value());
}

TEST_F(ModuleCardLayoutResolverTest, AnUnreadableSourceFallsThroughToTheNextOne) {
    ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Filter", hiding("user")));

    const auto newer = juce::JSON::parse(R"({"version":3,"sections":[]})");
    auto resolved = resolveModuleCardLayout("Filter", newer, &store, defaults);
    EXPECT_EQ(resolved.source, Source::TypeDefault) << "a newer-version override is skipped, not fatal";

    ASSERT_TRUE(store.getTypeDirectory("Filter").getChildFile("default.json").replaceWithText("not json"));
    resolved = resolveModuleCardLayout("Filter", juce::var("garbage"), &store, defaults);
    EXPECT_EQ(resolved.source, Source::CodeDefault);
}

TEST_F(ModuleCardLayoutResolverTest, ANullStoreAndAnEmptyRegistryAreAutomatic) {
    const auto resolved = resolveModuleCardLayout("Filter", juce::var(), nullptr, DefaultCardLayouts::builtIn());
    EXPECT_EQ(resolved.source, Source::Automatic);
    EXPECT_EQ(DefaultCardLayouts::builtIn().find("Filter"), nullptr) << "no type has a code default yet";
}

TEST_F(ModuleCardLayoutResolverTest, AV1LayoutIsUpgradedWhenTheParameterListIsKnown) {
    const juce::StringArray all{"cutoff", "resonance", "drive"};

    auto resolved = resolveModuleCardLayout("Filter", flat("cutoff").toVar(), nullptr, defaults, &all);

    ASSERT_EQ(resolved.source, Source::Instance);
    ASSERT_TRUE(resolved.layout.has_value());
    EXPECT_EQ(resolved.layout->hidden, juce::StringArray({"resonance", "drive"}));
    ASSERT_EQ(resolved.layout->sections.size(), 1u);

    resolved = resolveModuleCardLayout("Filter", flat("cutoff").toVar(), nullptr, defaults);
    EXPECT_EQ(resolved.layout, flat("cutoff")) << "without the list the layout is returned as read";
}
