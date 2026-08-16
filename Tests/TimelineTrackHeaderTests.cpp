// TimelineTrackHeaderTests.cpp
//
// The track header ROW and the track colour resolver, in isolation.
//
// Nothing here touches MainComponent, an AudioEngine or a juce::AudioProcessorGraph: the header
// talks to the app exclusively through synth::ui::TrackHeaderHost, so a stub host is enough to
// drive every behaviour (chip states, one-click re-bind, M/S/arm write-through, colour cycling).
// The app-level half — publish-on-mutation, the add-track flow's compound undo, reconciliation
// after a restore, .agsproj round trips — lives in TimelinePanelTests.cpp, where a MainComponent
// harness already exists.
//
// Consequently NONE of this file is #if SYNTH_ENABLE_TIMELINE-gated: the components and the
// resolver compile unconditionally (like TimelinePanelComponent itself); only MainComponent's use
// of them is gated.

#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/TimelineTrackHeaderComponent.h"
#include "../Source/UI/TrackColour.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

class StubTrackHeaderHost : public TrackHeaderHost {
public:
    explicit StubTrackHeaderHost(TimelineDoc& doc)
        : doc_(doc) {}

    std::vector<BindingOption> getAvailableTrackInNodes(TrackId /*forTrack*/) override { return options; }

    juce::String getNodeDisplayName(const juce::String& uuid) override {
        const auto found = names.find(uuid);
        return found != names.end() ? found->second : juce::String();
    }

    void bindTrackTo(TrackId track, const juce::String& uuid) override {
        lastBoundUuid = uuid;
        doc_.setTrackBinding(track, uuid);
    }

    void createAndBindTrackInNode(TrackId /*track*/) override { ++createAndBindCalls; }
    void selectNodeInGraph(const juce::String& uuid) override { lastSelectedUuid = uuid; }
    void deleteTrack(TrackId /*track*/) override { ++deleteCalls; }

    void performTrackEdit(const std::function<void()>& mutation) override {
        ++editCalls;
        if (mutation)
            mutation();
    }

    void addMidiTrack() override { ++addTrackCalls; }
    void addAudioTrack() override { ++addAudioTrackCalls; }

    // Not exercised by this file (it tests the track-header column, not the automation
    // strip's lane picker) — empty/no-op is the correct stub behaviour.
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    std::vector<BindingOption> options;
    std::map<juce::String, juce::String> names;
    juce::String lastBoundUuid;
    juce::String lastSelectedUuid;
    int createAndBindCalls = 0;
    int deleteCalls = 0;
    int editCalls = 0;
    int addTrackCalls = 0;
    int addAudioTrackCalls = 0;

private:
    TimelineDoc& doc_;
};

// A doc + one track + a header wired to a stub host — the shape every test below needs.
struct HeaderFixture {
    HeaderFixture() {
        trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        host = std::make_unique<StubTrackHeaderHost>(doc);
        header = std::make_unique<TimelineTrackHeaderComponent>(doc, trackId, host.get());
        header->setSize(160, TimelineTrackHeaderComponent::kRowHeight);
    }

    void bindTo(const juce::String& uuid, const juce::String& displayName) {
        host->names[uuid] = displayName;
        doc.setTrackBinding(trackId, uuid);
        header->refreshFromDoc();
    }

    const synth::Track* track() const { return doc.getTrack(trackId); }

    TimelineDoc doc;
    TrackId trackId;
    std::unique_ptr<StubTrackHeaderHost> host;
    std::unique_ptr<TimelineTrackHeaderComponent> header;
};

} // namespace

// =============================================================================
// 1. Binding chip states: bound / unbound / orphaned
// =============================================================================

TEST(TimelineTrackHeaderTest, ChipShowsTheBoundNodeName) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In #7");

    EXPECT_EQ(f.header->getBindingChipText(), "Track In #7");
    EXPECT_FALSE(f.header->isBindingChipWarning());
}

TEST(TimelineTrackHeaderTest, ChipIsAmberWhenUnbound) {
    HeaderFixture f;
    ASSERT_TRUE(f.track()->bindingUuid.isEmpty());

    EXPECT_EQ(f.header->getBindingChipText(), "Unbound");
    EXPECT_TRUE(f.header->isBindingChipWarning()) << "a track that plays nowhere must say so";
}

TEST(TimelineTrackHeaderTest, ChipIsAmberAndSaysMissingWhenOrphaned) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In #7");
    ASSERT_FALSE(f.header->isBindingChipWarning());

    // The node went away: reconcile against a resolver that resolves nothing (what
    // TimelineReconciler does against a graph that no longer holds the node).
    ASSERT_TRUE(f.doc.reconcileBindings([](const juce::String&) { return false; }));
    f.header->refreshFromDoc();

    EXPECT_EQ(f.header->getBindingChipText(), "Missing");
    EXPECT_TRUE(f.header->isBindingChipWarning());
    EXPECT_EQ(f.track()->bindingUuid, "uuid-a") << "an orphaned binding is retained, never cleared";
}

// =============================================================================
// 2. Chip menu: one-click re-bind, and "New Track In node"
// =============================================================================

TEST(TimelineTrackHeaderTest, ChipMenuChoiceRebindsTheDoc) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In #7");
    f.host->options = {{"uuid-a", "Track In #7"}, {"uuid-b", "Track In #9"}};

    const auto options = f.header->collectBindingOptions();
    ASSERT_EQ(options.size(), 2u);
    EXPECT_EQ(options[1].uuid, "uuid-b");

    f.header->applyBindingMenuChoice(2); // menu ids are 1-based over collectBindingOptions()
    EXPECT_EQ(f.host->lastBoundUuid, "uuid-b");
    EXPECT_EQ(f.track()->bindingUuid, "uuid-b");
}

TEST(TimelineTrackHeaderTest, ChipMenuNewNodeEntryAsksTheHostToCreateOne) {
    HeaderFixture f;
    f.header->applyBindingMenuChoice(TimelineTrackHeaderComponent::kNewTrackInNodeMenuId);
    EXPECT_EQ(f.host->createAndBindCalls, 1);
    EXPECT_TRUE(f.host->lastBoundUuid.isEmpty()) << "creating a node is not a re-bind of an existing one";
}

TEST(TimelineTrackHeaderTest, ChipMenuIgnoresAnOutOfRangeChoice) {
    HeaderFixture f;
    f.host->options = {{"uuid-a", "Track In #7"}};

    f.header->applyBindingMenuChoice(0);  // menu dismissed
    f.header->applyBindingMenuChoice(99); // not an id this menu ever offered
    EXPECT_TRUE(f.host->lastBoundUuid.isEmpty());
    EXPECT_TRUE(f.track()->bindingUuid.isEmpty());
}

// A binding NEVER changes by itself — not on a reconcile, not by matching a name. Only an explicit
// menu choice moves it (see docs/layout.md §16).
TEST(TimelineTrackHeaderTest, OrphanedTrackIsNeverAutoRebound) {
    HeaderFixture f;
    f.bindTo("uuid-gone", "Track In #7");
    ASSERT_TRUE(f.doc.reconcileBindings([](const juce::String&) { return false; }));

    // A replacement node exists and even carries the same display name...
    f.host->options = {{"uuid-new", "Track In #7"}};
    f.header->refreshFromDoc();

    EXPECT_EQ(f.track()->bindingUuid, "uuid-gone") << "refreshing the header must not re-bind anything";
    EXPECT_TRUE(f.host->lastBoundUuid.isEmpty());

    // ...and one click on it is all it takes to recover.
    f.header->applyBindingMenuChoice(1);
    EXPECT_EQ(f.track()->bindingUuid, "uuid-new");
}

// =============================================================================
// 3. Chip click selects the bound node (highlight affordance)
// =============================================================================

TEST(TimelineTrackHeaderTest, ChipClickSelectsTheBoundNode) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In #7");

    f.header->handleChipClick(/*showMenu=*/false);
    EXPECT_EQ(f.host->lastSelectedUuid, "uuid-a");
}

TEST(TimelineTrackHeaderTest, ChipClickSelectsNothingWhenUnboundOrOrphaned) {
    HeaderFixture f;
    f.header->handleChipClick(false);
    EXPECT_TRUE(f.host->lastSelectedUuid.isEmpty());

    f.bindTo("uuid-a", "Track In #7");
    ASSERT_TRUE(f.doc.reconcileBindings([](const juce::String&) { return false; }));
    f.header->refreshFromDoc();

    f.host->lastSelectedUuid = {};
    f.header->handleChipClick(false);
    EXPECT_TRUE(f.host->lastSelectedUuid.isEmpty()) << "there is no live node to highlight";
}

// =============================================================================
// 4. M / S / arm write-through, and the name label
// =============================================================================

TEST(TimelineTrackHeaderTest, MuteSoloArmWriteThroughToTheDoc) {
    HeaderFixture f;
    ASSERT_FALSE(f.track()->muted);

    f.header->getMuteButton().onClick();
    f.header->refreshFromDoc();
    EXPECT_TRUE(f.track()->muted);
    EXPECT_TRUE(f.header->getMuteButton().getToggleState());

    f.header->getSoloButton().onClick();
    f.header->getArmButton().onClick();
    f.header->refreshFromDoc();
    EXPECT_TRUE(f.track()->soloed);
    EXPECT_TRUE(f.track()->armed) << "arming flips document state only — recording starts elsewhere";

    // Every one of those went through the host, i.e. onto the undo stack in the real app.
    EXPECT_EQ(f.host->editCalls, 3);

    // ...and each toggles back off.
    f.header->getMuteButton().onClick();
    f.header->refreshFromDoc();
    EXPECT_FALSE(f.track()->muted);
    EXPECT_FALSE(f.header->getMuteButton().getToggleState());
}

TEST(TimelineTrackHeaderTest, NameEditWritesThroughToTheDoc) {
    HeaderFixture f;
    f.header->getNameLabel().setText("Bassline", juce::sendNotificationSync);
    EXPECT_EQ(f.track()->name, "Bassline");
    EXPECT_EQ(f.host->editCalls, 1);
}

// =============================================================================
// 5. Colour: swatch cycles the palette, and the value persists in the document
// =============================================================================

TEST(TimelineTrackHeaderTest, ColourSwatchCyclesThePaletteAndPersistsInTheDoc) {
    HeaderFixture f;
    const auto& palette = synth::ui::trackPaletteArgb();

    // A freshly added track carries TimelineDoc's neutral placeholder, which is not in the palette,
    // so the first click lands on entry 0 rather than "the one after grey".
    f.header->getColourSwatch().onClick();
    f.header->refreshFromDoc();
    EXPECT_EQ(f.track()->colourArgb, palette[0]);
    EXPECT_EQ(f.header->getResolvedColour(), juce::Colour(palette[0]));

    f.header->getColourSwatch().onClick();
    f.header->refreshFromDoc();
    EXPECT_EQ(f.track()->colourArgb, palette[1]);

    EXPECT_EQ(f.host->editCalls, 2) << "each cycle is one undoable edit";
}

TEST(TimelineTrackHeaderTest, MutedTrackResolvesToADimmedColour) {
    HeaderFixture f;
    f.header->getColourSwatch().onClick();
    f.header->refreshFromDoc();
    const auto unmuted = f.header->getResolvedColour();

    f.header->getMuteButton().onClick();
    f.header->refreshFromDoc();
    const auto muted = f.header->getResolvedColour();

    EXPECT_LT(muted.getSaturation(), unmuted.getSaturation());
    EXPECT_LT(muted.getFloatAlpha(), unmuted.getFloatAlpha());
    // Tolerance, not equality: withSaturation() round-trips through 8-bit RGB, which moves the hue
    // by a fraction of a percent. The claim under test is "same hue family", not "identical float".
    EXPECT_NEAR(muted.getHue(), unmuted.getHue(), 0.01f) << "muting dims, it never re-hues";
}

// =============================================================================
// 6. Context menu + paint smoke
// =============================================================================

TEST(TimelineTrackHeaderTest, ContextMenuDeleteAsksTheHost) {
    HeaderFixture f;
    f.header->applyContextMenuChoice(TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    EXPECT_EQ(f.host->deleteCalls, 1);

    f.header->applyContextMenuChoice(0); // dismissed
    EXPECT_EQ(f.host->deleteCalls, 1);
}

TEST(TimelineTrackHeaderTest, HeaderSnapshotSmoke) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In #7");

    const juce::Image bound = f.header->createComponentSnapshot(f.header->getLocalBounds());
    EXPECT_FALSE(bound.isNull());
    EXPECT_EQ(bound.getWidth(), 160);
    EXPECT_EQ(bound.getHeight(), TimelineTrackHeaderComponent::kRowHeight);

    // ...and again in the amber (orphaned) state, which paints a different chip.
    ASSERT_TRUE(f.doc.reconcileBindings([](const juce::String&) { return false; }));
    f.header->refreshFromDoc();
    const juce::Image orphaned = f.header->createComponentSnapshot(f.header->getLocalBounds());
    EXPECT_FALSE(orphaned.isNull());
}

// A header whose track has been removed from the doc must not touch anything — the panel destroys
// it on the next notification, but the button callbacks can still be reached in between.
TEST(TimelineTrackHeaderTest, SurvivesItsTrackDisappearing) {
    HeaderFixture f;
    ASSERT_TRUE(f.doc.removeTrack(f.trackId));

    f.header->refreshFromDoc();
    f.header->getMuteButton().onClick();
    f.header->getColourSwatch().onClick();
    f.header->handleChipClick(false);
    EXPECT_EQ(f.host->editCalls, 0);
    EXPECT_TRUE(f.host->lastSelectedUuid.isEmpty());
}

// =============================================================================
// 7. synth::ui::resolveTrackColour — the pure resolver
// =============================================================================

TEST(TrackColourTest, StoredColourWinsOverThePalette) {
    const juce::uint32 stored = 0xff123456;
    EXPECT_EQ(synth::ui::resolveTrackColour(stored, 3, false), juce::Colour(stored));
}

TEST(TrackColourTest, ZeroFallsBackToThePaletteByIndex) {
    for (int i = 0; i < synth::ui::kTrackPaletteSize; ++i)
        EXPECT_EQ(synth::ui::resolveTrackColour(0, i, false), synth::ui::trackPaletteColour(i));

    // Wraps, and tolerates a negative index.
    EXPECT_EQ(synth::ui::trackPaletteColour(synth::ui::kTrackPaletteSize), synth::ui::trackPaletteColour(0));
    EXPECT_EQ(synth::ui::trackPaletteColour(-1), synth::ui::trackPaletteColour(synth::ui::kTrackPaletteSize - 1));
}

TEST(TrackColourTest, PaletteEntriesAreDistinct) {
    const auto& palette = synth::ui::trackPaletteArgb();
    for (int i = 0; i < synth::ui::kTrackPaletteSize; ++i)
        for (int j = i + 1; j < synth::ui::kTrackPaletteSize; ++j)
            EXPECT_NE(palette[(size_t)i], palette[(size_t)j]);
}

TEST(TrackColourTest, NextPaletteColourCyclesAndWraps) {
    const auto& palette = synth::ui::trackPaletteArgb();
    EXPECT_EQ(synth::ui::nextTrackPaletteColour(palette[0]), palette[1]);
    EXPECT_EQ(synth::ui::nextTrackPaletteColour(palette[synth::ui::kTrackPaletteSize - 1]), palette[0]);
    // Anything outside the palette (the doc's grey placeholder) enters it at the first entry.
    EXPECT_EQ(synth::ui::nextTrackPaletteColour(0xff808080), palette[0]);
}
