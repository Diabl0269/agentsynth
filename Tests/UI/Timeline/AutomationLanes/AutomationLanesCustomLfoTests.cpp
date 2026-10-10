// AutomationLanesCustomLfoTests.cpp -- a lane's "Create custom LFO" as a person reaches it: the item first in the lane
// menu and what its text says when it is disabled (stub host), then the command against a real MainComponent: one new
// LFO with the drawn wave, its row and amount lane, the flattened lane, the spent range, one undo step and redo, a
// second row beside an existing LFO, and the motion.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesModulatorFixture.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/CustomLfoFromRange.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

using namespace modulator_test;
using namespace automation_lanes_test;
using synth::ui::AutomationLaneHeaderComponent;

namespace {

constexpr int kLinear = static_cast<int>(synth::BreakpointCurve::Linear);

// The Filter cutoff lane with the parameter's REAL range (the Scene's own lane has a 0..1 stand-in) and a drawn curve:
// up from 200 Hz to 4 kHz at beat 2, down to 800 Hz at beat 4, then level.
struct CustomLfoScene : Scene {
    juce::RangedAudioParameter* cutoff = nullptr;

    CustomLfoScene() {
        cutoff = findParameterByID(byUuid(targetUuid)->getProcessor(), "cutoff");
        doc().removeLane(lane);
        const auto range = cutoff->getNormalisableRange();
        lane = doc().addLane(track, targetUuid, "cutoff", {range.start, range.end, range.start});
        doc().addBreakpoint(lane, 0.0, 200.0, 0.0f, kLinear);
        doc().addBreakpoint(lane, 2.0, 4000.0, 0.0f, kLinear);
        doc().addBreakpoint(lane, 4.0, 800.0, 0.0f, kLinear);
        doc().addBreakpoint(lane, 8.0, 800.0, 0.0f, kLinear);
        panel().setTrackAutomationExpanded(track, true);
    }

    double normalised(double v) const { return (double)cutoff->convertTo0to1((float)v); }
    synth::LaneId laneId() const { return lane; }
    const synth::AutomationLane& laneNow() { return *doc().getLane(lane); }
    synth::ui::LaneRangeSelection& range() { return panel().getAutomationLanes().getLaneRange(); }
    AutomationLaneHeaderComponent& header() { return *panel().laneHeaderForTest(lane); }

    void selectRange(double start, double end) {
        range().begin(lane, start);
        range().extendTo(end);
    }
    void pick() { header().applyMenuChoice(AutomationLaneHeaderComponent::kCreateCustomLfoMenuId); }

    LFOModule* onlyLfo() {
        const auto lfos = nodesOf<LFOModule>();
        return lfos.size() == 1 ? dynamic_cast<LFOModule*>(lfos.front()->getProcessor()) : nullptr;
    }
    juce::String onlyLfoUuid() { return nodesOf<LFOModule>().front()->properties["uuid"].toString(); }

    // What the first menu item says and whether a click can pick it.
    std::pair<juce::String, bool> firstItem() {
        lane_menu_test::MenuCapture capture;
        header().showMenuAt(juce::PopupMenu::Options());
        juce::PopupMenu::MenuItemIterator it(capture.menu, true);
        it.next();
        return {it.getItem().text, it.getItem().isEnabled};
    }
};

std::vector<synth::AutomationLane::Breakpoint> pointsOf(const synth::AutomationLane& lane) { return lane.points; }

void expectSamePoints(const std::vector<synth::AutomationLane::Breakpoint>& a,
                      const std::vector<synth::AutomationLane::Breakpoint>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].beat, b[i].beat) << i;
        EXPECT_EQ(a[i].value, b[i].value) << i;
        EXPECT_EQ(a[i].tension, b[i].tension) << i;
        EXPECT_EQ(a[i].curve, b[i].curve) << i;
    }
}

double frac(double x) { return x - std::floor(x); }

} // namespace

TEST(AutomationLanesCustomLfoMenuTest, WithoutAHostThatCanModulateTheItemSaysNoCvInput) {
    lane_menu_test::MenuPanel p;
    p.panel.getAutomationLanes().getLaneRange().begin(p.lane, 1.0);
    p.panel.getAutomationLanes().getLaneRange().extendTo(3.0);
    lane_menu_test::MenuCapture capture;
    p.header(p.lane)->showMenuAt(juce::PopupMenu::Options());
    juce::PopupMenu::MenuItemIterator it(capture.menu, true);
    ASSERT_TRUE(it.next());
    EXPECT_EQ(it.getItem().text, "Create custom LFO (no CV input)");
    EXPECT_FALSE(it.getItem().isEnabled);
}

TEST_F(TimelinePanelIntegrationTest, TheLaneMenuOffersCreateCustomLfoFirstAndSaysWhyWhenItIsDisabled) {
    CustomLfoScene s;
    {
        lane_menu_test::MenuCapture capture;
        s.header().showMenuAt(juce::PopupMenu::Options());
        const auto texts = capture.itemTexts();
        ASSERT_GE(texts.size(), 3);
        EXPECT_TRUE(texts[0].startsWith("Create custom LFO"));
        EXPECT_TRUE(texts.contains("Add modulator..."));
        EXPECT_LT(texts.indexOf(texts[0]), texts.indexOf("Add modulator..."));
    }
    EXPECT_EQ(s.firstItem(), (std::pair<juce::String, bool>{"Create custom LFO (select a range first)", false}));

    s.selectRange(1.0, 3.0);
    EXPECT_EQ(s.firstItem(), (std::pair<juce::String, bool>{"Create custom LFO", true}));

    s.selectRange(0.0, 36.0);
    EXPECT_EQ(s.firstItem(), (std::pair<juce::String, bool>{"Create custom LFO (range longer than 8 bars)", false}));

    s.selectRange(5.0, 7.0); // 800 Hz level
    EXPECT_EQ(s.firstItem(), (std::pair<juce::String, bool>{"Create custom LFO (range is flat)", false}));

    s.range().clear();
    EXPECT_EQ(s.firstItem().first, "Create custom LFO (select a range first)");
}

TEST_F(TimelinePanelIntegrationTest, TheCurveEditorBackgroundOpensTheSameMenuWithTheItemFirst) {
    CustomLfoScene s;
    s.selectRange(1.0, 3.0);
    lane_menu_test::MenuCapture capture;
    auto* editor = s.panel().laneEditorForTest(s.lane);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(static_cast<bool>(editor->onLaneMenuRequested));
    editor->onLaneMenuRequested(juce::PopupMenu::Options());
    ASSERT_EQ(capture.count, 1);
    EXPECT_EQ(capture.itemTexts()[0], "Create custom LFO");
}

TEST_F(TimelinePanelIntegrationTest, CreateCustomLfoMakesOneLfoWhoseCustomWavePlaysTheDrawnCurve) {
    CustomLfoScene s;
    const auto original = s.laneNow();
    s.selectRange(1.0, 3.0);
    ASSERT_TRUE(s.nodesOf<LFOModule>().empty());

    s.pick();

    auto* lfo = s.onlyLfo();
    ASSERT_NE(lfo, nullptr) << "exactly one new LFO";
    const auto uuid = s.onlyLfoUuid();
    EXPECT_FLOAT_EQ(s.parameter(uuid, "shape"), (float)LFOModule::kCustomShapeIndex);
    EXPECT_FLOAT_EQ(s.parameter(uuid, "mode"), 1.0f) << "Sync";
    EXPECT_FLOAT_EQ(s.parameter(uuid, "rateSync"), 6.0f) << "two beats = 1/2";
    EXPECT_FLOAT_EQ(s.parameter(uuid, "retrig"), 0.0f);
    EXPECT_FLOAT_EQ(s.parameter(uuid, "bipolar"), 0.0f);
    EXPECT_NEAR(s.parameter(uuid, "phase"), 180.0f, 1e-3f) << "frac(-1 beat / 2 beats) of a cycle";
    const double hi = s.normalised(4000.0);
    const double lo = s.normalised(synth::ui::laneValueAt(original, 1.0));
    EXPECT_NEAR(s.parameter(uuid, "level"), std::ceil((hi - lo) * 100.0) / 100.0, 1e-5)
        << "the drawn height, rounded up to the knob's step";
    EXPECT_FALSE(lfo->getCustomWave().isDefault());

    // Base plus the LFO's CV is the curve that was drawn.
    const double divisionBeats = (double)synth::lfoRateDivisionBeats((int)std::lround(s.parameter(uuid, "rateSync")));
    double worst = 0.0;
    for (int i = 0; i < 300; ++i) {
        const double beat = 1.0 + 2.0 * ((double)i + 0.37) / 300.0;
        const double phase = frac(beat / divisionBeats + s.parameter(uuid, "phase") / 360.0);
        const double played = s.normalised(synth::ui::laneValueAt(s.laneNow(), beat)) +
                              s.parameter(uuid, "level") * lfo->getCustomWave().evaluate((float)phase);
        worst = std::max(worst, std::abs(played - s.normalised(synth::ui::laneValueAt(original, beat))));
    }
    EXPECT_LT(worst, 1e-3);

    // The lane plays flat in the range and as before outside it.
    for (double beat = 0.0; beat < 12.0; beat += 0.0731) {
        if (beat > 1.0 - 1e-5 && beat < 3.0)
            continue;
        EXPECT_NEAR(synth::ui::laneValueAt(s.laneNow(), beat), synth::ui::laneValueAt(original, beat), 1e-3)
            << "beat " << beat;
    }
    EXPECT_NEAR(synth::ui::laneValueAt(s.laneNow(), 2.0), synth::ui::laneValueAt(s.laneNow(), 1.0), 1e-6);

    // Its row and amount lane: 0 outside the range, +1 inside, no lane row of its own.
    ASSERT_NE(s.row(0), nullptr);
    EXPECT_EQ(s.row(1), nullptr);
    EXPECT_TRUE(s.row(0)->getInfo().isLfo);
    const auto attenuverter = s.row(0)->getInfo().attenuverterUuid;
    const auto* amount = synth::ui::amountLaneFor(s.doc(), attenuverter);
    ASSERT_NE(amount, nullptr);
    ASSERT_EQ(amount->points.size(), 3u);
    EXPECT_EQ(amount->points[1].beat, 1.0);
    EXPECT_EQ(amount->points[1].value, 1.0);
    EXPECT_EQ(amount->points[2].beat, 3.0);
    EXPECT_EQ(amount->points[2].value, 0.0);
    EXPECT_EQ(s.panel().laneHeaderForTest(amount->id), nullptr) << "drawn as the row's band, not a lane row";
    EXPECT_FLOAT_EQ(s.depthOf(s.chainsInto(uuid, s.channelFor("cutoff")).front()), 1.0f);

    EXPECT_FALSE(s.range().isActive()) << "the range is spent";
}

TEST_F(TimelinePanelIntegrationTest, CreateCustomLfoIsOneUndoStepAndRedoBringsTheWaveBack) {
    CustomLfoScene s;
    const auto originalPoints = pointsOf(s.laneNow());
    const auto lanesBefore = s.doc().getLane(s.lane) != nullptr;
    ASSERT_TRUE(lanesBefore);
    s.selectRange(1.0, 3.0);
    s.pick();
    ASSERT_NE(s.onlyLfo(), nullptr);
    const auto wave = s.onlyLfo()->getCustomWave();
    const auto uuid = s.onlyLfoUuid();
    const auto flatPoints = pointsOf(s.laneNow());
    ASSERT_NE(flatPoints.size(), originalPoints.size());

    ASSERT_TRUE(s.undo().undo());
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty()) << "the LFO goes";
    EXPECT_EQ(s.nodesOf<AttenuverterModule>().size(), s.baseAttenuverters) << "and its attenuverter";
    EXPECT_EQ(s.row(0), nullptr) << "and its row";
    expectSamePoints(pointsOf(s.laneNow()), originalPoints);
    for (const auto& track : s.doc().getTracks())
        for (const auto& l : track.lanes)
            EXPECT_EQ(l.paramId, "cutoff") << "no amount lane is left behind";

    ASSERT_TRUE(s.undo().redo());
    ASSERT_NE(s.onlyLfo(), nullptr);
    EXPECT_TRUE(s.onlyLfo()->getCustomWave() == wave) << "the custom wave is restored with the node";
    EXPECT_FLOAT_EQ(s.parameter(s.onlyLfoUuid(), "shape"), (float)LFOModule::kCustomShapeIndex);
    EXPECT_EQ(s.onlyLfoUuid(), uuid);
    expectSamePoints(pointsOf(s.laneNow()), flatPoints);
    ASSERT_NE(s.row(0), nullptr);
    EXPECT_NE(synth::ui::amountLaneFor(s.doc(), s.row(0)->getInfo().attenuverterUuid), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, CreateCustomLfoBesideAnExistingLfoAddsASecondRow) {
    CustomLfoScene s;
    s.addLfoFromLaneMenu();
    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    const auto firstUuid = s.nodesOf<LFOModule>().front()->properties["uuid"].toString();
    s.selectRange(1.0, 3.0);

    s.pick();

    ASSERT_EQ(s.nodesOf<LFOModule>().size(), 2u);
    EXPECT_NE(s.row(1), nullptr);
    EXPECT_EQ(s.row(2), nullptr);
    EXPECT_FLOAT_EQ(s.parameter(firstUuid, "shape"), 0.0f) << "the existing LFO is untouched";
    EXPECT_EQ(s.row(0)->getInfo().sourceUuid, firstUuid);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u);
    EXPECT_NE(s.row(0), nullptr);
    EXPECT_EQ(s.row(1), nullptr);
}

TEST_F(TimelinePanelIntegrationTest, ARangeThatCannotBecomeAnLfoChangesNothing) {
    CustomLfoScene s;
    const auto originalPoints = pointsOf(s.laneNow());
    s.selectRange(5.0, 7.0); // flat
    s.pick();
    EXPECT_TRUE(s.nodesOf<LFOModule>().empty());
    expectSamePoints(pointsOf(s.laneNow()), originalPoints);
    EXPECT_TRUE(s.range().isActive()) << "nothing was spent";
}

TEST_F(TimelinePanelIntegrationTest, TheNewRowFadesInAndTheLaneCurveMeltsIntoTheFlatLine) {
    CustomLfoScene s;
    FadeAnimateGuard guard; // after the scene: building the app applies the saved Animations preference
    auto* editor = s.panel().laneEditorForTest(s.lane);
    ASSERT_NE(editor, nullptr);
    editor->forceGlideAnimateForTest(true);
    s.selectRange(1.0, 3.0);

    s.pick();

    ASSERT_NE(s.row(0), nullptr);
    EXPECT_EQ(s.row(0)->getAlpha(), 0.0f) << "the row fades in";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(s.row(0)->getAlpha(), 1.0f);
    EXPECT_TRUE(editor->isMeltingForTest()) << "the old curve is cross-fading into the flat line";
}

TEST_F(TimelinePanelIntegrationTest, UnderAnimationsOffTheCurveLandsAtOnce) {
    CustomLfoScene s;
    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    auto* editor = s.panel().laneEditorForTest(s.lane);
    editor->forceGlideAnimateForTest(true);
    s.selectRange(1.0, 3.0);
    s.pick();
    EXPECT_FALSE(editor->isMeltingForTest());
}
