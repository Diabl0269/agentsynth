// OnCardTimeTempoTests.cpp -- the edit bar's "Time and tempo" switch on an ADSR card: it shows the layout's
// mode, writes the other one at once (Cancel undoes it, Done keeps it as the session's one undo step), and is
// absent on any other card. docs/layout/module-card-layout.md#editing-a-layout.

#include "../../../Accessibility/TabOrderHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"

using namespace oncard_test;
using synth::AdsrTimeTempo;

namespace {

constexpr int kShared = 0;
constexpr int kSeparate = 1;

void setParam(juce::AudioProcessor& module, const juce::String& id, float plainValue) {
    for (auto* parameter : module.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged != nullptr && ranged->paramID == id)
            ranged->setValueNotifyingHost(ranged->convertTo0to1(plainValue));
}

// The titles of the card's section headers, top to bottom.
juce::StringArray headersOf(ModuleComponent& card) {
    juce::StringArray titles;
    for (const auto& section : card.getCardBody()->getPlan().sections)
        if (section.header != nullptr)
            titles.add(section.title.value_or(juce::String()));
    return titles;
}

const synth::CardBodyItem& itemOf(ModuleComponent& card, const juce::String& paramId) {
    const auto& plan = card.getCardBody()->getPlan();
    return plan.items[(size_t)plan.findParam(paramId)];
}

AdsrTimeTempo modeOf(OnCardRig& rig, NodeID id) {
    return *synth::adsrTimeTempoOf(rig.card(id)->getCardBody()->explicitLayout());
}

void choose(CardLayoutOnCardEditor& editor, int index) {
    editor.getTimeTempoSwitchForTest().setSelectedIndex(index, juce::sendNotificationSync);
}

} // namespace

TEST(OnCardTimeTempo, AnAdsrCardsStripHasTheSwitchOnSharedWithItsTitleAndTooltip) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(editor->getTimeTempoSwitchForTest().isVisible());
    auto& toggle = editor->getTimeTempoSwitchForTest();
    EXPECT_TRUE(toggle.isVisible());
    EXPECT_EQ(toggle.getTitle(), "Time and tempo");
    EXPECT_EQ(toggle.getTooltip(),
              "Show each stage once (its time or tempo control follows the card's Time/Tempo switch), or as "
              "separate Time and Tempo groups");
    EXPECT_EQ(toggle.getNumSegments(), 2);
    EXPECT_EQ(toggle.getSegment(0)->getButtonText(), "Shared");
    EXPECT_EQ(toggle.getSegment(1)->getButtonText(), "Separate");
    EXPECT_EQ(toggle.getSelectedIndex(), kShared);
    EXPECT_TRUE(editor->getLocalBounds().contains(toggle.getBounds()));
    EXPECT_FALSE(toggle.getBounds().intersects(editor->getAddButtonForTest().getBounds()));
}

TEST(OnCardTimeTempo, ChoosingSeparateRebuildsTheCardWithTimeAndTempoGroupsBothVisible) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(headersOf(*rig.card(id)).isEmpty());

    choose(*editor, kSeparate);
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);
    EXPECT_EQ(headersOf(*rig.card(id)), juce::StringArray({"Time", "Tempo"}));
    for (const char* paramId :
         {"attack", "hold", "decay", "sustain", "release", "attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
        auto* widget = widgetOf(*rig.card(id), paramId);
        ASSERT_NE(widget, nullptr) << paramId;
        EXPECT_TRUE(widget->isVisible()) << paramId << " is always on the card";
    }
    EXPECT_EQ(editor->getTimeTempoSwitchForTest().getSelectedIndex(), kSeparate);
    EXPECT_NE(editor->getOutlineForTest("attackDiv"), nullptr) << "the divisions are outlined too";
}

TEST(OnCardTimeTempo, EachGroupIsDimmedWhileTheOtherModeIsOn) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);

    EXPECT_FALSE(itemOf(*rig.card(id), "attack").dimmed) << "Time mode: the times are live";
    EXPECT_TRUE(itemOf(*rig.card(id), "attackDiv").dimmed) << "the divisions are dimmed";

    setParam(*rig.canvas.processor(id), "tempoSync", 1.0f);
    rig.card(id)->getCardBody()->flushPendingConditionUpdate();
    EXPECT_TRUE(itemOf(*rig.card(id), "attack").dimmed);
    EXPECT_TRUE(itemOf(*rig.card(id), "sustain").dimmed);
    EXPECT_FALSE(itemOf(*rig.card(id), "attackDiv").dimmed);
}

TEST(OnCardTimeTempo, ChoosingSharedRestoresTheDefaultStages) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    const auto opening = rig.card(id)->getCardBody()->explicitLayout();
    choose(*editor, kSeparate);
    choose(*editor, kShared);
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
    EXPECT_EQ(rig.card(id)->getCardBody()->explicitLayout(), opening);
    EXPECT_TRUE(headersOf(*rig.card(id)).isEmpty());
}

TEST(OnCardTimeTempo, CancelRestoresTheOpeningLayoutAndRecordsNothing) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);
    ASSERT_TRUE(rig.storedLayout(id).has_value());

    editor->getEditBarForTest().getCancelButton().onClick();
    EXPECT_FALSE(rig.storedLayout(id).has_value());
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
    EXPECT_FALSE(rig.canvas.undo.canUndo());
}

TEST(OnCardTimeTempo, DoneKeepsItAsOneUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);
    choose(*editor, kShared);
    choose(*editor, kSeparate);
    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);

    ASSERT_TRUE(rig.canvas.undo.canUndo());
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "the whole session was one step";
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
}

TEST(OnCardTimeTempo, AnOpenSessionOnACardAlreadySeparateShowsSeparate) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* first = rig.openOnCard(id);
    ASSERT_NE(first, nullptr);
    choose(*first, kSeparate);
    first->getEditBarForTest().getDoneButton().onClick();
    rig.launched.reset();

    auto* second = rig.openOnCard(id);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->getTimeTempoSwitchForTest().getSelectedIndex(), kSeparate);
}

TEST(OnCardTimeTempo, OtherCardsHaveNoSwitch) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(editor->getTimeTempoSwitchForTest().isVisible());
    EXPECT_EQ(synth::test::walkTabOrder(*editor).names()[0], "Preset");
}

TEST(OnCardTimeTempo, TheSwitchIsATabStopBeforeAddControlThenArrowKeysChangeTheSegment) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);

    const auto names = synth::test::walkTabOrder(*editor).names();
    ASSERT_GE(names.size(), 6);
    EXPECT_EQ(names[0], "Preset");
    EXPECT_EQ(names[1], "Apply to");
    EXPECT_EQ(names[2], "Cancel");
    EXPECT_EQ(names[3], "Done");
    const auto at = [&](const char* name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
    EXPECT_LT(at("Time and tempo"), at("Add control"));
    EXPECT_EQ(at("Time and tempo") + 1, at("Add control"));

    auto& toggle = editor->getTimeTempoSwitchForTest();
    EXPECT_TRUE(toggle.getWantsKeyboardFocus());
    EXPECT_TRUE(toggle.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);
    EXPECT_TRUE(toggle.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
    EXPECT_TRUE(synth::test::auditAccessibility(*editor).empty());
}

TEST(OnCardTimeTempo, TheSwitchSitsLeftOfAddControlInTheStripUnderTheCardAndTheBarStaysOneRow) {
    OnCardRig rig;
    for (const char* type : {"ADSR", "Amp Env", "Filter Env"}) {
        const auto id = rig.add(synth::AIStateMapper::createModule(type));
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr) << type;
        auto& toggle = editor->getTimeTempoSwitchForTest();
        auto& add = editor->getAddButtonForTest();
        ASSERT_TRUE(toggle.isVisible()) << type;
        EXPECT_EQ(editor->getEditBarForTest().getHeight(), synth::ui::CardLayoutEditBar::kHeight) << type;
        EXPECT_EQ(toggle.getWidth(), CardLayoutOnCardEditor::kTimeTempoWidth) << type;
        EXPECT_EQ(toggle.getX(), 0) << type;
        EXPECT_EQ(toggle.getY(), add.getY()) << type;
        EXPECT_LT(toggle.getRight(), add.getX()) << type;
        EXPECT_GE(toggle.getY(), editor->getHeight() - CardLayoutOnCardEditor::kAddStripHeight) << type;
        EXPECT_EQ(add.getRight(), editor->getWidth()) << type;
        rig.launched.reset();
    }
}

TEST(OnCardTimeTempo, AddControlKeepsTheFullWidthOnCardsWithoutTheSwitch) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<FilterModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getAddButtonForTest().getX(), 0);
    EXPECT_EQ(editor->getAddButtonForTest().getRight(), editor->getWidth());
}
