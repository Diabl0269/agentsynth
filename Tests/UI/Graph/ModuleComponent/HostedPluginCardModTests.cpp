// HostedPluginCardModTests.cpp
//
// Mod cables onto a plugin card, through GraphEditor's own cable-drag path: a cable released on a plugin knob claims
// a modulation slot for its parameter and lands on it; undo frees the slot; a cable released on the card's body opens
// a search over every parameter, and the pick adds the knob and lands the cable (docs/control/plugin-card-layout.md).

#include "../../../StubPluginInstance.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
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
