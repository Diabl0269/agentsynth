// MixerFaderDragTests.cpp -- FRO150 (docs/mixer_fader.md): MixerFaderSlider's own mouse/wheel
// conventions, driven through its REAL mouseDown/mouseDrag/mouseUp/mouseDoubleClick/mouseWheelMove
// overrides with synthesized juce::MouseEvents (docs/testing.md's "test the real mouse path"
// convention, MacroPortRealMouseDragTests.cpp's template) -- never juce::Slider's own internal
// drag machinery, which MixerFaderSlider.h's class comment explains this class deliberately never
// calls (MixerFaderTests.cpp already found that hangs CI for a stock juce::Slider).
//
// Every expected value below is computed through the SAME faderDbToFraction/faderFractionToDb
// pair MixerFaderSlider itself uses (MixerFaderTaper.h), not hand-derived pixel arithmetic --
// MixerFaderTaperTests.cpp already pins the taper's own breakpoints, so these tests are free to
// treat the taper as a black box and focus purely on the drag/wheel/reset MECHANICS.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerFader.h"
#include "UI/Mixer/MixerFaderTaper.h"
#include <gtest/gtest.h>

using namespace synth::ui;

namespace {

// Builds a real ChannelStripModule + bound MixerFader, sized so the slider's own track is exactly
// 184 px tall (fader.setSize(24, 200) minus the 16 px readout MixerFader::resized() reserves).
struct MixerFaderDragFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    juce::AudioProcessorGraph::Node::Ptr node;
    juce::AudioParameterFloat* gainParam = nullptr;
    MixerFader fader;

    MixerFaderDragFixture() {
        engine.getGraph().setPlayConfigDetails(0, 2, 44100.0, 512);
        auto processor = std::make_unique<ChannelStripModule>();
        processor->setShape(ChannelStripModule::Shape::Stereo);
        node = engine.getGraph().addNode(std::move(processor));
        for (auto* param : node->getProcessor()->getParameters())
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(param); f != nullptr && f->paramID == "gain")
                gainParam = f;
        fader.setSize(24, 200);
        fader.bind(engine.getGraph(), undoManager, *gainParam);
    }

    /** Directly sets the param's dB value with NO gesture bracket -- so it never creates undo
     *  history, letting a test start a fader gesture from a known, undo-stack-clean dB value. */
    void setGainDbDirectly(float db) { gainParam->setValueNotifyingHost(gainParam->convertTo0to1(db)); }

    juce::Slider& slider() { return fader.getSlider(); }

    /** The real thumb-travel height MixerFaderSlider::mouseDrag/mouseWheelMove divide by -- read
     *  through the SAME juce::LookAndFeel call the production code makes
     *  (getSliderLayout().sliderBounds.getHeight()), not a hardcoded pixel count, so expected
     *  values below stay correct under whatever LookAndFeel resolves in a headless test. It is
     *  SMALLER than slider().getHeight() itself: getSliderLayout() already reduces the bounds by
     *  the thumb's own radius on each end (LookAndFeel_V2::getSliderLayout's
     *  `sliderBounds.reduce(0, thumbIndent)`), exactly like juce::Slider's own
     *  Pimpl::resized() computes sliderRegionSize. */
    double trackLengthPx() {
        auto& s = slider();
        return (double)s.getLookAndFeel().getSliderLayout(s).sliderBounds.getHeight();
    }
};

juce::MouseEvent faderMouseEvent(juce::Component& comp, juce::Point<float> pos, juce::Point<float> mouseDownPos,
                                 juce::ModifierKeys mods, bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

constexpr int kNoModifiers = juce::ModifierKeys::noModifiers;

} // namespace

TEST(MixerFaderDragTest, PlainDragMovesValueAlongTheTaperFromTheAnchor) {
    MixerFaderDragFixture f;
    // Near, not bit-exact: the initial value reaches the slider through a convertTo0to1/
    // convertFrom0to1 round-trip through the taper (attachment_'s sendInitialUpdate()), which
    // isn't guaranteed bit-identical for 0.0 -- far below anything audible or visible.
    ASSERT_NEAR(f.slider().getValue(), 0.0, 1.0e-5);
    ASSERT_EQ(f.slider().getHeight(), 184);

    const juce::Point<float> downPos(12.0f, 100.0f);
    f.slider().mouseDown(faderMouseEvent(f.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));

    const juce::Point<float> dragPos(12.0f, 82.0f); // 18 px UP -> value increases
    f.slider().mouseDrag(faderMouseEvent(f.slider(), dragPos, downPos, juce::ModifierKeys(kNoModifiers), true));

    const double expectedProportion =
        juce::jlimit(0.0, 1.0, (double)faderDbToFraction(0.0f) + 18.0 / f.trackLengthPx());
    const double expectedDb = (double)faderFractionToDb((float)expectedProportion);
    EXPECT_NEAR(f.slider().getValue(), expectedDb, 0.1);
    EXPECT_NEAR(f.gainParam->get(), (float)expectedDb, 0.1f) << "the bound parameter must track the slider";

    f.slider().mouseUp(faderMouseEvent(f.slider(), dragPos, downPos, juce::ModifierKeys(kNoModifiers), true));
}

TEST(MixerFaderDragTest, SamePixelDragMovesMoreDbNearTheBottomThanNearUnity) {
    // Near the bottom: anchor at -55 dB.
    MixerFaderDragFixture bottom;
    bottom.setGainDbDirectly(-55.0f);
    const juce::Point<float> downPos(12.0f, 150.0f);
    bottom.slider().mouseDown(
        faderMouseEvent(bottom.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));
    const juce::Point<float> dragPos(12.0f, 132.0f); // 18 px up
    bottom.slider().mouseDrag(
        faderMouseEvent(bottom.slider(), dragPos, downPos, juce::ModifierKeys(kNoModifiers), true));
    const double bottomDeltaDb = bottom.slider().getValue() - (-55.0);

    // Near unity: anchor at 0 dB.
    MixerFaderDragFixture unity;
    unity.slider().mouseDown(
        faderMouseEvent(unity.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));
    unity.slider().mouseDrag(faderMouseEvent(unity.slider(), dragPos, downPos, juce::ModifierKeys(kNoModifiers), true));
    const double unityDeltaDb = unity.slider().getValue() - 0.0;

    EXPECT_GT(bottomDeltaDb, unityDeltaDb) << "the same 18 px must move MORE dB near the bottom of the taper";
}

TEST(MixerFaderDragTest, ShiftDragMovesAboutOneEighthAsFarAsPlainDrag) {
    MixerFaderDragFixture plain;
    const juce::Point<float> downPos(12.0f, 100.0f);
    plain.slider().mouseDown(
        faderMouseEvent(plain.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));
    const juce::Point<float> dragPos(12.0f, 82.0f);
    plain.slider().mouseDrag(faderMouseEvent(plain.slider(), dragPos, downPos, juce::ModifierKeys(kNoModifiers), true));
    const double plainDeltaDb = plain.slider().getValue() - 0.0;

    MixerFaderDragFixture fine;
    fine.slider().mouseDown(
        faderMouseEvent(fine.slider(), downPos, downPos, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), false));
    fine.slider().mouseDrag(
        faderMouseEvent(fine.slider(), dragPos, downPos, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), true));
    const double fineDeltaDb = fine.slider().getValue() - 0.0;

    ASSERT_GT(plainDeltaDb, 0.0);
    EXPECT_NEAR(fineDeltaDb / plainDeltaDb, 1.0 / 8.0, 0.03) << "Shift-drag must move ~1/8 as far as a plain drag";
}

TEST(MixerFaderDragTest, TogglingShiftMidDragNeverJumpsTheValue) {
    MixerFaderDragFixture f;
    const juce::Point<float> downPos(12.0f, 100.0f);
    f.slider().mouseDown(faderMouseEvent(f.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));

    const juce::Point<float> midPos(12.0f, 82.0f); // 18 px up, no Shift yet
    f.slider().mouseDrag(faderMouseEvent(f.slider(), midPos, downPos, juce::ModifierKeys(kNoModifiers), true));
    const double valueBeforeShift = f.slider().getValue();

    // Shift is now pressed with the mouse held at the EXACT SAME position -- must not move the
    // value at all (the ticket's own "no jump" requirement).
    f.slider().mouseDrag(
        faderMouseEvent(f.slider(), midPos, downPos, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), true));
    EXPECT_NEAR(f.slider().getValue(), valueBeforeShift, 1.0e-6)
        << "toggling Shift with no mouse movement must not change the value";

    // Continuing to drag under Shift now moves at the fine rate, relative to THIS anchor.
    const juce::Point<float> laterPos(12.0f, 64.0f); // another 18 px up, Shift held
    f.slider().mouseDrag(
        faderMouseEvent(f.slider(), laterPos, downPos, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), true));
    const double anchorProportion = faderDbToFraction((float)valueBeforeShift);
    const double expectedProportion = juce::jlimit(0.0, 1.0, anchorProportion + (18.0 / f.trackLengthPx()) / 8.0);
    const double expectedDb = (double)faderFractionToDb((float)expectedProportion);
    EXPECT_NEAR(f.slider().getValue(), expectedDb, 0.1);

    // Releasing Shift again, still with no mouse movement, must again not jump.
    const double valueBeforeRelease = f.slider().getValue();
    f.slider().mouseDrag(faderMouseEvent(f.slider(), laterPos, downPos, juce::ModifierKeys(kNoModifiers), true));
    EXPECT_NEAR(f.slider().getValue(), valueBeforeRelease, 1.0e-6)
        << "releasing Shift with no mouse movement must not change the value";

    f.slider().mouseUp(faderMouseEvent(f.slider(), laterPos, downPos, juce::ModifierKeys(kNoModifiers), true));
}

TEST(MixerFaderDragTest, WholeDragCollapsesToOneUndoStep) {
    MixerFaderDragFixture f;
    const float before = f.gainParam->get();

    const juce::Point<float> downPos(12.0f, 100.0f);
    f.slider().mouseDown(faderMouseEvent(f.slider(), downPos, downPos, juce::ModifierKeys(kNoModifiers), false));
    f.slider().mouseDrag(
        faderMouseEvent(f.slider(), juce::Point<float>(12.0f, 90.0f), downPos, juce::ModifierKeys(kNoModifiers), true));
    f.slider().mouseDrag(
        faderMouseEvent(f.slider(), juce::Point<float>(12.0f, 70.0f), downPos, juce::ModifierKeys(kNoModifiers), true));
    f.slider().mouseDrag(
        faderMouseEvent(f.slider(), juce::Point<float>(12.0f, 60.0f), downPos, juce::ModifierKeys(kNoModifiers), true));
    f.slider().mouseUp(
        faderMouseEvent(f.slider(), juce::Point<float>(12.0f, 60.0f), downPos, juce::ModifierKeys(kNoModifiers), true));

    EXPECT_NE(f.gainParam->get(), before);
    ASSERT_TRUE(f.undoManager.canUndo());
    ASSERT_TRUE(f.undoManager.undo());
    EXPECT_NEAR(f.gainParam->get(), before, 1.0e-3f);
    EXPECT_FALSE(f.undoManager.canUndo()) << "the whole drag (3 mouseDrag calls) must collapse to ONE undo step";
}

TEST(MixerFaderDragTest, CommandClickResetsToZeroAsOneUndoStep) {
    MixerFaderDragFixture f;
    f.setGainDbDirectly(-20.0f);
    ASSERT_FALSE(f.undoManager.canUndo()) << "the direct set above must not have created undo history";

    const juce::Point<float> pos(12.0f, 100.0f);
    f.slider().mouseDown(
        faderMouseEvent(f.slider(), pos, pos, juce::ModifierKeys(juce::ModifierKeys::commandModifier), false));

    EXPECT_NEAR(f.gainParam->get(), 0.0f, 1.0e-3f);
    ASSERT_TRUE(f.undoManager.canUndo());
    ASSERT_TRUE(f.undoManager.undo());
    EXPECT_NEAR(f.gainParam->get(), -20.0f, 1.0e-2f);
    EXPECT_FALSE(f.undoManager.canUndo()) << "Cmd-click reset must be exactly ONE undo step";
}

TEST(MixerFaderDragTest, DoubleClickResetsToZero) {
    MixerFaderDragFixture f;
    f.setGainDbDirectly(7.5f);
    ASSERT_FALSE(f.undoManager.canUndo());

    const juce::Point<float> pos(12.0f, 100.0f);
    f.slider().mouseDoubleClick(faderMouseEvent(f.slider(), pos, pos, juce::ModifierKeys(kNoModifiers), false));

    EXPECT_NEAR(f.gainParam->get(), 0.0f, 1.0e-3f);
    ASSERT_TRUE(f.undoManager.canUndo());
    ASSERT_TRUE(f.undoManager.undo());
    EXPECT_NEAR(f.gainParam->get(), 7.5f, 1.0e-2f);
    EXPECT_FALSE(f.undoManager.canUndo()) << "double-click reset must be exactly ONE undo step";
}

TEST(MixerFaderDragTest, ShiftWheelMovesLessThanPlainWheel) {
    MixerFaderDragFixture plain;
    const juce::Point<float> pos(12.0f, 100.0f);
    plain.slider().mouseWheelMove(faderMouseEvent(plain.slider(), pos, pos, juce::ModifierKeys(kNoModifiers), false),
                                  juce::MouseWheelDetails{0.0f, 1.0f, false, false, false});
    const double plainDelta = plain.slider().getValue() - 0.0;
    ASSERT_GT(plainDelta, 0.0);

    MixerFaderDragFixture fine;
    fine.slider().mouseWheelMove(
        faderMouseEvent(fine.slider(), pos, pos, juce::ModifierKeys(juce::ModifierKeys::shiftModifier), false),
        juce::MouseWheelDetails{0.0f, 1.0f, false, false, false});
    const double fineDelta = fine.slider().getValue() - 0.0;

    EXPECT_GT(fineDelta, 0.0);
    EXPECT_LT(fineDelta, plainDelta) << "Shift+wheel must move a smaller step than a plain wheel notch";
}

TEST(MixerFaderDragTest, ManySmallWheelDeltasAccumulateReasonablyLikeATrackpad) {
    MixerFaderDragFixture f;
    const juce::Point<float> pos(12.0f, 100.0f);
    // A two-finger trackpad scroll delivers many SMALL-deltaY events per gesture, not one big
    // notch -- simulate 20 of them. An earlier version stepped a full 1 dB on every event
    // regardless of magnitude, which flew this exact sequence tens of dB in one gesture; scaling
    // by deltaY (mouseWheelMove's kWheelProportionSensitivity) must keep it modest instead.
    for (int i = 0; i < 20; ++i) {
        f.slider().mouseWheelMove(faderMouseEvent(f.slider(), pos, pos, juce::ModifierKeys(kNoModifiers), false),
                                  juce::MouseWheelDetails{0.0f, 0.02f, false, false, false});
    }
    EXPECT_GT(f.slider().getValue(), 0.0) << "small notches must still move the value forward";
    EXPECT_LT(f.slider().getValue(), 6.0) << "20 small trackpad-style notches must not fly across the whole range";
}

TEST(MixerFaderDragTest, EveryNonZeroWheelNotchMovesAtLeastOneGridStep) {
    MixerFaderDragFixture f;
    const juce::Point<float> pos(12.0f, 100.0f);
    const double before = f.slider().getValue();
    // A deltaY tiny enough that its scaled, taper-mapped raw delta is under one 0.1 dB grid step
    // must still move SOMETHING -- otherwise snapToLegalValue rounds the target straight back to
    // the value it started from and a real (if small) wheel notch does nothing at all.
    f.slider().mouseWheelMove(faderMouseEvent(f.slider(), pos, pos, juce::ModifierKeys(kNoModifiers), false),
                              juce::MouseWheelDetails{0.0f, 0.001f, false, false, false});
    EXPECT_GT(f.slider().getValue(), before) << "even a tiny wheel notch must move at least one 0.1 dB grid step";
}
