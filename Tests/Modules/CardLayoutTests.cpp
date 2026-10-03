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
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":3,"slots":[]})")).status,
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

    loaded.module.setCardLayoutOverride(juce::JSON::parse(R"({"version":3,"slots":[]})"));

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

// ---------------------------------------------------------------------------------------------
// Version 2: sections, items, conditions, hidden, and the "write v1 unless a v2 feature is used" rule.
// ---------------------------------------------------------------------------------------------

namespace {

using synth::CardCondition;
using synth::CardConditionEffect;
using synth::CardParamItem;
using synth::CardPresentation;
using synth::CardSection;
using synth::CardView;
using synth::CardViewItem;
using synth::CardWidget;

CardParamItem paramItem(const juce::String& id, CardWidget widget = CardWidget::Auto) {
    CardParamItem item;
    item.paramId = id;
    item.widget = widget;
    return item;
}

CardSection plainSection(std::initializer_list<const char*> ids) {
    CardSection section;
    section.id = "main";
    for (const char* id : ids)
        section.items.emplace_back(paramItem(id));
    return section;
}

CardLayout richLayout() {
    CardCondition dim;
    dim.param = "voices";
    dim.is = juce::StringArray{"2", "3"};
    dim.effect = CardConditionEffect::Dim;

    CardCondition show;
    show.param = "mode";
    show.is = juce::StringArray{"Granular", "Sample"};
    show.effect = CardConditionEffect::Show;

    CardParamItem detune = paramItem("detune", CardWidget::KnobLarge);
    detune.label = "Spread";
    detune.span = 2;
    detune.indexHint = 4;
    detune.when = dim;
    detune.at = juce::Point<int>(100, 40);
    detune.range = juce::Range<double>(-12.5, 12.5);
    CardParamItem member = paramItem("cutoff", CardWidget::FaderV);
    member.node = "uuid-1";

    CardSection pitch;
    pitch.id = "pitch";
    pitch.title = "Pitch";
    pitch.columns = 4;
    pitch.items = {detune, member, CardViewItem{CardView::Response, false}};

    CardSection grain;
    grain.id = "grain";
    grain.presentation = CardPresentation::Tab;
    grain.visibleWhen = show;
    grain.items = {paramItem("size", CardWidget::Segmented)};

    CardLayout layout;
    layout.basedOn = "Oscillator@3";
    layout.sections = {pitch, grain};
    layout.hidden = juce::StringArray{"pan", "level"};
    return layout;
}

juce::var reparsed(const CardLayout& layout) { return juce::JSON::parse(juce::JSON::toString(layout.toVar())); }

int writtenVersion(const CardLayout& layout) {
    return static_cast<int>(layout.toVar().getDynamicObject()->getProperty("version"));
}

} // namespace

TEST(CardLayoutV2Test, FullLayoutRoundTripsThroughJson) {
    const auto layout = richLayout();
    EXPECT_EQ(writtenVersion(layout), 2);

    const auto result = CardLayout::fromVar(reparsed(layout));
    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);
    EXPECT_EQ(result.layout, layout);
    EXPECT_TRUE(result.layout.slots.empty()) << "a v2 document fills sections, not slots";
}

TEST(CardLayoutV2Test, ConditionsKeepChoiceValueStringsNotIndices) {
    const auto result = CardLayout::fromVar(reparsed(richLayout()));
    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);

    const auto& grain = result.layout.sections[1];
    ASSERT_TRUE(grain.visibleWhen.has_value());
    EXPECT_EQ(grain.visibleWhen->is, juce::StringArray({"Granular", "Sample"}));
    EXPECT_EQ(grain.visibleWhen->effect, CardConditionEffect::Show);

    const auto* detune = std::get_if<CardParamItem>(&result.layout.sections[0].items[0]);
    ASSERT_NE(detune, nullptr);
    ASSERT_TRUE(detune->when.has_value());
    EXPECT_EQ(detune->when->is, juce::StringArray({"2", "3"}));
}

TEST(CardLayoutV2Test, V1ReadsAsSlotsAndUpgradeFillsHiddenWithTheAbsentIds) {
    const auto result = CardLayout::fromVar(
        juce::JSON::parse(R"({"version":1,"slots":[{"paramId":"cutoff","indexHint":0,"label":null,"kind":"knob"},)"
                          R"({"paramId":"mode","indexHint":2,"label":"Type","kind":"choice"}]})"));
    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(result.layout.slots.size(), 2u);
    EXPECT_TRUE(result.layout.sections.empty());

    const auto upgraded = synth::upgradeV1(result.layout, juce::StringArray{"cutoff", "res", "mode", "drive"});

    EXPECT_TRUE(upgraded.slots.empty());
    ASSERT_EQ(upgraded.sections.size(), 1u);
    EXPECT_FALSE(upgraded.sections[0].title.has_value());
    EXPECT_EQ(upgraded.sections[0].presentation, CardPresentation::Grid);
    ASSERT_EQ(upgraded.sections[0].items.size(), 2u);
    const auto* mode = std::get_if<CardParamItem>(&upgraded.sections[0].items[1]);
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->paramId, "mode");
    EXPECT_EQ(mode->widget, CardWidget::Choice);
    EXPECT_EQ(mode->label, std::optional<juce::String>("Type"));
    EXPECT_EQ(mode->indexHint, 2);
    EXPECT_EQ(upgraded.hidden, juce::StringArray({"res", "drive"})) << "absent from the layout means hidden";

    EXPECT_EQ(synth::upgradeV1(upgraded, juce::StringArray{"other"}), upgraded) << "applied once: v2 is left alone";
}

TEST(CardLayoutV2Test, UnknownKeysAreIgnoredAtEveryLevel) {
    const auto result = CardLayout::fromVar(juce::JSON::parse(R"({
        "version":2,"future":1,"hidden":["a"],
        "sections":[{"id":"s","extra":true,"items":[
            {"paramId":"p","widget":"knob","span":2,"surprise":"x"},
            {"view":"scope","open":true,"more":1}]}]})"));

    ASSERT_EQ(result.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(result.layout.sections.size(), 1u);
    EXPECT_EQ(result.layout.sections[0].items.size(), 2u);
    EXPECT_EQ(result.layout.hidden, juce::StringArray({"a"}));
}

TEST(CardLayoutV2Test, ANewerVersionIsRefusedAndBadNumbersOrNamesAreMalformed) {
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":3,"sections":[]})")).status,
              CardLayout::ParseStatus::UnsupportedVersion);

    const auto section = [](const juce::String& body) {
        return CardLayout::fromVar(juce::JSON::parse(R"({"version":2,"sections":[{"id":"s",)" + body + "}]}")).status;
    };
    EXPECT_EQ(section(R"("items":[])"), CardLayout::ParseStatus::Ok);
    EXPECT_EQ(section(R"("columns":7,"items":[])"), CardLayout::ParseStatus::Malformed) << "out of range is refused";
    EXPECT_EQ(section(R"("columns":0,"items":[])"), CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(section(R"("items":[{"paramId":"a","span":0}])"), CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(section(R"("items":[{"paramId":"a","widget":"dial"}])"), CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(section(R"("items":[{"view":"hologram"}])"), CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(section(R"("items":[{"paramId":"a","when":{"param":"m","is":["x"],"effect":"explode"}}])"),
              CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(section(R"("presentation":"carousel","items":[])"), CardLayout::ParseStatus::Malformed);
    EXPECT_EQ(CardLayout::fromVar(juce::JSON::parse(R"({"version":2})")).status, CardLayout::ParseStatus::Malformed)
        << "a v2 document needs its sections";
}

TEST(CardLayoutV2Test, ALayoutWithNoV2FeatureStillWritesVersionOne) {
    CardLayout flat;
    flat.slots = {slot("a", 0), slot("b", 1, juce::String("B"), CardSlotKind::Toggle)};
    EXPECT_FALSE(flat.usesV2Features());
    EXPECT_EQ(writtenVersion(flat), 1);

    CardLayout sectioned;
    sectioned.sections = {plainSection({"a", "b"})};
    EXPECT_FALSE(sectioned.usesV2Features());
    EXPECT_EQ(writtenVersion(sectioned), 1) << "one untitled grid section of plain params is expressible in v1";
    const auto back = CardLayout::fromVar(reparsed(sectioned));
    ASSERT_EQ(back.status, CardLayout::ParseStatus::Ok);
    ASSERT_EQ(back.layout.slots.size(), 2u);
    EXPECT_EQ(back.layout.slots[1].paramId, "b");

    CardLayout upgradedWithNothingHidden = synth::upgradeV1(flat, juce::StringArray{"a", "b"});
    EXPECT_EQ(writtenVersion(upgradedWithNothingHidden), 1) << "an upgrade that hides nothing writes v1 again";
}

TEST(CardLayoutV2Test, UsesV2FeaturesNamesEachFeature) {
    const auto with = [](auto mutate) {
        CardLayout layout;
        layout.sections = {plainSection({"a"})};
        mutate(layout);
        return layout;
    };
    const auto firstItem = [](CardLayout& layout) -> CardParamItem& {
        return std::get<CardParamItem>(layout.sections[0].items[0]);
    };

    EXPECT_FALSE(CardLayout{}.usesV2Features());
    EXPECT_FALSE(with([](CardLayout&) {}).usesV2Features());
    EXPECT_FALSE(with([&](CardLayout& l) { firstItem(l).widget = CardWidget::Choice; }).usesV2Features());

    EXPECT_TRUE(with([](CardLayout& l) { l.hidden.add("x"); }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.basedOn = "Filter@1"; }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.sections.push_back(plainSection({"b"})); }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.sections[0].title = "T"; }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.sections[0].columns = 4; }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.sections[0].presentation = CardPresentation::Tab; }).usesV2Features());
    EXPECT_TRUE(
        with([](CardLayout& l) { l.sections[0].visibleWhen = CardCondition{"m", {"x"}, {}}; }).usesV2Features());
    EXPECT_TRUE(with([](CardLayout& l) { l.sections[0].items.emplace_back(CardViewItem{}); }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).span = 2; }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).node = "u"; }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).when = CardCondition{"m", {"x"}, {}}; }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).widget = CardWidget::FaderH; }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).widget = CardWidget::KnobLarge; }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).at = juce::Point<int>(0, 0); }).usesV2Features());
    EXPECT_TRUE(with([&](CardLayout& l) { firstItem(l).range = juce::Range<double>(0.0, 1.0); }).usesV2Features());
}

TEST(CardLayoutV2Test, PositionAndRangeAreWrittenOnlyWhenSetAndReadBothOrNeither) {
    const auto item = [](const juce::String& body) {
        return CardLayout::fromVar(
            juce::JSON::parse(R"({"version":2,"sections":[{"id":"s","items":[{"paramId":"a",)" + body + "}]}]}"));
    };
    const auto ok = item(R"("x":10,"y":20,"min":-1.5,"max":3)");
    ASSERT_EQ(ok.status, CardLayout::ParseStatus::Ok);
    const auto& read = std::get<CardParamItem>(ok.layout.sections[0].items[0]);
    EXPECT_EQ(read.at, juce::Point<int>(10, 20));
    EXPECT_EQ(read.range, juce::Range<double>(-1.5, 3.0));

    const auto plain = item(R"("span":1)");
    ASSERT_EQ(plain.status, CardLayout::ParseStatus::Ok);
    const auto text = juce::JSON::toString(plain.layout.toVar());
    EXPECT_FALSE(text.contains("\"x\"") || text.contains("\"min\""));

    for (const char* bad :
         {R"("x":10)", R"("y":10)", R"("x":4001,"y":0)", R"("x":-1,"y":0)", R"("x":1.5,"y":0)", R"("x":"a","y":0)",
          R"("min":1)", R"("max":1)", R"("min":2,"max":2)", R"("min":3,"max":2)", R"("min":"a","max":2)"})
        EXPECT_EQ(item(bad).status, CardLayout::ParseStatus::Malformed) << bad;
}

TEST(CardLayoutTest, EveryUntitledSectionHasADisplayNameFromItsId) {
    const auto named = [](const char* id, std::optional<juce::String> title = std::nullopt) {
        synth::CardSection section;
        section.id = id;
        section.title = std::move(title);
        return synth::cardSectionDisplayName(section);
    };
    EXPECT_EQ(named("tone", juce::String("Tone colour")), "Tone colour") << "a title wins";
    EXPECT_EQ(named("tone", juce::String("  ")), "Tone") << "a blank title counts as none";
    EXPECT_EQ(named("footer"), "Footer");
    EXPECT_EQ(named("main"), "Controls");
    EXPECT_EQ(named(""), "Controls");
    EXPECT_EQ(named("group-3"), "Group 3");
    EXPECT_EQ(named("section-2"), "Group 2");
    EXPECT_EQ(named("trigger-meter"), "Trigger meter");
    EXPECT_EQ(named("envelope"), "Envelope");
}
