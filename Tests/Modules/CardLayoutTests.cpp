// CardLayoutTests.cpp
//
// Source/Modules/CardLayout.h (the value type) and Source/Plugin/Hosting/HostedPluginCardLayout.h
// (the precedence resolver and automatic default), plus the undo seam the per-instance override
// rides on (AppUndoManager::recordNodeExtraStateChange). See docs/control/plugin-card-layout.md.
//
// The hosted-plugin cases run against Tests/StubPluginInstance.h, like HostedPluginTests, so they
// pump the message loop for the async load.

#include "../StubPluginInstance.h"
#include "AppUndoManager.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginCardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include <chrono>
#include <gtest/gtest.h>

using synth::CardLayout;
using synth::CardSlot;
using synth::CardSlotKind;
using synth::HostedPluginModule;
using synth::PluginCardLayoutStore;
using synth::ResolvedCardLayout;
using synth::test::StubBackend;
using synth::test::StubHostedParameter;
using synth::test::StubParamSpec;
using synth::test::StubParamTraits;
using synth::test::StubPluginInstance;

namespace {

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

CardSlot slot(const juce::String& id, int hint = -1, std::optional<juce::String> label = std::nullopt,
              CardSlotKind kind = CardSlotKind::Auto) {
    CardSlot s;
    s.paramId = id;
    s.indexHint = hint;
    s.label = std::move(label);
    s.kind = kind;
    return s;
}

juce::PluginDescription description(const juce::String& name = "Layout Plugin", int uid = 0x4c41) {
    juce::PluginDescription d;
    d.name = name;
    d.pluginFormatName = "VST3";
    d.uniqueId = uid;
    d.deprecatedUid = uid;
    return d;
}

/** Owns a StubBackend whose instances carry `params`, and one module loaded against it. */
struct LoadedModule {
    explicit LoadedModule(std::vector<StubParamSpec> params)
        : backend([params] {
            return std::make_unique<StubPluginInstance>(2, 2, "Layout Plugin", 0x4c41, "VST3", params);
        }) {
        module.prepareToPlay(48000.0, 64);
        module.loadPlugin(description(), backend);
        EXPECT_TRUE(pumpUntil([&] { return module.hasInstance(); }));
    }

    StubBackend backend;
    HostedPluginModule module;
};

std::vector<StubParamSpec> numberedParams(int count) {
    std::vector<StubParamSpec> params;
    for (int i = 0; i < count; ++i)
        params.push_back({"id" + juce::String(i), "Param " + juce::String(i)});
    return params;
}

CardLayout layoutOf(std::initializer_list<CardSlot> slots) {
    CardLayout layout;
    layout.slots.assign(slots.begin(), slots.end());
    return layout;
}

} // namespace

// ============================================================================
// The value type
// ============================================================================

TEST(CardLayoutTest, RoundTripsEverySlotFieldThroughJsonText) {
    const CardLayout layout = layoutOf({slot("cutoff", 3, juce::String("Cutoff"), CardSlotKind::Knob),
                                        slot("legacy:7", 7), slot("mode", 1, std::nullopt, CardSlotKind::Choice),
                                        slot("on", 2, std::nullopt, CardSlotKind::Toggle)});

    const juce::var reparsed = juce::JSON::parse(juce::JSON::toString(layout.toVar()));
    const auto result = CardLayout::fromVar(reparsed);

    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);
    EXPECT_EQ(result.layout, layout);
    EXPECT_FALSE(result.layout.slots[1].label.has_value()) << "a null label must stay 'the parameter's own name'";
}

TEST(CardLayoutTest, EmptyLayoutRoundTrips) {
    const auto result = CardLayout::fromVar(juce::JSON::parse(juce::JSON::toString(CardLayout{}.toVar())));
    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);
    EXPECT_TRUE(result.layout.slots.empty());
}

TEST(CardLayoutTest, RefusesMalformedInputAndNewerVersions) {
    EXPECT_EQ(CardLayout::fromVar(juce::var()).status, CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"slots":[]})")).status, CardLayout::ParseStatus::Malformed)
        << "no version";
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":1})")).status, CardLayout::ParseStatus::Malformed)
        << "no slots array";
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":1,"slots":[{"indexHint":1}]})")).status,
              CardLayout::ParseStatus::Malformed)
        << "a slot without a paramId";
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":1,"slots":[{"paramId":"a","kind":"dial"}]})")).status,
              CardLayout::ParseStatus::Malformed)
        << "an unknown kind";
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":2,"slots":[]})")).status,
              CardLayout::ParseStatus::UnsupportedVersion);
}

TEST(CardLayoutTest, AutoKindIsDerivedFromTheParameter) {
    StubHostedParameter plain("p", "Plain");
    StubHostedParameter toggle("t", "Toggle", 0.0f, StubParamTraits{true, /*boolean=*/true, {}});
    StubHostedParameter choice("c", "Choice", 0.0f, StubParamTraits{true, false, {"Sine", "Saw", "Square"}});

    EXPECT_EQ(synth::deriveSlotKind(plain), CardSlotKind::Knob);
    EXPECT_EQ(synth::deriveSlotKind(toggle), CardSlotKind::Toggle);
    EXPECT_EQ(synth::deriveSlotKind(choice), CardSlotKind::Choice);

    EXPECT_EQ(synth::effectiveSlotKind(slot("c"), &choice), CardSlotKind::Choice);
    EXPECT_EQ(synth::effectiveSlotKind(slot("c", -1, std::nullopt, CardSlotKind::Knob), &choice), CardSlotKind::Knob)
        << "an explicit kind beats derivation";
    EXPECT_EQ(synth::effectiveSlotKind(slot("x"), nullptr), CardSlotKind::Knob) << "an unresolved slot draws as a knob";
}

// ============================================================================
// The automatic default
// ============================================================================

TEST(CardLayoutTest, AutomaticDefaultIsTheFirstEightAutomatableParametersSkippingBypass) {
    std::vector<StubParamSpec> params;
    params.push_back({"byp", "Bypass"});                                          // by name
    params.push_back({"pwr", "Power", 0.0f, {}, /*isBypass=*/true});              // by getBypassParameter()
    params.push_back({"hid", "Hidden", 0.0f, StubParamTraits{false, false, {}}}); // not automatable
    params.push_back({"bm", "Bypass Mode"});                                      // only an EXACT "Bypass" is skipped
    for (int i = 0; i < 10; ++i)
        params.push_back({"id" + juce::String(i), "Param " + juce::String(i)});
    LoadedModule loaded(params);

    const CardLayout automatic = synth::automaticCardLayout(loaded.module);

    ASSERT_EQ(automatic.slots.size(), 8u) << "capped at 8";
    EXPECT_EQ(automatic.slots[0].paramId, "bm");
    EXPECT_EQ(automatic.slots[0].indexHint, 3);
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(automatic.slots[(size_t)i + 1].paramId, "id" + juce::String(i));
}

TEST(CardLayoutTest, AutomaticDefaultIsEmptyWithoutAnInstanceOrAutomatableParameters) {
    HostedPluginModule bare;
    EXPECT_TRUE(synth::automaticCardLayout(bare).slots.empty());

    LoadedModule inert({{"a", "A", 0.0f, StubParamTraits{false, false, {}}}});
    EXPECT_TRUE(synth::automaticCardLayout(inert.module).slots.empty());
}

TEST(CardLayoutTest, AutomaticDefaultBindsIdLessParametersByTheirIndex) {
    LoadedModule loaded({{"", "Legacy A"}, {"", "Legacy B"}});

    const auto resolved = synth::resolveHostedCardLayout(loaded.module, nullptr);

    EXPECT_EQ(resolved.source, ResolvedCardLayout::Source::Automatic);
    ASSERT_EQ(resolved.slots.size(), 2u);
    EXPECT_EQ(resolved.slots[1].slot.paramId, "legacy:1");
    ASSERT_NE(resolved.slots[1].param, nullptr) << "legacy:<index> resolves through the lane rescue rule";
    EXPECT_EQ(resolved.slots[1].param->getName(64), "Legacy B");
    EXPECT_EQ(resolved.orphanCount(), 0);
}

// ============================================================================
// Orphans
// ============================================================================

TEST(CardLayoutTest, AnUnresolvableSlotIsKeptButFlaggedAndDroppedOnSave) {
    LoadedModule loaded(numberedParams(3));
    loaded.module.setCardLayoutOverride(layoutOf({slot("id1", 1), slot("gone", 2), slot("id0", 0)}).toVar());

    const auto resolved = synth::resolveHostedCardLayout(loaded.module, nullptr);

    ASSERT_EQ(resolved.slots.size(), 3u) << "an orphan stays in the resolved layout";
    EXPECT_FALSE(resolved.slots[0].orphaned);
    EXPECT_TRUE(resolved.slots[1].orphaned) << "id 'gone' is not on the instance, and the hint at index 2 holds a "
                                               "DIFFERENT real id: that is drift, not a rescue";
    EXPECT_EQ(resolved.slots[1].param, nullptr);
    EXPECT_EQ(resolved.orphanCount(), 1);
    EXPECT_EQ(resolved.layout.slots.size(), 3u);

    const CardLayout saved = resolved.layoutWithoutOrphans();
    ASSERT_EQ(saved.slots.size(), 2u);
    EXPECT_EQ(saved.slots[0].paramId, "id1");
    EXPECT_EQ(saved.slots[1].paramId, "id0");
}

TEST(CardLayoutTest, SlotsAreUnresolvedNotOrphanedWhileNoInstanceIsLive) {
    HostedPluginModule bare;
    bare.setCardLayoutOverride(layoutOf({slot("id0", 0)}).toVar());

    const auto resolved = synth::resolveHostedCardLayout(bare, nullptr);

    ASSERT_EQ(resolved.slots.size(), 1u);
    EXPECT_EQ(resolved.slots[0].param, nullptr);
    EXPECT_FALSE(resolved.slots[0].orphaned) << "the plugin may still be loading; an orphan is dropped on save";
    EXPECT_EQ(resolved.layoutWithoutOrphans().slots.size(), 1u);
}

// ============================================================================
// Precedence
// ============================================================================

class CardLayoutPrecedenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-cardlayout-tests-" + juce::Uuid().toString());
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File root;
};

TEST_F(CardLayoutPrecedenceTest, InstanceOverrideBeatsPluginDefaultBeatsAutomatic) {
    LoadedModule loaded(numberedParams(12));
    PluginCardLayoutStore store(root);
    const auto identity = loaded.module.getIdentity();

    auto resolved = synth::resolveHostedCardLayout(loaded.module, &store);
    EXPECT_EQ(resolved.source, ResolvedCardLayout::Source::Automatic);
    EXPECT_EQ(resolved.slots.size(), 8u);

    ASSERT_TRUE(store.setDefault(identity, layoutOf({slot("id5", 5), slot("id6", 6)})));
    resolved = synth::resolveHostedCardLayout(loaded.module, &store);
    EXPECT_EQ(resolved.source, ResolvedCardLayout::Source::PluginDefault);
    ASSERT_EQ(resolved.slots.size(), 2u);
    EXPECT_EQ(resolved.slots[0].slot.paramId, "id5");

    loaded.module.setCardLayoutOverride(layoutOf({slot("id9", 9)}).toVar());
    resolved = synth::resolveHostedCardLayout(loaded.module, &store);
    EXPECT_EQ(resolved.source, ResolvedCardLayout::Source::Instance);
    ASSERT_EQ(resolved.slots.size(), 1u);
    EXPECT_EQ(resolved.slots[0].slot.paramId, "id9");
    ASSERT_NE(resolved.slots[0].param, nullptr);

    loaded.module.setCardLayoutOverride({});
    EXPECT_EQ(synth::resolveHostedCardLayout(loaded.module, &store).source, ResolvedCardLayout::Source::PluginDefault);

    ASSERT_TRUE(store.clearDefault(identity));
    EXPECT_EQ(synth::resolveHostedCardLayout(loaded.module, &store).source, ResolvedCardLayout::Source::Automatic);
}

TEST_F(CardLayoutPrecedenceTest, AnEmptyOverrideIsStillAnOverride) {
    LoadedModule loaded(numberedParams(3));
    loaded.module.setCardLayoutOverride(CardLayout{}.toVar());

    const auto resolved = synth::resolveHostedCardLayout(loaded.module, nullptr);

    EXPECT_EQ(resolved.source, ResolvedCardLayout::Source::Instance) << "the user cleared every knob on purpose";
    EXPECT_TRUE(resolved.slots.empty());
}

TEST_F(CardLayoutPrecedenceTest, AnUnreadableSourceFallsThroughToTheNextOne) {
    LoadedModule loaded(numberedParams(3));
    PluginCardLayoutStore store(root);
    ASSERT_TRUE(store.setDefault(loaded.module.getIdentity(), layoutOf({slot("id2", 2)})));

    loaded.module.setCardLayoutOverride(juce::JSON::parse(R"({"version":2,"slots":[]})"));

    EXPECT_EQ(synth::resolveHostedCardLayout(loaded.module, &store).source, ResolvedCardLayout::Source::PluginDefault)
        << "a layout from a newer version is skipped, not applied and not fatal";
    EXPECT_FALSE(loaded.module.getCardLayoutOverride().isVoid()) << "...and it is left in place, not wiped";
}

// ============================================================================
// Undo
// ============================================================================

TEST(CardLayoutUndoTest, ExtraStateChangeUndoesAndRedoesWithoutReloadingThePlugin) {
    juce::AudioProcessorGraph graph;
    StubBackend backend(
        [] { return std::make_unique<StubPluginInstance>(2, 2, "Layout Plugin", 0x4c41, "VST3", numberedParams(4)); });
    auto owned = std::make_unique<HostedPluginModule>();
    auto* module = owned.get();
    const auto nodeId = graph.addNode(std::move(owned))->nodeID;
    module->prepareToPlay(48000.0, 64);
    module->loadPlugin(description(), backend);
    ASSERT_TRUE(pumpUntil([&] { return module->hasInstance(); }));
    const int createsBefore = backend.createCount;

    AppUndoManager undo;
    const juce::var none = HostedPluginModule::makeCardLayoutPatch({});
    const juce::var layout = layoutOf({slot("id1", 1)}).toVar();

    int notifications = 0;
    module->onCardLayoutChanged = [&] { ++notifications; };

    module->setCardLayoutOverride(layout);
    undo.recordNodeExtraStateChange(graph, nodeId, none, HostedPluginModule::makeCardLayoutPatch(layout));
    ASSERT_TRUE(undo.canUndo());
    EXPECT_EQ(notifications, 1);

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(module->getCardLayoutOverride().isVoid());
    EXPECT_EQ(notifications, 2) << "undo notifies, so an open card rebuilds";

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(juce::JSON::toString(module->getCardLayoutOverride()), juce::JSON::toString(layout));

    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(backend.createCount, createsBefore) << "layout undo must never re-load the plugin";
    EXPECT_TRUE(module->hasInstance());
}

TEST(CardLayoutUndoTest, AnUnchangedExtraStatePushesNothing) {
    juce::AudioProcessorGraph graph;
    const auto nodeId = graph.addNode(std::make_unique<HostedPluginModule>())->nodeID;

    AppUndoManager undo;
    const juce::var patch = HostedPluginModule::makeCardLayoutPatch(layoutOf({slot("a")}).toVar());
    undo.recordNodeExtraStateChange(graph, nodeId, patch, patch);

    EXPECT_FALSE(undo.canUndo());
}
