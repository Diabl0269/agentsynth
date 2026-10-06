// OnCardControlPanelTests.cpp -- the per-control panel of the on-card editor: opened by a real right-click,
// double-click and Return on an outline, every field writing live through the session, and the panel
// staying open until Esc or Hide. The range rules are pinned as pure functions.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "../../../Accessibility/TabOrderHelpers.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutControlPanel.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardControlOptions.h"
#include "UI/Graph/CardWidgets/CardFader.h"

using namespace oncard_test;
using synth::CardWidget;
using synth::ui::CardLayoutControlPanel;

namespace {

CardLayoutControlPanel* panelOf(OnCardRig& rig) {
    return dynamic_cast<CardLayoutControlPanel*>(rig.panelLaunched.get());
}

void rightClick(CardLayoutOnCardEditor& editor, const juce::String& key) {
    auto& outline = *editor.getOutlineForTest(key);
    outline.mouseDown(cardbody_test::mouseAt(outline, outline.getLocalBounds().getCentre().toFloat(),
                                             juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
}

CardLayoutControlPanel* openPanel(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& key) {
    rig.panelLaunched.reset();
    rightClick(editor, key);
    return panelOf(rig);
}

void chooseShowAs(CardLayoutControlPanel& panel, CardWidget widget) {
    const auto kinds = synth::ui::widgetKinds(panel.getOptions().widgetChoices);
    const auto at = std::find(kinds.begin(), kinds.end(), synth::ui::widgetKindOf(widget));
    ASSERT_NE(at, kinds.end());
    panel.getShowAsForTest()->setSelectedIndex((int)(at - kinds.begin()), juce::sendNotificationSync);
}

// A click on one segment of a panel switch, the way a user picks it.
void clickSegment(synth::ui::CardSegmentedSwitch* panelSwitch, int index) {
    ASSERT_NE(panelSwitch, nullptr);
    ASSERT_TRUE(panelSwitch->isVisible());
    const auto at = panelSwitch->getSegment(index)->getBounds().getCentre().toFloat();
    panelSwitch->mouseDown(
        cardbody_test::mouseAt(*panelSwitch, at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
}

void typeRange(CardLayoutControlPanel& panel, const juce::String& lo, const juce::String& hi) {
    panel.getMinimumEditorForTest().setText(lo, false);
    panel.getMaximumEditorForTest().setText(hi, false);
    panel.commitRangeForTest();
}

} // namespace

TEST(OnCardControlPanel, RightClickDoubleClickAndReturnEachOpenThePanelForThatControl) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    juce::String observed;
    editor->onControlOptions = [&](const juce::String& paramId) { observed = paramId; };

    auto* viaRight = openPanel(rig, *editor, "resonance");
    ASSERT_NE(viaRight, nullptr);
    EXPECT_EQ(editor->getControlPanelForTest(), viaRight);
    EXPECT_EQ(viaRight->getOptions().paramId, "resonance");
    EXPECT_EQ(viaRight->getTitle(), "Resonance options");
    EXPECT_EQ(observed, "resonance");

    rig.panelLaunched.reset();
    auto& outline = *editor->getOutlineForTest("cutoff");
    outline.mouseDoubleClick(cardbody_test::mouseAt(outline, outline.getLocalBounds().getCentre().toFloat(),
                                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    ASSERT_NE(panelOf(rig), nullptr);
    EXPECT_EQ(editor->getControlPanelForTest()->getOptions().paramId, "cutoff")
        << "a second control replaces the first";

    rig.panelLaunched.reset();
    EXPECT_TRUE(editor->getOutlineForTest("drive")->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_NE(panelOf(rig), nullptr);
    EXPECT_EQ(panelOf(rig)->getOptions().paramId, "drive");
    EXPECT_TRUE(editor->getControlPanelForTest() == panelOf(rig));
}

TEST(OnCardControlPanel, EveryFieldIsNamedTooltippedAndReachedByTabInTheBriefsOrder) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);

    EXPECT_EQ(panel->getShowAsForTest()->getTitle(), "Show as");
    EXPECT_EQ(panel->getSizeForTest()->getTitle(), "Size");
    EXPECT_FALSE(panel->getSizeForTest()->getTooltip().isEmpty());
    EXPECT_TRUE(panel->getSizeForTest()->getWantsKeyboardFocus());
    EXPECT_EQ(panel->getDirectionForTest()->getTitle(), "Direction");
    EXPECT_FALSE(panel->getDirectionForTest()->getTooltip().isEmpty());
    EXPECT_EQ(panel->getLabelEditorForTest().getTitle(), "Label");
    EXPECT_EQ(panel->getMinimumEditorForTest().getTitle(), "Minimum");
    EXPECT_EQ(panel->getMaximumEditorForTest().getTitle(), "Maximum");
    EXPECT_EQ(panel->getHideButtonForTest().getTitle(), "Hide from card");
    EXPECT_TRUE(panel->getShowAsForTest()->getWantsKeyboardFocus());
    for (auto* c : std::initializer_list<juce::SettableTooltipClient*>{
             panel->getShowAsForTest(), &panel->getLabelEditorForTest(), &panel->getMinimumEditorForTest(),
             &panel->getMaximumEditorForTest(), &panel->getHideButtonForTest()})
        EXPECT_FALSE(c->getTooltip().isEmpty());

    const auto walk = synth::test::walkTabOrder(*panel);
    EXPECT_EQ(walk.names(), juce::StringArray({"Show as", "Size", "Label", "Minimum", "Maximum", "Hide from card"}));
    const auto gaps = synth::test::auditAccessibility(*panel);
    EXPECT_TRUE(gaps.empty()) << "gaps: " << gaps.size();
}

TEST(OnCardControlPanel, ShowAsFaderWritesTheWidgetRebuildsTheCardAndTheSelectionFollows) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    EXPECT_NE(dynamic_cast<juce::Slider*>(widgetOf(*rig.card(id), "resonance")), nullptr);
    EXPECT_EQ(dynamic_cast<synth::ui::CardFader*>(widgetOf(*rig.card(id), "resonance")), nullptr);

    chooseShowAs(*panel, CardWidget::FaderV);
    const auto layout = rig.storedLayout(id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(storedItem(*layout, "resonance")->widget, CardWidget::FaderV);
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(widgetOf(*rig.card(id), "resonance")), nullptr);
    EXPECT_EQ(editor->getControlPanelForTest(), panel) << "the panel stays open";
    EXPECT_EQ(panel->getOptions().widget, CardWidget::FaderV);
    EXPECT_NE(editor->getOutlineForTest("resonance"), nullptr);
}

TEST(OnCardControlPanel, LabelRenamesTheCaptionAndClearingItRestoresTheName) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getLabelEditorForTest().getText(), "Resonance");

    panel->getLabelEditorForTest().setText("  Res ", false);
    panel->commitLabelForTest();
    EXPECT_EQ(cardlayouteditor_test::captionOf(*rig.card(id), "resonance"), "Res");
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "resonance")->label, std::optional<juce::String>("Res"));
    EXPECT_EQ(panel->getTitle(), "Res options");

    panel->getLabelEditorForTest().setText("", false);
    panel->commitLabelForTest();
    EXPECT_EQ(cardlayouteditor_test::captionOf(*rig.card(id), "resonance"), "Resonance");
    EXPECT_FALSE(storedItem(*rig.storedLayout(id), "resonance")->label.has_value());
    EXPECT_EQ(panel->getLabelEditorForTest().getText(), "Resonance");

    panel->getLabelEditorForTest().setText("Resonance", false);
    panel->commitLabelForTest();
    EXPECT_FALSE(storedItem(*rig.storedLayout(id), "resonance")->label.has_value()) << "its own name is no override";
}

TEST(OnCardControlPanel, RangeNarrowsTheKnobAndBlankingBothClearsIt) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isRangeRowShownForTest());

    typeRange(*panel, "200", "2000");
    auto* knob = dynamic_cast<juce::Slider*>(widgetOf(*rig.card(id), "cutoff"));
    ASSERT_NE(knob, nullptr);
    EXPECT_DOUBLE_EQ(knob->getMinimum(), 200.0);
    EXPECT_DOUBLE_EQ(knob->getMaximum(), 2000.0);
    EXPECT_EQ(panel->getMinimumEditorForTest().getText(), "200");
    EXPECT_EQ(editor->getControlPanelForTest(), panel);

    typeRange(*panel, "", "");
    knob = dynamic_cast<juce::Slider*>(widgetOf(*rig.card(id), "cutoff"));
    EXPECT_DOUBLE_EQ(knob->getMinimum(), 20.0);
    EXPECT_DOUBLE_EQ(knob->getMaximum(), 20000.0);
    EXPECT_FALSE(storedItem(*rig.storedLayout(id), "cutoff")->range.has_value());
}

TEST(OnCardControlPanel, AnInvalidRangeIsRefusedTheFieldsRevertAndAHintSaysWhy) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "cutoff");
    ASSERT_NE(panel, nullptr);
    typeRange(*panel, "200", "2000");

    for (const auto& [lo, hi] : std::vector<std::pair<const char*, const char*>>{
             {"abc", "2000"}, {"3000", "2000"}, {"500", "500"}, {"30000", "40000"}}) {
        SCOPED_TRACE(juce::String(lo) + " .. " + hi);
        typeRange(*panel, lo, hi);
        EXPECT_TRUE(panel->getHintForTest().isNotEmpty());
        EXPECT_EQ(panel->getMinimumEditorForTest().getText(), "200") << "reverts to what is stored";
        EXPECT_EQ(panel->getMaximumEditorForTest().getText(), "2000");
        const auto range = storedItem(*rig.storedLayout(id), "cutoff")->range;
        ASSERT_TRUE(range.has_value());
        EXPECT_DOUBLE_EQ(range->getStart(), 200.0);
    }
    typeRange(*panel, "100", "5000");
    EXPECT_TRUE(panel->getHintForTest().isEmpty()) << "a good entry clears the hint";
}

TEST(OnCardControlPanel, HideFromCardHidesTheControlAndClosesThePanel) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "drive");
    ASSERT_NE(panel, nullptr);

    panel->getHideButtonForTest().onClick();
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("drive"));
    EXPECT_EQ(editor->getOutlineForTest("drive"), nullptr);
    EXPECT_EQ(editor->getControlPanelForTest(), nullptr);
    EXPECT_FALSE(editor->isClosed()) << "the session goes on";
}

TEST(OnCardControlPanel, AChoiceControlHasNoRangeRowAndOnlyItsSuitableShowAsChoices) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "filterType");
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->isRangeRowShownForTest());
    EXPECT_FALSE(panel->getOptions().fullRange.has_value());

    synth::ui::BuiltInCardLayoutSource source(rig.canvas.editor, nullptr, id);
    for (const auto& param : source.parameters())
        if (param.paramId == "filterType") {
            EXPECT_EQ(panel->getOptions().widgetChoices, param.widgetChoices);
            EXPECT_EQ(panel->getShowAsForTest() != nullptr, param.widgetChoices.size() >= 2);
        }
}

TEST(OnCardControlPanel, EscClosesThePanelFirstAndASecondEscCancelsTheSession) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);

    EXPECT_TRUE(panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(editor->getControlPanelForTest(), nullptr);
    EXPECT_FALSE(editor->isClosed());

    ASSERT_NE(openPanel(rig, *editor, "resonance"), nullptr);
    EXPECT_TRUE(editor->getOutlineForTest("resonance")->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(editor->getControlPanelForTest(), nullptr) << "Esc on the outline closes the panel too";
    EXPECT_FALSE(editor->isClosed());

    EXPECT_TRUE(editor->getOutlineForTest("resonance")->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(editor->isClosed());
}

TEST(OnCardControlPanel, EachPanelEditIsItsOwnUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    chooseShowAs(*panel, CardWidget::FaderV);
    panel->getLabelEditorForTest().setText("Res", false);
    panel->commitLabelForTest();
    ASSERT_TRUE(rig.canvas.undo.canUndo());

    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_EQ(editor->getControlPanelForTest(), nullptr) << "ending the session closes the panel";
    EXPECT_TRUE(rig.canvas.undo.undo()); // the label
    ASSERT_TRUE(rig.canvas.undo.canUndo());
    EXPECT_TRUE(rig.canvas.undo.undo()); // the widget
    EXPECT_FALSE(rig.canvas.undo.canUndo());
    EXPECT_FALSE(rig.storedLayout(id).has_value());
}

TEST(OnCardControlOptions, RangeEntryRules) {
    const juce::Range<double> full(20.0, 20000.0);
    using synth::ui::parseRangeEntry;
    EXPECT_TRUE(parseRangeEntry("", "", full).ok);
    EXPECT_FALSE(parseRangeEntry("", "", full).range.has_value());
    auto narrowed = parseRangeEntry("200", "2000", full);
    ASSERT_TRUE(narrowed.ok && narrowed.range.has_value());
    EXPECT_DOUBLE_EQ(narrowed.range->getEnd(), 2000.0);
    auto lowOnly = parseRangeEntry("100", "", full);
    ASSERT_TRUE(lowOnly.ok && lowOnly.range.has_value());
    EXPECT_DOUBLE_EQ(lowOnly.range->getEnd(), 20000.0) << "a blank end is the parameter's own";
    EXPECT_DOUBLE_EQ(parseRangeEntry("1", "500", full).range->getStart(), 20.0) << "clamped to the parameter";
    EXPECT_FALSE(parseRangeEntry("1", "30000", full).range.has_value()) << "clamps to the full range: no override";
    EXPECT_FALSE(parseRangeEntry("12abc", "500", full).ok);
    EXPECT_FALSE(parseRangeEntry("500", "100", full).ok);
    EXPECT_FALSE(parseRangeEntry("30000", "40000", full).ok);
    EXPECT_EQ(synth::ui::formatRangeValue(2000.0), "2000");
    EXPECT_EQ(synth::ui::formatRangeValue(0.25), "0.25");
}

TEST(OnCardControlPanel, ShowAsListsKindsOnlyAndNoTwoSizesAreBothCalledKnob) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    auto* showAs = panel->getShowAsForTest();
    ASSERT_NE(showAs, nullptr);
    EXPECT_EQ(showAs->getNumSegments(), 2) << "Knob and Fader, not four widgets";
    EXPECT_EQ(showAs->getSegment(0)->getButtonText(), "Knob");
    EXPECT_EQ(showAs->getSegment(1)->getButtonText(), "Fader");
    EXPECT_EQ(synth::ui::cardLayoutWidgetName(CardWidget::Knob), "Small knob");
    EXPECT_EQ(synth::ui::cardLayoutWidgetName(CardWidget::KnobLarge), "Large knob");
}

TEST(OnCardControlPanel, SizeSwitchesAKnobBetweenSmallAndLargeAndTheCellGrowsOnTheRealCard) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isSizeRowShownForTest());
    EXPECT_FALSE(panel->isDirectionRowShownForTest());
    EXPECT_EQ(panel->getSizeForTest()->getSelectedIndex(), 0);
    EXPECT_EQ(panel->getSizeForTest()->getSegment(0)->getButtonText(), "Small");
    EXPECT_EQ(panel->getSizeForTest()->getSegment(1)->getButtonText(), "Large");
    const int smallHeight = widgetOf(*rig.card(id), "resonance")->getHeight();
    EXPECT_EQ(smallHeight, synth::cardbody::kKnobHeight);

    clickSegment(panel->getSizeForTest(), 1);
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "resonance")->widget, CardWidget::KnobLarge);
    EXPECT_EQ(panel->getOptions().widget, CardWidget::KnobLarge);
    EXPECT_EQ(panel->getSizeForTest()->getSelectedIndex(), 1);
    EXPECT_GT(widgetOf(*rig.card(id), "resonance")->getHeight(), smallHeight);
    EXPECT_EQ(widgetOf(*rig.card(id), "resonance")->getHeight(), synth::cardbody::kKnobLargeHeight);
    EXPECT_EQ(editor->getControlPanelForTest(), panel) << "the panel stays open";

    // Left on the focused switch goes back to Small, and each change is one undo step.
    EXPECT_TRUE(panel->getSizeForTest()->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "resonance")->widget, CardWidget::Knob);
    EXPECT_EQ(widgetOf(*rig.card(id), "resonance")->getHeight(), smallHeight);
    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.canvas.undo.canUndo());
}

TEST(OnCardControlPanel, ShowAsKeepsTheSizeAndTheDirectionRowFollowsAFader) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    clickSegment(panel->getSizeForTest(), 1);
    chooseShowAs(*panel, CardWidget::Knob);
    EXPECT_EQ(panel->getOptions().widget, CardWidget::KnobLarge) << "picking Knob again keeps a large knob large";

    chooseShowAs(*panel, CardWidget::FaderV);
    EXPECT_EQ(panel->getOptions().widget, CardWidget::FaderV);
    EXPECT_FALSE(panel->isSizeRowShownForTest());
    ASSERT_TRUE(panel->isDirectionRowShownForTest());
    clickSegment(panel->getDirectionForTest(), 1);
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "resonance")->widget, CardWidget::FaderH);
    EXPECT_EQ(panel->getDirectionForTest()->getSelectedIndex(), 1);
    EXPECT_FALSE(dynamic_cast<synth::ui::CardFader*>(widgetOf(*rig.card(id), "resonance"))->isVerticalFader());
}

TEST(OnCardControlOptions, KindsFoldTheWidgetChoicesAndPairsNeedBothHalves) {
    using synth::ui::WidgetKind;
    const std::vector<CardWidget> choices{CardWidget::Knob, CardWidget::KnobLarge, CardWidget::FaderV,
                                          CardWidget::FaderH};
    EXPECT_EQ(synth::ui::widgetKinds(choices), (std::vector<WidgetKind>{WidgetKind::Knob, WidgetKind::Fader}));
    EXPECT_TRUE(synth::ui::offersKnobSize(choices, CardWidget::Knob));
    EXPECT_FALSE(synth::ui::offersKnobSize(choices, CardWidget::FaderV));
    EXPECT_FALSE(synth::ui::offersKnobSize({CardWidget::Knob, CardWidget::Choice}, CardWidget::Knob));
    EXPECT_EQ(synth::ui::widgetForKind(WidgetKind::Fader, CardWidget::KnobLarge, choices), CardWidget::FaderV);
    EXPECT_EQ(synth::ui::widgetForKind(WidgetKind::Knob, CardWidget::FaderH, choices), CardWidget::Knob);
}
