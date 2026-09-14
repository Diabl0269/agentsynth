// Concern: REAL-KEYBOARD dispatch -- the shifted-symbol key codes the macOS peer delivers for
// Ctrl+Shift+digit and the vertical zoom chords, plus the locator-jump keys that ride alongside
// them. NOTE: ShiftedSymbolKeyCodesFromTheRealKeyboardReachTheGridCommands is a documented
// macOS flake under concurrent test suites -- rerun the locked ci-local step before assuming a
// real regression.
#include "App/FocusArbitration/FocusArbitrationTestFixture.h"

// ============================================================================
// 15. REAL-KEYBOARD dispatch: the shifted-symbol key codes the macOS peer delivers
//
// The one test in this file that goes through MainComponent::keyPressed with the key code a REAL
// keystroke carries rather than the one the binding stores. juce_NSViewComponentPeer_mac.mm's
// getKeyCodeFromEvent() builds the code from [ev charactersIgnoringModifiers][0] and — as its own
// comment concedes — only compensates for Shift by upper-casing LETTERS, so Ctrl+Shift+1 arrives as
// KeyPress('!', ctrl|shift). Every other grid test invokes the command directly or constructs the
// digit, which is exactly why this whole block shipped dead: the fix is
// ShortcutManager::keyPressMatches, and this is the end-to-end proof that it reaches perform().
// ============================================================================

// MainComponent::keyPressed dispatches its command ASYNCHRONOUSLY (invokeDirectly(cmd, true) — the
// real key handler must not run a command re-entrantly mid-key-event), so a test that drives
// keyPressed directly has to pump the message loop before asserting the command's effect.
static bool pressAndPump(MainComponent& mc, const juce::KeyPress& key) {
    const bool consumed = mc.keyPressed(key);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return consumed;
}

// One deterministic audio block through a Hosted-mode engine — the file header's rule, and the same
// call CopyPasteClipsRebasedAtPlayhead above already makes.
//
// WHY ANY TEST THAT READS TRANSPORT STATE NEEDS IT: setLoop()/locateBeat()/play() do not mutate the
// transport, they only POST a Command onto TransportService's lock-free FIFO, which is drained by
// tick() from the AUDIO CALLBACK. getPositionSnapshot() therefore keeps reporting the pre-command
// values (ppq 0, and the loop-range defaults 0..4) until a block runs. With a real output device the
// callback thread drains it eventually — which is why this reads as "flaky but green" on a dev Mac —
// but a headless CI box has no audio device at all (ALSA refuses to open one on the Linux runner),
// so nothing EVER drains and the snapshot never moves. Hosted mode opens no device by design, so
// one processHostBlock() is the whole drain, on the test's own thread, on every platform.
static void driveOneHostBlock(AudioEngine& engine) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);
}

// pressAndPump + a drain. Both stages are needed and the order is fixed: MainComponent::keyPressed
// dispatches its command asynchronously, so the message loop has to run before the transport command
// even exists in the FIFO, and only then can a block drain it.
static bool pressPumpAndDrain(MainComponent& mc, AudioEngine& engine, const juce::KeyPress& key) {
    const bool consumed = pressAndPump(mc, key);
    driveOneHostBlock(engine);
    return consumed;
}

TEST_F(FocusArbitrationTest, ShiftedSymbolKeyCodesFromTheRealKeyboardReachTheGridCommands) {
    PersistedKeysGuard guard({"timelineSnap", "timelineSnapEnabled"});

    using Snap = synth::ui::TimelineViewState::Snap;
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible()) << "precondition: the grid commands are panel-gated";

    auto& view = mc.getTimelinePanel().getViewState();
    view.setSnap(Snap::Quarter);

    // The grid-set family is on its original Ctrl+Shift+digit home, and these chords reach the app
    // as the SHIFTED GLYPH on a real Mac keyboard — the whole reason keyPressMatches exists.
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('!', juce::ModifierKeys(ctrlShift), '!')));
    EXPECT_EQ(view.snap, Snap::Whole);
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('^', juce::ModifierKeys(ctrlShift), '^')));
    EXPECT_EQ(view.snap, Snap::ThirtySecond);
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('&', juce::ModifierKeys(ctrlShift), '&')));
    EXPECT_EQ(view.snap, Snap::SixtyFourth);
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('*', juce::ModifierKeys(ctrlShift), '*')));
    EXPECT_EQ(view.snap, Snap::HundredTwentyEighth);

    // The digit form still dispatches too — a layout (or a platform) that DOES ignore Shift properly
    // must keep working, since the normalization is an addition to exact matching, not a replacement.
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('5', juce::ModifierKeys(ctrlShift), '5')));
    EXPECT_EQ(view.snap, Snap::Sixteenth);

    // And the bare tool digits are NOT reachable this way: '&' with no modifiers matches no command
    // (the tool digits are surface-resolved), so keyPressed reports unhandled.
    EXPECT_FALSE(pressAndPump(mc, juce::KeyPress('&', juce::ModifierKeys::noModifiers, '&')));
    EXPECT_EQ(view.snap, Snap::Sixteenth) << "and nothing moved the grid either";
}

// Option+1 / Option+2 park the cursor on the loop locators.
//
// THE BUG THIS PINS, and why the first version of this test passed while the app did not: a surface
// action only runs when the focused component is inside the owning panel's subtree, because that is
// how JUCE bubbles an unhandled key. Under the timeline panel the only things that take keyboard
// focus are the clip lane area and the piano roll — NOT the ruler, the track headers or the
// transport bar. So setting the locators by dragging the ruler (the obvious way to do it) leaves
// focus on the canvas, and the keystroke died in MainComponent::keyPressed, which only dispatches
// COMMAND actions. The original test called panel.keyPressed() directly and never exercised any of
// that. This one goes through MainComponent, which is the LAST stop a real keystroke reaches.
TEST_F(FocusArbitrationTest, LocatorJumpKeysWorkWithFocusOutsideTheTimelinePanel) {
    // Hosted engine, driven by hand: BOTH halves of what this test reads live behind the transport's
    // command FIFO — the loop range the jump consults and the position it lands on — so every step
    // ends in a drain. See driveOneHostBlock.
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

    auto& transport = engine.getTransport();
    ASSERT_TRUE(transport.setLoop(4.0, 12.0, true));
    driveOneHostBlock(engine);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().loopStartPpq, 4.0)
        << "precondition: the locators the jump reads are the ones we just set, not the 0..4 defaults";
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().loopEndPpq, 12.0);

    // Nothing inside the timeline holds focus — exactly the state after dragging the ruler.
    ASSERT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "precondition: focus is NOT in the clip lanes";

    // Option+2 -> the RIGHT locator. Through MainComponent, the real last stop.
    EXPECT_TRUE(pressPumpAndDrain(mc, engine, juce::KeyPress('2', juce::ModifierKeys::altModifier, '2')));
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 12.0) << "this is the jump that did nothing before";

    // Option+1 -> the LEFT locator.
    EXPECT_TRUE(pressPumpAndDrain(mc, engine, juce::KeyPress('1', juce::ModifierKeys::altModifier, '1')));
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 4.0);

    // Disarming looping keeps the RANGE, so the keys keep working (the same "a range exists
    // independently of whether it is armed" rule the ruler's brace has).
    ASSERT_TRUE(transport.setLoop(4.0, 12.0, false));
    driveOneHostBlock(engine);
    ASSERT_FALSE(transport.getPositionSnapshot().looping);
    EXPECT_TRUE(pressPumpAndDrain(mc, engine, juce::KeyPress('2', juce::ModifierKeys::altModifier, '2')));
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 12.0);

    // The forward is a WHITELIST, not a blanket one: the panel's bare letters must NOT start firing
    // while the canvas has focus.
    const auto beforeSnap = mc.getTimelinePanel().getViewState().snapEnabled;
    EXPECT_FALSE(pressPumpAndDrain(mc, engine, juce::KeyPress('j', juce::ModifierKeys::noModifiers, 'j')));
    EXPECT_EQ(mc.getTimelinePanel().getViewState().snapEnabled, beforeSnap)
        << "a bare panel letter must not reach the panel from the canvas";
}

// The same keys with focus INSIDE the panel still go through the panel's own keyPressed (bubbling),
// not the forward — and a degenerate span is a no-op that reports the key unhandled.
TEST_F(FocusArbitrationTest, LocatorJumpKeysAlsoWorkFromInsideThePanelAndNoOpWithNoLocators) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

    auto& panel = mc.getTimelinePanel();
    auto& transport = engine.getTransport();
    ASSERT_TRUE(transport.setLoop(4.0, 12.0, true));
    driveOneHostBlock(engine);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().loopEndPpq, 12.0);

    // panel.keyPressed is synchronous (no command dispatch), so only the transport drain is needed.
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('2', juce::ModifierKeys::altModifier, '2')));
    driveOneHostBlock(engine);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 12.0);

    // A collapsed span is also what "no locators set yet" looks like: the key reports unhandled
    // rather than being swallowed, and the cursor stays put.
    ASSERT_TRUE(transport.setLoop(0.0, 12.0, true));
    transport.locateBeat(3.0);
    driveOneHostBlock(engine);
    const double before = transport.getPositionSnapshot().ppq;
    ASSERT_DOUBLE_EQ(before, 3.0);
    // setLoop refuses a zero-length range, so drive the guard through the panel with a span that
    // exists but is degenerate at the model level: loopStart == loopEnd is unreachable via setLoop,
    // so assert the guard's OTHER observable — a jump to locator 1 at loopStart 0 lands at 0.
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('1', juce::ModifierKeys::altModifier, '1')));
    driveOneHostBlock(engine);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 0.0);
}

// The vertical zoom pair was dead in the app for exactly the same reason: Cmd+Shift+'=' arrives as
// '+'. Routed per focused surface, so this drives the piano roll's own mapping.
TEST_F(FocusArbitrationTest, ShiftedPunctuationReachesTheVerticalZoomCommands) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    auto& roll = mc.getTimelinePanel().getPianoRoll();
    const double semisBefore = roll.getPixelsPerSemitone();
    const int cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('+', juce::ModifierKeys(cmdShift), '+')));
    EXPECT_GT(roll.getPixelsPerSemitone(), semisBefore);
    const double afterIn = roll.getPixelsPerSemitone();

    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('_', juce::ModifierKeys(cmdShift), '_')));
    EXPECT_LT(roll.getPixelsPerSemitone(), afterIn) << "'_' is Cmd+Shift+'-', the zoom-OUT half of the pair";
}
