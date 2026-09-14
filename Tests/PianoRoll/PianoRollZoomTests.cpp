// PianoRoll wheel/zoom tests: the macOS Shift axis swap, the natural-vs-inverted scroll sign on
// both scroll axes, the anchored zoomHorizontal/zoomVertical commands, the wheel-zoom DIRECTION
// convention (wheel UP — the physical gesture, independent of isReversed — zooms IN by default;
// setZoomScrollInverted flips it), and SNAP vs the drawn grid: the chosen division stays visible
// with snap off. Shared PianoRollFixture and wheelOnY live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

// ============================================================================
// 10. Wheel policy (ScrollPolicy.h) and the anchored zoom commands
// ============================================================================

namespace {

// The macOS axis swap, reproduced exactly: the OS moves a Shift-held wheel gesture into deltaX and
// leaves deltaY at 0. Any branch that reads deltaY alone receives nothing at all.
juce::MouseWheelDetails wheelOnX(float deltaX) {
    juce::MouseWheelDetails wheel{}; // value-initialised: the struct has no default member initialisers
    wheel.deltaX = deltaX;
    wheel.deltaY = 0.0f;
    return wheel;
}

} // namespace

// THE regression this fixes: Cmd+Shift+wheel was reading wheel.deltaY, which macOS zeroes under
// Shift, so the vertical zoom was dead on the platform it was written on. dominantWheelDelta picks
// up whichever axis the gesture landed on.
TEST(PianoRollWheelTest, CmdShiftWheelZoomsVerticallyEvenWhenTheGestureArrivesOnDeltaX) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheelOnX(0.4f));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "reading deltaY alone here would have been a no-op";

    // And the opposite gesture cancels it exactly (the factor is exponential in the delta).
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), wheelOnX(-0.4f));
    EXPECT_NEAR(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone, 1.0e-9);

    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "a vertical zoom never touches the shared view state";
}

// Same robustness for the horizontal-zoom branch: it is chosen by the modifier, so it must not care
// which axis carried the gesture either.
TEST(PianoRollWheelTest, CmdWheelZoomsHorizontallyEvenWhenTheGestureArrivesOnDeltaX) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);

    const float anchorX = 300.0f;
    const double beatUnderCursor = f.roll.xToBeat((double)anchorX);
    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, juce::ModifierKeys::commandModifier), wheelOnX(0.5f));

    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0);
    EXPECT_NEAR(f.roll.xToBeat((double)anchorX), beatUnderCursor, 1.0e-9)
        << "still anchored on the beat under the cursor";
}

// Plain wheel = pitch scroll. Natural (the default) matches what a juce::Viewport does with the same
// gesture: firstVisiblePitch_ is the TOP row's pitch, so scrolling towards the top of the content
// RAISES it. setScrollInverted flips exactly that, and nothing else.
TEST(PianoRollWheelTest, PitchScrollFollowsTheGestureByDefaultAndFlipsWhenInverted) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_FALSE(f.roll.isScrollInverted()) << "natural is the default";

    // Distances are asserted in PIXELS via yForPitch, not via getFirstVisiblePitchForTest: the
    // scroll position is continuous now (topRowPosition_), and the derived legacy int floors it,
    // so a half-row landing quantizes asymmetrically around an integral start by construction —
    // the symmetry claim only holds (and only matters) for the real, continuous mapping.
    const int base = f.roll.getFirstVisiblePitchForTest();
    const int yBase = f.roll.yForPitch(60);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int yNatural = f.roll.yForPitch(60);
    EXPECT_GT(f.roll.getFirstVisiblePitchForTest(), base) << "a +deltaY gesture scrolls towards HIGHER pitches";
    EXPECT_GT(yNatural, yBase) << "so a fixed pitch's row moves DOWN the screen";

    // Undo it, then run the identical gesture inverted: it must land the same distance the other way.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(-0.5f));
    ASSERT_EQ(f.roll.yForPitch(60), yBase);

    f.roll.setScrollInverted(true);
    EXPECT_TRUE(f.roll.isScrollInverted());
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.5f));
    const int yInverted = f.roll.yForPitch(60);
    EXPECT_LT(yInverted, yBase) << "the same gesture now scrolls the other way";
    EXPECT_EQ(yBase - yInverted, yNatural - yBase) << "and by exactly the same number of pixels";
}

// THE regression: a trackpad's small deltaY (often 0.01-0.1 per event) scaled by
// kPitchScrollSemitonesPerWheelUnit rounds to ZERO whole rows on almost every individual event; a
// plain per-event round dropped the gesture entirely unless one event happened to be big enough to
// clear a row on its own — "only sometimes works". topRowPosition_ (a continuous double — see the
// class comment) simply never rounds at all, so N small events land on exactly the same
// (fractional) position ONE big event of the summed delta would, and hence on the same DERIVED
// firstVisiblePitch_ row.
TEST(PianoRollWheelTest, SmallPitchScrollEventsAccumulateToTheSameRowCountAsOneBigEvent) {
    PianoRollFixture accumulated;
    const auto trackA = accumulated.doc.addTrack(TrackKind::Midi, "Track 1");
    accumulated.open(accumulated.doc.addClip(trackA, 0.0, 8.0, "Clip"));
    const int base = accumulated.roll.getFirstVisiblePitchForTest();

    // Five small events, each individually below what a single-event round would register as a
    // whole row (0.1 * kPitchScrollSemitonesPerWheelUnit == 0.3 rows -- truncates to 0 alone).
    for (int i = 0; i < 5; ++i)
        accumulated.roll.mouseWheelMove(leftClick(accumulated.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    const int afterSmallEvents = accumulated.roll.getFirstVisiblePitchForTest();
    EXPECT_GT(afterSmallEvents, base) << "the gesture must not be dropped just because no single event cleared a row";

    PianoRollFixture oneBig;
    const auto trackB = oneBig.doc.addTrack(TrackKind::Midi, "Track 1");
    oneBig.open(oneBig.doc.addClip(trackB, 0.0, 8.0, "Clip"));
    ASSERT_EQ(oneBig.roll.getFirstVisiblePitchForTest(), base) << "test premise: identical starting state";
    oneBig.roll.mouseWheelMove(leftClick(oneBig.roll, {300.0f, 90.0f}),
                               wheelOnY(0.5f)); // the SAME total delta (5 * 0.1)
    EXPECT_EQ(afterSmallEvents, oneBig.roll.getFirstVisiblePitchForTest())
        << "five small events summing to 0.5 must land on exactly the same row as one 0.5 event";
}

// THE actual smoothness fix (as opposed to the "lost small deltas" fix above): before this, the
// wheel could DROP a small gesture, but even the fixed ("carry the remainder") version still left
// firstVisiblePitch_ — the only state yForPitch read — completely UNMOVED while a fraction
// accumulated, so the grid still visibly snapped in whole ~10px rows. Now topRowPosition_ itself is
// what moves, so yForPitch of a FIXED pitch takes a proportional, sub-row PIXEL step on every single
// event, matching the horizontal axis (rollView_.firstVisibleBeat).
TEST(PianoRollWheelTest, SmallPitchScrollEventsMoveYSmoothlyBelowARow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    const int fixedPitch = f.roll.getFirstVisiblePitchForTest() - 5; // comfortably inside the grid
    int previousY = f.roll.yForPitch(fixedPitch);
    // 0.05 * kPitchScrollSemitonesPerWheelUnit (3.0) == 0.15 rows/event -- well under one row, but
    // (at the default 10px/row) still more than a pixel, so llround can never tie two consecutive
    // events to the same y (see the .cpp derivation this test pins).
    for (int i = 0; i < 5; ++i) {
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.05f));
        const int y = f.roll.yForPitch(fixedPitch);
        EXPECT_GT(y, previousY) << "event " << i
                                << ": the grid must move a little on EVERY event, never sit "
                                   "frozen until a whole row accumulates";
        previousY = y;
    }
}

TEST(PianoRollWheelTest, PitchScrollAccumulatorCarriesTheFractionalRemainderAcrossEvents) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const int base = f.roll.getFirstVisiblePitchForTest();

    // 0.1 * kPitchScrollSemitonesPerWheelUnit (3.0) == 0.3 rows/event -- three events sum to 0.9
    // (still under a whole row), a FOURTH tips it over 1.0. firstVisiblePitch_ itself still only
    // ever reports a WHOLE row (it is derived from floor(topRowPosition_) — see the class comment),
    // so this whole-row-crossing behaviour is unchanged even though there is no longer a separate
    // pitchScrollRemainder_ member: topRowPosition_'s own fractional part now plays that role.
    for (int i = 0; i < 3; ++i) {
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
        EXPECT_EQ(f.roll.getFirstVisiblePitchForTest(), base) << "event " << i << ": still under one row's worth";
    }
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    EXPECT_GT(f.roll.getFirstVisiblePitchForTest(), base)
        << "the 4th event's carried fraction finally clears a whole row";
}

TEST(PianoRollWheelTest, PitchScrollFractionResetsAcrossAClipSwitch) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = f.doc.addClip(trackId, 0.0, 8.0, "A");
    const auto clipB = f.doc.addClip(trackId, 8.0, 8.0, "B");
    f.open(clipA);

    // Leave clip A's topRowPosition_ at a 0.9-row fraction -- one event short of clearing a row on
    // its own (see the previous test). If this carried into clip B unreset, a SINGLE further 0.1
    // event there (0.3 more) would tip it over 1.0 and move a row; openClip resets it to a whole
    // row (see its own comment), so it stays well under.
    for (int i = 0; i < 3; ++i)
        f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));

    f.open(clipB);
    const int baseB = f.roll.getFirstVisiblePitchForTest();
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnY(0.1f));
    EXPECT_EQ(f.roll.getFirstVisiblePitchForTest(), baseB)
        << "clip A's pending 0.9-row fraction must not surface in clip B";
}

// Shift+wheel (and a trackpad's own deltaX) = horizontal scroll, through the roll's OWN scroll
// origin. Same natural-by-default / invert-on-request contract as the pitch axis.
TEST(PianoRollWheelTest, HorizontalScrollFollowsTheGestureByDefaultAndFlipsWhenInverted) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 16.0, "Clip");
    f.open(clipId); // 40 px/beat, beat 4 at the gutter
    ASSERT_DOUBLE_EQ(f.roll.getFirstVisibleBeat(), 4.0);

    // 200 px per wheel unit at 40 px/beat -> 5 beats per unit, so half a unit is 2.5 beats.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheelOnY(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 6.5, 1.0e-9) << "a -delta gesture scrolls the view RIGHT";
    EXPECT_DOUBLE_EQ(f.state.firstVisibleBeat, 0.0) << "the lanes behind the roll keep their own scroll";

    f.roll.setScrollInverted(true);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, juce::ModifierKeys::shiftModifier), wheelOnY(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 4.0, 1.0e-9) << "the same gesture now scrolls the view LEFT";

    // A bare trackpad deltaX (no Shift at all) is the same branch and obeys the same flag.
    f.roll.setScrollInverted(false);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnX(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 6.5, 1.0e-9);
    f.roll.setScrollInverted(true);
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}), wheelOnX(-0.5f));
    EXPECT_NEAR(f.roll.getFirstVisibleBeat(), 4.0, 1.0e-9);
}

// zoomHorizontal is the command-friendly form of the Cmd+wheel zoom: same anchored math, anchored on
// the view centre instead of the cursor.
TEST(PianoRollZoomApiTest, ZoomHorizontalKeepsTheCentreBeatAndClampsAtBothEnds) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 32.0, "Clip");
    f.open(clipId, /*pixelsPerBeat*/ 40.0);

    // 900 px wide, 44 of them the keys gutter -> the grid's centre sits at x == 44 + 428.
    const double centreX = (double)PianoRollComponent::kKeysColumnWidth + (900.0 - 44.0) * 0.5;
    const double centreBeat = f.roll.xToBeat(centreX);

    int viewChanges = 0;
    f.roll.onHorizontalViewChanged = [&] { ++viewChanges; };

    f.roll.zoomHorizontal(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 80.0);
    EXPECT_NEAR(f.roll.xToBeat(centreX), centreBeat, 1.0e-9) << "the centre beat is the zoom's fixed point";
    EXPECT_GT(viewChanges, 0) << "the ruler mirrors the roll's mapping, so it has to be told";

    f.roll.zoomHorizontal(0.5);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0) << "equal-and-opposite factors cancel";
    EXPECT_NEAR(f.roll.xToBeat(centreX), centreBeat, 1.0e-9);

    // A factor that cannot mean anything is ignored rather than clamped to something.
    f.roll.zoomHorizontal(0.0);
    f.roll.zoomHorizontal(-2.0);
    f.roll.zoomHorizontal(std::nan(""));
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), 40.0);

    for (int i = 0; i < 20; ++i)
        f.roll.zoomHorizontal(4.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), TimelineViewState::kMaxPixelsPerBeat);

    for (int i = 0; i < 40; ++i)
        f.roll.zoomHorizontal(0.25);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerBeat(), TimelineViewState::kMinPixelsPerBeat);
}

TEST(PianoRollZoomApiTest, ZoomVerticalKeepsTheCentrePitchAndClampsAtBothEnds) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    // 160 px tall, 20 of them the header -> the grid's centre row sits at y == 20 + 70.
    const int centreY = f.roll.canvasTop() + (160 - f.roll.canvasTop()) / 2;
    const int centrePitch = f.roll.pitchForY(centreY);

    f.roll.zoomVertical(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), 20.0);
    // topRowPosition_ (the continuous anchor zoomVerticalAroundY actually moves) is never rounded
    // to a whole row mid-zoom any more, so the centre pitch now stays EXACTLY put rather than
    // merely "within one row".
    EXPECT_EQ(f.roll.pitchForY(centreY), centrePitch) << "the centre pitch stays put, exactly";

    f.roll.zoomVertical(0.0);
    f.roll.zoomVertical(-2.0);
    f.roll.zoomVertical(std::nan(""));
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), 20.0) << "a meaningless factor is ignored";

    for (int i = 0; i < 20; ++i)
        f.roll.zoomVertical(2.0);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMaxPixelsPerSemitone);

    for (int i = 0; i < 40; ++i)
        f.roll.zoomVertical(0.5);
    EXPECT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kMinPixelsPerSemitone);

    EXPECT_DOUBLE_EQ(f.state.pixelsPerBeat, 40.0) << "vertical zoom is the roll's alone";
    EXPECT_FALSE(f.undo.canUndo()) << "every zoom here is view-only";
}

// The case the OLD int-only firstVisiblePitch_ could never even represent: the anchor is
// FRACTIONAL (mid-row) BEFORE the zoom starts, via the Cmd+Shift+wheel branch — the same public
// path a real gesture takes, not a direct call into the private zoomVerticalAroundY.
TEST(PianoRollZoomApiTest, CmdShiftWheelZoomKeepsTheAnchoredYFixedEvenFromAFractionalStart) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);

    f.roll.setTopRowPositionForTest(std::floor(f.roll.getTopRowPositionForTest()) + 0.5);
    ASSERT_DOUBLE_EQ(f.roll.getTopRowPositionForTest(), std::floor(f.roll.getTopRowPositionForTest()) + 0.5);

    const int anchorY = f.roll.canvasTop() + 37; // an arbitrary point inside the grid
    const int pitchBefore = f.roll.pitchForY(anchorY);

    const int mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.4f;
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, (float)anchorY}, mods), wheel);

    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);
    EXPECT_EQ(f.roll.pitchForY(anchorY), pitchBefore)
        << "the row under the cursor stays exactly put, even though the pre-zoom anchor was fractional";
}

// ============================================================================
// 11. Wheel-zoom DIRECTION convention: UP (the PHYSICAL gesture) zooms IN by default
// ============================================================================
// wheelGestureIsUpward (ScrollPolicy.h) recovers the physical direction from `isReversed` XOR the
// delta's sign, so "wheel up zooms in" must read identically whichever way the OS's natural-
// scrolling setting has pre-flipped the delta. These tests exercise BOTH isReversed encodings of
// "up" and "down" against both wheel-zoom branches, setZoomScrollInverted_'s effect, and that the
// resulting factor depends only on |delta| and physical direction — never on isReversed itself.
//
// The section 10 axis-swap tests above (CmdWheelZoomsAroundTheCursorBeat,
// CmdShiftWheelZoomsPitchRowsWithinClamps, and the two …EvenWhenTheGestureArrivesOnDeltaX tests) all
// construct their wheels with isReversed left at its value-initialised `false`, so their expected
// GT/LT directions are unaffected by this change (wheelGestureIsUpward(false, +delta) ==
// (dominantWheelDelta(wheel) > 0), the same sign the old raw-delta code read) — they are left as
// they were, deliberately not touched here.

namespace {
// Builds a wheel gesture for the PHYSICAL direction `up`, under a given isReversed encoding — the
// same algebra wheelGestureIsUpward itself runs: isReversed==false needs a POSITIVE delta to read as
// "up"; isReversed==true needs a NEGATIVE one. Letting a test pick `up` directly (rather than a raw
// delta sign) is the point: two calls with the same `up` and different `isReversed` must be
// answered identically by mouseWheelMove, which is exactly what these tests check.
juce::MouseWheelDetails physicalWheelGesture(float magnitude, bool up, bool isReversed) {
    juce::MouseWheelDetails wheel{};               // value-initialised: the struct has no default member initialisers
    const bool positiveDelta = (up != isReversed); // XOR
    wheel.deltaY = positiveDelta ? magnitude : -magnitude;
    wheel.isReversed = isReversed;
    return wheel;
}
} // namespace

TEST(PianoRollWheelZoomDirectionTest, HorizontalWheelUpZoomsInAndDownZoomsOutRegardlessOfIsReversed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    const auto mods = juce::ModifierKeys::commandModifier;
    const float anchorX = 300.0f;

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, false));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "up, isReversed=false (+delta) zooms in";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, true));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "up, isReversed=true (-delta) also zooms in — same physical gesture";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ false, false));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "down, isReversed=false (-delta) zooms out";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {anchorX, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ false, true));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "down, isReversed=true (+delta) also zooms out";
}

TEST(PianoRollWheelZoomDirectionTest, VerticalWheelUpZoomsInAndDownZoomsOutRegardlessOfIsReversed) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_DOUBLE_EQ(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone);
    const auto mods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ true, false));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "up, isReversed=false zooms in";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ true, true));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "up, isReversed=true also zooms in";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ false, false));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "down, isReversed=false zooms out";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.4f, /*up*/ false, true));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone)
        << "down, isReversed=true also zooms out";
}

// setZoomScrollInverted is the zoom-only preference, independent of setScrollInverted (which the
// PitchScrollFollowsTheGestureByDefaultAndFlipsWhenInverted / HorizontalScrollFollowsThe…
// tests above cover for the plain-scroll branches) — it flips BOTH wheel-zoom branches together.
TEST(PianoRollWheelZoomDirectionTest, SetZoomScrollInvertedFlipsBothWheelZoomBranches) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_FALSE(f.roll.isZoomScrollInverted()) << "natural (up zooms in) is the default";
    f.roll.setZoomScrollInverted(true);
    EXPECT_TRUE(f.roll.isZoomScrollInverted());

    const auto cmdMods = juce::ModifierKeys::commandModifier;
    const auto cmdShiftMods = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    // Horizontal (Cmd+wheel): physical up now zooms OUT, down now zooms IN.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdMods), physicalWheelGesture(0.5f, /*up*/ true, false));
    EXPECT_LT(f.roll.getPixelsPerBeat(), 40.0) << "inverted: up zooms out";
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdMods), physicalWheelGesture(0.5f, /*up*/ false, false));
    EXPECT_GT(f.roll.getPixelsPerBeat(), 40.0) << "inverted: down zooms in";
    f.roll.setHorizontalView(40.0, 4.0);

    // Vertical (Cmd+Shift+wheel): the same flip, so the two branches never disagree.
    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdShiftMods),
                          physicalWheelGesture(0.4f, /*up*/ true, false));
    EXPECT_LT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "inverted: up zooms out";
    f.roll.setPixelsPerSemitone(PianoRollComponent::kPixelsPerSemitone);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, cmdShiftMods),
                          physicalWheelGesture(0.4f, /*up*/ false, false));
    EXPECT_GT(f.roll.getPixelsPerSemitone(), PianoRollComponent::kPixelsPerSemitone) << "inverted: down zooms in";
}

// The zoom factor must depend on |delta| and physical direction alone — never on isReversed's own
// value, which is only a SIGN-recovery input, not a second source of magnitude or direction.
TEST(PianoRollWheelZoomDirectionTest, MagnitudeMatchesRegardlessOfIsReversedSignForTheSamePhysicalGesture) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 4.0, 8.0, "Clip");
    f.open(clipId);
    const auto mods = juce::ModifierKeys::commandModifier;

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, false));
    const double afterNatural = f.roll.getPixelsPerBeat();
    f.roll.setHorizontalView(40.0, 4.0);

    f.roll.mouseWheelMove(leftClick(f.roll, {300.0f, 90.0f}, mods), physicalWheelGesture(0.5f, /*up*/ true, true));
    const double afterReversed = f.roll.getPixelsPerBeat();

    EXPECT_DOUBLE_EQ(afterNatural, afterReversed)
        << "same physical gesture, same |delta| -> the exact same zoom factor regardless of isReversed";
}

// ============================================================================
// 24. SNAP vs the DRAWN grid (3.3): the chosen division stays visible with snap off.
// ============================================================================

TEST(PianoRollGridVisibilityTest, TurningSnapOffKeepsEveryGridlineButStopsMagnetism) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);
    // A division FINER than a beat, so there is a subdivision level to lose in the first place.
    f.state.snap = TimelineViewState::Snap::Sixteenth;
    f.state.snapEnabled = true;

    const double division = f.roll.getDrawnGridDivisionForTest();
    ASSERT_GT(division, 0.0);
    ASSERT_LT(division, 1.0) << "the bug only shows on a sub-beat level";
    const int linesWithSnapOn = f.roll.getGridLineCountForTest(division);
    ASSERT_GT(linesWithSnapOn, 0);

    f.roll.toggleSnap();
    ASSERT_FALSE(f.state.snapEnabled);

    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), division) << "the DRAWN division is snap-independent";
    EXPECT_EQ(f.roll.getGridLineCountForTest(division), linesWithSnapOn)
        << "turning snap off must not erase a single gridline";
    // Magnetism, and only magnetism, went away.
    EXPECT_DOUBLE_EQ(f.roll.getGridDivisionForTest(), 0.0);
}

// Snap::Off is different from "snap switched off": no division is CHOSEN, so there genuinely is no
// sub-beat level to draw. The beat and bar levels are unconditional and stay.
TEST(PianoRollGridVisibilityTest, SnapOffAsADivisionChoiceHasNoSubBeatLevelAtAll) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    f.state.snap = TimelineViewState::Snap::Off;
    EXPECT_DOUBLE_EQ(f.roll.getDrawnGridDivisionForTest(), 0.0);
    EXPECT_GT(f.roll.getGridLineCountForTest(1.0), 0) << "beat lines are unconditional";
}
