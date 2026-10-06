// OnCardControlPanelTests.cpp -- the per-control panel of the on-card editor: opened by a real right-click,
// double-click and Return on an outline, every field writing live through the session, and the panel
// staying open until Esc or Hide. The range rules are pinned as pure functions.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "../../../Accessibility/TabOrderHelpers.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutControlPanel.h"
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
    const auto& choices = panel.getOptions().widgetChoices;
    const auto at = std::find(choices.begin(), choices.end(), widget);
    ASSERT_NE(at, choices.end());
    panel.getShowAsForTest()->setSelectedIndex((int)(at - choices.begin()), juce::sendNotificationSync);
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
    EXPECT_EQ(walk.names(), juce::StringArray({"Show as", "Label", "Minimum", "Maximum", "Hide from card"}));
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
