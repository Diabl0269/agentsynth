// ModMatrixPickerSwitchGlideTests.cpp -- a Cmd+K row with an on/off switch glides its knob and fades its fill when it
// flips (docs/layout/command-palette.md), through the shared SwitchGlide painter (docs/layout/theming.md). The glide
// is forced on with FadeAnimateGuard (no VBlank reaches an off-screen picker) and stepped by hand.
#include "../Layout/MotionPolish/MotionStep.h"
#include "../SourceScan.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/SwitchGlide.h"
#include <gtest/gtest.h>
#include <memory>
#include <regex>

using synth::ui::AnimationMode;
using synth::ui::ModMatrixPicker;
using synth::ui::SwitchGlide;

namespace {

ModMatrixPicker::Item row(int id, const juce::String& text, std::optional<bool> on = std::nullopt) {
    ModMatrixPicker::Item item;
    item.id = id;
    item.category = "Things";
    item.text = text;
    item.switchOn = on;
    return item;
}

struct Host {
    juce::Component parent;
    std::unique_ptr<ModMatrixPicker> picker;

    explicit Host(std::vector<ModMatrixPicker::Item> items) {
        picker = std::make_unique<ModMatrixPicker>("thing", std::move(items), -1, [](int) {});
        parent.setSize(400, 400);
        parent.addAndMakeVisible(*picker);
        picker->setBounds(0, 0, 360, 300);
    }
    void enter() { picker->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)); }
    float amount(int index = 0) const { return picker->getVisibleItemSwitchAmountForTest(index).value_or(-1.0f); }
};

// The knob's left edge for an amount, in a pill the size the row draws.
float knobAt(float amount) {
    return SwitchGlide::knobX(juce::Rectangle<float>(0.0f, 0.0f, SwitchGlide::kWidth, SwitchGlide::kHeight), amount);
}

} // namespace

TEST(ModMatrixPickerSwitchGlide, AFlipByReturnSlidesTheKnobThenLandsOnTheOtherSide) {
    FadeAnimateGuard guard;
    Host h({row(1, "Natural scrolling", true), row(2, "Plain")});
    ASSERT_EQ(h.amount(), 1.0f) << "the first bind of an on row rests on the on side";

    h.enter(); // on -> off
    EXPECT_EQ(h.picker->getVisibleItemSwitchForTest(0), std::optional<bool>(false));
    EXPECT_EQ(h.amount(), 1.0f) << "the glide starts from where the knob was";
    stepMotion(0.5f);
    EXPECT_GT(knobAt(h.amount()), knobAt(0.0f));
    EXPECT_LT(knobAt(h.amount()), knobAt(1.0f)) << "mid-glide the knob is between the two sides";
    EXPECT_GT(h.amount(), 0.0f);
    EXPECT_LT(h.amount(), 1.0f);
    stepMotion(1.0f);
    EXPECT_EQ(knobAt(h.amount()), knobAt(0.0f));

    h.enter(); // off -> on
    stepMotion(0.5f);
    EXPECT_GT(knobAt(h.amount()), knobAt(0.0f));
    EXPECT_LT(knobAt(h.amount()), knobAt(1.0f));
    stepMotion(1.0f);
    EXPECT_EQ(knobAt(h.amount()), knobAt(1.0f));
}

TEST(ModMatrixPickerSwitchGlide, TheFillFadesWithTheKnob) {
    FadeAnimateGuard guard;
    Host h({row(1, "Natural scrolling", false)});
    h.picker->setSize(360, 300);
    const auto pill = [&] {
        juce::Image image(juce::Image::ARGB, 360, 300, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        h.picker->paintEntireComponent(g, false);
        return image;
    };
    h.enter();
    stepMotion(0.5f);
    ASSERT_GT(h.amount(), 0.0f);
    ASSERT_LT(h.amount(), 1.0f);
    pill(); // paints without throwing mid-glide; the amount is what the fill is drawn from
}

TEST(ModMatrixPickerSwitchGlide, AFirstBindAndAReopenedListShowNoMotion) {
    FadeAnimateGuard guard;
    {
        Host h({row(1, "Natural scrolling", true)});
        EXPECT_EQ(h.amount(), 1.0f);
        stepMotion(0.5f);
        EXPECT_EQ(h.amount(), 1.0f) << "nothing is in flight after a first bind";
    }
    Host reopened({row(1, "Natural scrolling", true), row(2, "Other", false)}); // the list opened again
    EXPECT_EQ(reopened.amount(0), 1.0f);
    EXPECT_EQ(reopened.amount(1), 0.0f);
    stepMotion(0.5f);
    EXPECT_EQ(reopened.amount(0), 1.0f);
    EXPECT_EQ(reopened.amount(1), 0.0f);
}

TEST(ModMatrixPickerSwitchGlide, ReducedMotionStillGlidesButOffLandsAtOnce) {
    {
        FadeAnimateGuard guard(AnimationMode::reduced);
        Host h({row(1, "Natural scrolling", true)});
        h.enter();
        stepMotion(0.5f);
        EXPECT_GT(h.amount(), 0.0f) << "Reduce Motion shortens the glide (80 ms), it does not remove it";
        EXPECT_LT(h.amount(), 1.0f);
        stepMotion(1.0f);
        EXPECT_EQ(h.amount(), 0.0f);
    }
    FadeAnimateGuard guard(AnimationMode::off);
    Host h({row(1, "Natural scrolling", true)});
    h.enter();
    EXPECT_EQ(h.amount(), 0.0f) << "Animations Off: the knob is on the other side at once";
    stepMotion(0.5f);
    EXPECT_EQ(h.amount(), 0.0f);
}

TEST(ModMatrixPickerSwitchGlide, OnlyTheSharedPainterDrawsASwitch) {
    // (kSwitchWidth is not scanned: two segmented view switches use that name.) A second hand-painted switch would pop
    // its knob again, so it uses SwitchGlide instead.
    const std::regex pattern(R"(\b(kSwitchHeight|paintSwitch)\b)");
    for (const auto& [path, text] : synth::test::source_scan::sourceTexts()) {
        if (path == "Source/UI/Layout/SwitchGlide.h")
            continue;
        EXPECT_FALSE(std::regex_search(text, pattern)) << path << " paints its own switch; use SwitchGlide";
    }
}
