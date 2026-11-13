// The two-handle RangeSlider: real mouse drags of each handle, handles that never cross, one-step keys, the per-handle
// spoken value, and every built-in theme rendered to a PNG strip for review.
#include "UI/Controls/RangeSlider/RangeSlider.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::RangeSlider;

namespace {

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

// A real mouse event on `target` at `local` (in target's own coordinates), the way the component tree would deliver it.
juce::MouseEvent mouseAt(juce::Component& target, juce::Point<int> local, juce::Point<int> downLocal = {-1, -1}) {
    const auto at = local.toFloat();
    const auto down = downLocal.x < 0 ? at : downLocal.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &target, &target, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1,
                            false);
}

struct Fixture {
    RangeSlider slider;
    Fixture() {
        slider.setBounds(0, 0, 240, RangeSlider::kPreferredHeight);
        slider.setLimits(60, 200, 1);
        slider.setLabel("Tempo", "BPM");
        slider.setValues(90, 100);
        slider.finishMotionForTest();
    }
    // Drags `which` from where it is to the x of `value`, with mouseDown, drags and mouseUp on the handle itself.
    void drag(RangeSlider::Handle which, double value, bool throughTheEnd = true) {
        auto& handle = slider.getHandle(which);
        const auto start = handle.getLocalBounds().getCentre();
        handle.mouseDown(mouseAt(handle, start));
        const float targetX = slider.getWidth() > 0 ? targetXFor(value) : 0.0f;
        const auto inSlider = juce::Point<float>(targetX, (float)slider.getHeight() * 0.5f);
        const auto local = handle.getLocalPoint(&slider, inSlider);
        handle.mouseDrag(mouseAt(handle, local.roundToInt(), start));
        if (throughTheEnd)
            handle.mouseUp(mouseAt(handle, local.roundToInt(), start));
    }
    float targetXFor(double value) const {
        const float pad = 9.0f;
        const float t = (float)((value - 60.0) / 140.0);
        return pad + t * ((float)slider.getWidth() - 2.0f * pad);
    }
};

} // namespace

TEST(RangeSliderTest, DraggingTheLowerHandleMovesOnlyThatHandle) {
    Fixture f;
    double lo = 0, hi = 0;
    int calls = 0;
    f.slider.onChange = [&](double l, double h) {
        lo = l;
        hi = h;
        ++calls;
    };
    f.drag(RangeSlider::Handle::Lower, 75);
    EXPECT_NEAR(f.slider.getLower(), 75.0, 1.0);
    EXPECT_EQ(f.slider.getUpper(), 100.0);
    EXPECT_GE(calls, 1);
    EXPECT_EQ(lo, f.slider.getLower());
    EXPECT_EQ(hi, 100.0);
}

TEST(RangeSliderTest, DraggingTheUpperHandleMovesOnlyThatHandle) {
    Fixture f;
    f.drag(RangeSlider::Handle::Upper, 150);
    EXPECT_NEAR(f.slider.getUpper(), 150.0, 1.0);
    EXPECT_EQ(f.slider.getLower(), 90.0);
}

TEST(RangeSliderTest, HandlesNeverCross) {
    Fixture f;
    f.drag(RangeSlider::Handle::Lower, 190); // dragged far past the upper handle
    EXPECT_EQ(f.slider.getLower(), 100.0);
    EXPECT_EQ(f.slider.getUpper(), 100.0);
    // Both sit on 100: dragging the pair left takes the lower one, and the upper stays where it was.
    f.drag(RangeSlider::Handle::Upper, 62);
    EXPECT_LE(f.slider.getLower(), f.slider.getUpper());
    EXPECT_NEAR(f.slider.getLower(), 62.0, 1.0);
    EXPECT_EQ(f.slider.getUpper(), 100.0);
}

TEST(RangeSliderTest, TwoHandlesOnOneSpotCanBothBeDraggedAway) {
    Fixture f;
    f.slider.setValues(120, 120);
    f.slider.finishMotionForTest();
    f.drag(RangeSlider::Handle::Upper, 80); // the top handle is pulled left: it is the lower one that moves
    EXPECT_NEAR(f.slider.getLower(), 80.0, 1.0);
    EXPECT_EQ(f.slider.getUpper(), 120.0);
}

TEST(RangeSliderTest, ArrowsMoveTheFocusedHandleOneStep) {
    Fixture f;
    auto& lower = f.slider.getHandle(RangeSlider::Handle::Lower);
    auto& upper = f.slider.getHandle(RangeSlider::Handle::Upper);
    EXPECT_TRUE(lower.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.slider.getLower(), 91.0);
    EXPECT_TRUE(lower.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_TRUE(lower.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.slider.getLower(), 89.0);
    EXPECT_TRUE(upper.keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.slider.getUpper(), 101.0);
    EXPECT_TRUE(upper.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.slider.getUpper(), 100.0);
    EXPECT_EQ(f.slider.getLower(), 89.0);
}

TEST(RangeSliderTest, KeysStopAtTheLimitsAndAtTheOtherHandle) {
    Fixture f;
    auto& lower = f.slider.getHandle(RangeSlider::Handle::Lower);
    auto& upper = f.slider.getHandle(RangeSlider::Handle::Upper);
    EXPECT_TRUE(lower.keyPressed(key(juce::KeyPress::homeKey)));
    EXPECT_EQ(f.slider.getLower(), 60.0);
    EXPECT_TRUE(lower.keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(f.slider.getLower(), 100.0) << "the lower handle cannot pass the upper one";
    EXPECT_TRUE(upper.keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(f.slider.getUpper(), 200.0);
    EXPECT_TRUE(upper.keyPressed(key(juce::KeyPress::pageDownKey)));
    EXPECT_EQ(f.slider.getUpper(), 190.0);
    EXPECT_FALSE(lower.keyPressed(key(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier)));
}

TEST(RangeSliderTest, EachHandleSpeaksItsOwnNameAndValue) {
    Fixture f;
    auto& lower = f.slider.getHandle(RangeSlider::Handle::Lower);
    auto& upper = f.slider.getHandle(RangeSlider::Handle::Upper);
    EXPECT_EQ(lower.getTitle(), "Tempo, lower handle");
    EXPECT_EQ(upper.getTitle(), "Tempo, upper handle");
    EXPECT_EQ(f.slider.spokenValue(RangeSlider::Handle::Lower), "90 BPM");
    EXPECT_EQ(f.slider.spokenValue(RangeSlider::Handle::Upper), "100 BPM");
    EXPECT_TRUE(lower.getWantsKeyboardFocus());
    EXPECT_TRUE(upper.getWantsKeyboardFocus());
    EXPECT_NE(dynamic_cast<juce::TooltipClient*>(&lower)->getTooltip(), "");
    f.slider.textForValue = [](double v) { return juce::String(v / 10.0, 1) + " tens"; };
    EXPECT_EQ(f.slider.spokenValue(RangeSlider::Handle::Lower), "9.0 tens");
}

TEST(RangeSliderTest, ProgrammaticValuesDoNotNotifyAndAreRoundedAndOrdered) {
    Fixture f;
    int calls = 0;
    f.slider.onChange = [&](double, double) { ++calls; };
    f.slider.setValues(150.4, 70.6);
    EXPECT_EQ(f.slider.getLower(), 71.0);
    EXPECT_EQ(f.slider.getUpper(), 150.0);
    f.slider.setValues(10, 500);
    EXPECT_EQ(f.slider.getLower(), 60.0);
    EXPECT_EQ(f.slider.getUpper(), 200.0);
    EXPECT_EQ(calls, 0);
}

TEST(RangeSliderTest, ClickingTheTrackMovesTheNearerHandle) {
    Fixture f;
    f.slider.mouseDown(mouseAt(f.slider, {(int)f.targetXFor(70), 13}));
    f.slider.finishMotionForTest();
    EXPECT_NEAR(f.slider.getLower(), 70.0, 1.0);
    EXPECT_EQ(f.slider.getUpper(), 100.0);
    f.slider.mouseDown(mouseAt(f.slider, {(int)f.targetXFor(180), 13}));
    f.slider.finishMotionForTest();
    EXPECT_NEAR(f.slider.getUpper(), 180.0, 1.0);
}

TEST(RangeSliderTest, EveryThemeRendersAPngStrip) {
    const auto dir = juce::File(juce::SystemStats::getEnvironmentVariable("RANGE_SLIDER_THEME_STRIP_DIR", {}));
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        RangeSlider slider;
        slider.setLookAndFeel(&laf);
        slider.setBounds(0, 0, 240, RangeSlider::kPreferredHeight);
        slider.setLimits(60, 200, 1);
        slider.setValues(90, 140);
        slider.finishMotionForTest();
        auto image = slider.createComponentSnapshot(slider.getLocalBounds(), false, 2.0f);
        ASSERT_FALSE(image.isNull());
        // Not blank: the filled range uses the accent, which differs from the background.
        bool varied = false;
        const auto first = image.getPixelAt(0, 0);
        for (int x = 0; x < image.getWidth() && !varied; ++x)
            varied = image.getPixelAt(x, image.getHeight() / 2) != first;
        EXPECT_TRUE(varied) << theme.name;
        if (dir.getFullPathName().isNotEmpty()) {
            dir.createDirectory();
            juce::FileOutputStream out(dir.getChildFile("range-slider-" + theme.id + ".png"));
            if (out.openedOk()) {
                out.setPosition(0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream(image, out);
            }
        }
        slider.setLookAndFeel(nullptr);
    }
}
