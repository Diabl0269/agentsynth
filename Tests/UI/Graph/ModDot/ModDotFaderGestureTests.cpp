// The mod dot of the VCA's Gain as its designed default draws it, a horizontal fader: every dot gesture a knob's dot
// has, with each event delivered to whichever component sits under the painted dot on the canvas (not to the fader
// directly). docs/layout/module-card.md#modulation-dot.

#include "ModDotTestFixture.h"

#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

struct FaderDot : Fixture {
    FaderDot()
        : Fixture(/*modulated*/ true, /*gainAsKnob*/ false, /*designedLayout*/ true) {}

    juce::Component* canvas() { return editor->getChildComponent(0); }
    juce::Point<int> dotOnCanvas() {
        return canvas()->getLocalPoint(vcaCard, vcaCard->getModTargetKnobAnchor(gainChannel)->roundToInt());
    }
    // The component a real press at `p` (canvas space) reaches, and the event as it receives it.
    juce::Component* under(juce::Point<int> p) { return canvas()->getComponentAt(p); }
    juce::MouseEvent eventAt(juce::Component& c, juce::Point<int> canvasPoint, int clicks = 1) {
        return makeModuleClickWithMods(c, c.getLocalPoint(canvas(), canvasPoint), kPlain, clicks);
    }
};

} // namespace

TEST_F(ModuleComponentTest, TheDefaultGainIsAHorizontalFader) {
    FaderDot f;
    auto* fader = dynamic_cast<synth::ui::CardFader*>(f.gainKnob);
    ASSERT_NE(fader, nullptr);
    EXPECT_FALSE(fader->isVerticalFader());
}

TEST_F(ModuleComponentTest, ClickingTheDotOfTheGainFaderOpensItsPanel) {
    FaderDot f;
    ASSERT_NE(f.gainKnob, nullptr);
    const auto p = f.dotOnCanvas();
    auto* hit = f.under(p);
    ASSERT_NE(hit, nullptr);
    hit->mouseDown(f.eventAt(*hit, p));
    hit->mouseUp(f.eventAt(*hit, p));
    EXPECT_NE(f.popover(), nullptr);
}

TEST_F(ModuleComponentTest, DraggingTheDotOfTheGainFaderChangesTheAmountAndEscapeRestoresIt) {
    FaderDot f;
    ASSERT_NE(f.gainKnob, nullptr);
    const double gainBefore = f.gainKnob->getValue();
    const auto p = f.dotOnCanvas();
    auto* hit = f.under(p);
    ASSERT_NE(hit, nullptr);

    hit->mouseDown(f.eventAt(*hit, p));
    hit->mouseDrag(f.eventAt(*hit, p + juce::Point<int>(0, -30)));
    EXPECT_NEAR(f.amount(f.attenId), 0.8f, 0.005f);
    EXPECT_DOUBLE_EQ(f.gainKnob->getValue(), gainBefore);

    EXPECT_TRUE(hit->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    hit->mouseUp(f.eventAt(*hit, p + juce::Point<int>(0, -30)));
    EXPECT_FALSE(f.undo.canUndo());
}

TEST_F(ModuleComponentTest, DoubleClickingTheDotOfTheGainFaderRemovesItsOnlySource) {
    synth::ui::setReducedMotionForTest(true);
    FaderDot f;
    ASSERT_NE(f.gainKnob, nullptr);
    const double gainBefore = f.gainKnob->getValue();
    const auto p = f.dotOnCanvas();
    auto* hit = f.under(p);
    ASSERT_NE(hit, nullptr);
    hit->mouseDown(f.eventAt(*hit, p));
    hit->mouseUp(f.eventAt(*hit, p));

    hit->mouseDown(f.eventAt(*hit, p, 2));
    hit->mouseUp(f.eventAt(*hit, p, 2));

    EXPECT_TRUE(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel, true).empty());
    EXPECT_EQ(f.gainKnob->getValue(), gainBefore) << "the dot's double-click does not reset the fader";
    synth::ui::setReducedMotionForTest(std::nullopt);
}
