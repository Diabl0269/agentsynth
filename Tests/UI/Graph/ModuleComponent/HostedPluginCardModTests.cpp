// HostedPluginCardModTests.cpp
//
// Mod cables onto a plugin card, through GraphEditor's own cable-drag path: a cable released on a plugin knob claims
// a modulation slot for its parameter and lands on it; undo frees the slot; a cable released on the card's body opens
// a search over every parameter, and the pick adds the knob and lands the cable (docs/control/plugin-card-layout.md).

#include "../../../StubPluginInstance.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"
#include "UI/Graph/GraphEditor/CableEndName.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
#include "UI/Graph/PluginKnobPicker/PluginTouchToAdd.h"
#include <chrono>

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
