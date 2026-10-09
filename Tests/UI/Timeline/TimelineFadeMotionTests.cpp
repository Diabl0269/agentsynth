// TimelineFadeMotionTests.cpp (docs/layout/animation.md, "Fading things in and out"): the Timeline's own fades -- the
// piano roll cross-fading with the clip lanes, the routing pane's controls and a track header's chips and fold arrow
// fading in and out while the row around them slides. A control that is going away stays visible until its fade ends.
// The fades are forced on for off-screen components and stepped by hand (FadeVisibility::stepAllForTest).
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "AppUndoManager.h"
#include "AutomationLanes/AutomationLanesTestFixture.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackChannelLinkSurface.h"
#include <gtest/gtest.h>

namespace {

using synth::TrackKind;
using synth::ui::FadeVisibility;
using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineTrackHeaderComponent;

void clickRow(TimelinePanelComponent& panel, int index) {
    auto* row = panel.getTrackHeaderAt(index);
    ASSERT_NE(row, nullptr);
    row->mouseDown(makeClickEvent(*row, {110.0f, 3.0f}));
}

// A host with a channel-link surface whose answer a test can change.
struct LinkSurface : synth::ui::TrackChannelLinkSurface {
    ChannelInfo info;
    ChannelInfo getChannelInfo(synth::TrackId) const override { return info; }
    float getChannelMeterPeak(synth::TrackId) const override { return 0.0f; }
    bool renameLinkedTrackAndChannel(synth::TrackId, const juce::String&) override { return false; }
    std::unique_ptr<synth::ui::ColourPickerPopup> buildOwnedMacroColourPicker(synth::TrackId,
                                                                              juce::PropertiesFile*) override {
        return nullptr;
    }
    bool toggleLinkedChannelMuted(synth::TrackId) override { return false; }
    bool toggleLinkedChannelSoloed(synth::TrackId) override { return false; }
    void revealChannelForTrack(synth::TrackId) override {}
};

struct LinkHost : synth::ui::TrackHeaderHost {
    LinkSurface link;
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Track In"; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    synth::ui::TrackChannelLinkSurface* getChannelLinkSurface() override { return &link; }
};

} // namespace

TEST(TimelineFadeMotionTest, OpeningAndClosingThePianoRollCrossFadesWithTheClipLanes) {
    FadeAnimateGuard guard;
    automation_lanes_test::LanesPanel f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "C1");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.panel.getClipLaneArea().isVisible());

    f.panel.openPianoRoll(clip);
    EXPECT_TRUE(f.panel.getPianoRoll().isOpen()) << "the state lands before the fade";
    EXPECT_TRUE(f.panel.getPianoRoll().isVisible());
    EXPECT_EQ(f.panel.getPianoRoll().getAlpha(), 0.0f) << "the roll fades in";
    EXPECT_TRUE(f.panel.getClipLaneArea().isVisible()) << "the lanes stay until their fade ends";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(f.panel.getPianoRoll().getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(f.panel.getClipLaneArea().getAlpha(), 0.5f, 0.01f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(f.panel.getClipLaneArea().isVisible());
    EXPECT_EQ(f.panel.getPianoRoll().getAlpha(), 1.0f);

    f.panel.closePianoRoll();
    EXPECT_FALSE(f.panel.getPianoRoll().isOpen());
    EXPECT_TRUE(f.panel.getPianoRoll().isVisible()) << "the roll fades out before it goes";
    EXPECT_TRUE(f.panel.getClipLaneArea().isVisible());
    EXPECT_EQ(f.panel.getClipLaneArea().getAlpha(), 0.0f);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(f.panel.getPianoRoll().isVisible());
    EXPECT_EQ(f.panel.getClipLaneArea().getAlpha(), 1.0f);
}

TEST(TimelineFadeMotionTest, OffScreenThePianoRollSwapsAtOnce) {
    automation_lanes_test::LanesPanel f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = f.doc.addClip(track, 0.0, 8.0, "C1");
    f.panel.openPianoRoll(clip);
    EXPECT_TRUE(f.panel.getPianoRoll().isVisible());
    EXPECT_FALSE(f.panel.getClipLaneArea().isVisible());
    f.panel.closePianoRoll();
    EXPECT_FALSE(f.panel.getPianoRoll().isVisible());
    EXPECT_TRUE(f.panel.getClipLaneArea().isVisible());
    EXPECT_EQ(f.panel.getClipLaneArea().getAlpha(), 1.0f);
}

TEST(TimelineFadeMotionTest, TheRoutingPaneControlsFadeAndTheRowsBelowSlide) {
    FadeAnimateGuard guard;
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc;
    TimelinePanelComponent panel;
    panel.setSize(1200, 400);
    panel.setTimelineDoc(&doc);
    panel.getSidePane().setOpen(true);
    doc.addTrack(TrackKind::Midi, "Lead");
    doc.addTrack(TrackKind::Audio, "Vox");
    auto& pane = panel.getRoutingPane();

    clickRow(panel, 0); // a MIDI track: the canvas node and the MIDI destinations come in
    EXPECT_TRUE(pane.getCanvasNodeButtonForTest().isVisible());
    EXPECT_EQ(pane.getCanvasNodeButtonForTest().getAlpha(), 0.0f) << "it fades in";
    EXPECT_TRUE(pane.isMidiDestinationsShownForTest());
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(pane.getCanvasNodeButtonForTest().getAlpha(), 1.0f);
    const int midiHeight = pane.getContentHeight();

    clickRow(panel, 1); // an audio track: the MIDI destinations go
    EXPECT_TRUE(pane.isMidiDestinationsShownForTest()) << "hidden only after the fade ends";
    EXPECT_TRUE(pane.getCanvasNodeButtonForTest().isVisible()) << "an audio track still has a canvas node";
    EXPECT_EQ(pane.getContentHeight(), midiHeight) << "nothing moves at frame 0";
    FadeVisibility::stepAllForTest(0.5f);
    const int midway = pane.getContentHeight();
    EXPECT_LT(midway, midiHeight) << "the rows below slide up with the fade";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(pane.isMidiDestinationsShownForTest());
    EXPECT_LT(pane.getContentHeight(), midway);
    EXPECT_EQ(pane.getMidiDestinationsButtonForTest().getAlpha(), 1.0f);
}

TEST(TimelineFadeMotionTest, ATrackHeadersChannelChipFadesInAndOutAndTheRowFollows) {
    FadeAnimateGuard guard;
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    LinkHost host;
    TimelineTrackHeaderComponent header(doc, track, &host);
    header.setSize(160, TimelineTrackHeaderComponent::kRowHeight);
    auto& chip = header.getChannelChip();
    auto& binding = header.getBindingChip();
    FadeVisibility::stepAllForTest(1.0f); // the chip the constructor's first refresh fades out is gone
    EXPECT_FALSE(chip.isVisible());
    const int fullBinding = binding.getWidth();

    host.link.info.hasChannel = true;
    host.link.info.channelName = "Lead";
    header.refreshFromDoc();
    EXPECT_TRUE(chip.isVisible());
    EXPECT_EQ(chip.getAlpha(), 0.0f) << "it fades in";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(chip.getAlpha(), 0.5f, 0.01f);
    EXPECT_GT(chip.getWidth(), 0);
    EXPECT_LT(binding.getWidth(), fullBinding) << "the binding chip gives way as the channel chip grows";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(chip.getAlpha(), 1.0f);
    const int sharedBinding = binding.getWidth();

    host.link.info.hasChannel = false;
    header.refreshFromDoc();
    EXPECT_TRUE(chip.isVisible()) << "hidden only after the fade ends";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_GT(binding.getWidth(), sharedBinding) << "the binding chip takes the room back as the chip shrinks";
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(chip.isVisible());
    EXPECT_EQ(chip.getAlpha(), 1.0f);
    EXPECT_EQ(binding.getWidth(), fullBinding);
}

TEST(TimelineFadeMotionTest, TheFoldArrowFadesInWhenTheUnassignedSectionGainsItsFirstLane) {
    FadeAnimateGuard guard;
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Automation, "Automation");
    LinkHost host;
    TimelineTrackHeaderComponent header(doc, track, &host);
    header.setSize(160, TimelineTrackHeaderComponent::kRowHeight);
    ASSERT_TRUE(header.isSectionHeader());
    EXPECT_FALSE(header.getFoldArrow().isVisible()) << "an empty Unassigned section has no arrow";

    synth::AutomationLane::RangeSnapshot range;
    doc.addLane(track, "node", "cutoff", range);
    header.refreshFromDoc();
    EXPECT_TRUE(header.getFoldArrow().isVisible());
    EXPECT_EQ(header.getFoldArrow().getAlpha(), 0.0f) << "it fades in";
    FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(header.getFoldArrow().getAlpha(), 0.5f, 0.01f);
    EXPECT_GT(header.getFoldArrow().getWidth(), 0);
    FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(header.getFoldArrow().getAlpha(), 1.0f);
}

TEST(TimelineFadeMotionTest, OffScreenAHeadersChipLandsAtOnce) {
    synth::TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    LinkHost host;
    TimelineTrackHeaderComponent header(doc, track, &host);
    header.setSize(160, TimelineTrackHeaderComponent::kRowHeight);
    host.link.info.hasChannel = true;
    host.link.info.channelName = "Lead";
    header.refreshFromDoc();
    EXPECT_TRUE(header.getChannelChip().isVisible());
    EXPECT_EQ(header.getChannelChip().getAlpha(), 1.0f);
    host.link.info.hasChannel = false;
    header.refreshFromDoc();
    EXPECT_FALSE(header.getChannelChip().isVisible());
}
