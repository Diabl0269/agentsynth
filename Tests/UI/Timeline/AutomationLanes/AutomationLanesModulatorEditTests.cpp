// AutomationLanesModulatorEditTests.cpp -- a modulator row's controls against a real MainComponent: shape,
// rate and sync edits and the band's amount drag change the live LFO / attenuverter parameters, each is one undo step,
// and an edit on the canvas card reaches the row. Also: with the LFO modulating a parameter, the CV reaching that
// parameter moves across a rendered span, and at depth 0 it does not; and a saved project with a modulator
// inside a macro reopens with its row.

#include "AutomationLanesModulatorFixture.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"

using namespace modulator_test;
using namespace automation_lanes_test;

namespace {
juce::String onlyLfoUuid(Scene& s) {
    const auto lfos = s.nodesOf<LFOModule>();
    return lfos.size() == 1 ? lfos.front()->properties["uuid"].toString() : juce::String();
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, RowEditsChangeTheLiveParametersAndEachIsOneUndoStep) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfo = onlyLfoUuid(s);
    ASSERT_TRUE(lfo.isNotEmpty());
    const auto attenUuid = s.row()->getInfo().attenuverterUuid;
    ASSERT_TRUE(attenUuid.isNotEmpty());

    // Shape: a pick from the combo.
    s.row()->getShapeCombo().setSelectedId(3, juce::sendNotificationSync);
    EXPECT_FLOAT_EQ(s.parameter(lfo, "shape"), 2.0f) << "Sawtooth";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FLOAT_EQ(s.parameter(lfo, "shape"), 0.0f);
    EXPECT_EQ(s.row()->getShapeCombo().getSelectedId(), 1) << "the row follows the undo";

    // Sync rate.
    s.row()->getSyncRateCombo().setSelectedId(4, juce::sendNotificationSync);
    EXPECT_FLOAT_EQ(s.parameter(lfo, "rateSync"), 3.0f) << "1/16";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FLOAT_EQ(s.parameter(lfo, "rateSync"), 5.0f) << "back to the 1/4 default";

    // Amount: a real upward drag on the band's flat line is one graph step, however many moves it took, and
    // creates no amount lane.
    auto* band = s.panel().modulatorBandForTest(s.lane, 0);
    ASSERT_NE(band, nullptr);
    const auto mid = band->getLocalBounds().getCentre().toFloat();
    // The band's full height is the whole 200% span, so a whole-pixel eighth of it is about +25%.
    const int up = juce::roundToInt((float)band->getHeight() / 8.0f);
    const double expected = 0.5 + 2.0 * up / band->getHeight();
    dragAcross(*band, mid, mid.translated(0.0f, -(float)up), 6);
    EXPECT_NEAR(s.parameter(attenUuid, "amount"), expected, 1.0e-3);
    EXPECT_EQ(s.doc().getLaneForParam(attenUuid, "amount"), nullptr);
    s.panel().updateFromTransport(synth::TransportService::PositionSnapshot{}, 0.0);
    EXPECT_EQ(s.row()->getAmountText(), synth::ui::amountText(expected));
    ASSERT_TRUE(s.undo().undo());
    EXPECT_FLOAT_EQ(s.parameter(attenUuid, "amount"), 0.5f);
    s.panel().updateFromTransport(synth::TransportService::PositionSnapshot{}, 0.0);
    EXPECT_EQ(s.row()->getAmountText(), "+50%");

    // Sync off: the Hz bar replaces the sync-rate combo, and a drag on it moves the free rate.
    clickButton(s.row()->getSyncToggle());
    EXPECT_FLOAT_EQ(s.parameter(lfo, "mode"), 0.0f);
    auto& rate = s.row()->getRateSlider();
    ASSERT_TRUE(rate.isVisible());
    const float rateBefore = s.parameter(lfo, "rateHz");
    const auto rmid = rate.getLocalBounds().getCentre().toFloat();
    dragAcross(rate, rmid, rmid.translated((float)rate.getWidth() / 3.0f, 0.0f), 4);
    EXPECT_NE(s.parameter(lfo, "rateHz"), rateBefore);
    ASSERT_TRUE(s.undo().undo()); // the rate drag
    EXPECT_FLOAT_EQ(s.parameter(lfo, "rateHz"), rateBefore);
    ASSERT_TRUE(s.undo().undo()); // the sync click
    EXPECT_FLOAT_EQ(s.parameter(lfo, "mode"), 1.0f);
    EXPECT_TRUE(s.row()->getSyncRateCombo().isVisible());

    // An edit on the canvas card (here: straight on the parameter) reaches the row on the panel's poll.
    auto* shape = findParameterByID(s.byUuid(lfo)->getProcessor(), "shape");
    shape->setValueNotifyingHost(shape->convertTo0to1(3.0f));
    s.panel().updateFromTransport(synth::TransportService::PositionSnapshot{}, 0.0);
    EXPECT_EQ(s.row()->getShapeCombo().getSelectedId(), 4);
}

TEST_F(TimelinePanelIntegrationTest, TheModulatedParametersCvMovesAcrossARenderAndStopsAtDepthZero) {
    Scene s;
    s.addLfoFromLaneMenu();
    const auto lfo = onlyLfoUuid(s);
    ASSERT_TRUE(lfo.isNotEmpty());
    // Free-running at 5 Hz, so the test needs no transport.
    for (const auto& [id, value] : {std::pair<const char*, float>{"mode", 0.0f}, {"rateHz", 5.0f}}) {
        auto* p = findParameterByID(s.byUuid(lfo)->getProcessor(), id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    }
    const auto chains = s.chainsInto(lfo, s.channelFor("cutoff"));
    ASSERT_EQ(chains.size(), 1u);
    auto* atten =
        dynamic_cast<AttenuverterModule*>(s.graph().getNodeForId(chains.front().attenuverterNodeID)->getProcessor());
    ASSERT_NE(atten, nullptr);

    constexpr double kRate = 44100.0;
    constexpr int kBlock = 512;
    auto& g = s.graph();
    g.setPlayConfigDetails(0, 2, kRate, kBlock);
    g.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(juce::jmax(2, g.getMainBusNumOutputChannels()), kBlock);
    const auto renderSpan = [&](int blocks) {
        float lo = 1.0f, hi = -1.0f;
        for (int b = 0; b < blocks; ++b) {
            buffer.clear();
            juce::MidiBuffer midi;
            g.processBlock(buffer, midi);
            lo = std::min(lo, atten->getLastModValue());
            hi = std::max(hi, atten->getLastModValue());
        }
        return hi - lo;
    };

    EXPECT_GT(renderSpan(60), 0.3f) << "half depth of a full-level bipolar LFO swings the CV";

    auto* amount = findParameterByID(atten, "amount");
    amount->setValueNotifyingHost(amount->convertTo0to1(0.0f));
    renderSpan(4); // let the amount's 10 ms smoothing land
    EXPECT_LT(renderSpan(60), 1.0e-4f) << "depth 0: the parameter is not modulated";
    g.releaseResources();
}

TEST_F(TimelinePanelIntegrationTest, AProjectWithAModulatorInsideAMacroReopensWithItsRow) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-modulator-load-" + juce::Uuid().toString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    {
        Scene s;
        synth::Macro macro;
        macro.name = "Tone";
        macro.collapsed = false;
        macro.bounds = {300, 300, 400, 300};
        macro.members.push_back(s.targetUuid);
        s.mc.getGraphEditor().getMacros().add(macro);
        s.mc.getGraphEditor().updateComponents();
        s.addLfoFromLaneMenu();
        const auto lfo = onlyLfoUuid(s);
        ASSERT_TRUE(lfo.isNotEmpty());

        ASSERT_TRUE(s.mc.saveProjectForTest(bundle));
        ASSERT_TRUE(s.mc.openProjectForTest(bundle));

        const auto* lane = s.doc().getLaneForParam(s.targetUuid, "cutoff");
        ASSERT_NE(lane, nullptr);
        s.panel().setTrackAutomationExpanded(s.doc().getTrackForLane(lane->id)->id, true);
        auto* row = s.panel().modulatorRowForTest(lane->id, 0);
        ASSERT_NE(row, nullptr) << "the row is derived from the reopened graph";
        EXPECT_TRUE(row->getInfo().isLfo);
        EXPECT_EQ(row->getInfo().sourceUuid, lfo);
        EXPECT_FLOAT_EQ(s.parameter(row->getInfo().attenuverterUuid, "amount"), 0.5f);
        const auto* owner = s.mc.getGraphEditor().getMacros().findByMember(lfo);
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(owner->name, "Tone");
    }
    scratch.deleteRecursively();
}

// The row's sync-rate picker offers the app-wide tempo-division list, shortest first; item id = index + 1.
TEST_F(TimelinePanelIntegrationTest, SyncRateComboListsTheSharedDivisionsShortestFirst) {
    Scene s;
    s.addLfoFromLaneMenu();
    auto& combo = s.row()->getSyncRateCombo();
    const auto& divisions = synth::envelopeNoteDivisions();
    ASSERT_EQ(combo.getNumItems(), divisions.size());
    for (int i = 0; i < divisions.size(); ++i) {
        EXPECT_EQ(combo.getItemText(i), divisions[i]);
        EXPECT_EQ(combo.getItemId(i), i + 1);
    }
    EXPECT_EQ(combo.getItemText(0), "1/128") << "shortest first";
    EXPECT_EQ(combo.getItemText(divisions.size() - 1), "1/1");
}
