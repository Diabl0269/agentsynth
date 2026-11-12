// HostedPluginCardModTests.cpp
//
// Mod cables onto a plugin card, through GraphEditor's own cable-drag path: a cable released on a plugin knob claims
// a modulation slot for its parameter and lands on it; undo frees the slot; a cable released on the card's body opens
// a search over every parameter, and the pick adds the knob and lands the cable (docs/control/plugin-card-layout.md).

#include "../../../StubPluginInstance.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../ModMatrixPickerFirstRow.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"
#include "UI/Graph/GraphEditor/CableEndName.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Graph/ModMatrixLists.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
#include "UI/Graph/PluginKnobPicker/PluginTouchToAdd.h"
#include "UI/Graph/PortPanel/PortConnectionList.h"
#include <chrono>
#include <set>

using namespace cardbody_test;
using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
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

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Mod Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE02;
    description.deprecatedUid = 0xC0DE02;
    description.fileOrIdentifier = "/nonexistent/test/path/ModPlugin.vst3";
    return description;
}

// Ten parameters: the automatic card layout shows the first eight, so "p8" and "p9" start off the card.
std::vector<StubParamSpec> tenParams() {
    std::vector<StubParamSpec> specs;
    for (int i = 0; i < 10; ++i)
        specs.push_back({"p" + juce::String(i), "Param " + juce::String(i), 0.25f});
    return specs;
}

struct PluginCanvas : CardCanvas {
    StubBackend backend;
    NodeID lfoId, pluginId;
    HostedPluginModule* hosted = nullptr;

    PluginCanvas() {
        backend.setFactory(
            [] { return std::make_unique<StubPluginInstance>(2, 2, "Mod Plugin", 0xC0DE02, "VST3", tenParams()); });
        lfoId = add(std::make_unique<LFOModule>(), 0, 0);
        auto owned = std::make_unique<HostedPluginModule>();
        hosted = owned.get();
        pluginId = add(std::move(owned), 600, 0);
        hosted->loadPlugin(stubDescription(), backend);
        EXPECT_TRUE(pumpUntil([this] { return hosted->hasInstance(); }));
        editor.updateComponents();
        card(lfoId)->setTopLeftPosition(0, 0);
        card(pluginId)->setTopLeftPosition(600, 0);
    }

    juce::Point<int> knobPoint(const juce::String& paramId) {
        auto* plugin = card(pluginId);
        for (auto* child : plugin->getChildren())
            if (child->getComponentID() == "hostedKnob:" + paramId)
                return plugin->getPosition() + child->getBounds().getCentre();
        ADD_FAILURE() << "no knob for " << paramId;
        return {};
    }

    bool routedTo(int channel) {
        for (const auto& routing : engine.getModulationRoutings())
            if (routing.sourceNodeID == lfoId && routing.destNodeID == pluginId && routing.destChannelIndex == channel)
                return true;
        return false;
    }

    void dropCableAt(juce::Point<int> point) {
        editor.beginConnectionDrag(card(lfoId), 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
        editor.dragConnection(point);
        editor.endConnectionDrag(point);
    }
};

} // namespace

TEST(HostedPluginCardModTest, ACableDroppedOnAPluginKnobModulatesThatParameter) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));

    const int channel = canvas.hosted->modChannelForParam("p2");
    ASSERT_EQ(channel, HostedPluginModule::kModChannelBase);
    EXPECT_TRUE(canvas.routedTo(channel));
    const auto targets = canvas.hosted->getModulationTargets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0].name, "Param 2");
}

TEST(HostedPluginCardModTest, UndoingTheCableFreesTheParametersSlot) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    ASSERT_GE(canvas.hosted->modChannelForParam("p2"), 0);
    const int loads = canvas.backend.createCount;

    canvas.undo.undo();
    canvas.editor.updateComponents();

    EXPECT_FALSE(canvas.routedTo(HostedPluginModule::kModChannelBase));
    EXPECT_EQ(canvas.hosted->modChannelForParam("p2"), -1);
    EXPECT_EQ(canvas.backend.createCount, loads) << "undo restores the plugin in place, never reloads it";
}

TEST(HostedPluginCardModTest, ACableDroppedOnTheCardBodyOffersEveryParameterAndThePickAddsItsKnob) {
    PluginCanvas canvas;
    {
        auto* card = canvas.card(canvas.pluginId);
        const auto body =
            card->getPosition() + juce::Point<int>(card->getWidth() / 2, ModuleComponent::kHeaderHeight / 2);
        canvas.editor.beginConnectionDrag(canvas.card(canvas.lfoId), 0, false, false, {0, 0});
        canvas.editor.dragConnection(body);
        EXPECT_EQ(card->getModDropTargetChannel(), ModuleComponent::kHostedCardBodyDropChannel)
            << "the whole card is outlined while the cable is over its body";
        canvas.editor.dragConnection(canvas.knobPoint("p2"));
        EXPECT_NE(card->getModDropTargetChannel(), ModuleComponent::kHostedCardBodyDropChannel)
            << "over a knob, the knob is the target";
        canvas.editor.endConnectionDrag({-50, -50});
    }
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    auto* plugin = canvas.card(canvas.pluginId);
    canvas.dropCableAt(plugin->getPosition() +
                       juce::Point<int>(plugin->getWidth() / 2, ModuleComponent::kHeaderHeight / 2));
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = nullptr;

    ASSERT_NE(picker, nullptr) << "a release on the body (not a knob) opens the parameter search";
    const auto rows = picker->getVisibleItemTextsForTest();
    ASSERT_EQ(rows.size(), 10u);
    EXPECT_EQ(rows.back(), "Param 9") << "parameters not on the card come after the card's own";

    picker->setSearchTextForTest("param 9");
    ASSERT_EQ(picker->getVisibleItemTextsForTest().size(), 1u);
    picker->chooseVisibleItemForTest(0);

    const int channel = canvas.hosted->modChannelForParam("p9");
    ASSERT_GE(channel, 0);
    EXPECT_TRUE(canvas.routedTo(channel));
    canvas.editor.updateComponents();
    canvas.knobPoint("p9"); // the parameter is on the card now
}

TEST(HostedPluginCardModTest, TheParameterSearchOpensAtItsFirstRow) {
    PluginCanvas canvas;
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    auto* plugin = canvas.card(canvas.pluginId);
    canvas.dropCableAt(plugin->getPosition() +
                       juce::Point<int>(plugin->getWidth() / 2, ModuleComponent::kHeaderHeight / 2));
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = nullptr;
    ASSERT_NE(picker, nullptr);
    first_row_test::expectOpensAtFirstRow(*picker, "hosted mod target");
}

// Regression test for FRO1157: the knob was its own undo step, so one Cmd+Z took the cable and left the knob.
TEST(HostedPluginCardModTest, OneUndoTakesBackThePicksKnobAndCableAndOneRedoBringsThemBack) {
    PluginCanvas canvas;
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    auto* plugin = canvas.card(canvas.pluginId);
    canvas.dropCableAt(plugin->getPosition() +
                       juce::Point<int>(plugin->getWidth() / 2, ModuleComponent::kHeaderHeight / 2));
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = nullptr;
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("param 9");
    const auto knobs = [&canvas] {
        return synth::ui::HostedCardLayoutSource(*canvas.hosted, nullptr, canvas.engine.getGraph(), canvas.pluginId,
                                                 nullptr)
            .parameterState("p9");
    };
    using State = synth::ui::HostedCardLayoutSource::ParameterState;
    ASSERT_EQ(knobs(), State::Available);
    const int loads = canvas.backend.createCount;

    picker->chooseVisibleItemForTest(0);
    canvas.editor.updateComponents();
    const int channel = canvas.hosted->modChannelForParam("p9");
    ASSERT_GE(channel, 0);
    ASSERT_TRUE(canvas.routedTo(channel));
    ASSERT_EQ(knobs(), State::OnCard);

    canvas.undo.undo();
    canvas.editor.updateComponents();
    EXPECT_EQ(knobs(), State::Available) << "the knob goes with the cable";
    EXPECT_EQ(canvas.hosted->modChannelForParam("p9"), -1);
    EXPECT_FALSE(canvas.routedTo(channel));
    EXPECT_EQ(canvas.backend.createCount, loads) << "undo restores in place, it never reloads the plugin";

    canvas.undo.redo();
    canvas.editor.updateComponents();
    EXPECT_EQ(knobs(), State::OnCard);
    EXPECT_GE(canvas.hosted->modChannelForParam("p9"), 0);
    EXPECT_TRUE(canvas.routedTo(canvas.hosted->modChannelForParam("p9")));
}

namespace {
HostedPluginModule* hostedIn(CardCanvas& canvas) {
    for (auto* node : canvas.engine.getGraph().getNodes())
        if (auto* hosted = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            return hosted;
    return nullptr;
}
} // namespace

// Replace with another plugin: the cable follows its parameter onto the new plugin's own slot for it.
TEST(HostedPluginCardModTest, ReplacingThePluginKeepsTheCableOnTheSameParameter) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    ASSERT_GE(canvas.hosted->modChannelForParam("p2"), 0);

    canvas.editor.replaceModule(canvas.card(canvas.pluginId), "Hosted Plugin", [&canvas](juce::AudioProcessor& p) {
        static_cast<HostedPluginModule&>(p).loadPlugin(stubDescription(), canvas.backend);
    });
    auto* replaced = hostedIn(canvas);
    ASSERT_NE(replaced, nullptr);
    ASSERT_NE(replaced, canvas.hosted);
    ASSERT_TRUE(pumpUntil([replaced] { return replaced->hasInstance(); }));

    const int channel = replaced->modChannelForParam("p2");
    ASSERT_GE(channel, 0);
    bool routed = false;
    for (const auto& routing : canvas.engine.getModulationRoutings())
        routed |= routing.sourceNodeID == canvas.lfoId && routing.destChannelIndex == channel;
    EXPECT_TRUE(routed);
}

// Replace with a built-in module: it has no input for a plugin parameter, so the cable goes rather than landing on
// whatever raw channel 16 means there.
TEST(HostedPluginCardModTest, ReplacingThePluginWithABuiltInModuleDropsItsParameterCables) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    ASSERT_EQ(canvas.engine.getModulationRoutings().size(), 1u);

    canvas.editor.replaceModule(canvas.card(canvas.pluginId), "Filter");

    for (const auto& routing : canvas.engine.getModulationRoutings())
        EXPECT_NE(routing.sourceNodeID, canvas.lfoId) << "no cable from the LFO survives onto the Filter";
}

// Regression test for FRO1028: a plugin card that changed size (a knob added) dropped every parameter cable, because
// the resize pass kept only cables on visible jacks and a modulation slot has none.
TEST(HostedPluginCardModTest, AddingAKnobToTheCardKeepsItsOtherParameterCables) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    const int channel = canvas.hosted->modChannelForParam("p2");
    ASSERT_TRUE(canvas.routedTo(channel));

    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
    auto* plugin = canvas.card(canvas.pluginId);
    canvas.dropCableAt(plugin->getPosition() +
                       juce::Point<int>(plugin->getWidth() / 2, ModuleComponent::kHeaderHeight / 2));
    synth::ui::test_hooks::hostedModTargetPickerHookForTest() = nullptr;
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("param 9");
    picker->chooseVisibleItemForTest(0);
    canvas.editor.updateComponents();

    EXPECT_TRUE(canvas.routedTo(channel)) << "the Param 2 cable survives the card growing";
    EXPECT_TRUE(canvas.routedTo(canvas.hosted->modChannelForParam("p9")));
}

// The Mod Matrix reaches every plugin parameter, not only the card's: its destination picker lists the parameters
// without a slot under "Plugins", and a pick claims one and re-points the routing there (the keyboard path to a
// parameter that is not on the card).
TEST(HostedPluginCardModTest, TheModMatrixCanPointARoutingAtAnyPluginParameter) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    auto& matrix = canvas.editor.getModMatrix();
    matrix.updateRowsFromGraph();
    ASSERT_EQ(matrix.getNumRowsForTest(), 1);

    std::unique_ptr<juce::Component> captured;
    matrix.setPickerLauncherForTest(
        [&captured](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) { captured = std::move(p); });
    matrix.getRowDestComboForTest(0)->showPopup();
    auto* picker = dynamic_cast<synth::ui::ModMatrixPicker*>(captured.get());
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("param 9");
    ASSERT_EQ(picker->getVisibleItemTextsForTest().size(), 1u);
    picker->chooseVisibleItemForTest(0);
    canvas.editor.updateComponents();

    const int channel = canvas.hosted->modChannelForParam("p9");
    ASSERT_GE(channel, 0);
    EXPECT_TRUE(canvas.routedTo(channel));
    EXPECT_EQ(canvas.hosted->modChannelForParam("p2"), -1) << "the routing moved; the old slot is freed";
}

namespace {
// What a person finds searching a parameter in a matrix row's destination list.
struct ListedDestination {
    std::vector<juce::String> rows;
    std::vector<juce::String> details;
    bool operator==(const ListedDestination& o) const { return rows == o.rows && details == o.details; }
};

std::unique_ptr<juce::Component> openDestinationList(PluginCanvas& canvas) {
    auto& matrix = canvas.editor.getModMatrix();
    matrix.updateRowsFromGraph();
    std::unique_ptr<juce::Component> captured;
    matrix.setPickerLauncherForTest(
        [&captured](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) { captured = std::move(p); });
    if (matrix.getNumRowsForTest() > 0)
        matrix.getRowDestComboForTest(0)->showPopup();
    return captured;
}

ListedDestination listed(PluginCanvas& canvas, const juce::String& search) {
    auto captured = openDestinationList(canvas);
    auto* picker = dynamic_cast<synth::ui::ModMatrixPicker*>(captured.get());
    if (picker == nullptr)
        return {};
    picker->setSearchTextForTest(search);
    return {picker->getVisibleRowNamesForTest(), picker->getVisibleItemDetailsForTest()};
}

void pickInMatrix(PluginCanvas& canvas, const juce::String& search) {
    auto captured = openDestinationList(canvas);
    auto* picker = dynamic_cast<synth::ui::ModMatrixPicker*>(captured.get());
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest(search);
    ASSERT_EQ(picker->getVisibleItemTextsForTest().size(), 1u);
    picker->chooseVisibleItemForTest(0);
    canvas.editor.updateComponents();
}

bool hasKnob(PluginCanvas& canvas, const juce::String& paramId) {
    for (auto* child : canvas.card(canvas.pluginId)->getChildren())
        if (child->getComponentID() == "hostedKnob:" + paramId)
            return true;
    return false;
}

// A routing from the LFO with no destination yet, the row a person opens in the matrix.
void addSourcedRow(PluginCanvas& canvas) {
    canvas.engine.addEmptyModRouting();
    auto& matrix = canvas.editor.getModMatrix();
    matrix.updateRowsFromGraph();
    ASSERT_EQ(matrix.getNumRowsForTest(), 1);
    const auto sources = matrix.getSourceListForTest();
    ASSERT_FALSE(sources.empty());
    matrix.getRowSourceComboForTest(0)->setSelectedId(sources.front().first, juce::sendNotificationSync);
    canvas.editor.updateComponents();
}
} // namespace

// Regression test for FRO1158: a parameter sat under Effects once modulated and under Plugins once free.
TEST(HostedPluginCardModTest, APluginParameterStaysUnderPluginsModulatedOrNot) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    const auto bound = listed(canvas, "param 2");
    ASSERT_EQ(bound.rows.size(), 2u);
    EXPECT_EQ(bound.rows[0], "Plugins");
    ASSERT_EQ(bound.details.size(), 1u);
    EXPECT_EQ(bound.details[0], "Modulated");

    pickInMatrix(canvas, "param 7"); // the routing moves off p2
    const auto freed = listed(canvas, "param 2");
    EXPECT_EQ(freed.rows, bound.rows);
    EXPECT_EQ(freed.details, std::vector<juce::String>{""});
}

// Regression test for FRO1158: a matrix pick claimed the slot and left the cable ending on no knob, outside the undo.
TEST(HostedPluginCardModTest, AMatrixPickShowsTheKnobAndOneUndoTakesItAllBack) {
    PluginCanvas canvas;
    addSourcedRow(canvas);
    ASSERT_FALSE(hasKnob(canvas, "p9"));
    pickInMatrix(canvas, "param 9");
    const int channel = canvas.hosted->modChannelForParam("p9");
    ASSERT_GE(channel, 0);
    EXPECT_TRUE(hasKnob(canvas, "p9"));
    EXPECT_TRUE(canvas.routedTo(channel));

    ASSERT_TRUE(canvas.editor.getUndoManager()->undo());
    canvas.editor.updateComponents();
    EXPECT_EQ(canvas.hosted->modChannelForParam("p9"), -1);
    EXPECT_FALSE(hasKnob(canvas, "p9"));
    EXPECT_FALSE(canvas.routedTo(channel));

    ASSERT_TRUE(canvas.editor.getUndoManager()->redo());
    canvas.editor.updateComponents();
    EXPECT_GE(canvas.hosted->modChannelForParam("p9"), 0);
    EXPECT_TRUE(hasKnob(canvas, "p9"));
    EXPECT_TRUE(canvas.routedTo(canvas.hosted->modChannelForParam("p9")));
}

// The guard for this family: however a parameter gets bound, it ends the same.
TEST(HostedPluginCardModTest, CardDropMatrixPickAndTouchToAddAllEndTheSame) {
    struct EndState {
        bool knob = false;
        size_t slots = 0;
        bool routed = false;
        ListedDestination list;
    };
    const auto endState = [](PluginCanvas& canvas) {
        const int channel = canvas.hosted->modChannelForParam("p9");
        return EndState{hasKnob(canvas, "p9"), canvas.hosted->getModulationTargets().size(),
                        channel >= 0 && canvas.routedTo(channel), listed(canvas, "param 9")};
    };

    PluginCanvas viaDrop;
    ASSERT_TRUE(synth::ui::applyHostedModTarget(viaDrop.editor, viaDrop.pluginId, viaDrop.lfoId, 0, "p9"));
    viaDrop.editor.updateComponents();

    PluginCanvas viaMatrix;
    addSourcedRow(viaMatrix);
    pickInMatrix(viaMatrix, "param 9");

    PluginCanvas viaTouch;
    synth::ui::PluginTouchToAdd touch(*viaTouch.hosted, viaTouch.editor.getPluginCardLayoutStore(),
                                      viaTouch.engine.getGraph(), viaTouch.pluginId, viaTouch.editor.getUndoManager());
    touch.start();
    touch.simulateTouchGestureForTest(9);
    viaTouch.editor.updateComponents();
    ASSERT_TRUE(pumpUntil([&] { return hasKnob(viaTouch, "p9"); }));
    viaTouch.dropCableAt(viaTouch.knobPoint("p9"));

    const auto drop = endState(viaDrop);
    EXPECT_TRUE(drop.knob);
    EXPECT_EQ(drop.slots, 1u);
    EXPECT_TRUE(drop.routed);
    ASSERT_EQ(drop.list.rows.size(), 2u);
    EXPECT_EQ(drop.list.rows[0], "Plugins");
    for (auto* other : {&viaMatrix, &viaTouch}) {
        const auto state = endState(*other);
        EXPECT_EQ(state.knob, drop.knob);
        EXPECT_EQ(state.slots, drop.slots);
        EXPECT_EQ(state.routed, drop.routed);
        EXPECT_TRUE(state.list == drop.list);
    }
}

// Regression test for FRO1158: a cable on a plugin parameter did not say which control it drives.
TEST(HostedPluginCardModTest, ACableOnAPluginParameterIsNamedWithoutThePlugin) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    auto& graph = canvas.engine.getGraph();
    EXPECT_EQ(synth::ui::cableEndName(graph, canvas.pluginId, canvas.hosted->modChannelForParam("p2")), "Param 2");
    EXPECT_TRUE(synth::ui::cableEndName(graph, canvas.lfoId, 1).isEmpty());
}

// A modulated plugin knob's mod dot sits exactly where a built-in knob's does: the same offset from its knob, from
// the same code (syncModDotButtons and the shared knob anchor).
TEST(HostedPluginCardModTest, APluginKnobsModDotSitsWhereABuiltInKnobsDoes) {
    PluginCanvas canvas;
    const auto filterId = canvas.add(std::make_unique<FilterModule>(), 0, 500);
    canvas.editor.updateComponents();
    auto* filterCard = canvas.card(filterId);
    filterCard->setTopLeftPosition(0, 500);
    auto* cutoff = filterCard->getCardBody()->findWidget("cutoff");
    ASSERT_NE(cutoff, nullptr);

    canvas.dropCableAt(canvas.knobPoint("p2"));
    canvas.dropCableAt(filterCard->getPosition() + cutoff->getBounds().getCentre());
    canvas.editor.updateComponents();
    canvas.editor.timerCallback(); // refreshes the routing cache the dots read
    canvas.card(canvas.pluginId)->syncModDotButtons();
    filterCard->syncModDotButtons();

    auto* pluginDot = canvas.card(canvas.pluginId)->getModDotButton(canvas.hosted->modChannelForParam("p2"));
    auto* filterDot = filterCard->getModDotButton(1); // Filter Cutoff's CV channel
    ASSERT_NE(pluginDot, nullptr) << "the modulated plugin knob gets a mod dot";
    ASSERT_NE(filterDot, nullptr);
    ASSERT_NE(pluginDot->getKnob(), nullptr);
    // A plugin card draws smaller knobs than a built-in card, so the dot is compared against its own dial: the same
    // angle (where the ring starts) and the same distance outside the dial's edge.
    struct DotPlace {
        float angle, gap;
    };
    const auto placeOf = [](const synth::ui::ModDotButton& dot) {
        auto& knob = *dynamic_cast<juce::Slider*>(dot.getKnob());
        const auto dial =
            knob.getLookAndFeel().getSliderLayout(knob).sliderBounds.toFloat() + knob.getPosition().toFloat();
        const auto delta = dot.getBounds().toFloat().getCentre() - dial.getCentre();
        return DotPlace{std::atan2(delta.y, delta.x), delta.getDistanceFromOrigin() - dial.getHeight() / 2.0f};
    };
    const auto plugin = placeOf(*pluginDot);
    const auto builtIn = placeOf(*filterDot);
    EXPECT_NEAR(plugin.angle, builtIn.angle, 0.05f);
    EXPECT_NEAR(plugin.gap, builtIn.gap, 1.5f);
}

namespace {
// The Mod Matrix's closed-box text for the routing a row stands for: its destination's "<card title> - <name>".
juce::String matrixLabel(PluginCanvas& canvas, NodeID node, int rawChannel) {
    synth::ui::ModMatrixLists lists(canvas.engine.getGraph());
    return lists.destinationLabel((int)((node.uid << 8) | (juce::uint32)rawChannel));
}
} // namespace

// Regression test for FRO1428: the connections panel named a plugin parameter "In 18" where the Mod Matrix says
// "Serum 2 - Filter 1 Freq".
TEST(HostedPluginCardModTest, ThePortPanelNamesAPluginParameterTheWayTheModMatrixDoes) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    const int channel = canvas.hosted->modChannelForParam("p2");
    ASSERT_GE(channel, 0);
    canvas.editor.updateComponents();

    const auto rows = synth::ui::listPortConnections(canvas.editor, synth::ui::PortRef{canvas.lfoId, 0, false, false});
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].label, matrixLabel(canvas, canvas.pluginId, channel));
    EXPECT_TRUE(rows[0].label.endsWith("Param 2")) << rows[0].label;
    EXPECT_FALSE(rows[0].label.contains(" In ")) << rows[0].label;
}

// Kind jack-raw-mismatch: a row names a jack by what the card shows, the cable carries a raw channel. Every cable of a
// small patch (a knob target, a plugin slot, a plain jack) must read the same in the panel as in the Mod Matrix
// wherever the Mod Matrix lists that routing.
TEST(HostedPluginCardModTest, EveryPortPanelRowNamesItsDestinationLikeTheModMatrixWhereItIsListed) {
    PluginCanvas canvas;
    canvas.dropCableAt(canvas.knobPoint("p2"));
    const auto filterId = canvas.add(std::make_unique<FilterModule>(), 300, 400);
    int cutoff = -1;
    for (const auto& target : dynamic_cast<ModuleBase*>(canvas.processor(filterId))->getModulationTargets())
        if (target.paramId == "cutoff")
            cutoff = target.channelIndex;
    ASSERT_GE(cutoff, 0);
    canvas.engine.addModRouting(canvas.lfoId, 0, filterId, cutoff);
    canvas.engine.getGraph().addConnection({{filterId, 0}, {canvas.pluginId, 0}}); // a plain jack
    canvas.editor.updateComponents();

    std::set<std::pair<juce::uint32, int>> seen;
    int compared = 0;
    int plain = 0;
    for (const auto& cable : canvas.editor.buildVisibleCables()) {
        auto* source = dynamic_cast<ModuleBase*>(canvas.processor(juce::AudioProcessorGraph::NodeID{cable.id.srcUid}));
        ASSERT_NE(source, nullptr);
        const synth::ui::PortRef port{juce::AudioProcessorGraph::NodeID{cable.id.srcUid},
                                      source->mapOutputChannel(cable.id.srcPort).visibleJackIndex, false, false};
        if (!seen.insert({port.node.uid, port.jack}).second)
            continue; // one pass per source jack
        for (const auto& row : synth::ui::listPortConnections(canvas.editor, port)) {
            if (row.cable.kind == GraphEditor::VisibleCable::Kind::Direct) {
                ++plain;
                EXPECT_TRUE(row.label.isNotEmpty());
                continue;
            }
            const auto listed =
                matrixLabel(canvas, juce::AudioProcessorGraph::NodeID{row.cable.destNodeId}, row.farRawChannel);
            ASSERT_TRUE(listed.isNotEmpty()) << row.label;
            EXPECT_EQ(row.label.replace(juce::String::fromUTF8(" \xC2\xB7 "), " - "), listed);
            ++compared;
        }
    }
    EXPECT_EQ(compared, 2) << "the plugin slot and the Filter's Cutoff knob";
    EXPECT_GE(plain, 1);
}

// ---- A project saved before a cable always added its knob (bug kind: real-project)
// ---------------------------

namespace {

// A project JSON whose LFO is cabled to a plugin parameter that is not on the card: made by cabling "p2" (on the card)
// and then pointing the saved mod slot at "p9" (off it), which is the shape an older build wrote.
juce::var legacyProjectJson(const juce::String& paramId) {
    PluginCanvas source;
    source.dropCableAt(source.knobPoint("p2"));
    juce::var json = synth::AIStateMapper::graphToJSON(source.engine.getGraph());
    for (auto& node : *json.getProperty("nodes", {}).getArray())
        if (node.getProperty("type", {}).toString() == "Hosted Plugin")
            (*node.getProperty("state", {}).getProperty("modSlots", {}).getArray())[0].getDynamicObject()->setProperty(
                "paramId", paramId);
    return json;
}

struct LoadedLegacyCanvas : CardCanvas {
    StubBackend backend;
    HostedPluginModule* hosted = nullptr;
    NodeID pluginId;

    bool hasKnob(const juce::String& paramId) {
        auto* plugin = card(pluginId);
        if (plugin == nullptr)
            return false;
        for (auto* child : plugin->getChildren())
            if (child->getComponentID() == "hostedKnob:" + paramId)
                return true;
        return false;
    }

    LoadedLegacyCanvas(const juce::var& json, bool instanceBeforeCards) {
        backend.setFactory(
            [] { return std::make_unique<StubPluginInstance>(2, 2, "Mod Plugin", 0xC0DE02, "VST3", tenParams()); });
        synth::HostedPluginBackend::ScopedDefault installed(&backend);
        EXPECT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, engine.getGraph(), /*clearExisting=*/true,
                                                           /*trusted=*/true));
        for (auto* node : engine.getGraph().getNodes())
            if (auto* plugin = dynamic_cast<HostedPluginModule*>(node->getProcessor())) {
                hosted = plugin;
                pluginId = node->nodeID;
            }
        EXPECT_NE(hosted, nullptr);
        if (instanceBeforeCards)
            EXPECT_TRUE(pumpUntil([this] { return hosted->hasInstance(); }));
        editor.updateComponents();
        EXPECT_TRUE(pumpUntil([this] { return hosted->hasInstance(); }));
        pumpUntil([] { return false; }, 50);
    }

    bool cableEndsOnAKnob() {
        for (const auto& cable : editor.buildVisibleCables())
            if (cable.destNodeId == pluginId.uid && cable.landsOnKnob)
                return true;
        return false;
    }
};

} // namespace

TEST(HostedPluginCardModTest, ASavedRoutingOnAParameterWithNoKnobShowsItsKnobWhenTheProjectOpens) {
    const auto json = legacyProjectJson("p9");
    for (const bool instanceFirst : {false, true}) {
        LoadedLegacyCanvas loaded(json, instanceFirst);
        ASSERT_NE(loaded.card(loaded.pluginId), nullptr);
        EXPECT_TRUE(loaded.hasKnob("p9")) << "instanceFirst=" << instanceFirst;
        EXPECT_TRUE(loaded.cableEndsOnAKnob()) << "the cable's end is on the new knob";
        EXPECT_FALSE(loaded.undo.canUndo()) << "load-time reconciliation is not a user edit";
        EXPECT_EQ(loaded.hosted->modChannelForParam("p9"), HostedPluginModule::kModChannelBase);
    }
}

TEST(HostedPluginCardModTest, ASavedRoutingOnAParameterAlreadyOnTheCardAddsNothing) {
    LoadedLegacyCanvas loaded(legacyProjectJson("p2"), false);
    EXPECT_TRUE(loaded.hasKnob("p2"));
    EXPECT_FALSE(loaded.hasKnob("p9"));
    EXPECT_TRUE(loaded.hosted->getCardLayoutOverride().isVoid()) << "no layout was written";
    EXPECT_FALSE(loaded.undo.canUndo());
}

TEST(HostedPluginCardModTest, TheReconciledKnobIsThisInstancesAndAKnobHiddenLaterStaysHidden) {
    LoadedLegacyCanvas loaded(legacyProjectJson("p9"), false);
    ASSERT_TRUE(loaded.hasKnob("p9"));
    EXPECT_FALSE(loaded.hosted->getCardLayoutOverride().isVoid()) << "written to this card's own override";

    // The user hides it again: a rebuild must not bring it back (the check is once per publish).
    synth::ui::HostedCardLayoutSource source(*loaded.hosted, loaded.editor.getPluginCardLayoutStore(),
                                             loaded.engine.getGraph(), loaded.pluginId, nullptr);
    synth::CardLayout layout;
    layout.version = synth::CardLayout::kCurrentVersion;
    layout.slots = source.currentLayout().flatSlots();
    std::erase_if(layout.slots, [](const synth::CardSlot& slot) { return slot.paramId == "p9"; });
    source.apply(layout, /*allOfType*/ false);
    loaded.editor.updateComponents();
    EXPECT_FALSE(loaded.hasKnob("p9"));
}
