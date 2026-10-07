// ManyTracksProfileTests.cpp
//
// A disabled profiling bench, not a test: builds N instrument tracks through the real "+ Track -> Instrument" path in
// a real MainComponent and prints the per-frame cost of the canvas tick, the card glide (auto-arrange, undo, redo),
// the timeline and the mixer. Run by hand (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch):
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*ManyTracksProfile*'
// PROFILE_TRACKS=<n> (with PROFILE_EXPAND=1 to open every track's macro) runs one size; PROFILE_SPIN_TICK=<s>
// (plus PROFILE_SPIN_PAINT=1) or PROFILE_SPIN_UNDO=<s> loops one pass for that long, to attach `sample` to.

#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "AppUndoManager.h"
#include "MacroSet.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

namespace {

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
void profileDeleteUndo(MainComponent& mc, const char* label); // below the Load test bench

double avgMs(int reps, const std::function<void()>& fn) {
    fn(); // warm-up: the first paint rasterizes the card caches
    const double t0 = nowMs();
    for (int i = 0; i < reps; ++i)
        fn();
    return (nowMs() - t0) / reps;
}

void paintOnce(juce::Component& c) {
    if (c.getWidth() > 0 && c.getHeight() > 0)
        (void)c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
}

// Paints what the glide's last frame asked to repaint: its area, or the whole editor when it asked for everything.
void paintGlideFrame(GraphEditor& editor) {
    const auto area = editor.getCardGlideForTest().lastFrameArea();
    auto* canvas =
        editor.getModuleComponents().isEmpty() ? nullptr : editor.getModuleComponents()[0]->getParentComponent();
    if (area.isEmpty() || canvas == nullptr) {
        paintOnce(editor);
        return;
    }
    const auto r = editor.getLocalArea(canvas, area).getIntersection(editor.getLocalBounds());
    if (!r.isEmpty())
        (void)editor.createComponentSnapshot(r, true, 1.0f);
}

// One glide frame as the VBlank runs it: the animator's frame (tween, cables, repaint request) + the paint it asked
// for.
double glideFrameMs(GraphEditor& editor, bool withPaint) {
    const double t0 = nowMs();
    int frames = 0;
    for (float t = 0.1f; t < 1.0f; t += 0.1f, ++frames) {
        editor.getCardGlideForTest().stepFrameForTest(t);
        (void)editor.buildVisibleCables();
        if (withPaint)
            paintGlideFrame(editor);
    }
    return (nowMs() - t0) / frames;
}

void profile(int tracks, bool expand) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.simulateToggleBottomPanelClick(); // timeline visible
    double t0 = nowMs();
    for (int i = 0; i < tracks; ++i)
        mc.getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
    const double buildMs = nowMs() - t0;
    // Each track gets 8 four-bar clips of 32 notes and one automation lane with 16 points.
    auto& liveDoc = mc.getTimelineDoc();
    synth::TimelineDoc doc; // populated off-line, then loaded in one notification
    doc.fromVar(liveDoc.toVar());
    std::vector<synth::TrackId> trackIds;
    for (const auto& tr : doc.getTracks())
        trackIds.push_back(tr.id);
    for (auto id : trackIds) {
        for (int c = 0; c < 8; ++c) {
            const auto clip = doc.addClip(id, c * 16.0, 16.0, "Clip");
            for (int n = 0; n < 32; ++n) {
                synth::MidiNote note;
                note.startBeat = n * 0.5;
                note.lengthBeats = 0.4;
                note.pitch = 48 + (n * 7) % 24;
                doc.addNote(clip, note);
            }
        }
        synth::AutomationLane::RangeSnapshot range;
        range.maxValue = 1.0f;
        const auto lane = doc.addLane(id, "node-x", "cutoff", range);
        for (int p = 0; p < 16; ++p)
            doc.addBreakpoint(lane, p * 8.0, (p % 2) ? 0.2 : 0.8);
    }
    liveDoc.fromVar(doc.toVar());
    const auto firstClip = liveDoc.getTracks().front().clips.front().id;
    int noteN = 0;
    const double docMut = avgMs(5, [&] {
        synth::MidiNote note;
        note.startBeat = 0.25 * (++noteN % 60);
        note.pitch = 30 + noteN % 60;
        liveDoc.addNote(firstClip, note);
    });
    auto& editor = mc.getGraphEditor();
    auto& glide = editor.getCardGlideForTest();
    if (expand) {
        std::vector<juce::String> ids;
        for (const auto& m : editor.getMacros().getAll())
            ids.push_back(m.id);
        for (const auto& id : ids)
            editor.getMacroController().setMacroCollapsed(id, false);
        editor.finishCardGlideForTest();
    }

    int cards = 0; // module cards showing (a collapsed track's members are hidden behind its macro card)
    for (auto* m : editor.getModuleComponents())
        if (m != nullptr && m->isVisible())
            ++cards;
    const int macros = (int)editor.getMacros().getAll().size();
    const int cables = (int)editor.buildVisibleCables().size();

    if (const char* spin = std::getenv("PROFILE_SPIN_TICK")) { // for `sample`: spin the canvas tick
        const double until = nowMs() + 1000.0 * std::atof(spin);
        std::printf("[spin]\n");
        std::fflush(stdout);
        while (nowMs() < until) {
            static_cast<juce::Timer&>(editor).timerCallback();
            if (std::getenv("PROFILE_SPIN_PAINT") != nullptr)
                paintOnce(editor);
        }
        return;
    }
    const double tick = avgMs(10, [&] { static_cast<juce::Timer&>(editor).timerCallback(); });
    const double tickCables = avgMs(10, [&] {
        static_cast<juce::Timer&>(editor).timerCallback();
        (void)editor.buildVisibleCables();
    });
    const double tickPaint = avgMs(10, [&] {
        static_cast<juce::Timer&>(editor).timerCallback();
        paintOnce(editor);
    });

    t0 = nowMs();
    editor.autoArrange();
    const double arrangeMs = nowMs() - t0;
    const bool armed = glide.isLive();
    const double arrangeFrameNoPaint = glideFrameMs(editor, false);
    const double arrangeFrame = glideFrameMs(editor, true);
    editor.finishCardGlideForTest();

    if (const char* spin = std::getenv("PROFILE_SPIN_UNDO")) { // for `sample`: spin undo/redo of the arrange
        std::printf("[spin]\n");
        std::fflush(stdout);
        const double until = nowMs() + 1000.0 * std::atof(spin);
        while (nowMs() < until) {
            mc.getUndoManager().undo();
            editor.finishCardGlideForTest();
            mc.getUndoManager().redo();
            editor.finishCardGlideForTest();
        }
        return;
    }
    t0 = nowMs();
    mc.getUndoManager().undo();
    const double undoMs = nowMs() - t0;
    const double undoFrame = glideFrameMs(editor, true);
    editor.finishCardGlideForTest();

    t0 = nowMs();
    mc.getUndoManager().redo();
    const double redoMs = nowMs() - t0;
    const double redoFrame = glideFrameMs(editor, true);
    editor.finishCardGlideForTest();

    auto& panel = mc.getTimelinePanel();
    const double mcTick = avgMs(10, [&] { static_cast<juce::Timer&>(mc).timerCallback(); });
    const double tlPaint = avgMs(5, [&] { paintOnce(panel); });
    const double tlStrip =
        avgMs(10, [&] { (void)panel.createComponentSnapshot({600, 0, 7, panel.getHeight()}, true, 1.0f); });
    auto& vp = panel.getTrackHeaderViewport();
    int y = 0;
    const double tlScroll = avgMs(10, [&] {
        y = (y + 40) % juce::jmax(1, vp.getViewedComponent()->getHeight());
        vp.setViewPosition(0, y);
        paintOnce(panel);
    });
    auto& mixer = mc.getBottomDock().getMixerPanel();
    if (mixer.getWidth() == 0)
        mixer.setBounds(0, 0, 1600, 400);
    const double mixPaint = avgMs(5, [&] { paintOnce(mixer); });

    std::printf(
        "[profile] tracks=%d expand=%d macros=%d moduleCards=%d cables=%d build=%.0f | canvasTick=%.2f "
        "tick+cables=%.2f "
        "tick+paint=%.2f | arrange=%.1f armed=%d frameNoPaint=%.2f frame=%.2f | undo=%.1f frame=%.2f | "
        "redo=%.1f frame=%.2f | docMutation=%.2f mcTick=%.2f tlPaint=%.2f tlStrip=%.2f tlScroll=%.2f mixPaint=%.2f\n",
        tracks, expand ? 1 : 0, macros, cards, cables, buildMs, tick, tickCables, tickPaint, arrangeMs, armed ? 1 : 0,
        arrangeFrameNoPaint, arrangeFrame, undoMs, undoFrame, redoMs, redoFrame, docMut, mcTick, tlPaint, tlStrip,
        tlScroll, mixPaint);
    std::fflush(stdout);
    profileDeleteUndo(mc, ("tracks=" + juce::String(tracks) + " expand=" + juce::String(expand ? 1 : 0)).toRawUTF8());
}

// Runs the message loop briefly so async work a real frame would do (graph rebuild, deferred repaints) is counted.
double pumpMs() {
    const double t0 = nowMs();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return nowMs() - t0 - 20.0;
}

// The saved Load test project (docs/layout/rendering.md#the-load-test-project): open it, then duplicate its first track
// N times through the real Cmd+D entry point, timing each step; a rising curve is the freeze the duplicate used to
// cause.
void profileProject(const juce::File& original, int duplicates) {
    // Work on a temporary copy without its autosave sidecars: the bench's MainComponent autosaves, and an autosave of
    // the grown project left in the real bundle is what that bundle would reopen to.
    const auto project =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("LoadTestBench", ".agsproj");
    if (!original.copyDirectoryTo(project)) {
        std::printf("[project] could not copy %s\n", original.getFullPathName().toRawUTF8());
        return;
    }
    for (const auto& f : project.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
        f.deleteFile();
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{project};
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    mc.getAudioEngine().suspendDeviceCallback();
    double t0 = nowMs();
    if (!mc.openProjectForTest(project)) {
        std::printf("[project] could not open %s\n", original.getFullPathName().toRawUTF8());
        return;
    }
    const double openMs = nowMs() - t0;
    const double openPump = pumpMs();
    paintOnce(mc);
    std::printf("[project] %s tracks=%d nodes=%d open=%.0f pump=%.0f\n", original.getFileName().toRawUTF8(),
                (int)mc.getTimelineDoc().getTracks().size(), (int)mc.getAudioEngine().getGraph().getNumNodes(), openMs,
                openPump);
    std::fflush(stdout);
    if (duplicates <= 0 || mc.getTimelineDoc().getTracks().empty())
        return;
    const auto first = mc.getTimelineDoc().getTracks().front().id;
    // TrackHeaderHost is a private base of MainComponent; the C-style cast is the header's own call path.
    auto& host = (synth::ui::TrackHeaderHost&)mc;
    for (int i = 0; i < duplicates; ++i) {
        t0 = nowMs();
        host.duplicateTrack(first);
        const double dupMs = nowMs() - t0;
        const double pump = pumpMs();
        t0 = nowMs();
        paintOnce(mc);
        const double paint = nowMs() - t0;
        std::printf("[duplicate] #%d tracks=%d nodes=%d call=%.0f pump=%.0f paint=%.0f\n", i + 1,
                    (int)mc.getTimelineDoc().getTracks().size(), (int)mc.getAudioEngine().getGraph().getNumNodes(),
                    dupMs, pump, paint);
        std::fflush(stdout);
    }
    if (const char* save = std::getenv("PROFILE_SAVE")) // keep the grown project for the next size's runs
        std::printf("[project] saved %s: %d\n", save, mc.saveProjectForTest(juce::File(save)) ? 1 : 0);
}

} // namespace

// PROFILE_PROJECT=<bundle> (default ~/Music/AgentSynth/Load test.agsproj), PROFILE_DUPLICATE=<n> (default 10) and
// PROFILE_SAVE=<bundle> to save the grown project (how the 80 and 200 track sizes are made).
TEST_F(ChannelFlowTest, DISABLED_LoadTestProjectProfile) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto project = path != nullptr ? juce::File(path)
                                         : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                               .getChildFile("AgentSynth/Load test.agsproj");
    const char* n = std::getenv("PROFILE_DUPLICATE");
    profileProject(project, n != nullptr ? std::atoi(n) : 10);
}

namespace {

// A glide or ghost frame as the VBlank runs it (the animator's own frame, its repaint request) plus the paint.
double ghostFrameMs(GraphEditor& editor, CardGlideAnimator& glide) {
    if (!glide.isLive())
        return 0.0;
    const double t0 = nowMs();
    int frames = 0;
    for (float t = 0.1f; t < 1.0f; t += 0.1f, ++frames) {
        glide.stepFrameForTest(t);
        paintGlideFrame(editor);
    }
    return (nowMs() - t0) / frames;
}

// The on-screen card with the most cables: the card a delete takes the most with it.
ModuleComponent* busiestVisibleCard(GraphEditor& editor) {
    std::map<uint32_t, int> cablesOf;
    for (const auto& c : editor.buildVisibleCables()) {
        ++cablesOf[c.id.srcUid];
        ++cablesOf[c.id.dstUid];
    }
    ModuleComponent* best = nullptr;
    for (auto* m : editor.getModuleComponents())
        if (m != nullptr && m->isVisible() && m->getBounds().intersects(card_glide_detail::visibleCanvasArea(*m)) &&
            (best == nullptr || cablesOf[m->getNodeId().uid] > cablesOf[best->getNodeId().uid]))
            best = m;
    return best;
}

// Deletes the busiest on-screen card with the Delete key, undoes it, then makes and undoes a parameter-only change:
// the one-off cost of each call (the model change plus arming the animation) and the per-frame cost of its frames.
void profileDeleteUndo(MainComponent& mc, const char* label) {
    auto& editor = mc.getGraphEditor();
    auto& glide = editor.getCardGlideForTest();
    synth::ui::setReducedMotionForTest(false);
    glide.setForceAnimateForTest(true);
    editor.finishCardGlideForTest();
    paintOnce(editor); // the card caches are warm, as on screen
    auto* card = busiestVisibleCard(editor);
    if (card == nullptr) {
        std::printf("[delete-undo] %s no card on screen\n", label);
        return;
    }
    const auto id = card->getNodeId();
    editor.setSelectedNodes({id});
    double t0 = nowMs();
    editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
    const double deleteMs = nowMs() - t0;
    const int exits = glide.exitGhostCount();
    t0 = nowMs();
    paintOnce(editor); // the one whole-canvas repaint arming asks for
    const double deletePaint = nowMs() - t0;
    const double deleteFrame = ghostFrameMs(editor, glide);
    editor.finishCardGlideForTest();

    t0 = nowMs();
    mc.getUndoManager().undo();
    const double undoMs = nowMs() - t0;
    const int enters = glide.enterGhostCount();
    t0 = nowMs();
    paintOnce(editor);
    const double undoPaint = nowMs() - t0;
    const double undoFrame = ghostFrameMs(editor, glide);
    editor.finishCardGlideForTest();

    auto& graph = mc.getAudioEngine().getGraph();
    juce::AudioProcessorParameter* param = nullptr;
    for (auto* node : graph.getNodes())
        if (node->nodeID == id && !node->getProcessor()->getParameters().isEmpty())
            param = node->getProcessor()->getParameters()[0];
    double paramUndoMs = 0.0;
    bool paramArmed = false;
    if (param != nullptr) {
        mc.getUndoManager().captureBeforeState(graph);
        param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.1f : 0.9f);
        mc.getUndoManager().pushSnapshotFromCapture(graph);
        t0 = nowMs();
        mc.getUndoManager().undo();
        paramUndoMs = nowMs() - t0;
        paramArmed = glide.isLive();
        editor.finishCardGlideForTest();
    }
    std::printf("[delete-undo] %s cables=%d | delete=%.1f paint=%.1f exits=%d frame=%.2f | undo=%.1f paint=%.1f "
                "enters=%d frame=%.2f | paramUndo=%.1f armed=%d snapshots=%d rendered=%d\n",
                label, (int)editor.buildVisibleCables().size(), deleteMs, deletePaint, exits, deleteFrame, undoMs,
                undoPaint, enters, undoFrame, paramUndoMs, paramArmed ? 1 : 0, glide.snapshotCount(),
                glide.renderedSnapshotCount());
    std::fflush(stdout);
    glide.setForceAnimateForTest(false);
    synth::ui::setReducedMotionForTest(std::nullopt);
}

} // namespace

// The Load test project's delete and undo animation costs (docs/layout/rendering.md#the-load-test-project), on a
// temporary copy without its autosave sidecars so the bundle itself is never opened or written.
TEST_F(ChannelFlowTest, DISABLED_LoadTestDeleteUndoProfile) {
    const char* path = std::getenv("PROFILE_PROJECT");
    const auto original = path != nullptr ? juce::File(path)
                                          : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                .getChildFile("AgentSynth/Load test.agsproj");
    const auto copy =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("LoadTestBench", ".agsproj");
    ASSERT_TRUE(original.copyDirectoryTo(copy)) << original.getFullPathName();
    for (const auto& f : copy.findChildFiles(juce::File::findFiles, false, "autosave*.json"))
        f.deleteFile();
    const struct RemoveCopy {
        juce::File dir;
        ~RemoveCopy() { dir.deleteRecursively(); }
    } removeCopy{copy};
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 1000);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(copy));
    pumpMs();
    profileDeleteUndo(mc, original.getFileNameWithoutExtension().toRawUTF8());
}

TEST_F(ChannelFlowTest, DISABLED_ManyTracksProfile) {
    if (const char* n = std::getenv("PROFILE_TRACKS")) {
        profile(std::atoi(n), std::getenv("PROFILE_EXPAND") != nullptr);
        return;
    }
    for (int n : {10, 40, 80})
        for (bool expand : {false, true})
            profile(n, expand);
}
