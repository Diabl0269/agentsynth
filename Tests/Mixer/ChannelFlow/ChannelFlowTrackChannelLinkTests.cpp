// =================================================================================================
// FRO14 (P9-4, docs/mixer/mixer.md#channels-follow-audio-not-tracks) — what a LINK actually does, driven through the
// real track-header buttons: names sync both ways, colour previews live and commits as one undo step, a linked track's
// M/S drive its CHANNEL (not note gating) while a shared channel's tracks keep today's note gating,
// and the channel chip names/reveals the channel with a repaint-gated meter.
//
// Most cases run on a rig of real collaborators (AudioEngine + GraphEditor + TimelineDoc +
// AppUndoManager + TrackChannelLinkController + real TimelineTrackHeaderComponents over a stub
// TrackHeaderHost) rather than a whole MainComponent: that rig is renderable, so the mixed
// linked-solo/shared-solo case can assert what the mix actually does rather than only what a flag
// says. The last test closes the loop on a REAL MainComponent, proving the wiring is reachable from
// the shipped "+ Track -> Audio" flow and not just from this file.
//
// The link rule itself is proved at the Core layer in ChannelFlowTrackChannelLinkCoreTests.cpp.
// =================================================================================================

#include "AppUndoManager.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/PeakMeterLatch.h"
#include "Mixer/TrackChannelLink.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MeterColourStops.h"
#include "UI/Mixer/MixerMeterScale.h"
#include "UI/Theme/Theme.h"
#include "UI/Timeline/ChannelChipComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackChannelLinkController.h"
#include <gtest/gtest.h>
#include <memory>

using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackChannelLinkController;

namespace {

// The minimum TrackHeaderHost a header needs, plus the one thing this file is about: the channel
// link surface. Doc edits route through a REAL AppUndoManager so the undo-step assertions below
// exercise the actual contract.
class LinkStubHost : public synth::ui::TrackHeaderHost {
public:
    LinkStubHost(synth::TimelineDoc& doc, AppUndoManager& undo, synth::ui::TrackChannelLinkSurface& link)
        : doc_(doc)
        , undo_(undo)
        , link_(link) {}

    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Track In"; }
    void bindTrackTo(synth::TrackId track, const juce::String& uuid) override { doc_.setTrackBinding(track, uuid); }
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        undo_.recordTimelineChange(doc_, mutation);
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    synth::ui::TrackChannelLinkSurface* getChannelLinkSurface() override { return &link_; }

private:
    synth::TimelineDoc& doc_;
    AppUndoManager& undo_;
    synth::ui::TrackChannelLinkSurface& link_;
};

// One linked channel (track "Lead" alone into Osc -> Strip -> Audio Output, boxed as macro "Lead")
// and one SHARED channel (tracks "Kick"/"Snare" both into one Sampler -> Strip -> Audio Output,
// boxed as macro "Drums"). Both strips render into the same output, which is what lets the mixed
// solo case below assert on the real mix.
struct LinkRigApp {
    HostedPatchCFT patch;
    AppUndoManager undo;
    synth::TimelineDoc doc;
    GraphEditor editor{patch.engine, &undo};
    TrackChannelLinkController link{patch.engine, doc, undo, editor};
    LinkStubHost host{doc, undo, link};

    synth::TrackId lead, kick, snare;
    juce::AudioProcessorGraph::Node* leadStrip = nullptr;
    juce::AudioProcessorGraph::Node* sharedStrip = nullptr;
    juce::String leadMacroId, drumsMacroId;
    std::vector<std::unique_ptr<TimelineTrackHeaderComponent>> headers;

    juce::AudioProcessorGraph& graph() { return patch.engine.getGraph(); }

    LinkRigApp() {
        // Mirrors MainComponent's own restore hooks: every undo/redo ends in the reconcile + publish
        // seam, which is what re-settles the engine's solo count and the headers' displayed state.
        undo.setRestoreHooks({}, [this] {
            link.reconcileLinkedTracks();
            patch.engine.publishTimeline(doc);
            for (auto& header : headers)
                header->refreshFromDoc();
        });

        juce::String leadInUuid, leadStripUuid, oscUuid;
        auto* leadIn = addPlainNodeCFT(graph(), "Track In", {0, 0}, leadInUuid);
        auto* osc = addPlainNodeCFT(graph(), "Oscillator", {200, 0}, oscUuid);
        leadStrip = addPlainNodeCFT(graph(), "Channel Strip", {400, 0}, leadStripUuid);
        wireMidi(leadIn, osc);
        wireStereo(osc, leadStrip);
        wireStripToOutput(leadStrip);
        lead = addTrack("Lead", leadInUuid);
        leadMacroId =
            editor.getMacroController().addMacroForMembers({leadInUuid, oscUuid, leadStripUuid}, "Lead", {0, 0});

        juce::String kickInUuid, snareInUuid, samplerUuid, sharedStripUuid;
        auto* kickIn = addPlainNodeCFT(graph(), "Track In", {0, 400}, kickInUuid);
        auto* snareIn = addPlainNodeCFT(graph(), "Track In", {0, 500}, snareInUuid);
        auto* sampler = addPlainNodeCFT(graph(), "Oscillator", {200, 400}, samplerUuid);
        sharedStrip = addPlainNodeCFT(graph(), "Channel Strip", {400, 400}, sharedStripUuid);
        wireMidi(kickIn, sampler);
        wireMidi(snareIn, sampler);
        wireStereo(sampler, sharedStrip);
        wireStripToOutput(sharedStrip);
        kick = addTrack("Kick", kickInUuid);
        snare = addTrack("Snare", snareInUuid);
        drumsMacroId = editor.getMacroController().addMacroForMembers(
            {kickInUuid, snareInUuid, samplerUuid, sharedStripUuid}, "Drums", {0, 400});

        editor.updateComponents();
        for (const auto& track : doc.getTracks())
            headers.push_back(std::make_unique<TimelineTrackHeaderComponent>(doc, track.id, &host));
    }

    synth::TrackId addTrack(const juce::String& name, const juce::String& bindingUuid) {
        const auto id = doc.addTrack(synth::TrackKind::Midi, name);
        doc.setTrackBinding(id, bindingUuid);
        return id;
    }
    void wireMidi(juce::AudioProcessorGraph::Node* from, juce::AudioProcessorGraph::Node* to) {
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        graph().addConnection({{from->nodeID, midi}, {to->nodeID, midi}});
    }
    void wireStereo(juce::AudioProcessorGraph::Node* from, juce::AudioProcessorGraph::Node* strip) {
        const int fromRight = dynamic_cast<ModuleBase*>(from->getProcessor())->rightAudioLegChannel();
        graph().addConnection({{from->nodeID, 0}, {strip->nodeID, 0}});
        graph().addConnection({{from->nodeID, fromRight}, {strip->nodeID, ChannelStripModule::kRightBase}});
    }
    void wireStripToOutput(juce::AudioProcessorGraph::Node* strip) {
        graph().addConnection({{strip->nodeID, 0}, {patch.output->nodeID, 0}});
        graph().addConnection({{strip->nodeID, ChannelStripModule::kRightBase}, {patch.output->nodeID, 1}});
    }

    TimelineTrackHeaderComponent& header(synth::TrackId track) {
        for (auto& h : headers)
            if (h->getTrackId() == track)
                return *h;
        jassertfalse;
        return *headers.front();
    }
    ChannelStripModule* strip(juce::AudioProcessorGraph::Node* node) {
        return dynamic_cast<ChannelStripModule*>(node->getProcessor());
    }
    /** Re-resolves a strip by uuid: an undo rebuilds the graph from JSON and REASSIGNS NodeIDs, so
     *  no pointer or id taken before one survives it. */
    ChannelStripModule* stripForMacro(const juce::String& macroId) {
        const auto* macro = editor.getMacros().find(macroId);
        if (macro == nullptr)
            return nullptr;
        for (const auto& uuid : macro->members)
            if (auto* node = nodeForUuidCFT(graph(), uuid))
                if (auto* s = dynamic_cast<ChannelStripModule*>(node->getProcessor()))
                    return s;
        return nullptr;
    }
    void render(int blocks) {
        patch.engine.publishTimeline(doc);
        patch.render(blocks);
    }
};

} // namespace

// -------------------------------------------------------------------------------------------
// (a) Names sync both ways
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, RenamingALinkedTrackRenamesItsChannelAsOneUndoStep) {
    LinkRigApp rig;
    ASSERT_FALSE(rig.undo.canUndo()) << "the rig is built without recording, so the stack starts empty";

    // The REAL label edit path, not a direct doc write.
    auto& label = rig.header(rig.lead).getNameLabel();
    label.setText("Vocals", juce::sendNotificationSync);

    EXPECT_EQ(rig.doc.getTrack(rig.lead)->name, "Vocals");
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->name, "Vocals");

    // ONE undo step: a single Cmd+Z restores BOTH halves and leaves nothing behind. (Counting
    // getEditSerial() would read 2 here -- a compound transaction pushes one action per DOMAIN,
    // timeline and macro; what "one step" means to the user is one undo(), which is what this
    // asserts.)
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.doc.getTrack(rig.lead)->name, "Lead");
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->name, "Lead") << "one Cmd+Z restores BOTH";
    EXPECT_FALSE(rig.undo.canUndo()) << "...and there is no second step left to undo";
}

TEST_F(ChannelFlowTest, RenamingAChannelMacroRenamesItsLinkedTrackAsOneUndoStep) {
    LinkRigApp rig;
    ASSERT_FALSE(rig.undo.canUndo());

    rig.editor.getMacroController().renameMacro(rig.leadMacroId, "Vocals");

    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->name, "Vocals");
    EXPECT_EQ(rig.doc.getTrack(rig.lead)->name, "Vocals") << "renaming the channel renames the track";

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.doc.getTrack(rig.lead)->name, "Lead");
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->name, "Lead");
    EXPECT_FALSE(rig.undo.canUndo()) << "one Cmd+Z, not two - the track name joined renameMacro's transaction";
}

TEST_F(ChannelFlowTest, RenamingASharedChannelsTrackLeavesTheChannelAlone) {
    LinkRigApp rig;
    rig.header(rig.kick).getNameLabel().setText("Kik", juce::sendNotificationSync);

    EXPECT_EQ(rig.doc.getTrack(rig.kick)->name, "Kik");
    EXPECT_EQ(rig.editor.getMacros().find(rig.drumsMacroId)->name, "Drums")
        << "a channel fed by several tracks keeps its own independently-chosen name";
}

// -------------------------------------------------------------------------------------------
// (b) Colour syncs live
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, LiveColourPreviewUpdatesTrackAndChannelOnEveryTickWithNoUndoStep) {
    LinkRigApp rig;
    const int serialBefore = rig.undo.getEditSerial();
    auto picker = rig.header(rig.lead).createColourPickerForTest();
    ASSERT_NE(picker, nullptr);

    for (const auto colour : {juce::Colours::red, juce::Colours::green, juce::Colours::blue}) {
        picker->setCurrentColourForTest(colour);
        EXPECT_EQ(rig.doc.getTrack(rig.lead)->colourArgb, colour.getARGB()) << "the track follows every tick";
        EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->colour, colour) << "and so does the channel";
    }
    EXPECT_EQ(rig.undo.getEditSerial(), serialBefore) << "a drag pushes no undo step at all";
}

TEST_F(ChannelFlowTest, ColourPickerCancelRestoresBothTargets) {
    LinkRigApp rig;
    const juce::uint32 originalTrack = rig.doc.getTrack(rig.lead)->colourArgb;
    const juce::Colour originalMacro = rig.editor.getMacros().find(rig.leadMacroId)->colour;
    const int serialBefore = rig.undo.getEditSerial();

    auto picker = rig.header(rig.lead).createColourPickerForTest();
    ASSERT_NE(picker, nullptr);
    picker->setCurrentColourForTest(juce::Colours::magenta);
    picker->setCurrentColourForTest(juce::Colour(originalTrack)); // dragged back to where it started
    picker->commitForTest();

    EXPECT_EQ(rig.doc.getTrack(rig.lead)->colourArgb, originalTrack);
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->colour, originalMacro);
    EXPECT_EQ(rig.undo.getEditSerial(), serialBefore) << "a no-net-change close records nothing";
}

TEST_F(ChannelFlowTest, ColourPickerCommitIsOneUndoStepCoveringTrackAndChannel) {
    LinkRigApp rig;
    const juce::uint32 originalTrack = rig.doc.getTrack(rig.lead)->colourArgb;
    const juce::Colour originalMacro = rig.editor.getMacros().find(rig.leadMacroId)->colour;
    ASSERT_FALSE(rig.undo.canUndo());

    auto picker = rig.header(rig.lead).createColourPickerForTest();
    ASSERT_NE(picker, nullptr);
    picker->setCurrentColourForTest(juce::Colours::magenta);
    picker->setCurrentColourForTest(juce::Colours::orange);
    picker->commitForTest();

    EXPECT_EQ(rig.doc.getTrack(rig.lead)->colourArgb, juce::Colours::orange.getARGB());
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->colour, juce::Colours::orange);

    // A dozen preview colours, ONE undo step (see the rename test on why this is undo()-counted
    // rather than getEditSerial()-counted).
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.doc.getTrack(rig.lead)->colourArgb, originalTrack);
    EXPECT_EQ(rig.editor.getMacros().find(rig.leadMacroId)->colour, originalMacro) << "one Cmd+Z, both targets";
    EXPECT_FALSE(rig.undo.canUndo()) << "the preview frames left no steps of their own behind";
}

TEST_F(ChannelFlowTest, ASharedChannelsTrackKeepsTheOrdinarySingleTargetColourPicker) {
    LinkRigApp rig;
    const juce::Colour originalMacro = rig.editor.getMacros().find(rig.drumsMacroId)->colour;

    auto picker = rig.header(rig.kick).createColourPickerForTest();
    ASSERT_NE(picker, nullptr);
    picker->setCurrentColourForTest(juce::Colours::magenta);

    EXPECT_EQ(rig.doc.getTrack(rig.kick)->colourArgb, juce::Colours::magenta.getARGB());
    EXPECT_EQ(rig.editor.getMacros().find(rig.drumsMacroId)->colour, originalMacro)
        << "a shared channel keeps its own colour";
}

// -------------------------------------------------------------------------------------------
// (c) The track header's M/S drive the strip
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, MuteOnALinkedTrackDrivesTheChannelMuteAndNotNoteGating) {
    LinkRigApp rig;
    auto& header = rig.header(rig.lead);
    ASSERT_FALSE(rig.strip(rig.leadStrip)->isMuted());
    const int serialBefore = rig.undo.getEditSerial();

    header.getMuteButton().onClick(); // the real button's own handler

    EXPECT_TRUE(rig.stripForMacro(rig.leadMacroId)->isMuted()) << "the CHANNEL is muted";
    EXPECT_FALSE(rig.doc.getTrack(rig.lead)->muted) << "one mute, not two - the doc flag stays off";
    EXPECT_TRUE(header.getMuteButton().getToggleState()) << "the button shows the channel's state";
    EXPECT_EQ(rig.undo.getEditSerial(), serialBefore + 1);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_FALSE(rig.stripForMacro(rig.leadMacroId)->isMuted());
    EXPECT_FALSE(header.getMuteButton().getToggleState()) << "and follows it back across an undo";
}

TEST_F(ChannelFlowTest, SoloOnALinkedTrackEngagesTheEngineGateAndOneUndoRevertsFlagAndCount) {
    LinkRigApp rig;
    auto& header = rig.header(rig.lead);
    ASSERT_EQ(rig.patch.engine.getSoloedStripCount(), 0);
    const int serialBefore = rig.undo.getEditSerial();

    header.getSoloButton().onClick();

    EXPECT_TRUE(rig.stripForMacro(rig.leadMacroId)->isSoloed());
    EXPECT_EQ(rig.patch.engine.getSoloedStripCount(), 1) << "the render-time gate is engaged";
    EXPECT_FALSE(rig.doc.getTrack(rig.lead)->soloed) << "one solo, not two";
    EXPECT_TRUE(header.getSoloButton().getToggleState());
    EXPECT_EQ(rig.undo.getEditSerial(), serialBefore + 1);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_FALSE(rig.stripForMacro(rig.leadMacroId)->isSoloed()) << "undo restores the strip's flag...";
    EXPECT_EQ(rig.patch.engine.getSoloedStripCount(), 0) << "...AND the engine's count";
    EXPECT_FALSE(header.getSoloButton().getToggleState());
}

TEST_F(ChannelFlowTest, MuteOnASharedChannelTrackKeepsNoteGatingAndNeverTouchesTheStrip) {
    LinkRigApp rig;
    auto& header = rig.header(rig.kick);

    header.getMuteButton().onClick();

    EXPECT_TRUE(rig.doc.getTrack(rig.kick)->muted) << "a shared channel's tracks keep today's note gating";
    EXPECT_FALSE(rig.strip(rig.sharedStrip)->isMuted()) << "and never touch the shared strip";
    EXPECT_FALSE(rig.doc.getTrack(rig.snare)->muted) << "nor the other track sharing it";

    header.getSoloButton().onClick();
    EXPECT_TRUE(rig.doc.getTrack(rig.kick)->soloed);
    EXPECT_FALSE(rig.strip(rig.sharedStrip)->isSoloed());
    EXPECT_EQ(rig.patch.engine.getSoloedStripCount(), 0) << "the mixer gate is not engaged by a track solo";
}

// THE decided semantics (plan review): soloing a linked channel silences every OTHER channel,
// including a shared one whose own track is note-gate-soloed. That is a DAW mixer solo, not a bug.
TEST_F(ChannelFlowTest, SoloOnALinkedTrackSilencesASharedChannelEvenWhenItsOwnTrackIsSoloed) {
    LinkRigApp rig;
    rig.render(4);
    ASSERT_GT(rig.strip(rig.leadStrip)->takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f)
        << "both channels must be audible to start with";
    ASSERT_GT(rig.strip(rig.sharedStrip)->takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f);

    rig.header(rig.lead).getSoloButton().onClick(); // a CHANNEL solo (linked)
    rig.header(rig.kick).getSoloButton().onClick(); // a NOTE-GATE solo (shared channel)
    rig.render(4);

    // The strip meter is post gain, pan, mute AND solo (ChannelStripModule), so this is what the mix
    // actually carries, not merely what a flag says.
    EXPECT_GT(rig.strip(rig.leadStrip)->takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f)
        << "the soloed channel is audible";
    EXPECT_EQ(rig.strip(rig.sharedStrip)->takeMeterPeak(synth::MeterReader::Mixer, 0), 0.0f)
        << "every non-soloed channel is silenced by the render-time gate, shared ones included";
}

TEST_F(ChannelFlowTest, AFormingLinkTakesOverTheTracksMuteSoThereIsOnlyEverOne) {
    LinkRigApp rig;
    // "Snare" leaves the shared sampler: "Kick" becomes that channel's only source, so the link
    // forms around a track that is currently note-gate-muted.
    rig.header(rig.kick).getMuteButton().onClick();
    ASSERT_TRUE(rig.doc.getTrack(rig.kick)->muted);
    ASSERT_FALSE(rig.strip(rig.sharedStrip)->isMuted());

    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    auto* snareIn = nodeForUuidCFT(rig.graph(), rig.doc.getTrack(rig.snare)->bindingUuid);
    ASSERT_NE(snareIn, nullptr);
    for (const auto& conn : rig.graph().getConnections())
        if (conn.source.nodeID == snareIn->nodeID && conn.source.channelIndex == midi)
            rig.graph().removeConnection(conn);
    ASSERT_TRUE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, rig.kick).linked);

    rig.link.reconcileLinkedTracks(); // what MainComponent::reconcileTimelineAfterGraphChange runs

    EXPECT_TRUE(rig.strip(rig.sharedStrip)->isMuted()) << "the mute moved onto the channel...";
    EXPECT_FALSE(rig.doc.getTrack(rig.kick)->muted) << "...and off the track, so exactly one is stored";
}

// -------------------------------------------------------------------------------------------
// The channel chip
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, TheChannelChipNamesTheChannelForLinkedAndSharedTracksAlike) {
    LinkRigApp rig;
    auto& linkedChip = rig.header(rig.lead).getChannelChip();
    auto& sharedChip = rig.header(rig.kick).getChannelChip();

    EXPECT_TRUE(linkedChip.isVisible());
    EXPECT_EQ(linkedChip.getChannelName(), "Lead");
    EXPECT_TRUE(sharedChip.isVisible()) << "every track playing into a channel gets a chip, linked or not";
    EXPECT_EQ(sharedChip.getChannelName(), "Drums") << "a shared channel shows its OWN name";
}

TEST_F(ChannelFlowTest, ClickingTheChannelChipSelectsThatChannelInTheGraph) {
    LinkRigApp rig;
    rig.editor.getMacroController().selectMacro(rig.drumsMacroId, /*additive=*/false);
    ASSERT_FALSE(rig.editor.getMacroController().isMacroSelected(rig.leadMacroId));

    rig.header(rig.lead).getChannelChip().onClick();

    EXPECT_TRUE(rig.editor.getMacroController().isMacroSelected(rig.leadMacroId)) << "the chip reveals its own channel";
    EXPECT_FALSE(rig.editor.getMacroController().isMacroSelected(rig.drumsMacroId));
}

TEST_F(ChannelFlowTest, TheChipMeterRepaintsOnlyWhenTheDrawnLevelActuallyMoves) {
    synth::ui::ChannelChipComponent chip;
    ASSERT_TRUE(chip.setMeterLevel(0.5f)) << "the first real level is always drawn";

    const float belowThreshold = 0.5f + synth::ui::ChannelChipComponent::kMeterRepaintThreshold * 0.5f;
    EXPECT_FALSE(chip.setMeterLevel(belowThreshold)) << "a tick that moved nothing visible repaints nothing";
    EXPECT_FALSE(chip.setMeterLevel(0.5f));

    EXPECT_TRUE(chip.setMeterLevel(0.9f)) << "a tick past the threshold repaints once";
    EXPECT_TRUE(chip.setMeterLevel(0.0f)) << "and silence is always drawn, however small the step";
}

TEST_F(ChannelFlowTest, TheChipUsesTheSameDbScaleAndTurnsTheClipColourOnAnOver) {
    synth::ui::ChannelChipComponent chip;
    // A linear peak > 1.0 is a real over -- above 0 dBFS, the CLIP zone (docs/mixer/mixer.md meters
    // section: same scale/zones as the mixer's own MixerMeter, FRO146).
    chip.setMeterLevel(1.5f);
    EXPECT_GT(chip.getMeterDbForTest(), 0.0f);
    const synth::ui::MeterColourStops stops = synth::ui::MeterColourStops::fromTheme(synth::theme::Colors{});
    EXPECT_EQ(stops.colourForDb(chip.getMeterDbForTest()), synth::theme::Colors{}.meterClip);

    // A quiet, non-clipping level sits in the LOW zone instead.
    synth::ui::ChannelChipComponent quietChip;
    quietChip.setMeterLevel(0.05f); // roughly -26 dBFS
    EXPECT_LT(quietChip.getMeterDbForTest(), synth::ui::MeterColourStops::kMidFromDb);
    EXPECT_EQ(stops.colourForDb(quietChip.getMeterDbForTest()), synth::theme::Colors{}.meterFill);
}

TEST_F(ChannelFlowTest, TheMeterTickReportsTheChannelsRealLevelThroughTheCheapRead) {
    LinkRigApp rig;
    auto& header = rig.header(rig.lead);
    ASSERT_EQ(rig.link.getChannelMeterPeak(rig.lead), 0.0f) << "silent before anything has rendered";

    rig.render(4);
    // A DIFFERENT reader's slot (Mixer) than the tick path below reads (TrackHeader) -- FRO146's
    // per-reader latch means this reference read must not steal the peak getChannelMeterPeak() is
    // about to consume.
    const float level = rig.strip(rig.leadStrip)->takeMeterPeak(synth::MeterReader::Mixer, 0);
    ASSERT_GT(level, 0.0f);

    // The tick path proper: the level reaches the chip through getChannelMeterPeak()'s cached strip
    // id -- two atomic reads -- NOT through a per-frame getChannelInfo() walk of the whole graph.
    // FRO146: getChannelMeterPeak() is now consuming (it reads the strip's OWN TrackHeader latch
    // slot, PeakMeterLatch.h) -- tickChannelMeter() below is that slot's one real consumer, so this
    // test must not also call getChannelMeterPeak() directly first (that would drain the very peak
    // the tick is about to read, exactly the "two readers must not steal from each other" bug the
    // latch exists to prevent -- here both "readers" would be the same call site, but the effect on
    // the slot is identical). `level`, read from a DIFFERENT reader (Mixer) above, is what proves
    // the tick reports the real magnitude without needing a separate pre-drain of TrackHeader's own.
    EXPECT_TRUE(header.tickChannelMeter()) << "a level that moved is drawn";
    // FRO146: the chip now displays a fraction of the -60..+3 dB scale, not the raw linear peak.
    const float expectedFraction = synth::ui::meterDbToFraction(synth::ui::meterLinearToDb(level));
    EXPECT_NEAR(header.getChannelChip().getMeterLevel(), expectedFraction, 1.0e-5f);

    // FRO146: the latch is consume-on-read, so a tick with NO new block in between reads silence
    // (nothing new arrived) rather than replaying the same stale value forever -- that decay-to-
    // silence is itself always drawn (ChannelChipComponent::setMeterLevel's own "crossing to
    // silence" exception), so THIS tick moves the display.
    EXPECT_TRUE(header.tickChannelMeter()) << "no new block since the last tick reads silence, which is always drawn";
    EXPECT_EQ(header.getChannelChip().getMeterLevel(), 0.0f);
    // ...and the NEXT tick, still no new block, reads silence again -- unchanged from the one
    // above, so (the original, pre-FRO146 gate) this one repaints nothing.
    EXPECT_FALSE(header.tickChannelMeter()) << "back-to-back silent ticks repaint only the first crossing";

    EXPECT_EQ(rig.link.getChannelMeterPeak(synth::TrackId{}), 0.0f)
        << "a track with no resolved channel reads silent rather than walking the graph to find out";
}

// -------------------------------------------------------------------------------------------
// A hand-built (unboxed) chain: linked, but with no macro to carry the channel's name or colour
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, AnUnboxedLinkedChainDrivesTheChannelsMuteButKeepsTheOrdinaryNameAndColour) {
    LinkRigApp rig;

    // Same shape as "Lead", deliberately never passed to addMacroForMembers.
    juce::String inUuid, oscUuid, stripUuid;
    auto* trackIn = addPlainNodeCFT(rig.graph(), "Track In", {0, 800}, inUuid);
    auto* osc = addPlainNodeCFT(rig.graph(), "Oscillator", {200, 800}, oscUuid);
    auto* stripNode = addPlainNodeCFT(rig.graph(), "Channel Strip", {400, 800}, stripUuid);
    rig.wireMidi(trackIn, osc);
    rig.wireStereo(osc, stripNode);
    rig.wireStripToOutput(stripNode);
    const auto bare = rig.addTrack("Bare", inUuid);
    rig.headers.push_back(std::make_unique<TimelineTrackHeaderComponent>(rig.doc, bare, &rig.host));
    auto& header = rig.header(bare);
    ASSERT_TRUE(synth::resolveTrackChannelLink(rig.graph(), rig.doc, bare).linked);

    // The chip still names it -- unboxed, the name falls back to the one feeding track's.
    EXPECT_TRUE(header.getChannelChip().isVisible());
    EXPECT_EQ(header.getChannelChip().getChannelName(), "Bare");

    // Name and colour need a macro to sync INTO, so both decline and the header keeps its ordinary
    // track-only behaviour...
    EXPECT_FALSE(rig.link.renameLinkedTrackAndChannel(bare, "Renamed"));
    EXPECT_EQ(rig.link.buildLinkedChannelColourPicker(bare, nullptr), nullptr);
    header.getNameLabel().setText("Renamed", juce::sendNotificationSync);
    EXPECT_EQ(rig.doc.getTrack(bare)->name, "Renamed") << "the track itself still renames";

    // ...but M/S do not: they drive the STRIP, which exists with or without a macro around it.
    auto* strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(strip, nullptr);
    header.getMuteButton().onClick();
    EXPECT_TRUE(strip->isMuted()) << "the mute lands on the channel even with no macro";
    EXPECT_FALSE(rig.doc.getTrack(bare)->muted) << "...and never on the track, so there is only ever one";
}

// -------------------------------------------------------------------------------------------
// End to end, on a real MainComponent: the link is reachable from the shipped flow
// -------------------------------------------------------------------------------------------

TEST_F(ChannelFlowTest, AddAudioTrackProducesALinkedTrackWhoseHeaderShowsAndRenamesItsChannel) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateToggleTimelineClick(); // the panel starts hidden; the headers only lay out once shown
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());

    addAudioTrack(mc);
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
    const auto track = mc.getTimelineDoc().getTracks().front().id;
    auto* header = headerForCFT(mc, track);
    ASSERT_NE(header, nullptr);

    const auto info = synth::resolveTrackChannelLink(mc.getAudioEngine().getGraph(), mc.getTimelineDoc(), track);
    EXPECT_TRUE(info.linked) << "an audio track is its channel's only source";
    EXPECT_TRUE(header->getChannelChip().isVisible());

    // Laid out at a REAL size, so this is the one place the chip's geometry is provable: calling
    // onClick() by hand (as the rig tests do) would pass just as happily on a zero-sized chip no
    // mouse could ever hit, and it shares its row with the binding chip.
    const auto chipBounds = header->getChannelChip().getBounds();
    EXPECT_FALSE(chipBounds.isEmpty()) << "a chip with no bounds is a chip nobody can click";
    EXPECT_FALSE(chipBounds.intersects(header->getBindingChip().getBounds()))
        << "the two chips split the bottom row, they do not sit on top of each other";

    header->getNameLabel().setText("Vocals", juce::sendNotificationSync);
    EXPECT_EQ(mc.getTimelineDoc().getTrack(track)->name, "Vocals");
    ASSERT_EQ(mc.getGraphEditor().getMacros().size(), 1);
    EXPECT_EQ(mc.getGraphEditor().getMacros().getAll().front().name, "Vocals")
        << "the whole link is wired through MainComponent, not just this test file's rig";
}
