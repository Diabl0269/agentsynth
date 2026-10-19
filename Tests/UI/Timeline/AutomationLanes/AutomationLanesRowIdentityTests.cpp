// AutomationLanesRowIdentityTests.cpp -- a modulator row whose routing one change replaced in place (Change source,
// Change parameter, the undo of either: a new Attenuverter, so a new key) keeps its row: the pairing of the old and
// new routings, the row re-bound in place (same component, same slot, same fold), its name and controls swapping with
// the in-place swap motion, and its edits reaching the new source. Against a stub host, headless.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesModulatorHost.h"
#include "UI/Graph/CardBody/CardBodySwapMotion.h"
#include "UI/Layout/ControlMotion.h"

using namespace modulator_rows_test;
using synth::TrackKind;
using synth::ui::replacedRoutings;

namespace {

ModulatorInfo envelopeInfo(const juce::String& n) {
    ModulatorInfo info;
    info.sourceUuid = "env-" + n;
    info.sourceTitle = "Envelope " + n;
    info.isEnvelope = true;
    info.attenuverterUuid = "atten-e" + n;
    info.paramId = "cutoff";
    info.targetChannel = 2;
    return info;
}

// Track "Bass" with one cutoff lane showing the host's routings (LFO 1, LFO 2), its lanes open.
struct IdentityScene {
    HostedLanes h;
    synth::TrackId bass;
    synth::LaneId cutoff;

    IdentityScene() {
        h.host.modulators = {lfoInfo("1"), lfoInfo("2")};
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }

    synth::ui::TimelinePanelComponent& panel() { return h.f.panel; }
    synth::ui::ModulatorRow* row(int i) { return panel().modulatorRowForTest(cutoff, i); }
    // The host's routing `index` replaced in place by `info` (what a Change source does to the graph), then the
    // refresh a graph change sends.
    void replace(int index, const ModulatorInfo& info) {
        h.host.modulators[(size_t)index] = info;
        panel().refreshModulators();
    }
};
} // namespace

TEST(AutomationLanesRowIdentityTest, ReplacedRoutingsPairBySourceFirstThenByRowOrder) {
    const auto lfo1 = lfoInfo("1");
    const auto lfo2 = lfoInfo("2");
    // Change parameter: both routings re-cabled from the same sources, the new rows in the other order.
    auto lfo1Moved = lfo1;
    lfo1Moved.attenuverterUuid = "atten-9";
    auto lfo2Moved = lfo2;
    lfo2Moved.attenuverterUuid = "atten-8";
    const auto moved = replacedRoutings({lfo1, lfo2}, {lfo2Moved, lfo1Moved});
    ASSERT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.at(lfo1.key()), lfo1Moved.key()) << "matched by source, never by position";
    EXPECT_EQ(moved.at(lfo2.key()), lfo2Moved.key());

    // Change source: a new source in the first slot pairs with the routing it replaced.
    const auto env = envelopeInfo("1");
    const auto swapped = replacedRoutings({lfo1, lfo2}, {env, lfo2});
    ASSERT_EQ(swapped.size(), 1u);
    EXPECT_EQ(swapped.at(lfo1.key()), env.key());
}

TEST(AutomationLanesRowIdentityTest, AReallyRemovedOrAddedRoutingIsNeverPaired) {
    const auto lfo1 = lfoInfo("1");
    const auto lfo2 = lfoInfo("2");
    EXPECT_TRUE(replacedRoutings({lfo1, lfo2}, {lfo2}).empty()) << "a removal";
    EXPECT_TRUE(replacedRoutings({lfo1}, {lfo1, lfo2}).empty()) << "an addition";
    EXPECT_TRUE(replacedRoutings({lfo1}, {lfo1}).empty()) << "nothing changed";
    EXPECT_TRUE(replacedRoutings({lfo1}, {lfoInfo("2"), envelopeInfo("1")}).empty())
        << "one went and two came: not a replacement";
}

TEST(AutomationLanesRowIdentityTest, ASourceChangeKeepsTheRowItsSlotAndItsFold) {
    IdentityScene s;
    auto* first = s.row(0);
    auto* second = s.row(1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    clickButton(first->getFoldArrow());
    ASSERT_TRUE(first->isCollapsed());
    const int foldedHeight = s.panel().modulatorRowBoundsForTest(s.cutoff, 0).getHeight();

    auto lfo9 = lfoInfo("9"); // a new source through a new Attenuverter: a new key
    s.replace(0, lfo9);

    EXPECT_EQ(s.row(0), first) << "the row is re-bound, not rebuilt";
    EXPECT_EQ(s.row(1), second);
    EXPECT_EQ(first->getInfo().key(), lfo9.key());
    EXPECT_EQ(first->getNameButton().getTitle(), "Change source of LFO 9 on Cutoff");
    EXPECT_EQ(first->getTitle(), "LFO 9 mod lane on Cutoff");
    EXPECT_TRUE(first->isCollapsed()) << "the fold goes with the slot";
    EXPECT_TRUE(s.panel().getAutomationLanes().isModulatorCollapsed(lfo9.key()));
    EXPECT_FALSE(s.panel().getAutomationLanes().isModulatorCollapsed(lfoInfo("1").key())) << "no stale fold left";
    EXPECT_EQ(s.panel().modulatorRowBoundsForTest(s.cutoff, 0).getHeight(), foldedHeight);
}

TEST(AutomationLanesRowIdentityTest, TheOldNameShrinksAwayThenTheNewOneGrowsInPlace) {
    FadeAnimateGuard animate;
    IdentityScene s;
    auto* first = s.row(0);
    ASSERT_NE(first, nullptr);
    first->setForceSwapMotionForTest(true);
    const int children = first->getNumChildComponents();

    s.replace(0, lfoInfo("9"));

    ASSERT_EQ(s.row(0), first);
    ASSERT_TRUE(first->isSwapMotionRunning());
    EXPECT_FALSE(first->getNameButton().isVisible()) << "the new name waits while the old one shrinks";
    EXPECT_GT(first->getNumChildComponents(), children) << "pictures of the leaving parts";
    first->stepSwapMotionForTest(synth::ui::control_motion::kSwapShrinkMs / 2.0);
    EXPECT_FALSE(first->getNameButton().isVisible());
    first->stepSwapMotionForTest(synth::ui::control_motion::kSwapShrinkMs + 1.0);
    EXPECT_TRUE(first->getNameButton().isVisible()) << "then it grows in";
    EXPECT_EQ(first->getNumChildComponents(), children) << "and the pictures are gone";
    first->stepSwapMotionForTest(synth::SwapMotion::totalMs());
    EXPECT_FALSE(first->isSwapMotionRunning());
    EXPECT_TRUE(first->getShapeCombo().isVisible());
    EXPECT_EQ(first->getNameButton().getTransform(), juce::AffineTransform());
}

TEST(AutomationLanesRowIdentityTest, TheSwapLandsAtOnceUnderReduceMotion) {
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    IdentityScene s;
    auto* first = s.row(0);
    first->setForceSwapMotionForTest(true);
    s.replace(0, lfoInfo("9"));
    ASSERT_EQ(s.row(0), first);
    EXPECT_FALSE(first->isSwapMotionRunning());
    EXPECT_TRUE(first->getNameButton().isVisible());
    EXPECT_EQ(first->getNameButton().getTitle(), "Change source of LFO 9 on Cutoff");
}

TEST(AutomationLanesRowIdentityTest, AnLfoRowBecomingAnEnvelopeHidesItsLfoControlsAndBackShowsThem) {
    IdentityScene s;
    auto* first = s.row(0);
    ASSERT_TRUE(first->getShapeCombo().isVisible());

    s.replace(0, envelopeInfo("1"));
    ASSERT_EQ(s.row(0), first);
    EXPECT_FALSE(first->getShapeCombo().isVisible());
    EXPECT_FALSE(first->getSyncToggle().isVisible());
    EXPECT_FALSE(first->getShapeIcon().isVisible());
    EXPECT_FALSE(first->getRateSlider().isVisible());
    EXPECT_FALSE(first->getSyncRateCombo().isVisible());
    EXPECT_EQ(first->getNameButton().getTitle(), "Change source of Envelope 1 on Cutoff");

    s.replace(0, lfoInfo("1")); // what an undo does
    ASSERT_EQ(s.row(0), first);
    EXPECT_TRUE(first->getShapeCombo().isVisible());
    EXPECT_TRUE(first->getSyncToggle().isVisible());
    EXPECT_TRUE(first->getShapeIcon().isVisible());
    EXPECT_EQ(first->getNameButton().getTitle(), "Change source of LFO 1 on Cutoff");
}

TEST(AutomationLanesRowIdentityTest, AnEditAfterTheSourceChangedReachesTheNewSource) {
    IdentityScene s;
    auto* first = s.row(0);
    s.replace(0, lfoInfo("9"));
    ASSERT_EQ(s.row(0), first);

    first->getShapeCombo().setSelectedId(3, juce::sendNotificationSync); // Sawtooth, as a pick in the combo
    EXPECT_FLOAT_EQ(s.h.host.values["lfo-9.shape"], 2.0f);
    EXPECT_NE(s.h.host.values["lfo-1.shape"], 2.0f) << "the old LFO is left alone";

    first->getRateSlider().setValue(4.0, juce::sendNotificationSync); // a key press on the bar: one edit of its own
    EXPECT_FLOAT_EQ(s.h.host.values["lfo-9.rateHz"], 4.0f);
}
