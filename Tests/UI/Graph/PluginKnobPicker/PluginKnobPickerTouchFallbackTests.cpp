// PluginKnobPickerTouchFallbackTests.cpp -- FRO241 (docs/control/plugin-card-layout.md#choosing-knobs):
// the value-change fallback for "Touch in the plugin editor to add", for a plugin that never emits a
// gesture. Sibling to PluginKnobPickerTests.cpp (kept separate to stay under the file-size cap; that
// file's own "4. Touch-to-add" group covers the gesture path and its off-thread hop, unchanged here).
//
// These tests drive `PluginKnobPickerTouchCapture` directly rather than through the full
// `PluginKnobPickerComponent` picker/rig -- the burst-filter contract this file pins belongs entirely
// to the capture class, and `forceBurstWindowCloseForTest()` (the clock seam: it invokes the same
// `timerCallback()` a real 200 ms tick would, instantly and deterministically) needs no store, graph,
// or undo manager to exercise.

#include "../../../StubPluginInstance.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerTouchCapture.h"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;
using synth::ui::PluginKnobPickerTouchCapture;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

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

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Fallback Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xFA1100;
    description.deprecatedUid = 0xFA1100;
    description.fileOrIdentifier = "/nonexistent/test/path/FallbackPlugin.vst3";
    return description;
}

StubParamSpec knobSpec(const juce::String& id, const juce::String& name) { return {id, name, 0.0f, {}, false}; }

/** A live HostedPluginModule with `count` automatable stub parameters, no graph/engine/store needed --
 *  PluginKnobPickerTouchCapture only ever touches the module's own instance. */
struct TouchFixture {
    explicit TouchFixture(int paramCount = 4) {
        std::vector<StubParamSpec> specs;
        for (int i = 0; i < paramCount; ++i)
            specs.push_back(knobSpec("p" + juce::String(i), "Param " + juce::String(i)));
        backend.setFactory(
            [specs] { return std::make_unique<StubPluginInstance>(2, 2, "Fallback Plugin", 0xFA1100, "VST3", specs); });
        module = std::make_unique<HostedPluginModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
        module->loadPlugin(stubDescription(), backend);
        EXPECT_TRUE(pumpUntil([this] { return module->hasInstance(); }));
    }

    StubBackend backend;
    std::unique_ptr<HostedPluginModule> module;
};

} // namespace

TEST(PluginKnobPickerTouchFallbackTest, AValueChangeAddsTheParameterAfterTheWindowCloses) {
    TouchFixture fixture;
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.setArmed(true);

    capture.simulateValueChangeForTest(2);
    pump();
    EXPECT_TRUE(touched.empty()) << "not reported before the burst window closes";

    capture.forceBurstWindowCloseForTest();
    ASSERT_EQ(touched.size(), 1u);
    EXPECT_EQ(touched[0], 2);

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, MoreThanThreeDistinctParamsInTheWindowAddsNone) {
    TouchFixture fixture(8);
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.setArmed(true);

    for (int index : {0, 1, 2, 3})
        capture.simulateValueChangeForTest(index);
    pump();
    capture.forceBurstWindowCloseForTest();

    EXPECT_TRUE(touched.empty()) << "4 distinct params within the window is a burst -- none are added";

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, ExactlyThreeDistinctParamsInTheWindowAreAllAdded) {
    TouchFixture fixture(8);
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.setArmed(true);

    for (int index : {0, 1, 2})
        capture.simulateValueChangeForTest(index);
    pump();
    capture.forceBurstWindowCloseForTest();

    ASSERT_EQ(touched.size(), 3u);
    EXPECT_NE(std::find(touched.begin(), touched.end(), 0), touched.end());
    EXPECT_NE(std::find(touched.begin(), touched.end(), 1), touched.end());
    EXPECT_NE(std::find(touched.begin(), touched.end(), 2), touched.end());

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, AParameterAlreadyInTheLayoutIsIgnored) {
    TouchFixture fixture;
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.isParameterAlreadyInLayout = [](int index) { return index == 1; };
    capture.setArmed(true);

    capture.simulateValueChangeForTest(1); // already in the layout -- must not even open a window
    capture.simulateValueChangeForTest(2);
    pump();
    capture.forceBurstWindowCloseForTest();

    ASSERT_EQ(touched.size(), 1u);
    EXPECT_EQ(touched[0], 2);

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, AnOffThreadValueChangeIsHoppedToTheMessageThread) {
    TouchFixture fixture;
    PluginKnobPickerTouchCapture capture(*fixture.module);
    int touchedIndex = -1;
    capture.onParameterTouched = [&](int index) { touchedIndex = index; };
    capture.setArmed(true);

    auto* param = fixture.module->getActiveInstanceForEditor()->getParameters()[0];
    std::thread worker([param] { param->setValueNotifyingHost(0.5f); });
    worker.join();

    EXPECT_EQ(touchedIndex, -1) << "must not run on the reporting thread";
    pump();
    capture.forceBurstWindowCloseForTest();
    EXPECT_EQ(touchedIndex, 0);

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, TheGesturePathStillWorksAlongsideTheValueChangeFallback) {
    TouchFixture fixture;
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.setArmed(true);

    capture.simulateGestureStartForTest(0); // reported immediately, no burst window involved
    pump();
    ASSERT_EQ(touched.size(), 1u);
    EXPECT_EQ(touched[0], 0);

    capture.simulateValueChangeForTest(3);
    pump();
    capture.forceBurstWindowCloseForTest();
    ASSERT_EQ(touched.size(), 2u);
    EXPECT_EQ(touched[1], 3);

    capture.setArmed(false);
}

TEST(PluginKnobPickerTouchFallbackTest, DisarmingCancelsAPendingBurstWindow) {
    TouchFixture fixture;
    PluginKnobPickerTouchCapture capture(*fixture.module);
    std::vector<int> touched;
    capture.onParameterTouched = [&](int index) { touched.push_back(index); };
    capture.setArmed(true);

    capture.simulateValueChangeForTest(1);
    pump();

    capture.setArmed(false);
    capture.forceBurstWindowCloseForTest(); // no-op: the window was cancelled by setArmed(false)

    EXPECT_TRUE(touched.empty());
}
