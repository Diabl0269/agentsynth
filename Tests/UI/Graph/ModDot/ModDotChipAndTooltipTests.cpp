// The mod hover chip never leaves the card (it flips above a knob in the card's last row), and a control whose name is
// already drawn beside it has no tooltip repeating it (docs/modules/modulation.md#modulation-rings-on-knobs).

#include "ModDotTestFixture.h"
#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

using synth::ui::modChipRowRects;

const juce::Rectangle<float> kCard(0.0f, 0.0f, 200.0f, 160.0f);

bool allInside(const std::vector<juce::Rectangle<float>>& rects, juce::Rectangle<float> area) {
    return std::all_of(rects.begin(), rects.end(), [&area](const auto& r) { return area.contains(r); });
}

juce::DrawableButton* drawableButtonNamed(ModuleComponent& card, const juce::String& name) {
    for (auto* child : card.getChildren())
        if (auto* button = dynamic_cast<juce::DrawableButton*>(child); button != nullptr && button->getName() == name)
            return button;
    return nullptr;
}

} // namespace

TEST(ModChipRowRectsTest, TheStackHangsUnderAKnobWithRoomBelow) {
    const juce::Rectangle<float> knob(80.0f, 20.0f, 40.0f, 40.0f);
    const auto rects = modChipRowRects(knob, {70.0f, 70.0f, 70.0f}, kCard);
    ASSERT_EQ(rects.size(), 3u);
    EXPECT_FLOAT_EQ(rects[0].getY(), knob.getBottom() + 2.0f);
    EXPECT_GT(rects[1].getY(), rects[0].getBottom() - 0.01f - 1.0f);
    EXPECT_TRUE(allInside(rects, kCard));
}

// Regression test for FRO1567: a font measured at a fractional width put the box on half pixels, and its 1 px border
// blurred (the Linux font measures differently from the Mac one).
TEST(ModChipRowRectsTest, EveryBoxSitsOnWholePixels) {
    const juce::Rectangle<float> knob(80.3f, 20.6f, 40.0f, 40.0f);
    for (const auto& r : modChipRowRects(knob, {70.4f, 63.7f}, kCard)) {
        EXPECT_FLOAT_EQ(r.getX(), std::round(r.getX()));
        EXPECT_FLOAT_EQ(r.getY(), std::round(r.getY()));
        EXPECT_FLOAT_EQ(r.getWidth(), std::round(r.getWidth()));
        EXPECT_GE(r.getWidth(), 63.7f) << "the text still fits";
    }
}

TEST(ModChipRowRectsTest, AKnobInTheLastRowGetsTheStackAboveIt) {
    const juce::Rectangle<float> knob(80.0f, 100.0f, 40.0f, 50.0f);
    const auto rects = modChipRowRects(knob, {70.0f, 70.0f, 70.0f}, kCard);
    ASSERT_EQ(rects.size(), 3u);
    EXPECT_TRUE(allInside(rects, kCard)) << "every row of three mods is fully visible";
    EXPECT_LE(rects.back().getBottom(), knob.getY()) << "the stack sits above the knob, not over it";
}

TEST(ModChipRowRectsTest, ACardTooShortToFlipPushesTheStackUpToFit) {
    const juce::Rectangle<float> card(0.0f, 0.0f, 200.0f, 70.0f);
    const juce::Rectangle<float> knob(80.0f, 20.0f, 40.0f, 45.0f);
    const auto rects = modChipRowRects(knob, {70.0f, 70.0f, 70.0f}, card);
    EXPECT_TRUE(allInside(rects, card));
}

TEST(ModChipRowRectsTest, ARowStaysInsideTheCardHorizontally) {
    const juce::Rectangle<float> knob(0.0f, 20.0f, 30.0f, 30.0f);
    const auto rects = modChipRowRects(knob, {120.0f}, kCard);
    EXPECT_TRUE(allInside(rects, kCard));
}

TEST_F(ModuleComponentTest, ThreeModsOnAKnobInTheCardsOnlyRowAreAllInsideTheCardOnHover) {
    synth::theme::AppLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    const struct Restore {
        ~Restore() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    } restore;
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    f.addSecondLfo();
    f.addSecondLfo();
    f.editor->setHoveredModTarget(GraphEditor::HoveredModTarget{f.vcaId, f.gainChannel});

    auto state = std::make_unique<juce::DynamicObject>();
    f.vcaCard->addTestModeState(*state);
    const auto* list = state->getProperty("modHoverChipRects").getArray();
    ASSERT_NE(list, nullptr);
    std::vector<juce::Rectangle<float>> rects;
    for (const auto& item : *list)
        rects.push_back(juce::Rectangle<float>::fromString(item.toString()));
    ASSERT_EQ(rects.size(), 3u) << "every mod on the knob is listed";
    EXPECT_TRUE(allInside(rects, f.vcaCard->getLocalBounds().toFloat()))
        << "the stack is not cut off by the card's edge";
}

TEST_F(ModuleComponentTest, AKnobWithItsNameDrawnBesideItShowsNoNameTipButKeepsItsScreenReaderNameAndTooltip) {
    Fixture f;
    ASSERT_NE(f.gainKnob, nullptr);
    using synth::ui::AppTooltipWindow;
    const auto tip = AppTooltipWindow::withoutDrawnName(*f.gainKnob, f.gainKnob->getTooltip());
    EXPECT_TRUE(tip.isEmpty()) << tip;
    EXPECT_TRUE(f.gainKnob->getTooltip().isNotEmpty()) << "the tooltip stays for the coverage rules";
    EXPECT_TRUE(f.gainKnob->getTitle().isNotEmpty());
}

TEST_F(ModuleComponentTest, AControlWithNoVisibleNameKeepsItsTooltip) {
    Fixture f;
    ASSERT_NE(f.vcaCard, nullptr);
    auto* bypass = drawableButtonNamed(*f.vcaCard, "Bypass");
    ASSERT_NE(bypass, nullptr);
    // juce's own getTipFor is empty unless the app is in front (never on a headless runner), so ask the name rule.
    using synth::ui::AppTooltipWindow;
    EXPECT_TRUE(AppTooltipWindow::withoutDrawnName(*bypass, bypass->getTooltip()).isNotEmpty());
}

TEST(AppTooltipWindowDrawnName, ATipKeepsOnlyWhatFollowsTheNameDrawnBesideTheControl) {
    juce::Component c;
    c.getProperties().set(synth::ui::AppTooltipWindow::kNameDrawnBeside, "Gain");
    using synth::ui::AppTooltipWindow;
    EXPECT_EQ(AppTooltipWindow::withoutDrawnName(c, "Gain"), juce::String());
    EXPECT_EQ(AppTooltipWindow::withoutDrawnName(c, "Gain - MIDI: Knob 1"), "MIDI: Knob 1");
    EXPECT_EQ(AppTooltipWindow::withoutDrawnName(c, "Gain\nAutomated: Lane"), "Automated: Lane");
    EXPECT_EQ(AppTooltipWindow::withoutDrawnName(c, "Gainful tip"), "Gainful tip");
    juce::Component plain;
    EXPECT_EQ(AppTooltipWindow::withoutDrawnName(plain, "Gain"), "Gain");
}
