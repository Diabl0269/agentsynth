// OnCardControlPanelTests.cpp -- the per-control panel of the on-card editor: opened by a real right-click,
// double-click and Return on an outline, every field writing live through the session, and the panel
// staying open until Esc or Hide. The range rules are pinned as pure functions.

#include "../../../../TestWait.h"
#include "../../../Accessibility/AccessibilityAudit.h"
#include "../../../Accessibility/TabOrderHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/MacroControlModule.h"
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

struct MacrosRig {
    OnCardRig rig;
    MacroControlModule* macros = nullptr;
    NodeID id;
    CardLayoutOnCardEditor* editor = nullptr;

    MacrosRig() {
        auto module = std::make_unique<MacroControlModule>();
        macros = module.get();
        id = rig.add(std::move(module), 200, 200);
        editor = rig.openOnCard(id);
    }

    ModuleComponent& card() { return *rig.card(id); }
    juce::Point<int> pos(const juce::String& key) { return widgetOf(card(), key)->getPosition(); }

    void choosePreset(const juce::String& name) {
        const auto menu = editor->buildPresetMenuForTest();
        const auto* item = cardbody_test::menuItem(menu, name);
        ASSERT_NE(item, nullptr) << name;
        ASSERT_TRUE(item->action);
        item->action();
    }

    bool undo() {
        const bool did = rig.canvas.undo.undo();
        editor->runQueuedSyncForTest();
        return did;
    }

    void setCount(int count) {
        juce::AudioParameterInt* param = nullptr;
        for (auto* p : macros->getParameters())
            if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p); i != nullptr && i->paramID == "macroCount")
                param = i;
        ASSERT_NE(param, nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(count));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // the card re-measures on the next turn
        card().getCardBody()->refreshConditions();
    }
};

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

TEST(OnCardControlPanel, ALongFaderGrowsTheRealCardsCellAndUndoRestoresIt) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "attack");
    ASSERT_NE(panel, nullptr);
    ASSERT_TRUE(panel->isDirectionRowShownForTest());
    ASSERT_TRUE(panel->isFaderSizeRowShownForTest());
    EXPECT_FALSE(panel->isSizeRowShownForTest()) << "the knob Size row is not a fader's";
    auto* size = panel->getFaderSizeForTest();
    EXPECT_EQ(size->getTitle(), "Fader size");
    EXPECT_FALSE(size->getTooltip().isEmpty());
    EXPECT_EQ(size->getSegment(0)->getButtonText(), "Short");
    EXPECT_EQ(size->getSegment(1)->getButtonText(), "Long");
    EXPECT_EQ(size->getSelectedIndex(), 0);
    EXPECT_EQ(widgetOf(*rig.card(id), "attack")->getHeight(), synth::cardbody::kFaderVHeight);

    clickSegment(size, 1);
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "attack")->widget, CardWidget::FaderVLong);
    EXPECT_EQ(panel->getOptions().widget, CardWidget::FaderVLong);
    EXPECT_EQ(size->getSelectedIndex(), 1);
    EXPECT_EQ(panel->getDirectionForTest()->getSelectedIndex(), 0) << "a long fader still stands up";
    EXPECT_EQ(widgetOf(*rig.card(id), "attack")->getHeight(), synth::cardbody::kFaderVLongHeight);

    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(widgetOf(*rig.card(id), "attack")->getHeight(), synth::cardbody::kFaderVHeight);
}

TEST(OnCardControlPanel, ADirectionPickMovesBetweenLongAndHorizontalAndTheSizeRowFollows) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "attack");
    ASSERT_NE(panel, nullptr);
    clickSegment(panel->getFaderSizeForTest(), 1);
    clickSegment(panel->getDirectionForTest(), 1);
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "attack")->widget, CardWidget::FaderH);
    EXPECT_FALSE(panel->isFaderSizeRowShownForTest()) << "a horizontal fader has no Size row";
    EXPECT_TRUE(panel->isDirectionRowShownForTest());
    clickSegment(panel->getDirectionForTest(), 0);
    EXPECT_EQ(storedItem(*rig.storedLayout(id), "attack")->widget, CardWidget::FaderV);
    EXPECT_TRUE(panel->isFaderSizeRowShownForTest());
}

TEST(OnCardControlPanel, AKnobHasNoFaderSizeRowAndFaderSizeStaysOutOfShowAs) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* panel = openPanel(rig, *editor, "resonance");
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->isFaderSizeRowShownForTest());
    EXPECT_EQ(panel->getShowAsForTest()->getNumSegments(), 2) << "Long is a size, not a kind";
    using synth::ui::offersFaderSize;
    const std::vector<CardWidget> choices{CardWidget::Knob, CardWidget::FaderV, CardWidget::FaderVLong,
                                          CardWidget::FaderH};
    EXPECT_TRUE(offersFaderSize(choices, CardWidget::FaderVLong));
    EXPECT_FALSE(offersFaderSize(choices, CardWidget::FaderH));
    EXPECT_TRUE(synth::ui::offersFaderDirection(choices, CardWidget::FaderVLong));
    EXPECT_EQ(synth::ui::widgetKindOf(CardWidget::FaderVLong), synth::ui::WidgetKind::Fader);
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

// ---- The Macros card: presets, the Knobs count and the per-knob Name and Polarity ----

TEST(OnCardMacros, EditLayoutOpensOnTheMacrosCardWithAnOutlinePerVisibleKnob) {
    MacrosRig m;
    ASSERT_NE(m.editor, nullptr);
    EXPECT_NE(m.editor->getOutlineForTest("macro1"), nullptr);
    EXPECT_NE(m.editor->getOutlineForTest("macro8"), nullptr);
    EXPECT_EQ(m.editor->getOutlineForTest("macro9"), nullptr) << "a knob above the count is not on the card";
}

TEST(OnCardMacros, TheDefaultIsAGridOfTwoRowsOfFourAndTheJacksStayInTheRightGutter) {
    MacrosRig m;
    EXPECT_EQ(m.pos("macro1").y, m.pos("macro4").y);
    EXPECT_GT(m.pos("macro5").y, m.pos("macro1").y);
    EXPECT_EQ(m.pos("macro5").x, m.pos("macro1").x);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(m.card().getPortCenter(i, false).x, m.card().getWidth() - 10);
}

TEST(OnCardMacros, TabReachesTheCountThenEveryMacroKnobInOrderAndEveryStopIsNamed) {
    MacrosRig m;
    m.editor->cancel();
    const auto walk = synth::test::walkTabOrder(m.card());
    EXPECT_TRUE(walk.isCompleteCycle());
    auto names = walk.names();
    names.removeString("<unnamed SliderLabelComp>", false); // a knob's own value box: its knob is the named stop
    EXPECT_FALSE(names.joinIntoString("|").contains("<unnamed")) << names.joinIntoString("|");
    int last = names.indexOf("Knobs");
    EXPECT_GE(last, 0) << names.joinIntoString("|");
    for (int i = 1; i <= 8; ++i) {
        const int at = names.indexOf("M" + juce::String(i));
        EXPECT_GT(at, last) << "M" << i << " follows M" << i - 1 << " in the tab order: " << names.joinIntoString("|");
        last = at;
    }
}

TEST(OnCardMacros, APresetGlidesTheKnobsToTheirNewPlacesInsteadOfJumpingThem) {
    MacrosRig m;
    synth::ui::setReducedMotionForTest(false); // the rig starts with motion off
    const auto before = m.pos("macro5");
    m.editor->setForceAnimateForTest(true);
    m.choosePreset("Row");
    EXPECT_TRUE(m.editor->isGlidingForTest());
    EXPECT_EQ(m.pos("macro5"), before) << "the first frame still shows the knob where it was";
    m.editor->stepGlideForTest(80.0f);
    const auto midway = m.pos("macro5");
    m.editor->stepGlideForTest(1000.0f);
    const auto after = m.pos("macro5");
    ASSERT_NE(after, before);
    EXPECT_NE(midway, before) << "it has left its old place";
    EXPECT_NE(midway, after) << "and not yet landed";
    EXPECT_EQ(m.editor->getCellRectForTest("macro5").getX(), after.x) << "the outline lands with the knob";
}

TEST(OnCardMacros, TheBodySitsBesideTheJackGutterUnderTheHeaderAndNeverUnderOrOverTheJacks) {
    MacrosRig m;
    const int gutterLeft = m.card().getWidth() - synth::cardbody::kMacroJackGutter;
    const int lastJack = m.card().getPortCenter(7, false).y;
    for (const char* preset : {"Grid", "Row", "Faders"}) {
        m.choosePreset(preset);
        for (int i = 1; i <= 8; ++i) {
            const auto bounds = widgetOf(m.card(), "macro" + juce::String(i))->getBounds();
            EXPECT_LE(bounds.getRight(), gutterLeft) << preset << " macro" << i << " stays left of the jack labels";
        }
        EXPECT_LT(widgetOf(m.card(), "macroCount")->getY(), 70)
            << preset << ": the count control starts right under the header";
        EXPECT_LT(m.pos("macro1").y, 140) << preset << ": the knobs follow it, not the jacks";
        EXPECT_LT(m.card().getHeight(), lastJack + 200) << preset << ": no large empty area";
    }
    m.choosePreset("Grid");
    EXPECT_LT(m.card().getHeight(), lastJack + 140)
        << "the default card is about as tall as its jack gutter plus the body";
}

TEST(OnCardMacros, ThePresetMenuListsRowGridAndFadersAndEachReshapesTheCard) {
    MacrosRig m;
    const auto menu = m.editor->buildPresetMenuForTest();
    for (const auto* name : {"Row", "Grid", "Faders"})
        EXPECT_NE(cardbody_test::menuItem(menu, name), nullptr) << name;

    m.choosePreset("Row");
    EXPECT_EQ(m.pos("macro1").y, m.pos("macro8").y) << "the row runs eight across";
    const int rowHeight = m.card().getHeight();

    m.choosePreset("Faders");
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(widgetOf(m.card(), "macro1")), nullptr);
    EXPECT_NE(m.card().getHeight(), rowHeight) << "the size follows the layout";

    m.choosePreset("Grid");
    EXPECT_GT(m.pos("macro5").y, m.pos("macro1").y);
    EXPECT_EQ(dynamic_cast<synth::ui::CardFader*>(widgetOf(m.card(), "macro1")), nullptr);
}

TEST(OnCardMacros, APresetIsOneUndoStepAndTheCardMeasuresWhatItDraws) {
    MacrosRig m;
    const auto before = m.pos("macro5");
    const int heightBefore = m.card().getHeight();
    m.choosePreset("Row");
    ASSERT_NE(m.pos("macro5"), before);
    ASSERT_TRUE(m.undo());
    EXPECT_EQ(m.pos("macro5"), before);
    EXPECT_EQ(m.card().getHeight(), heightBefore);
    EXPECT_FALSE(m.editor->isClosed());
}

TEST(OnCardMacros, TheKnobsCountKeepsTheLayoutAndGrowingItBringsTheKnobsBackInPlace) {
    MacrosRig m;
    m.choosePreset("Faders");
    const auto layout = m.card().getCardBody()->explicitLayout();
    const auto fifth = m.pos("macro5");
    const int full = m.card().getHeight();

    m.setCount(4);
    EXPECT_EQ(m.card().getCardBody()->explicitLayout(), layout) << "the layout is untouched by the count";
    EXPECT_FALSE(widgetOf(m.card(), "macro5")->isVisible());
    EXPECT_TRUE(widgetOf(m.card(), "macro4")->isVisible());
    EXPECT_LT(m.card().getHeight(), full + 1);

    m.setCount(8);
    EXPECT_TRUE(widgetOf(m.card(), "macro5")->isVisible());
    EXPECT_EQ(m.pos("macro5"), fifth) << "back where the layout puts it";
    EXPECT_EQ(m.card().getHeight(), full);
}

TEST(OnCardMacros, DraggingAKnobToTheNextRowWritesTheLayoutThroughTheRealMousePathAndUndoTakesItBack) {
    MacrosRig m;
    ASSERT_NE(m.editor, nullptr);
    const auto start = m.pos("macro1");
    const auto fifth = m.pos("macro5");
    {
        Pointer pointer(*m.editor, "macro1");
        pointer.moveBy({0, fifth.y - start.y});
        pointer.release();
    }
    ASSERT_TRUE(m.rig.storedLayout(m.id).has_value()) << "the drop wrote the node's layout";
    EXPECT_GT(m.pos("macro1").y, start.y) << "the rebuilt card draws the knob on the second row";
    ASSERT_TRUE(m.undo());
    EXPECT_EQ(m.pos("macro1"), start);
    EXPECT_FALSE(m.rig.storedLayout(m.id).has_value());
}

TEST(OnCardMacros, ThePanelOfAMacroKnobHasNameAndPolarityInPlaceOfLabel) {
    MacrosRig m;
    auto* panel = openControlPanel(m.rig, *m.editor, "macro3");
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->isMacroRowsShownForTest());
    EXPECT_FALSE(panel->isLabelRowShownForTest());
    EXPECT_EQ(panel->getNameEditorForTest().getTitle(), "Name");
    EXPECT_EQ(panel->getPolarityForTest()->getTitle(), "Polarity");
    EXPECT_FALSE(panel->getNameEditorForTest().getTooltip().isEmpty());
    EXPECT_FALSE(panel->getPolarityForTest()->getTooltip().isEmpty());
    EXPECT_TRUE(panel->getPolarityForTest()->getWantsKeyboardFocus());
    const auto panelWalk = synth::test::walkTabOrder(*panel);
    EXPECT_TRUE(panelWalk.isCompleteCycle());
    EXPECT_EQ(panelWalk.names(),
              juce::StringArray({"Show as", "Size", "Name", "Polarity", "Minimum", "Maximum", "Hide from card"}));
    EXPECT_TRUE(synth::test::auditAccessibility(*panel).empty()) << "Name and Polarity are named, tipped, focusable";

    OnCardRig other;
    const auto filter = other.add(std::make_unique<FilterModule>());
    auto* filterEditor = other.openOnCard(filter);
    auto* filterPanel = openControlPanel(other, *filterEditor, "cutoff");
    ASSERT_NE(filterPanel, nullptr);
    EXPECT_FALSE(filterPanel->isMacroRowsShownForTest());
    EXPECT_TRUE(filterPanel->isLabelRowShownForTest());
}

TEST(OnCardMacros, RenamingAKnobCaptionsTheCardNamesTheJackAndIsOneUndoStep) {
    MacrosRig m;
    auto* panel = openControlPanel(m.rig, *m.editor, "macro3");
    ASSERT_NE(panel, nullptr);
    panel->getNameEditorForTest().setText("Cutoff", false);
    panel->commitNameForTest();

    EXPECT_EQ(m.macros->getMacroName(2), "Cutoff");
    EXPECT_EQ(m.macros->getOutputPortLabel(2), "Cutoff");
    EXPECT_EQ(widgetOf(m.card(), "macro3")->getTitle(), "Cutoff");
    EXPECT_EQ(panel->getNameEditorForTest().getText(), "Cutoff");

    ASSERT_TRUE(m.undo());
    synth::test::waitUntil(
        [&] { return widgetOf(m.card(), "macro3")->getTitle() == "M3"; }); // the card's poll puts the caption back
    EXPECT_EQ(m.macros->getMacroName(2), "M3") << "one step takes the name back";
    EXPECT_EQ(widgetOf(m.card(), "macro3")->getTitle(), "M3");
}

TEST(OnCardMacros, FlippingAKnobToBiThroughThePanelSwitchIsPerKnobAndOneUndoStep) {
    MacrosRig m;
    auto* panel = openControlPanel(m.rig, *m.editor, "macro2");
    ASSERT_NE(panel, nullptr);
    clickSegment(panel->getPolarityForTest(), 1);
    EXPECT_TRUE(m.macros->isBipolar(1));
    EXPECT_FALSE(m.macros->isBipolar(0));
    EXPECT_FALSE(m.macros->isBipolar(2));
    EXPECT_EQ(panel->getPolarityForTest()->getSelectedIndex(), 1);

    ASSERT_TRUE(m.undo());
    EXPECT_FALSE(m.macros->isBipolar(1));
}
