// ScaleHangTests.cpp
//
// The main-thread-stall guard: the work an action does on the message thread, counted at N and at 4N, for the lists and
// ticks whose cost grows with the project (a plugin's parameter count, the number of routings). Counters, never clocks,
// so it holds on a loaded CI machine. The disabled bench at the bottom prints the real times; run it by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='ScaleHang.DISABLED_*'

#include "../StubPluginInstance.h"
#include "../UI/Graph/CardBody/CardCanvas.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "StallProbe.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
#include <chrono>
#include <cstdio>
#include <gtest/gtest.h>

using namespace cardbody_test;
using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    return predicate();
}

juce::PluginDescription manyParamDescription() {
    juce::PluginDescription description;
    description.name = "Big Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xB16;
    description.deprecatedUid = 0xB16;
    description.fileOrIdentifier = "/nonexistent/test/path/BigPlugin.vst3";
    return description;
}

std::vector<StubParamSpec> manyParams(int count) {
    std::vector<StubParamSpec> specs;
    specs.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        specs.push_back({"p" + juce::String(i), "Param " + juce::String(i), 0.25f});
    return specs;
}

// An LFO and a plugin with `params` parameters on a real canvas.
struct ManyParamCanvas : CardCanvas {
    StubBackend backend;
    NodeID lfoId, pluginId;
    HostedPluginModule* hosted = nullptr;

    explicit ManyParamCanvas(int params) {
        backend.setFactory([params] {
            return std::make_unique<StubPluginInstance>(2, 2, "Big Plugin", 0xB16, "VST3", manyParams(params));
        });
        lfoId = add(std::make_unique<LFOModule>(), 0, 0);
        auto owned = std::make_unique<HostedPluginModule>();
        hosted = owned.get();
        pluginId = add(std::move(owned), 600, 0);
        hosted->loadPlugin(manyParamDescription(), backend);
        EXPECT_TRUE(pumpUntil([this] { return hosted->hasInstance(); }));
        editor.updateComponents();
    }

    juce::Point<int> cardBodyPoint() {
        auto* plugin = card(pluginId);
        return plugin->getPosition() + juce::Point<int>(plugin->getWidth() / 2, ModuleComponent::kHeaderHeight / 2);
    }

    // A cable released on the card's body: the picker it opens, instead of a call-out (none can open headless).
    std::unique_ptr<synth::ui::ModMatrixPicker> dropCableOnBody() {
        std::unique_ptr<synth::ui::ModMatrixPicker> picker;
        synth::ui::test_hooks::hostedModTargetPickerHookForTest() = [&picker](auto p) { picker = std::move(p); };
        editor.beginConnectionDrag(card(lfoId), 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
        editor.dragConnection(cardBodyPoint());
        editor.endConnectionDrag(cardBodyPoint());
        synth::ui::test_hooks::hostedModTargetPickerHookForTest() = nullptr;
        return picker;
    }
};

// `routings` routings from one LFO, each onto its own Filter's Cutoff, on a real canvas.
struct ManyRoutingCanvas : CardCanvas {
    NodeID lfoId;

    explicit ManyRoutingCanvas(int routings) {
        lfoId = add(std::make_unique<LFOModule>(), 0, 0);
        for (int i = 0; i < routings; ++i) {
            const auto filter = add(std::make_unique<FilterModule>(), 400 + 300 * (i % 8), 300 * (i / 8));
            engine.addModRouting(lfoId, 0, filter, 1);
        }
        editor.updateComponents();
    }
};

} // namespace

namespace {

int nameReads(HostedPluginModule& hosted) {
    int total = 0;
    for (auto* param : hosted.getActiveInstanceForEditor()->getParameters())
        if (auto* stub = dynamic_cast<synth::test::StubHostedParameter*>(param))
            total += stub->nameReads;
    return total;
}

struct PickerOpenWork {
    int modelBuilds = 0;
    int nameReads = 0;
};

// What one more open of the list costs once the plugin's list has been read.
PickerOpenWork workToOpenThePicker(int params) {
    ManyParamCanvas c(params);
    (void)c.hosted->getInstanceParameters();
    auto& counters = graph_editor_paint::workCounters();
    counters = {};
    const int readsBefore = nameReads(*c.hosted);
    auto picker = c.dropCableOnBody();
    EXPECT_NE(picker, nullptr);
    if (picker != nullptr)
        EXPECT_EQ((int)picker->getVisibleItemTextsForTest().size(), params);
    return {counters.cardLayoutModelBuilds, nameReads(*c.hosted) - readsBefore};
}

} // namespace

// Regression test for FRO1157: a cable dropped on a big plugin's card froze the app, because the list read the whole
// card layout again for every parameter.
TEST(ScaleHang, OpeningTheParameterListReadsTheCardLayoutOnceHoweverBigThePlugin) {
    const auto at750 = workToOpenThePicker(750);
    const auto at3000 = workToOpenThePicker(3000);
    EXPECT_EQ(at750.modelBuilds, 1);
    EXPECT_EQ(at3000.modelBuilds, 1);
}

// The card's own knobs read their labels, a handful however many parameters the plugin lists.
TEST(ScaleHang, OpeningTheParameterListAsksThePluginForNoNameItAlreadyKnows) {
    const int at750 = workToOpenThePicker(750).nameReads;
    EXPECT_EQ(workToOpenThePicker(3000).nameReads, at750);
    EXPECT_LE(at750, 16);
}

TEST(ScaleHang, ThePluginsParameterNamesAreReadOnceAndAgainOnlyWhenItSaysTheyChanged) {
    ManyParamCanvas c(50);
    (void)c.hosted->getInstanceParameters();
    const int settled = nameReads(*c.hosted);
    (void)c.hosted->getInstanceParameters();
    EXPECT_EQ(nameReads(*c.hosted), settled);

    auto* instance = c.hosted->getActiveInstanceForEditor();
    dynamic_cast<synth::test::StubHostedParameter*>(instance->getParameters()[3])->setNameForTest("Renamed");
    EXPECT_EQ(c.hosted->getInstanceParameters()[3].displayName, "Param 3") << "no notice yet, the list is kept";
    instance->updateHostDisplay(juce::AudioProcessor::ChangeDetails().withParameterInfoChanged(true));
    EXPECT_EQ(c.hosted->getInstanceParameters()[3].displayName, "Renamed");
}

TEST(ScaleHang, APickFromTheParameterListCostsAFixedNumberOfLayoutReads) {
    const auto work = [](int params) {
        ManyParamCanvas c(params);
        auto picker = c.dropCableOnBody();
        auto& counters = graph_editor_paint::workCounters();
        counters = {};
        picker->setSearchTextForTest("param " + juce::String(params - 1));
        picker->chooseVisibleItemForTest(0);
        EXPECT_GE(c.hosted->modChannelForParam("p" + juce::String(params - 1)), 0);
        return counters.cardLayoutModelBuilds;
    };
    EXPECT_EQ(work(750), work(3000));
}

// One tick of the open matrix reads the cables once and refreshes each row once, however many routings there are.
TEST(ScaleHang, AMatrixTickIndexesTheCablesOnceAndRefreshesEachRowOnce) {
    int scans[2]{};
    const int sizes[2]{10, 40};
    for (int i = 0; i < 2; ++i) {
        ManyRoutingCanvas c(sizes[i]);
        auto& matrix = c.editor.getModMatrix();
        matrix.updateRowsFromGraph();
        auto& counters = graph_editor_paint::workCounters();
        counters = {};
        matrix.updateRowsFromGraph();
        scans[i] = counters.cableScans;
        EXPECT_EQ(counters.modRowRefreshes, sizes[i]);
    }
    EXPECT_EQ(scans[0], scans[1]);
    EXPECT_LE(scans[1], 1);
}

namespace {
std::vector<synth::ui::ModMatrixPicker::Item> manyPickerItems(int count) {
    std::vector<synth::ui::ModMatrixPicker::Item> items;
    for (int i = 0; i < count; ++i)
        items.push_back({i + 1, "Plugins", "Param " + juce::String(i + 1)});
    return items;
}
constexpr int kMostRowsInAPicker = 40; // what fits in the picker's height at the shortest row, plus the overscan
} // namespace

// A list of thousands makes only the rows in view, and a key press steers only those.
TEST(ScaleHang, APickerOfThousandsMakesOnlyTheRowsInViewAndAKeySteersOnlyThose) {
    synth::ui::ModMatrixPicker picker("destination", manyPickerItems(3000), 0, [](int) {});
    EXPECT_GT(picker.getRowComponentCountForTest(), 0);
    EXPECT_LE(picker.getRowComponentCountForTest(), kMostRowsInAPicker);
    const int steered = picker.getRowsSteeredForTest();
    picker.setSearchTextForTest("param 29");
    EXPECT_EQ(picker.getVisibleItemTextsForTest().size(), 159u); // every number holding "29"
    EXPECT_LE(picker.getRowsSteeredForTest() - steered, 2 * kMostRowsInAPicker);
    EXPECT_LE(picker.getRowComponentCountForTest(), 2 * kMostRowsInAPicker);
}

// Up, Down and Return reach rows that had no component when the list opened.
TEST(ScaleHang, TheKeyboardWalksAPickerOfThousandsPastTheRowsThatExisted) {
    int chosen = 0;
    synth::ui::ModMatrixPicker picker("destination", manyPickerItems(3000), 0, [&](int id) { chosen = id; });
    for (int i = 0; i < 500; ++i) {
        picker.sendKeyForTest(juce::KeyPress(juce::KeyPress::downKey));
        ASSERT_TRUE(picker.isHighlightInViewForTest()) << i;
    }
    EXPECT_EQ(picker.getHighlightedItemIndexForTest(), 500);
    EXPECT_GT(picker.getViewTopForTest(), 0);
    EXPECT_LE(picker.getRowComponentCountForTest(), kMostRowsInAPicker);
    for (int i = 0; i < 100; ++i)
        picker.sendKeyForTest(juce::KeyPress(juce::KeyPress::upKey));
    EXPECT_TRUE(picker.isHighlightInViewForTest());
    picker.sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_EQ(chosen, 401);
}

// Regression test for FRO874: a filter pass scrolled to the top after the highlight, losing the best match's row.
TEST(ScaleHang, FilteringAScrolledPickerKeepsTheBestMatchInView) {
    synth::ui::ModMatrixPicker picker("destination", manyPickerItems(3000), 0, [](int) {});
    for (int i = 0; i < 300; ++i)
        picker.sendKeyForTest(juce::KeyPress(juce::KeyPress::downKey));
    picker.setSearchTextForTest("param 2999");
    EXPECT_EQ(picker.getVisibleItemTextsForTest().size(), 1u);
    EXPECT_TRUE(picker.isHighlightInViewForTest());
}

TEST(ScaleHang, DISABLED_PrintsWhatEachActionCosts) {
    for (const int params : {750, 3000}) {
        ManyParamCanvas c(params);
        auto t0 = nowMs();
        const auto items = synth::ui::hostedModTargetItems(*c.hosted, c.editor, c.pluginId);
        const double build = nowMs() - t0;
        t0 = nowMs();
        auto picker = c.dropCableOnBody();
        const double drop = nowMs() - t0;
        ASSERT_NE(picker, nullptr);
        double keysWorst = 0;
        for (const auto* query : {"p", "pa", "par", "para", "param", "param ", "param 2", "param 29", ""}) {
            t0 = nowMs();
            picker->setSearchTextForTest(query);
            keysWorst = std::max(keysWorst, nowMs() - t0);
        }
        picker->setSearchTextForTest("param 29");
        t0 = nowMs();
        picker->chooseVisibleItemForTest(0);
        const double pick = nowMs() - t0;
        const double stall = bench::worstStallMs(500);
        std::printf(
            "params=%d items=%zu itemBuild=%.2fms dropToPicker=%.2fms worstKey=%.2fms pick=%.2fms stallAfter=%.2fms\n",
            params, items.size(), build, drop, keysWorst, pick, stall);
    }
    for (const int routings : {10, 40}) {
        ManyRoutingCanvas c(routings);
        auto& matrix = c.editor.getModMatrix();
        auto t0 = nowMs();
        matrix.updateRowsFromGraph();
        const double open = nowMs() - t0;
        ASSERT_EQ(matrix.getNumRowsForTest(), routings);
        t0 = nowMs();
        constexpr int kTicks = 50;
        for (int i = 0; i < kTicks; ++i)
            matrix.updateRowsFromGraph();
        const double tick = (nowMs() - t0) / kTicks;
        std::printf("routings=%d firstFill=%.2fms tick=%.3fms\n", routings, open, tick);
    }
    std::fflush(stdout);
}
