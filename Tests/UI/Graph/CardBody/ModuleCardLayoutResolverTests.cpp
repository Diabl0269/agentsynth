// ModuleCardLayoutResolverTests.cpp
//
// Source/UI/Graph/CardBody/ModuleCardLayoutResolver.h: instance override, then the type's stored
// default, then the code default, then automatic. Registries and stores are injected, so nothing
// touches the real settings folder.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "UI/Graph/CardBody/ModuleCardLayoutResolver.h"
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::DefaultCardLayouts;
using synth::ModuleCardLayoutStore;
using synth::resolveModuleCardLayout;
using Source = synth::ResolvedModuleCardLayout::Source;

namespace {

// A sections-form layout (it round-trips through JSON as itself) that hides `id`, based on the Filter default's
// current revision unless `basedOn` says otherwise (empty: none).
CardLayout hiding(const juce::String& id, const juce::String& basedOn = "Filter@4") {
    synth::CardSection section;
    section.id = "main";
    CardLayout layout;
    layout.sections = {section};
    layout.hidden.add(id);
    if (basedOn.isNotEmpty())
        layout.basedOn = basedOn;
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
        defaults.add("Filter", hiding("code", ""), 4);
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
    EXPECT_EQ(resolved.layout, hiding("code", ""));
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
    const auto resolved = resolveModuleCardLayout("NoSuchType", juce::var(), nullptr, DefaultCardLayouts::builtIn());
    EXPECT_EQ(resolved.source, Source::Automatic);
    EXPECT_EQ(DefaultCardLayouts::builtIn().find("NoSuchType"), nullptr) << "a type with no code default";
}

TEST_F(ModuleCardLayoutResolverTest, AV1LayoutIsUpgradedWhenTheParameterListIsKnown) {
    const juce::StringArray all{"cutoff", "resonance", "drive"};

    // A type with no code default keeps whatever layout was saved.
    auto resolved = resolveModuleCardLayout("Gadget", flat("cutoff").toVar(), nullptr, defaults, &all);

    ASSERT_EQ(resolved.source, Source::Instance);
    ASSERT_TRUE(resolved.layout.has_value());
    EXPECT_EQ(resolved.layout->hidden, juce::StringArray({"resonance", "drive"}));
    ASSERT_EQ(resolved.layout->sections.size(), 1u);

    resolved = resolveModuleCardLayout("Gadget", flat("cutoff").toVar(), nullptr, defaults);
    EXPECT_EQ(resolved.layout, flat("cutoff")) << "without the list the layout is returned as read";
}

// Regression test for FRO2322: a layout saved before a redesign of the default hid the redesign for ever, so the Osc
// card kept drawing its old layout.
TEST_F(ModuleCardLayoutResolverTest, AStoredDefaultBasedOnAnOlderRevisionOrNoneIsIgnored) {
    ModuleCardLayoutStore store(root);
    for (const char* basedOn :
         {"Filter@3", "Filter@1", "Filter@0", "", "Oscillator@4", "Filter@", "Filter@x", "junk"}) {
        ASSERT_TRUE(store.setDefault("Filter", hiding("user", basedOn))) << basedOn;
        const auto resolved = resolveModuleCardLayout("Filter", juce::var(), &store, defaults);
        EXPECT_EQ(resolved.source, Source::CodeDefault) << "basedOn \"" << basedOn << "\"";
        EXPECT_EQ(resolved.layout, hiding("code", ""));
        EXPECT_EQ(resolved.defaultRevision, 4);
    }
}

TEST_F(ModuleCardLayoutResolverTest, AStoredDefaultBasedOnTheCurrentOrANewerRevisionStillWins) {
    ModuleCardLayoutStore store(root);
    for (const char* basedOn : {"Filter@4", "Filter@5"}) {
        ASSERT_TRUE(store.setDefault("Filter", hiding("user", basedOn)));
        const auto resolved = resolveModuleCardLayout("Filter", juce::var(), &store, defaults);
        EXPECT_EQ(resolved.source, Source::TypeDefault) << basedOn;
        EXPECT_EQ(resolved.layout, hiding("user", basedOn));
    }
}

TEST_F(ModuleCardLayoutResolverTest, AnInstanceOverrideBasedOnAnOlderRevisionFallsThroughToTheNextSource) {
    ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Filter", hiding("user")));
    for (const char* basedOn : {"Filter@3", ""}) {
        auto resolved = resolveModuleCardLayout("Filter", hiding("instance", basedOn).toVar(), &store, defaults);
        EXPECT_EQ(resolved.source, Source::TypeDefault) << "an old override gives way to the current stored default";
        EXPECT_EQ(resolved.layout, hiding("user"));
        ASSERT_TRUE(store.clearDefault("Filter"));
        resolved = resolveModuleCardLayout("Filter", hiding("instance", basedOn).toVar(), &store, defaults);
        EXPECT_EQ(resolved.source, Source::CodeDefault) << "and with no stored default, to the code default";
        ASSERT_TRUE(store.setDefault("Filter", hiding("user")));
    }
    const auto current = resolveModuleCardLayout("Filter", hiding("instance", "Filter@4").toVar(), &store, defaults);
    EXPECT_EQ(current.source, Source::Instance);
}

TEST_F(ModuleCardLayoutResolverTest, ALayoutSavedFromTheCardIsMarkedAsBasedOnTheCurrentDefault) {
    const auto saved = synth::withBasedOnCurrentDefault(hiding("edited", ""), "Filter", defaults);
    EXPECT_EQ(saved.basedOn, juce::String("Filter@4"));
    EXPECT_EQ(synth::withBasedOnCurrentDefault(hiding("edited", "Filter@9"), "Filter", defaults).basedOn,
              juce::String("Filter@9"))
        << "an existing mark is kept";
    EXPECT_FALSE(synth::withBasedOnCurrentDefault(hiding("edited", ""), "Gadget", defaults).basedOn.has_value())
        << "a type with no code default has nothing to be based on";
    ModuleCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault("Filter", saved));
    EXPECT_EQ(resolveModuleCardLayout("Filter", juce::var(), &store, defaults).source, Source::TypeDefault);
}

// updateModuleNames numbers two ADSRs "ADSR 1" and "ADSR 2". The card's layout is looked up by
// the factory type name, which for an ADSR used to be that display name, so a card rebuilt after an
// undo found no default and drew the flat automatic list instead of the envelope card.
TEST(ModuleCardLayoutResolver, ANumberedEnvelopeStillGetsItsCodeDefault) {
    for (const char* name : {"ADSR 1", "ADSR 2", "Amp Env 3", "Filter Env 1"}) {
        ADSRModule module(name);
        const auto type = synth::AIStateMapper::getFactoryTypeName(&module);
        const auto resolved = resolveModuleCardLayout(type, juce::var(), nullptr, DefaultCardLayouts::builtIn());
        EXPECT_EQ(resolved.source, Source::CodeDefault) << name << " resolved as \"" << type << "\"";
        EXPECT_NE(synth::AIStateMapper::createModule(type), nullptr) << "\"" << type << "\" must still rebuild";
    }
}
