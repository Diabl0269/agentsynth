// MacroEditProfileTests.cpp
//
// A disabled profiling bench, not a test: opens a temporary copy of the Load test project
// (docs/layout/rendering.md#the-load-test-project), opens one macro and drags its busiest member out of the macro and
// back in through the real ModuleComponent mouse callbacks, printing what each step costs: the drag tick that crosses
// the border (the membership change is applied live), the drop, the message-loop work left behind and the next paint,
// then each undo. Run by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestMacroDragProfile*'
// PROFILE_PROJECT=<bundle> picks another project, PROFILE_MACRO_INDEX=<n> the n-th macro that qualifies;
// PROFILE_SPIN_MACRO=<s> loops the out-and-in drag for that long, to attach `sample <pid> 10` to; PROFILE_SPIN_UNDO=<s>
// loops the two undos; PROFILE_SPIN_UNDO_PAINT=<s> loops the second undo and the full paint after it.

#include "../Macros/MacroContainer/MacroDragTestHelpers.h"
#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "../UniqueTempDir.h"
#include "AI/AIStateMapper/GraphRebuildBatch.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "StallProbe.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
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

double benchPaintMs(juce::Component& c) {
    const double t0 = benchNowMs();
    if (c.getWidth() > 0 && c.getHeight() > 0)
        (void)c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    return benchNowMs() - t0;
}

// The `skip`-th top-level macro (0 the first) with at least three ordinary members, so dragging one out leaves the
// macro standing.
juce::String pickMacro(const synth::MacroSet& macros, int skip = 0) {
    for (const auto& m : macros.getAll()) {
        if (m.parentId.isNotEmpty() || !macros.childrenOf(m.id).empty())
            continue;
        int ordinary = 0;
        for (const auto& member : m.members)
            if (!m.memberIsPort(member))
                ++ordinary;
        if (ordinary >= 3 && skip-- == 0)
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
    const auto start = comp.getPosition();
    for (int i = 1; i <= ticks; ++i) {
        // An event's position is relative to the card where it is now, as a real mouse event's is, so the card ends
        // `delta` away from where it was pressed, not the sum of every tick's offset.
        const auto pos =
            pressPos + juce::Point<int>(delta.x * i / ticks, delta.y * i / ticks) - (comp.getPosition() - start);
        t0 = benchNowMs();
        comp.mouseDrag(realMouseEvent(comp, pos, pressPos, kCmdClick, true));
        const double tick = benchNowMs() - t0;
        cost.drag += tick;
        cost.worstTick = std::max(cost.worstTick, tick);
        cost.worstTick = std::max(cost.worstTick, bench::worstStallMs(16));
    }
    t0 = benchNowMs();
    comp.mouseUp(realMouseEvent(comp, dragPos - (comp.getPosition() - start), pressPos, kCmdClick, true));
    cost.up = benchNowMs() - t0;
    cost.pump = benchPumpMs();
    cost.paint = benchPaintMs(mc);
    cost.stall = bench::worstStallMs(800);
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
    const char* which = std::getenv("PROFILE_MACRO_INDEX"); // the n-th such macro, 0 the first
    const auto macroId = pickMacro(editor.getMacros(), which != nullptr ? std::atoi(which) : 0);
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
        auto& counters = graph_editor_paint::workCounters();
        counters = {};
        std::map<juce::uint32, juce::Rectangle<int>> boundsBefore;
        for (auto* card : editor.getModuleComponents())
            boundsBefore[card->getNodeId().uid] = card->getBounds();
        t0 = benchNowMs();
        mc.getUndoManager().undo();
        const double undoMs = benchNowMs() - t0;
        const int cards = counters.cardsBuilt, reconciles = counters.componentUpdates;
        const double pump = benchPumpMs();
        std::map<ModuleComponent*, int> rasters;
        for (auto* card : editor.getModuleComponents())
            if (auto* image = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage()))
                rasters[card] = image->getRasterCountForTest();
        const double paint = benchPaintMs(mc);
        int redrawn = 0, moved = 0, resized = 0;
        for (auto* card : editor.getModuleComponents())
            if (const auto it = boundsBefore.find(card->getNodeId().uid); it != boundsBefore.end()) {
                moved += it->second.getPosition() != card->getPosition() ? 1 : 0;
                resized += it->second.getWidth() != card->getWidth() || it->second.getHeight() != card->getHeight();
            }
        juce::String names;
        for (const auto& [card, count] : rasters)
            if (auto* image = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage());
                image != nullptr && image->getRasterCountForTest() != count && ++redrawn <= 4)
                names << card->getModule()->getName() << (card->isShowing() ? "" : "(hidden)") << " ";
        std::printf("[macro-undo] #%d call=%.1f pump=%.1f paint=%.1f stall=%.1f | cards=%d reconciles=%d redrawn=%d/%d "
                    "moved=%d resized=%d %s\n",
                    i + 1, undoMs, pump, paint, bench::worstStallMs(800), cards, reconciles, redrawn,
                    (int)rasters.size(), moved, resized, names.toRawUTF8());
    }
    // PROFILE_SPIN_UNDO_PAINT=<s>: redo the move out and undo it again, painting the window after each, for that long.
    if (const char* spinPaint = std::getenv("PROFILE_SPIN_UNDO_PAINT"))
        for (const double until = benchNowMs() + std::atof(spinPaint) * 1000.0; benchNowMs() < until;) {
            (void)mc.getUndoManager().redo();
            const double redoPaint = benchPaintMs(mc), redoStall = bench::worstStallMs(400);
            (void)mc.getUndoManager().undo();
            const double undoPaint = benchPaintMs(mc);
            std::printf("[macro-undo-paint] redo paint=%.1f stall=%.1f | undo paint=%.1f stall=%.1f\n", redoPaint,
                        redoStall, undoPaint, bench::worstStallMs(400));
        }
    // PROFILE_SPIN_UNDO=<s>: redo both moves and undo them again for that long, to attach `sample` to the undos.
    if (const char* spinUndo = std::getenv("PROFILE_SPIN_UNDO"))
        for (const double until = benchNowMs() + std::atof(spinUndo) * 1000.0; benchNowMs() < until;)
            for (const bool redo : {true, true, false, false}) {
                (void)(redo ? mc.getUndoManager().redo() : mc.getUndoManager().undo());
                std::printf("[macro-%s] stall=%.1f\n", redo ? "redo" : "undo", bench::worstStallMs(400));
            }
    std::fflush(stdout);
}

namespace {

// One edit as the user feels it: the call, then the worst message-loop stall in the 600 ms after it (animation
// frames, deferred refreshes, async graph work), then the next full paint.
// Also how often the edit ran each whole-project pass: canvas reconciles (updateComponents), cards built, visible-cable
// rebuilds and audio-graph rebuilds.
void timedEdit(MainComponent& mc, const char* label, const std::function<void()>& edit) {
    auto& counters = graph_editor_paint::workCounters();
    counters = {};
    const int cablesBefore = mc.getGraphEditor().getCableRebuildCountForTest();
    const int graphBefore = synth::GraphRebuildBatch::rebuildCountForTest();
    const double t0 = benchNowMs();
    edit();
    const double call = benchNowMs() - t0;
    const auto inCall = counters;
    const int cablesInCall = mc.getGraphEditor().getCableRebuildCountForTest() - cablesBefore;
    const double stall = bench::worstStallMs(600);
    std::printf("[edit] %-22s call=%.1f stall=%.1f paint=%.1f | in call: reconciles=%d cards=%d cableRebuilds=%d "
                "graphRebuilds=%d | after: reconciles=%d cableRebuilds=%d\n",
                label, call, stall, benchPaintMs(mc), inCall.componentUpdates, inCall.cardsBuilt, cablesInCall,
                synth::GraphRebuildBatch::rebuildCountForTest() - graphBefore,
                counters.componentUpdates - inCall.componentUpdates,
                mc.getGraphEditor().getCableRebuildCountForTest() - cablesBefore - cablesInCall);
    std::fflush(stdout);
}

// What one run of each whole-project pass an edit can make costs on this project, each timed alone.
void printPassCosts(MainComponent& mc) {
    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();
    const auto timed = [](const std::function<void()>& pass) {
        pass(); // warm
        const double t0 = benchNowMs();
        for (int i = 0; i < 5; ++i)
            pass();
        return (benchNowMs() - t0) / 5.0;
    };
    const double reconcile = timed([&] { editor.updateComponents(); });
    const double reconcileAndCables = timed([&] {
        editor.updateComponents();
        (void)editor.buildVisibleCables();
    });
    const double capture = timed([&] { mc.getUndoManager().captureBeforeState(graph); });
    const double audio = timed([&] { graph.rebuild(); });
    std::printf("[passes] updateComponents=%.1f cableRebuild=%.1f undoCapture=%.1f graphRebuild=%.1f\n", reconcile,
                reconcileAndCables - reconcile, capture, audio);
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
    bench::worstStallMs(300);
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
    printPassCosts(*mc);

    // PROFILE_SPIN_EDIT=<s>: add a module and undo it for that long, to attach `sample <pid> 10` to.
    if (const char* spinEdit = std::getenv("PROFILE_SPIN_EDIT"))
        for (const double until = benchNowMs() + std::atof(spinEdit) * 1000.0; benchNowMs() < until;) {
            timedEdit(*mc, "add module",
                      [&] { editor.addModuleAtCanvasPosition("Filter", editor.getViewportCentreInCanvasSpace(), {}); });
            timedEdit(*mc, "undo add module", [&] { undo.undo(); });
        }
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

// The track edits on their own, one project size per run (PROFILE_PROJECT=<bundle> a grown copy of the Load test
// project, see ManyTracksProfileTests.cpp): duplicate, delete and a three-track paste, each with its undo and redo.
//   PROFILE_ON_SCREEN=1 ./Tests --gtest_also_run_disabled_tests --gtest_filter='*LoadTestTrackEditProfile*'
TEST_F(ChannelFlowTest, DISABLED_LoadTestTrackEditProfile) {
    juce::File copy;
    auto mc = openBenchCopy(copy);
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{copy};
    ASSERT_NE(mc, nullptr);
    auto& undo = mc->getUndoManager();
    auto& host = (synth::ui::TrackHeaderHost&)*mc; // a private base; the header's own call path
    const auto& tracks = mc->getTimelineDoc().getTracks();
    std::printf("[track-edit] tracks=%d\n", (int)tracks.size());
    const auto first = tracks.front().id;
    const auto last = tracks.back().id;
    for (int round = 0; round < 2; ++round) { // the first round warms caches; the second is what a user feels
        timedEdit(*mc, "duplicate track", [&] { host.duplicateTrack(first); });
        timedEdit(*mc, "undo duplicate", [&] { undo.undo(); });
        timedEdit(*mc, "redo duplicate", [&] { (void)undo.redo(); });
        timedEdit(*mc, "undo duplicate", [&] { undo.undo(); });
        timedEdit(*mc, "delete track", [&] { host.deleteTrack(last); });
        timedEdit(*mc, "undo delete", [&] { undo.undo(); });
        timedEdit(*mc, "redo delete", [&] { (void)undo.redo(); });
        timedEdit(*mc, "undo delete", [&] { undo.undo(); });
        const std::vector<synth::TrackId> three = {tracks[0].id, tracks[1].id, tracks[2].id};
        host.copyTracks(three);
        timedEdit(*mc, "paste 3 tracks", [&] { host.pasteTracks(last); });
        timedEdit(*mc, "undo paste", [&] { undo.undo(); });
        timedEdit(*mc, "redo paste", [&] { (void)undo.redo(); });
        timedEdit(*mc, "undo paste", [&] { undo.undo(); });
    }
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
        bench::worstStallMs(200);
        const double t0 = benchNowMs();
        ASSERT_TRUE(mc.openProjectForTest(copy));
        const double open = benchNowMs() - t0;
        const double paint = benchPaintMs(mc);
        const double stall = bench::worstStallMs(1500);
        std::printf("[open] #%d open=%.1f paint=%.1f stall=%.1f\n", run + 1, open, paint, stall);
        std::fflush(stdout);
    }
}
