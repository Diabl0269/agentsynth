// OnCardTimeTempoTests.cpp -- the edit strip's "Controls" switch on an ADSR card (Shared | Separate): it shows
// the layout's mode, writes the other one at once (Cancel undoes it, Done keeps it as the session's one undo
// step), and is absent on any other card; in Separate only the look the card's own Sync switch is on is shown
// and outlined, the Sync switch stays usable under the overlay, and ending the session puts Sync back.
// docs/layout/module-card-layout.md#editing-a-layout.

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
    EXPECT_EQ(toggle.getTitle(), "Controls");
    EXPECT_EQ(toggle.getTooltip(),
              "Shared: one set of controls for the Time and Tempo looks. Separate: each look has its own controls "
              "and positions");
    EXPECT_EQ(toggle.getNumSegments(), 2);
    EXPECT_EQ(toggle.getSegment(0)->getButtonText(), "Shared");
    EXPECT_EQ(toggle.getSegment(1)->getButtonText(), "Separate");
    EXPECT_EQ(toggle.getSelectedIndex(), kShared);
    EXPECT_TRUE(editor->getLocalBounds().contains(toggle.getBounds()));
    EXPECT_FALSE(toggle.getBounds().intersects(editor->getAddButtonForTest().getBounds()));
}

TEST(OnCardTimeTempo, ChoosingSeparateRebuildsTheCardWithOnlyTheTimeLooksControlsShownAndOutlined) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    EXPECT_TRUE(headersOf(*rig.card(id)).isEmpty());
    const auto bounds = rig.card(id)->getBounds();

    choose(*editor, kSeparate);
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);
    EXPECT_TRUE(headersOf(*rig.card(id)).isEmpty()) << "two looks of one area, not two titled groups";
    EXPECT_EQ(rig.card(id)->getBounds(), bounds) << "the card keeps its size";
    for (const char* paramId : {"attack", "hold", "decay", "sustain", "release"}) {
        EXPECT_TRUE(widgetOf(*rig.card(id), paramId)->isVisible()) << paramId;
        EXPECT_NE(editor->getOutlineForTest(paramId), nullptr) << paramId << " is outlined";
    }
    for (const char* paramId : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
        EXPECT_FALSE(widgetOf(*rig.card(id), paramId)->isVisible()) << paramId << " is the other look";
        EXPECT_EQ(editor->getOutlineForTest(paramId), nullptr) << paramId << " has no outline";
    }
    EXPECT_EQ(editor->getTimeTempoSwitchForTest().getSelectedIndex(), kSeparate);
}

TEST(OnCardTimeTempo, FlippingSyncInTheEditorEditsTheOtherLookAndResyncsTheOutlines) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);
    const auto bounds = rig.card(id)->getBounds();

    setParam(*rig.canvas.processor(id), "tempoSync", 1.0f);
    rig.card(id)->getCardBody()->flushPendingConditionUpdate();
    editor->runQueuedSyncForTest();
    EXPECT_EQ(rig.card(id)->getBounds(), bounds);
    for (const char* paramId : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv", "sustain"})
        EXPECT_NE(editor->getOutlineForTest(paramId), nullptr) << paramId << " is outlined in the Tempo look";
    for (const char* paramId : {"attack", "hold", "decay", "release"})
        EXPECT_EQ(editor->getOutlineForTest(paramId), nullptr) << paramId << " is not in the Tempo look";

    // Moving a control in the Tempo look writes only the Tempo look.
    const auto before = rig.card(id)->getCardBody()->explicitLayout();
    Pointer pointer(*editor, "attackDiv");
    pointer.moveBy({0, 0});
    pointer.release();
    EXPECT_EQ(rig.card(id)->getCardBody()->explicitLayout(), before) << "a click that moves nothing writes nothing";
}

TEST(OnCardTimeTempo, EachLooksSustainKeepsItsOwnPosition) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);
    setParam(*rig.canvas.processor(id), "tempoSync", 1.0f);
    rig.card(id)->getCardBody()->flushPendingConditionUpdate();
    editor->runQueuedSyncForTest();

    ASSERT_NE(editor->getOutlineForTest("sustain"), nullptr);
    Pointer pointer(*editor, "sustain");
    pointer.moveBy({-40, 0});
    pointer.release();
    const auto layout = rig.card(id)->getCardBody()->explicitLayout();
    const auto at = [&](const char* section) {
        for (const auto& s : layout.sections)
            if (s.id == section)
                for (const auto& item : s.items)
                    if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                        param != nullptr && param->paramId == "sustain")
                        return param->at;
        return std::optional<juce::Point<int>>();
    };
    EXPECT_TRUE(at("stages-tempo").has_value()) << "the Tempo look's Sustain was placed";
    EXPECT_FALSE(at("stages-time").has_value()) << "the Time look's was left alone";
}

TEST(OnCardTimeTempo, TheCardsOwnSyncSwitchStaysUsableUnderTheOverlay) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    auto* sync = widgetOf(*rig.card(id), "tempoSync");
    ASSERT_NE(sync, nullptr);

    // Where a click on the switch lands: on the switch (or one of its segments), not on the overlay.
    const auto inCanvas = rig.card(id)->getPosition() + sync->getBounds().getCentre();
    auto* parent = rig.card(id)->getParentComponent();
    ASSERT_NE(parent, nullptr);
    auto* hit = parent->getComponentAt(inCanvas);
    ASSERT_NE(hit, nullptr);
    EXPECT_TRUE(hit == sync || sync->isParentOf(hit)) << "the overlay lets the mouse through over the Sync switch";
    const auto elsewhere = rig.card(id)->getPosition() + widgetOf(*rig.card(id), "attack")->getBounds().getCentre();
    EXPECT_TRUE(editor->isParentOf(parent->getComponentAt(elsewhere)) || parent->getComponentAt(elsewhere) == editor)
        << "everywhere else the overlay still takes the mouse";
    auto* outline = editor->getOutlineForTest("tempoSync");
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->isPassThrough());
    EXPECT_TRUE(outline->getWantsKeyboardFocus());

    // The Tempo segment, clicked the way a person does.
    auto* switchWidget = dynamic_cast<synth::ui::CardSegmentedSwitch*>(sync);
    ASSERT_NE(switchWidget, nullptr);
    switchWidget->setSelectedIndex(1, juce::sendNotificationSync);
    EXPECT_EQ(findParameterByID(rig.canvas.processor(id), "tempoSync")->getValue(), 1.0f);

    // Space on its outline flips it back.
    EXPECT_TRUE(outline->onKey(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_EQ(findParameterByID(rig.canvas.processor(id), "tempoSync")->getValue(), 0.0f);
}

TEST(OnCardTimeTempo, DoneAndCancelPutSyncBackAsTheEditorOpened) {
    for (const bool keep : {true, false}) {
        OnCardRig rig;
        const auto id = rig.add(std::make_unique<ADSRModule>());
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr);
        choose(*editor, kSeparate);
        setParam(*rig.canvas.processor(id), "tempoSync", 1.0f);
        rig.card(id)->getCardBody()->flushPendingConditionUpdate();
        ASSERT_EQ(findParameterByID(rig.canvas.processor(id), "tempoSync")->getValue(), 1.0f);

        auto& bar = editor->getEditBarForTest();
        if (keep)
            bar.getDoneButton().onClick();
        else
            bar.getCancelButton().onClick();
        EXPECT_EQ(findParameterByID(rig.canvas.processor(id), "tempoSync")->getValue(), 0.0f)
            << (keep ? "Done" : "Cancel");
    }
}

TEST(OnCardTimeTempo, ASessionOpenedOnTempoEndsOnTempo) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    setParam(*rig.canvas.processor(id), "tempoSync", 1.0f);
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    setParam(*rig.canvas.processor(id), "tempoSync", 0.0f);
    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_EQ(findParameterByID(rig.canvas.processor(id), "tempoSync")->getValue(), 1.0f);
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
    ASSERT_TRUE(rig.canvas.undo.canUndo()) << "the cancel is one more step";
    EXPECT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate) << "which brings the switch back";
}

TEST(OnCardTimeTempo, DoneKeepsEachSwitchAsItsOwnUndoStep) {
    OnCardRig rig;
    const auto id = rig.add(std::make_unique<ADSRModule>());
    auto* editor = rig.openOnCard(id);
    ASSERT_NE(editor, nullptr);
    choose(*editor, kSeparate);
    choose(*editor, kShared);
    choose(*editor, kSeparate);
    editor->getEditBarForTest().getDoneButton().onClick();
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);

    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Separate);
    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_EQ(modeOf(rig, id), AdsrTimeTempo::Shared);
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "three switches, three steps";
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
    EXPECT_LT(at("Controls"), at("Add control"));
    EXPECT_EQ(at("Controls") + 1, at("Add control"));

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
