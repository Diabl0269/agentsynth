// MacroEditProfileTests.cpp
//
// A disabled profiling bench, not a test: opens a temporary copy of the Load test project
// (docs/layout/rendering.md#the-load-test-project), opens one macro and drags its busiest member out of the macro and
// back in through the real ModuleComponent mouse callbacks, printing what each step costs: the drag tick that crosses
// the border (the membership change is applied live), the drop, the message-loop work left behind and the next paint,
// then each undo. Run by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestMacroDragProfile*'
// PROFILE_PROJECT=<bundle> picks another project; PROFILE_SPIN_MACRO=<s> loops the out-and-in drag for that long, to
// attach `sample <pid> 10` to.

#include "../Macros/MacroContainer/MacroDragTestHelpers.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "../UniqueTempDir.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>

namespace {

double benchNowMs() { return juce::Time::getMillisecondCounterHiRes(); }

double benchPumpMs() {
    const double t0 = benchNowMs();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return benchNowMs() - t0 - 20.0;
}

// Runs the message loop for `ms` and returns the longest stretch the loop was blocked (a frozen UI), measured by a
// 1 ms timer: animation frames, deferred refreshes and async graph work all run inside it.
struct StallProbe : juce::Timer {
    double last = 0, worst = 0;
    void timerCallback() override {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (last > 0)
            worst = std::max(worst, now - last);
        last = now;
    }
};

double benchWorstStallMs(double ms) {
    StallProbe probe;
    probe.startTimer(1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil((int)ms);
    probe.stopTimer();
    return probe.worst;
}

double benchPaintMs(juce::Component& c) {
    const double t0 = benchNowMs();
    if (c.getWidth() > 0 && c.getHeight() > 0)
        (void)c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    return benchNowMs() - t0;
}

// A top-level macro with at least three ordinary members, so dragging one out leaves the macro standing.
juce::String pickMacro(const synth::MacroSet& macros) {
    for (const auto& m : macros.getAll()) {
        if (m.parentId.isNotEmpty() || !macros.childrenOf(m.id).empty())
            continue;
        int ordinary = 0;
        for (const auto& member : m.members)
            if (!m.memberIsPort(member))
                ++ordinary;
        if (ordinary >= 3)
            return m.id;
    }
    return {};
}

// The member card with the most cables: the one whose crossing re-routes the most through the macro's ports.
ModuleComponent* busiestMember(GraphEditor& editor, const juce::String& macroId) {
    std::map<uint32_t, int> cablesOf;
    for (const auto& c : editor.buildVisibleCables()) {
        ++cablesOf[c.id.srcUid];
        ++cablesOf[c.id.dstUid];
    }
    auto& controller = editor.getMacroController();
    ModuleComponent* best = nullptr;
    for (auto* m : editor.getModuleComponents()) {
        if (m == nullptr || !m->isVisible())
            continue;
        const auto* owner = controller.macroForNode(m->getNodeId());
        if (owner == nullptr || owner->id != macroId || owner->memberIsPort(controller.nodeUuidFor(m->getNodeId())))
            continue;
        if (best == nullptr || cablesOf[m->getNodeId().uid] > cablesOf[best->getNodeId().uid])
            best = m;
    }
    return best;
}

struct DragCost {
    double down = 0, drag = 0, worstTick = 0, up = 0, pump = 0, paint = 0, stall = 0;
};

// One real Cmd-drag of `comp` by `delta`: press, one drag tick (the crossing), release.
DragCost timedDrag(MainComponent& mc, ModuleComponent& comp, juce::Point<int> delta) {
    DragCost cost;
    const juce::Point<int> pressPos(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    const auto dragPos = pressPos + delta;
    double t0 = benchNowMs();
    comp.mouseDown(realMouseEvent(comp, pressPos, pressPos, kCmdClick));
    cost.down = benchNowMs() - t0;
    // A real drag is a stream of ticks with frames in between, so the crossing tick and the ticks after it count.
    constexpr int ticks = 24;
    for (int i = 1; i <= ticks; ++i) {
        const auto pos = pressPos + juce::Point<int>(delta.x * i / ticks, delta.y * i / ticks);
        t0 = benchNowMs();
        comp.mouseDrag(realMouseEvent(comp, pos, pressPos, kCmdClick, true));
        const double tick = benchNowMs() - t0;
        cost.drag += tick;
        cost.worstTick = std::max(cost.worstTick, tick);
        cost.worstTick = std::max(cost.worstTick, benchWorstStallMs(16));
    }
    t0 = benchNowMs();
    comp.mouseUp(realMouseEvent(comp, dragPos, pressPos, kCmdClick, true));
    cost.up = benchNowMs() - t0;
    cost.pump = benchPumpMs();
    cost.paint = benchPaintMs(mc);
    cost.stall = benchWorstStallMs(800);
    return cost;
}

void printDrag(const char* label, const DragCost& c, bool member) {
    std::printf(
        "[macro-drag] %s down=%.1f drag=%.1f worstTick=%.1f up=%.1f pump=%.1f paint=%.1f stall=%.1f member=%d\n", label,
        c.down, c.drag, c.worstTick, c.up, c.pump, c.paint, c.stall, member ? 1 : 0);
    std::fflush(stdout);
}

} // namespace

TEST_F(ChannelFlowTest, DISABLED_LoadTestMacroDragProfile) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto original = path != nullptr ? juce::File(path)
                                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                .getChildFile("AgentSynth/Load test.agsproj");
    const auto project = synth::test::uniqueTempPath("LoadTestBench", ".agsproj");
    ASSERT_TRUE(original.copyDirectoryTo(project));
    for (const auto& f : project.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
        f.deleteFile();
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{project};

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    if (std::getenv("PROFILE_AUDIO") == nullptr) // PROFILE_AUDIO=1 keeps the device running, as in the app
        mc.getAudioEngine().suspendDeviceCallback();
    if (std::getenv("PROFILE_ON_SCREEN") != nullptr) // a real window: the VBlank animations and paints run
        mc.addToDesktop(juce::ComponentPeer::windowHasTitleBar), mc.setVisible(true);
    ASSERT_TRUE(mc.openProjectForTest(project));
    benchPumpMs();
    benchPaintMs(mc);

    auto& editor = mc.getGraphEditor();
    const auto macroId = pickMacro(editor.getMacros());
    ASSERT_FALSE(macroId.isEmpty()) << "no top-level macro with three ordinary members";
    double t0 = benchNowMs();
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const double expandMs = benchNowMs() - t0;
    const double expandPump = benchPumpMs();
    std::printf("[macro] %s nodes=%d expand=%.1f pump=%.1f paint=%.1f\n", macroId.toRawUTF8(),
                (int)mc.getAudioEngine().getGraph().getNumNodes(), expandMs, expandPump, benchPaintMs(mc));

    auto* comp = busiestMember(editor, macroId);
    ASSERT_NE(comp, nullptr);
    const auto id = comp->getNodeId();
    const char* spin = std::getenv("PROFILE_SPIN_MACRO");
    const double spinUntil = benchNowMs() + (spin != nullptr ? std::atof(spin) * 1000.0 : 0.0);
    do {
        editor.setSelectedNodes({id});
        auto* card = findComponent(editor, id);
        ASSERT_NE(card, nullptr);
        printDrag("out", timedDrag(mc, *card, {2400, 0}), editor.getMacroController().macroForNode(id) != nullptr);

        card = findComponent(editor, id);
        ASSERT_NE(card, nullptr);
        const auto hull = editor.getMacroController().macroHullBounds(macroId);
        ASSERT_FALSE(hull.isEmpty());
        printDrag("in", timedDrag(mc, *card, hull.getCentre() - card->getBounds().getCentre()),
                  editor.getMacroController().macroForNode(id) != nullptr);
    } while (benchNowMs() < spinUntil);

    for (int i = 0; i < 2; ++i) {
        t0 = benchNowMs();
        mc.getUndoManager().undo();
        const double undoMs = benchNowMs() - t0;
        std::printf("[macro-undo] #%d call=%.1f pump=%.1f paint=%.1f\n", i + 1, undoMs, benchPumpMs(),
                    benchPaintMs(mc));
    }
    std::fflush(stdout);
}

namespace {

// One edit as the user feels it: the call, then the worst message-loop stall in the 600 ms after it (animation
// frames, deferred refreshes, async graph work), then the next full paint.
void timedEdit(MainComponent& mc, const char* label, const std::function<void()>& edit) {
    const double t0 = benchNowMs();
    edit();
    const double call = benchNowMs() - t0;
    const double stall = benchWorstStallMs(600);
    std::printf("[edit] %-22s call=%.1f stall=%.1f paint=%.1f\n", label, call, stall, benchPaintMs(mc));
    std::fflush(stdout);
}

std::unique_ptr<MainComponent> openBenchCopy(juce::File& copyOut) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto original = path != nullptr ? juce::File(path)
                                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                .getChildFile("AgentSynth/Load test.agsproj");
    copyOut = synth::test::uniqueTempPath("LoadTestBench", ".agsproj");
    if (!original.copyDirectoryTo(copyOut))
        return nullptr;
    for (const auto& f : copyOut.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
        f.deleteFile();
    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProviderCFT>());
    mc->setSize(1600, 1000);
    if (std::getenv("PROFILE_AUDIO") == nullptr)
        mc->getAudioEngine().suspendDeviceCallback();
    if (std::getenv("PROFILE_ON_SCREEN") != nullptr)
        mc->addToDesktop(juce::ComponentPeer::windowHasTitleBar), mc->setVisible(true);
    if (!mc->openProjectForTest(copyOut))
        return nullptr;
    benchWorstStallMs(300);
    benchPaintMs(*mc);
    return mc;
}

} // namespace

// The other edits Tal reported as hanging on big projects: adding and deleting a module, deleting a macro and undoing
// it, folding and unfolding every macro, resizing the window, and duplicating, deleting and undoing a track.
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestEditProfile*'
TEST_F(ChannelFlowTest, DISABLED_LoadTestEditProfile) {
    juce::File copy;
    auto mc = openBenchCopy(copy);
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{copy};
    ASSERT_NE(mc, nullptr);
    auto& editor = mc->getGraphEditor();
    auto& undo = mc->getUndoManager();

    timedEdit(*mc, "add module",
              [&] { editor.addModuleAtCanvasPosition("Filter", editor.getViewportCentreInCanvasSpace(), {}); });
    timedEdit(*mc, "undo add module", [&] { undo.undo(); });
    ModuleComponent* card = nullptr;
    for (auto* m : editor.getModuleComponents())
        if (m != nullptr && m->isVisible() && (card == nullptr || m->getNodeId().uid > card->getNodeId().uid))
            card = m;
    ASSERT_NE(card, nullptr);
    editor.setSelectedNodes({card->getNodeId()});
    timedEdit(*mc, "delete module", [&] { editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)); });
    timedEdit(*mc, "undo delete module", [&] { undo.undo(); });

    const auto macroId = pickMacro(editor.getMacros());
    ASSERT_FALSE(macroId.isEmpty());
    timedEdit(*mc, "delete macro", [&] { editor.getMacroController().deleteMacroAndMembers(macroId); });
    timedEdit(*mc, "undo delete macro", [&] { undo.undo(); });

    auto& controller = editor.getMacroController();
    for (const auto& m : editor.getMacros().getAll())
        controller.selectMacro(m.id, true);
    timedEdit(*mc, "fold all macros", [&] { controller.foldAndPackSelectionMacros(); });
    timedEdit(*mc, "unfold all macros", [&] { controller.foldAndPackSelectionMacros(); });

    timedEdit(*mc, "resize window", [&] { mc->setSize(1300, 850); });
    timedEdit(*mc, "resize back", [&] { mc->setSize(1600, 1000); });

    auto& host = (synth::ui::TrackHeaderHost&)*mc; // a private base; the header's own call path
    const auto first = mc->getTimelineDoc().getTracks().front().id;
    timedEdit(*mc, "duplicate track", [&] { host.duplicateTrack(first); });
    timedEdit(*mc, "undo duplicate track", [&] { undo.undo(); });
    timedEdit(*mc, "delete track", [&] { host.deleteTrack(first); });
    timedEdit(*mc, "undo delete track", [&] { undo.undo(); });
}

// Opening the project as the app does: the open call, the first full paint, and the longest stall while the load
// reveal plays. PROFILE_ON_SCREEN=1 PROFILE_AUDIO=1 for the on-screen path.
TEST_F(ChannelFlowTest, DISABLED_LoadTestOpenProfile) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto original = path != nullptr ? juce::File(path)
                                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                .getChildFile("AgentSynth/Load test.agsproj");
    const auto copy = synth::test::uniqueTempPath("LoadTestBench", ".agsproj");
    ASSERT_TRUE(original.copyDirectoryTo(copy));
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{copy};
    for (int run = 0; run < 3; ++run) {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 1000);
        if (std::getenv("PROFILE_AUDIO") == nullptr)
            mc.getAudioEngine().suspendDeviceCallback();
        if (std::getenv("PROFILE_ON_SCREEN") != nullptr)
            mc.addToDesktop(juce::ComponentPeer::windowHasTitleBar), mc.setVisible(true);
        benchWorstStallMs(200);
        const double t0 = benchNowMs();
        ASSERT_TRUE(mc.openProjectForTest(copy));
        const double open = benchNowMs() - t0;
        const double paint = benchPaintMs(mc);
        const double stall = benchWorstStallMs(1500);
        std::printf("[open] #%d open=%.1f paint=%.1f stall=%.1f\n", run + 1, open, paint, stall);
        std::fflush(stdout);
    }
}
