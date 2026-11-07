// One readout per mod dot gesture: while a mod dot drag or key step shows its "LFO 1 . +42%" readout (the chip under
// the knob), no hover tooltip of the knob (or anything the pointer passes over) shows beside it, and the knob's own
// hover tooltip is back as soon as the readout goes. A real AppTooltipWindow decides what a hover would show.

#include "ModDotTestFixture.h"
#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

using synth::ui::AppTooltipWindow;

// juce gives no hover tip outside the foreground app, and a headless Linux test process never is. A hidden automation
// process counts as foreground, so this lets the real tip path run on every platform.
struct CountsAsForeground {
    CountsAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = true; }
    ~CountsAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = previous; }
    const bool previous = juce::ComponentPeer::hideWindowsForAutomation;
};

// How many readouts the user sees now: the mod dot's own, plus every hover tip the tooltip window would show for the
// components the pointer is over or passes over during the gesture (the knob the dot sits on, the dot's Tab-stop
// button, and the card around them).
int visibleReadouts(Fixture& f, AppTooltipWindow& window) {
    const int readout = f.editor->getModDot().getTooltip().isShown() ? 1 : 0;
    bool hoverTip = false; // the pointer is over one component at a time, so any hover tip is one tooltip
    for (juce::Component* over : {static_cast<juce::Component*>(f.gainKnob),
                                  static_cast<juce::Component*>(f.vcaCard->getModDotButton(f.gainChannel)),
                                  static_cast<juce::Component*>(f.vcaCard)})
        if (over != nullptr && window.getTipFor(*over).isNotEmpty())
            hoverTip = true;
    return readout + (hoverTip ? 1 : 0);
}

} // namespace

// Regression test for FRO1189: a second tooltip (the knob's hover tip) showed under the knob next to the mod dot's
// readout while the dot was dragged.
TEST_F(ModuleComponentTest, DraggingTheDotShowsExactlyOneReadoutAndTheKnobsOwnTipComesBack) {
    CountsAsForeground foreground;
    Fixture f;
    f.gainKnob->setTooltip("Scales the signal"); // the caption already names the knob, so give it a tip of its own
    ASSERT_NE(f.gainKnob, nullptr);
    AppTooltipWindow window(f.editor.get());
    ASSERT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty()) << "the knob has a hover tip when nothing else is up";
    EXPECT_EQ(visibleReadouts(f, window), 1) << "only the knob's hover tip before the gesture";

    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    for (const int dy : {-4, -12, 6, -30}) {
        f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, dy), kPlain));
        EXPECT_EQ(visibleReadouts(f, window), 1) << "drag step " << dy;
    }
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));

    EXPECT_FALSE(f.editor->getModDot().getTooltip().isShown());
    EXPECT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty()) << "the knob's normal hover tooltip appears afterwards";
}

namespace {
// The chip rows the card reports through test-mode state.
std::vector<juce::String> chipRows(ModuleComponent& card) {
    juce::DynamicObject state;
    card.addTestModeState(state);
    std::vector<juce::String> rows;
    if (const auto* list = state.getProperty("modHoverChipRows").getArray())
        for (const auto& row : *list)
            rows.push_back(row.toString());
    return rows;
}
} // namespace

// The dot's readout is the chip under the knob: it shows without the pointer resting on the knob, with the dragged
// source's live amount, and it is gone when the readout is.
TEST_F(ModuleComponentTest, TheDotReadoutIsTheChipUnderTheKnobWithTheLiveAmount) {
    Fixture f;
    f.gainKnob->setTooltip("Scales the signal"); // the caption already names the knob, so give it a tip of its own
    ASSERT_NE(f.gainKnob, nullptr);
    EXPECT_FALSE(synth::ui::modHoverChipShows(*f.editor, f.vcaId));
    EXPECT_TRUE(chipRows(*f.vcaCard).empty());

    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    EXPECT_FALSE(synth::ui::modHoverChipShows(*f.editor, f.vcaId)) << "a press alone starts no readout";
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -20), kPlain));
    ASSERT_TRUE(f.editor->getModDot().getTooltip().isShown());
    EXPECT_TRUE(synth::ui::modHoverChipShows(*f.editor, f.vcaId));
    const auto rows = chipRows(*f.vcaCard);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0], f.editor->getModDot().getTooltip().getText());
    EXPECT_TRUE(rows[0].contains("+70%")) << rows[0];

    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    EXPECT_FALSE(synth::ui::modHoverChipShows(*f.editor, f.vcaId)) << "the readout has gone";

    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_TRUE(synth::ui::modHoverChipShows(*f.editor, f.vcaId)) << "a key step shows the chip too";
    EXPECT_EQ(chipRows(*f.vcaCard).size(), 1u);
}

// With two sources on the knob the dragged one is the live row; hovering lists both either way.
TEST_F(ModuleComponentTest, HoveringAKnobListsEveryModulationOnItAndADragKeepsTheOthers) {
    Fixture f;
    f.gainKnob->setTooltip("Scales the signal"); // the caption already names the knob, so give it a tip of its own
    ASSERT_NE(f.gainKnob, nullptr);
    f.addSecondLfo();
    f.editor->setHoveredModTarget(graph_editor_types::HoveredModTarget{f.vcaId, f.gainChannel});
    const auto hovered = chipRows(*f.vcaCard);
    ASSERT_EQ(hovered.size(), 2u);
    EXPECT_TRUE(hovered[0].contains("+50%")) << hovered[0];
    EXPECT_TRUE(hovered[1].contains("-25%")) << hovered[1];

    const auto dot = f.dotInKnob();
    f.gainKnob->mouseDown(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    f.gainKnob->mouseDrag(makeModuleClickWithMods(*f.gainKnob, dot + juce::Point<int>(0, -10), kPlain));
    const auto dragging = chipRows(*f.vcaCard);
    ASSERT_EQ(dragging.size(), 2u);
    EXPECT_TRUE(dragging[0].contains("+60%")) << dragging[0];
    EXPECT_EQ(dragging[1], hovered[1]);
    f.gainKnob->mouseUp(makeModuleClickWithMods(*f.gainKnob, dot, kPlain));
    EXPECT_EQ(chipRows(*f.vcaCard).size(), 2u) << "the hover still lists both once the readout is gone";
}

// Five sources on a knob paint four rows and "+1 more" inside the card (clipped to it, never moved).
TEST_F(ModuleComponentTest, TheStackStaysInsideTheCardWhateverTheKnobIsNear) {
    Fixture f;
    f.gainKnob->setTooltip("Scales the signal"); // the caption already names the knob, so give it a tip of its own
    ASSERT_NE(f.gainKnob, nullptr);
    synth::theme::AppLookAndFeel lnf;
    f.vcaCard->setLookAndFeel(&lnf);
    for (int i = 0; i < 4; ++i)
        f.addSecondLfo(); // five sources: four rows and "+1 more"
    auto paint = [&f] {
        juce::Image image(juce::Image::ARGB, f.vcaCard->getWidth(), f.vcaCard->getHeight(), true,
                          juce::SoftwareImageType());
        juce::Graphics g(image);
        f.vcaCard->paintEntireComponent(g, true);
        return image;
    };
    const auto before = paint();
    f.editor->setHoveredModTarget(graph_editor_types::HoveredModTarget{f.vcaId, f.gainChannel});
    ASSERT_EQ(chipRows(*f.vcaCard).size(), 5u);
    EXPECT_EQ(chipRows(*f.vcaCard).back(), "+1 more");
    const auto after = paint();
    EXPECT_EQ(after.getWidth(), f.vcaCard->getWidth()) << "the stack is clipped to the card, never moved or resized";
    bool changed = false;
    for (int y = 0; y < after.getHeight() && !changed; ++y)
        for (int x = 0; x < after.getWidth() && !changed; ++x)
            changed = before.getPixelAt(x, y) != after.getPixelAt(x, y);
    // A knob whose stack starts below the card's bottom edge is clipped away entirely; one with room shows it.
    const bool room = f.gainKnob->getBottom() + 2 < f.vcaCard->getHeight();
    EXPECT_EQ(changed, room) << "knob bottom " << f.gainKnob->getBottom() << ", card height " << f.vcaCard->getHeight();
    f.vcaCard->setLookAndFeel(nullptr);
}

TEST_F(ModuleComponentTest, SteppingTheDotWithTheKeyboardShowsExactlyOneReadout) {
    CountsAsForeground foreground;
    Fixture f;
    f.gainKnob->setTooltip("Scales the signal"); // the caption already names the knob, so give it a tip of its own
    auto* button = f.vcaCard->getModDotButton(f.gainChannel);
    ASSERT_NE(button, nullptr);
    AppTooltipWindow window(f.editor.get());

    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(visibleReadouts(f, window), 1);
    EXPECT_TRUE(button->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_EQ(visibleReadouts(f, window), 1);

    f.editor->getModDot().getTooltip().hide(); // what the key step's hide timer does
    EXPECT_TRUE(window.getTipFor(*f.gainKnob).isNotEmpty());
}

TEST(AppTooltipWindowBlock, BlocksAreCountedSoTwoReadoutsNeedTwoReleases) {
    juce::Component host;
    AppTooltipWindow window(&host);
    EXPECT_FALSE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(true);
    AppTooltipWindow::blockHoverTips(true);
    AppTooltipWindow::blockHoverTips(false);
    EXPECT_TRUE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false);
    EXPECT_FALSE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false); // an unmatched release never goes below zero
    AppTooltipWindow::blockHoverTips(true);
    EXPECT_TRUE(AppTooltipWindow::hoverTipsBlocked());
    AppTooltipWindow::blockHoverTips(false);
}

// The knob's sources panel lists the same rows, so the hover chip is not drawn under it while it is open.
TEST_F(ModuleComponentTest, TheHoverChipStepsAsideWhileTheKnobsSourcesPanelIsOpen) {
    Fixture f;
    f.editor->setHoveredModTarget(GraphEditor::HoveredModTarget{f.vcaId, f.gainChannel});
    EXPECT_TRUE(synth::ui::modHoverChipShows(*f.editor, f.vcaId));
    ASSERT_NE(f.clickDot(), nullptr);
    ASSERT_EQ(f.editor->getModDot().getPopover(), f.popover());
    f.editor->setHoveredModTarget(GraphEditor::HoveredModTarget{f.vcaId, f.gainChannel});
    EXPECT_FALSE(synth::ui::modHoverChipShows(*f.editor, f.vcaId)) << "the open panel already lists the sources";
    f.held.reset();
    EXPECT_TRUE(synth::ui::modHoverChipShows(*f.editor, f.vcaId)) << "back once the panel has closed";
}
