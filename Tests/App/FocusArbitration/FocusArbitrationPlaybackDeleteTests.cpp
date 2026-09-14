// Concern: Space play/stop, panel-local per-surface Delete, and resolveEditSurface() itself
// (override path + panel-visibility fallback + surface actions falling through the global
// handler).
#include "FocusArbitrationTestFixture.h"

// ============================================================================
// 6. Space — global play/stop toggle
// ============================================================================

TEST_F(FocusArbitrationTest, SpaceTogglesPlayback) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    auto& cm = mc.getCommandManager();

    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Works from any surface override — Space is deliberately NOT routed by resolveEditSurface().
    for (auto surface : {MainComponent::EditSurface::Graph, MainComponent::EditSurface::TimelineClips,
                         MainComponent::EditSurface::PianoRoll}) {
        mc.setEditSurfaceOverrideForTest(surface);

        engine.processHostBlock(buffer, midi);
        ASSERT_FALSE(transport.getPositionSnapshot().playing);

        // perform() reuses the transport bar's play/stop button, and juce::Button::triggerClick()
        // always POSTS its click (never fires onClick synchronously) — pump the message loop
        // briefly so it actually runs, the same idiom GraphEditorLayoutTests.cpp's setKnobs() helper uses for a
        // marshalled callback, before draining the resulting transport command with a tick.
        ASSERT_TRUE(cm.invokeDirectly(AppCommands::togglePlayback, false));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        engine.processHostBlock(buffer, midi);
        EXPECT_TRUE(transport.getPositionSnapshot().playing) << "surface " << (int)surface;

        ASSERT_TRUE(cm.invokeDirectly(AppCommands::togglePlayback, false));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        engine.processHostBlock(buffer, midi);
        EXPECT_FALSE(transport.getPositionSnapshot().playing) << "surface " << (int)surface;
    }
}

// ============================================================================
// 7. Delete — panel-local, per surface (already implemented; this pins the routing)
// ============================================================================

TEST_F(FocusArbitrationTest, DeletePerSurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    // A specific node, not selectAllModules() — MainComponent's default patch already has several
    // nodes, and selectAllModules() would select (and later delete) all of them, not just this one.
    auto node = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_NE(node, nullptr);
    editor.setSelectedNodes({node->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 1);
    const int nodesBefore = graph.getNumNodes();

    const juce::KeyPress deleteKey(juce::KeyPress::deleteKey);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    mc.getTimelinePanel().getClipSelection().setSelection({clip1});
    auto& clipLane = mc.getTimelinePanel().getClipLaneArea();

    // A) Clips-focused Delete acts ONLY on the clip selection.
    EXPECT_TRUE(clipLane.keyPressed(deleteKey));
    EXPECT_TRUE(doc.getTrack(trackA)->clips.empty());
    EXPECT_EQ(editor.getSelectionCount(), 1) << "graph selection untouched by a clips-focused Delete";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    // An empty clip selection falls through rather than eating the key.
    EXPECT_FALSE(clipLane.keyPressed(deleteKey));

    // A fresh clip must survive a graph-focused Delete below.
    const auto clip2 = doc.addClip(trackA, 0.0, 4.0, "C2");
    ASSERT_TRUE(clip2.isValid());

    // B) Graph-focused Delete acts ONLY on the graph selection.
    EXPECT_TRUE(editor.keyPressed(deleteKey));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 1);
    EXPECT_NE(doc.getClip(clip2), nullptr) << "clips untouched by a graph-focused Delete";

    // An empty graph selection falls through rather than eating the key.
    EXPECT_FALSE(editor.keyPressed(deleteKey));
}

// ============================================================================
// 8. resolveEditSurface() itself — override path + panel-visibility fallback
// ============================================================================

// A real, OS-tracked keyboard-focus grab requires a native peer (Component::addToDesktop()).
// Nothing in this test suite has ever done that (grabKeyboardFocus()/getCurrentlyFocusedComponent
// have zero prior test usages), and it is a real risk of flakiness/hangs on a headless CI runner
// with no display. Rather than being the first test to try it, this pins the two things
// resolveEditSurface() actually depends on without one:
//   (1) the test-override path every OTHER test in this file relies on as real focus's headless
//       stand-in, across all three EditSurface values;
//   (2) the panel-visibility fallback — the timeline panel starts hidden, and resolveEditSurface()
//       must resolve to Graph regardless of what (if anything) getCurrentlyFocusedComponent()
//       reports, including a best-effort grabKeyboardFocus() call on a component that was never
//       added to the desktop (which may or may not actually move JUCE's static focus pointer in
//       this environment — the assertion holds either way, because isTimelineVisible gates the
//       focus check entirely).
TEST_F(FocusArbitrationTest, SurfaceResolverRealFocus) {
    MainComponent mc(std::make_unique<FocusMockProvider>());

    ASSERT_FALSE(mc.isTimelineConfiguredVisible()) << "the panel starts hidden by default";
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph) << "no override, panel hidden -> Graph";

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::TimelineClips);
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::PianoRoll);
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph);

    mc.setEditSurfaceOverrideForTest(std::nullopt);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "clearing the override falls back to the real-focus resolver, which is Graph here since "
           "the panel is hidden";

    // Best-effort real-focus attempt: the component is never added to the desktop, so this may
    // silently no-op. Either way the panel-visibility gate must win.
    mc.getTimelinePanel().getClipLaneArea().grabKeyboardFocus();
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "a hidden panel never owns the verbs, whatever the focused component is";
}

// ============================================================================
// 9. Surface actions must fall THROUGH the global handler
//
// The shortcut table now holds bare arrows, Q/L/P and the tool digits — rebindable, but resolved by
// the component that owns the key rather than dispatched as commands. MainComponent::keyPressed is
// the last stop for every key, so it has to ignore them: a bare Left that got this far means no
// surface claimed it, and both possible mistakes are silent. Swallowing it (returning true) breaks
// whatever the parent chain would have done next; trying to dispatch it looks up a command that does
// not exist. This is the test that would have caught either.
// ============================================================================

TEST_F(FocusArbitrationTest, BareArrowKeyFallsThroughTheGlobalHandlerUntouched) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& shortcuts = mc.getShortcutManager();

    const juce::KeyPress bareLeft(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);

    // Precondition, and the whole point: the key IS bound — to a surface action with no command. A
    // key that were simply unbound would make the assertion below pass for the wrong reason.
    ASSERT_TRUE(shortcuts.getActionsForKeyPress(bareLeft).contains("pianoRollNudgeLeft"));
    ASSERT_EQ(AppCommands::getCommandForAction("pianoRollNudgeLeft"), AppCommands::kNoCommand);

    auto node = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_NE(node, nullptr);
    editor.setSelectedNodes({node->nodeID});
    const int nodesBefore = graph.getNumNodes();
    const int selectionBefore = editor.getSelectionCount();

    EXPECT_FALSE(mc.keyPressed(bareLeft)) << "an unclaimed surface key must fall through, not be swallowed";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "nothing was invoked";
    EXPECT_EQ(editor.getSelectionCount(), selectionBefore) << "and nothing was cleared either";

    // Same for the other five piano-roll arrows and for the bare tool digits, so a future rename
    // cannot leave one of them dispatching.
    for (const auto& kp : {juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0),
                           juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0),
                           juce::KeyPress('3', juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0)})
        EXPECT_FALSE(mc.keyPressed(kp)) << ShortcutManager::keyPressToDisplayString(kp).toStdString();
}
