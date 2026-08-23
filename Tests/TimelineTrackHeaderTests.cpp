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

#include "../Source/AppUndoManager.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/ColourPickerPopup.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include "../Source/UI/TimelineTrackHeaderComponent.h"
#include "../Source/UI/TrackColour.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

// True when the icon BinaryData is actually linked in (mirrors IconLibraryTests.cpp's constant):
// the headless test target links Core, which normally compiles this in, but the #else keeps the
// icon-badge test meaningful either way rather than assuming one or the other.
constexpr bool kAssetsPresent =
#ifdef HAS_FONT_ASSETS
    true;
#else
    false;
#endif

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
        // When a real AppUndoManager is wired in (the colour-picker commit-once tests), route
        // through it so those tests exercise the ACTUAL undo/redo contract rather than a stub
        // that merely counts calls. Every other test in this file leaves undoManager null, which
        // keeps their existing "editCalls" assertions exactly as they were.
        if (undoManager != nullptr)
            undoManager->recordTimelineChange(doc_, mutation);
        else if (mutation)
            mutation();
    }

    void addMidiTrack() override { ++addTrackCalls; }
    void addAudioTrack() override { ++addAudioTrackCalls; }

    // Not exercised by this file (it tests the track-header column, not the automation
    // strip's lane picker) — empty/no-op is the correct stub behaviour.
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }

    juce::ApplicationProperties* getAppProperties() override { return appProperties; }

    std::vector<MidiDestinationOption> getMidiDestinationOptions(TrackId /*forTrack*/) override {
        return midiDestinationOptions;
    }

    void setMidiDestinationConnected(TrackId /*forTrack*/, juce::uint32 nodeUid, bool connect) override {
        lastMidiDestinationNodeUid = nodeUid;
        lastMidiDestinationConnect = connect;
        ++setMidiDestinationCalls;
    }

    std::vector<BindingOption> options;
    std::vector<MidiDestinationOption> midiDestinationOptions;
    juce::uint32 lastMidiDestinationNodeUid = 0;
    bool lastMidiDestinationConnect = false;
    int setMidiDestinationCalls = 0;
    std::map<juce::String, juce::String> names;
    juce::String lastBoundUuid;
    juce::String lastSelectedUuid;
    int createAndBindCalls = 0;
    int deleteCalls = 0;
    int editCalls = 0;
    int addTrackCalls = 0;
    int addAudioTrackCalls = 0;
    // Null by default (in-memory-only colour picker). A test that needs the real undo contract
    // sets this to a real AppUndoManager it owns.
    AppUndoManager* undoManager = nullptr;
    // Null by default (in-memory-only favourites shelf, per ColourPickerPopup.h's contract).
    juce::ApplicationProperties* appProperties = nullptr;

private:
    TimelineDoc& doc_;
};

// Mirrors the "#id" rule in MainComponent::getAvailableTrackInNodes (MainComponent.cpp, ~line
// 2712): an option's display name gets " #<n>" appended ONLY when another option in the same list
// carries the same plain name. Reimplemented here (rather than exercised through MainComponent,
// which this file deliberately never touches — see the file comment) so the RULE itself has a
// pinned regression test, independent of the real graph plumbing.
std::vector<TrackHeaderHost::BindingOption>
dedupedBindingOptions(const std::vector<std::pair<juce::String, juce::String>>& uuidsAndPlainNames) {
    std::map<juce::String, int> nameOccurrences;
    for (const auto& entry : uuidsAndPlainNames)
        ++nameOccurrences[entry.second];

    std::vector<TrackHeaderHost::BindingOption> options;
    for (size_t i = 0; i < uuidsAndPlainNames.size(); ++i) {
        const auto& [uuid, name] = uuidsAndPlainNames[i];
        const juce::String display = nameOccurrences[name] > 1 ? name + " #" + juce::String((int)i + 1) : name;
        options.push_back({uuid, display});
    }
    return options;
}

// A doc + one track + a header wired to a stub host — the shape every test below needs.
struct HeaderFixture {
    explicit HeaderFixture(TrackKind kind = TrackKind::Midi) {
        trackId = doc.addTrack(kind, "Track 1");
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
// 1a. Plain names by default; "#id" only for a menu with a real name collision
// =============================================================================

TEST(TimelineTrackHeaderTest, ChipShowsThePlainNameWithNoIdSuffix) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In"); // what MainComponent::getNodeDisplayName now returns by default

    EXPECT_EQ(f.header->getBindingChipText(), "Track In");
    EXPECT_FALSE(f.header->getBindingChipText().containsChar('#'))
        << "the chip shows identity, not disambiguation — see MainComponent::describeNodeForBinding";
}

TEST(TimelineTrackHeaderTest, MenuOptionsGetIdSuffixOnlyWhenNamesCollide) {
    HeaderFixture f;
    f.host->options =
        dedupedBindingOptions({{"uuid-a", "Track In"}, {"uuid-b", "Track In"}, {"uuid-c", "Track Audio"}});

    const auto options = f.header->collectBindingOptions();
    ASSERT_EQ(options.size(), 3u);
    EXPECT_TRUE(options[0].displayName.containsChar('#')) << "two nodes share this name — it must disambiguate";
    EXPECT_TRUE(options[1].displayName.containsChar('#'));
    EXPECT_FALSE(options[2].displayName.containsChar('#')) << "a name nothing else shares stays plain";
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

// =============================================================================
// 2b. Chip menu: "MIDI destinations..." entry (MIDI tracks only)
// =============================================================================

TEST(TimelineTrackHeaderTest, BindingMenuOffersMidiDestinationsForAMidiTrack) {
    HeaderFixture f(TrackKind::Midi);
    EXPECT_TRUE(f.header->offersMidiDestinationsMenuEntryForTest());
}

TEST(TimelineTrackHeaderTest, BindingMenuOmitsMidiDestinationsForAnAudioOrAutomationTrack) {
    HeaderFixture audio(TrackKind::Audio);
    EXPECT_FALSE(audio.header->offersMidiDestinationsMenuEntryForTest());

    HeaderFixture automation(TrackKind::Automation);
    EXPECT_FALSE(automation.header->offersMidiDestinationsMenuEntryForTest());
}

TEST(TimelineTrackHeaderTest, ChipMenuMidiDestinationsChoiceInvokesTheOpenHook) {
    HeaderFixture f;
    int openCalls = 0;
    f.header->setOpenMidiDestinationsPickerHookForTest([&openCalls] { ++openCalls; });

    f.header->applyBindingMenuChoice(TimelineTrackHeaderComponent::kMidiDestinationsMenuId);
    EXPECT_EQ(openCalls, 1);
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
// 5. Colour: swatch click opens a picker; preview writes live; commit is ONE undo step
// =============================================================================

// The swatch no longer cycles the palette on click (see TrackColour.h's file comment) — it opens
// synth::ui::ColourPickerPopup. This drives that popup through its OWN test seams
// (setCurrentColourForTest / commitForTest), never via a real juce::CallOutBox, which cannot run
// headlessly.
TEST(TimelineTrackHeaderTest, ColourPickerPreviewWritesLiveWithNoUndoStep) {
    HeaderFixture f;
    AppUndoManager undoManager;
    f.host->undoManager = &undoManager;

    auto popup = f.header->createColourPickerForTest();
    ASSERT_NE(popup, nullptr);

    popup->setCurrentColourForTest(juce::Colour(0xff112233));
    EXPECT_EQ(f.track()->colourArgb, 0xff112233u) << "preview writes the doc directly";
    EXPECT_FALSE(undoManager.canUndo()) << "a live preview must not push an undo transaction";
    EXPECT_EQ(f.host->editCalls, 0);
}

TEST(TimelineTrackHeaderTest, ColourPickerCommitPushesExactlyOneUndoStepRestoringTheOriginal) {
    HeaderFixture f;
    AppUndoManager undoManager;
    f.host->undoManager = &undoManager;
    const juce::uint32 original = f.track()->colourArgb; // TimelineDoc's neutral placeholder

    auto popup = f.header->createColourPickerForTest();
    ASSERT_NE(popup, nullptr);
    popup->setCurrentColourForTest(juce::Colour(0xff445566)); // preview, no undo yet
    popup->commitForTest();

    EXPECT_EQ(f.track()->colourArgb, 0xff445566u) << "the committed colour sticks";
    EXPECT_EQ(f.host->editCalls, 1) << "exactly one undoable edit for the whole gesture";
    ASSERT_TRUE(undoManager.canUndo());

    undoManager.undo();
    EXPECT_EQ(f.track()->colourArgb, original) << "undoing the ONE step restores the ORIGINAL colour";

    // A second commitForTest() call must not fire again (commit-once).
    popup->commitForTest();
    EXPECT_EQ(f.host->editCalls, 1);
}

TEST(TimelineTrackHeaderTest, ColourPickerCommitWithNoNetChangePushesNoUndoStep) {
    HeaderFixture f;
    AppUndoManager undoManager;
    f.host->undoManager = &undoManager;
    const juce::uint32 original = f.track()->colourArgb;

    auto popup = f.header->createColourPickerForTest();
    ASSERT_NE(popup, nullptr);
    popup->setCurrentColourForTest(juce::Colour(0xff778899)); // preview away from the original...
    popup->setCurrentColourForTest(juce::Colour(original));   // ...and back again before closing
    popup->commitForTest();

    EXPECT_EQ(f.track()->colourArgb, original);
    EXPECT_EQ(f.host->editCalls, 0) << "no NET change must record no undo step";
    EXPECT_FALSE(undoManager.canUndo());
}

TEST(TimelineTrackHeaderTest, SwatchClickReturnsNullPickerWhenTrackIsGone) {
    HeaderFixture f;
    ASSERT_TRUE(f.doc.removeTrack(f.trackId));
    EXPECT_EQ(f.header->createColourPickerForTest(), nullptr);
}

TEST(TimelineTrackHeaderTest, MutedTrackResolvesToADimmedColour) {
    HeaderFixture f;
    f.doc.setTrackColour(f.trackId, 0xff4FC1FFu);
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
// 6a. Tooltips — the founder-facing "what does this do" text
// =============================================================================

TEST(TimelineTrackHeaderTest, MuteSoloArmTooltipsExplainTheControls) {
    HeaderFixture f;
    EXPECT_EQ(f.header->getMuteButton().getTooltip(), "Mute this track");
    EXPECT_EQ(f.header->getSoloButton().getTooltip(), "Solo this track");
    EXPECT_EQ(f.header->getArmButton().getTooltip(), "Arm this track for recording");
}

TEST(TimelineTrackHeaderTest, BindingChipTooltipExplainsTheBindingAndHowToChangeIt) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In");

    EXPECT_EQ(f.header->getBindingChip().getTooltip(),
              "This track plays through the 'Track In' node in the graph. Click to choose a different node.");
}

TEST(TimelineTrackHeaderTest, OrphanedChipTooltipExplainsTheMissingNode) {
    HeaderFixture f;
    f.bindTo("uuid-a", "Track In");
    ASSERT_TRUE(f.doc.reconcileBindings([](const juce::String&) { return false; }));
    f.header->refreshFromDoc();

    EXPECT_EQ(f.header->getBindingChip().getTooltip(), "The node this track was bound to is gone. Click to re-bind.");
}

// =============================================================================
// 7. Track-kind badge, the automation open/close button, and the Automation-track binding chip
// =============================================================================

TEST(TimelineTrackHeaderTest, KindBadgeTextPerTrackKind) {
    HeaderFixture midi(TrackKind::Midi);
    EXPECT_EQ(midi.header->getKindBadgeTextForTest(), "MIDI");

    HeaderFixture audio(TrackKind::Audio);
    EXPECT_EQ(audio.header->getKindBadgeTextForTest(), "AUD");

    HeaderFixture automation(TrackKind::Automation);
    EXPECT_EQ(automation.header->getKindBadgeTextForTest(), "AUTO");
}

TEST(TimelineTrackHeaderTest, KindBadgeTextEmptyWhenTrackIsGone) {
    HeaderFixture f;
    ASSERT_TRUE(f.doc.removeTrack(f.trackId));
    f.header->refreshFromDoc();
    EXPECT_EQ(f.header->getKindBadgeTextForTest(), juce::String());
}

TEST(TimelineTrackHeaderTest, AutomationButtonHiddenUntilTheTrackHasALane) {
    HeaderFixture f;
    EXPECT_FALSE(f.header->getAutomationButton().isVisible());

    synth::AutomationLane::RangeSnapshot range;
    const auto laneId = f.doc.addLane(f.trackId, "node-uuid", "cutoff", range);
    ASSERT_TRUE(laneId.isValid());
    f.header->refreshFromDoc();

    EXPECT_TRUE(f.header->getAutomationButton().isVisible());
}

TEST(TimelineTrackHeaderTest, AutomationButtonClickFiresOnAutomationToggleRequestedWithTheTrackId) {
    HeaderFixture f;
    synth::AutomationLane::RangeSnapshot range;
    f.doc.addLane(f.trackId, "node-uuid", "cutoff", range);
    f.header->refreshFromDoc();

    synth::TrackId requestedTrack;
    int calls = 0;
    f.header->onAutomationToggleRequested = [&](synth::TrackId trackId) {
        requestedTrack = trackId;
        ++calls;
    };

    f.header->getAutomationButton().onClick();
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(requestedTrack == f.trackId);
}

TEST(TimelineTrackHeaderTest, AutomationTrackHasNoBindingChipButMidiTrackKeepsIt) {
    HeaderFixture automation(TrackKind::Automation);
    EXPECT_FALSE(automation.header->getBindingChip().isVisible());

    HeaderFixture midi(TrackKind::Midi);
    EXPECT_TRUE(midi.header->getBindingChip().isVisible());
}

// The kind badge draws a themed icon when one is available, matching the per-kind mapping
// text badge used to encode 1:1 — TrackMidi/TrackAudio/TrackAutomation — and falls back to the
// text badge (already covered above) with no AppLookAndFeel installed at all.
TEST(TimelineTrackHeaderTest, KindBadgeIconPerTrackKindOrTextFallback) {
    synth::theme::AppLookAndFeel lf;

    HeaderFixture midi(TrackKind::Midi);
    EXPECT_EQ(midi.header->getKindBadgeIconForTest(), -1) << "no LookAndFeel installed yet — text fallback";
    midi.header->setLookAndFeel(&lf);
    EXPECT_EQ(midi.header->getKindBadgeIconForTest(), kAssetsPresent ? (int)synth::theme::Icon::TrackMidi : -1);
    midi.header->setLookAndFeel(nullptr);

    HeaderFixture audio(TrackKind::Audio);
    audio.header->setLookAndFeel(&lf);
    EXPECT_EQ(audio.header->getKindBadgeIconForTest(), kAssetsPresent ? (int)synth::theme::Icon::TrackAudio : -1);
    audio.header->setLookAndFeel(nullptr);

    HeaderFixture automation(TrackKind::Automation);
    automation.header->setLookAndFeel(&lf);
    EXPECT_EQ(automation.header->getKindBadgeIconForTest(),
              kAssetsPresent ? (int)synth::theme::Icon::TrackAutomation : -1);
    automation.header->setLookAndFeel(nullptr);
}

// =============================================================================
// 7a. Theme-switch bug fix regression: the chip and the M/S/R active colours must follow a theme
// switch even when nothing about the doc changed (the bug this wave fixes — see
// TimelineTrackHeaderComponent::applyThemeDerivedColours / lookAndFeelChanged).
// =============================================================================

TEST(TimelineTrackHeaderTest, ThemeSwitchReappliesChipAndMSRColoursWithNoDocChange) {
    HeaderFixture f;
    synth::theme::AppLookAndFeel lf;
    const auto themeA = synth::theme::makeObsidian();
    const auto themeB = synth::theme::makeNeon();

    lf.applyTheme(themeA);
    f.header->setLookAndFeel(&lf); // installing triggers lookAndFeelChanged() once already

    EXPECT_EQ(f.header->getBindingChip().findColour(juce::TextButton::buttonColourId), themeA.colors.warning)
        << "a freshly-added track is unbound — the chip's WARNING colour, not surface";
    EXPECT_EQ(f.header->getMuteButton().findColour(juce::TextButton::buttonOnColourId), themeA.colors.trackMuteOn);
    EXPECT_EQ(f.header->getSoloButton().findColour(juce::TextButton::buttonOnColourId), themeA.colors.trackSoloOn);
    EXPECT_EQ(f.header->getArmButton().findColour(juce::TextButton::buttonOnColourId), themeA.colors.trackArmOn);

    // Theme mutates in place, then the app broadcasts the switch — exactly the sequence a real
    // theme switch runs, and exactly the case that had no effect before this fix (no refreshFromDoc
    // call in between, only lookAndFeelChanged()).
    lf.applyTheme(themeB);
    f.header->sendLookAndFeelChange();

    EXPECT_EQ(f.header->getBindingChip().findColour(juce::TextButton::buttonColourId), themeB.colors.warning);
    EXPECT_EQ(f.header->getMuteButton().findColour(juce::TextButton::buttonOnColourId), themeB.colors.trackMuteOn);
    EXPECT_EQ(f.header->getSoloButton().findColour(juce::TextButton::buttonOnColourId), themeB.colors.trackSoloOn);
    EXPECT_EQ(f.header->getArmButton().findColour(juce::TextButton::buttonOnColourId), themeB.colors.trackArmOn);

    // ...and the NORMAL (bound, non-warning) chip variant follows too.
    f.bindTo("uuid-a", "Track In");
    EXPECT_EQ(f.header->getBindingChip().findColour(juce::TextButton::buttonColourId), themeB.colors.surface);

    f.header->setLookAndFeel(nullptr);
}

// =============================================================================
// 7b. ColourPickerPopup favourites — pure free functions, no juce::Component involved.
// =============================================================================

TEST(ColourPickerPopupTest, SerializeParseRoundTrips) {
    const std::vector<juce::Colour> colours{juce::Colour(0xff4FC1FFu), juce::Colour(0xff7FD962u),
                                            juce::Colour(0x00000000u)};
    const auto serialized = synth::ui::serializeFavouriteColours(colours);
    const auto parsed = synth::ui::parseFavouriteColours(serialized);

    ASSERT_EQ(parsed.size(), colours.size());
    for (size_t i = 0; i < colours.size(); ++i)
        EXPECT_EQ(parsed[i].getARGB(), colours[i].getARGB());
}

TEST(ColourPickerPopupTest, ParseToleratesJunkEntriesWithoutLosingTheRest) {
    const auto parsed = synth::ui::parseFavouriteColours("FF4FC1FF,not-a-colour,,FF7FD962,zzzzzzzz");
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].getARGB(), 0xFF4FC1FFu);
    EXPECT_EQ(parsed[1].getARGB(), 0xFF7FD962u);
}

TEST(ColourPickerPopupTest, ParseEmptyStringIsEmptyList) { EXPECT_TRUE(synth::ui::parseFavouriteColours("").empty()); }

TEST(ColourPickerPopupTest, LoadWithNoPropsSeedsDefaultsFromTrackPalette) {
    const auto loaded = synth::ui::loadFavouriteColours(nullptr);
    const auto& palette = synth::ui::trackPaletteArgb();
    ASSERT_EQ(loaded.size(), palette.size());
    for (size_t i = 0; i < palette.size(); ++i)
        EXPECT_EQ(loaded[i].getARGB(), palette[i]);
}

TEST(ColourPickerPopupTest, AddFavouriteDedupesAndPreservesOrder) {
    std::vector<juce::Colour> favourites;
    synth::ui::addFavourite(favourites, juce::Colour(0xff111111u));
    synth::ui::addFavourite(favourites, juce::Colour(0xff222222u));
    synth::ui::addFavourite(favourites, juce::Colour(0xff111111u)); // duplicate — must not append again

    ASSERT_EQ(favourites.size(), 2u);
    EXPECT_EQ(favourites[0].getARGB(), 0xff111111u);
    EXPECT_EQ(favourites[1].getARGB(), 0xff222222u);
}

TEST(ColourPickerPopupTest, RemoveFavouriteRemovesOnlyTheMatch) {
    std::vector<juce::Colour> favourites{juce::Colour(0xff111111u), juce::Colour(0xff222222u),
                                         juce::Colour(0xff333333u)};
    synth::ui::removeFavourite(favourites, juce::Colour(0xff222222u));

    ASSERT_EQ(favourites.size(), 2u);
    EXPECT_EQ(favourites[0].getARGB(), 0xff111111u);
    EXPECT_EQ(favourites[1].getARGB(), 0xff333333u);
}

// =============================================================================
// 8. synth::ui::resolveTrackColour — the pure resolver
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
