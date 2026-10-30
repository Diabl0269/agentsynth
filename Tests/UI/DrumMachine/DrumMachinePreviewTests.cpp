// Play clip and the buttons that open the drum machine: the grid's loop control (button, Space, every way it ends), the
// panel's wiring to the app's host, and opening the grid on a Drum Kit track (the clip under the playhead, or a new
// one-bar "Beat N" as ONE undo step).

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Transport/TransportService.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/DrumMachine/DrumMachineHost.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <cmath>

using synth::ui::DrumMachineComponent;

namespace {

// A host the grid's Play clip is wired to, recording what it was asked and answering with a beat the test controls.
struct PreviewLog {
    std::vector<std::pair<double, double>> starts;
    int stops = 0;
    bool accept = true;
    double beat = std::nan("");
    std::vector<bool> changes;

    void wire(DrumMachineComponent& grid) {
        grid.onStartClipPreview = [this](double start, double end) {
            starts.emplace_back(start, end);
            return accept;
        };
        grid.onStopClipPreview = [this] { ++stops; };
        grid.clipPreviewBeat = [this] { return beat; };
        grid.onClipPreviewChanged = [this](bool on) { changes.push_back(on); };
    }
};

struct PlayFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    PreviewLog log;
    synth::TrackId track;
    ClipId clip;

    explicit PlayFixture(double clipStart = 16.0, double clipLength = 4.0) {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, clipStart, clipLength, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, 320);
        log.wire(grid);
        grid.openClip(clip);
    }

    void clickPlayClip() {
        auto& button = grid.getPlayClipButton();
        button.onClick();
    }
};

} // namespace

TEST(DrumMachinePlayClipTest, ClickingThePlayClipButtonStartsTheLoopOnTheClipsAbsoluteRange) {
    PlayFixture f;
    f.log.beat = 16.0;
    f.clickPlayClip();
    ASSERT_EQ(f.log.starts.size(), 1u);
    EXPECT_EQ(f.log.starts[0], std::make_pair(16.0, 20.0)) << "the clip's own start and end, not clip-relative beats";
    EXPECT_TRUE(f.grid.isClipPreviewing());
    EXPECT_EQ(f.log.changes, (std::vector<bool>{true}));
}

TEST(DrumMachinePlayClipTest, WhileLoopingTheButtonReadsStopClipAndIsLitAndAClickStopsIt) {
    PlayFixture f;
    auto& button = f.grid.getPlayClipButton();
    EXPECT_EQ(button.getTitle(), "Play clip");
    EXPECT_FALSE(button.getToggleState());
    f.log.beat = 16.0;
    f.clickPlayClip();
    EXPECT_EQ(button.getTitle(), "Stop clip");
    EXPECT_EQ(button.getButtonText(), "Stop clip");
    EXPECT_TRUE(button.getToggleState()) << "the lit (accent) state";

    f.clickPlayClip();
    EXPECT_FALSE(f.grid.isClipPreviewing());
    EXPECT_EQ(f.log.stops, 1);
    EXPECT_EQ(button.getTitle(), "Play clip");
    EXPECT_FALSE(button.getToggleState());
    EXPECT_EQ(f.log.changes, (std::vector<bool>{true, false}));
}

TEST(DrumMachinePlayClipTest, TheButtonIsATabStopWithATooltipNamingTheShortcut) {
    PlayFixture f;
    auto& button = f.grid.getPlayClipButton();
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_TRUE(button.isVisible());
    EXPECT_TRUE(button.getTooltip().contains("Space")) << button.getTooltip();

    ShortcutManager manager;
    manager.setBinding("drumMachinePlayClip", juce::KeyPress('p'));
    f.grid.setShortcutManager(&manager);
    EXPECT_TRUE(f.grid.getPlayClipButton().getTooltip().contains("p")) << "the tooltip follows a rebind";
    EXPECT_FALSE(f.grid.getPlayClipButton().getTooltip().contains("Space"));
}

TEST(DrumMachinePlayClipTest, SpaceTogglesPlayClipAndReturnStillTogglesAHit) {
    PlayFixture f;
    f.log.beat = 16.0;
    f.grid.setFocusCell({f.grid.rowOfPitch(38), 3});

    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(f.grid.isClipPreviewing());
    EXPECT_TRUE(f.doc.getClip(f.clip)->notes.empty()) << "Space no longer toggles a hit";
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(f.grid.isClipPreviewing());

    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.doc.getClip(f.clip)->notes.size(), 1u);
    EXPECT_FALSE(f.grid.isClipPreviewing());
}

TEST(DrumMachinePlayClipTest, SpaceFollowsTheRebindableAction) {
    PlayFixture f;
    f.log.beat = 16.0;
    ShortcutManager manager;
    f.grid.setShortcutManager(&manager);
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))) << "Space is the default binding";
    EXPECT_TRUE(f.grid.isClipPreviewing());
    f.grid.stopClipPreview();

    manager.setBinding("drumMachinePlayClip", juce::KeyPress('p'));
    EXPECT_FALSE(f.grid.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(f.grid.isClipPreviewing());
    EXPECT_TRUE(f.grid.keyPressed(juce::KeyPress('p')));
    EXPECT_TRUE(f.grid.isClipPreviewing());
}

TEST(DrumMachinePlayClipTest, AHostThatCannotPlayTheClipLeavesTheButtonAlone) {
    PlayFixture f;
    f.log.accept = false;
    f.clickPlayClip();
    EXPECT_FALSE(f.grid.isClipPreviewing());
    EXPECT_EQ(f.grid.getPlayClipButton().getTitle(), "Play clip");
    EXPECT_TRUE(f.log.changes.empty());
}

TEST(DrumMachinePlayClipTest, ClosingTheGridOpeningAnotherClipOrTheTransportPlayingStopsTheLoop) {
    {
        PlayFixture f;
        f.log.beat = 16.0;
        f.clickPlayClip();
        f.grid.closeGrid();
        EXPECT_FALSE(f.grid.isClipPreviewing());
        EXPECT_EQ(f.log.stops, 1);
    }
    {
        PlayFixture f;
        f.log.beat = 16.0;
        f.clickPlayClip();
        f.grid.openClip(f.doc.addClip(f.track, 0.0, 4.0, "Beat 2"));
        EXPECT_FALSE(f.grid.isClipPreviewing()) << "a loop belongs to the clip it was started on";
        EXPECT_EQ(f.log.stops, 1);
    }
    {
        PlayFixture f;
        f.log.beat = 16.0;
        f.clickPlayClip();
        f.grid.setTransportPlaying(false);
        EXPECT_TRUE(f.grid.isClipPreviewing()) << "a stopped song leaves the loop alone";
        f.grid.setTransportPlaying(true);
        EXPECT_FALSE(f.grid.isClipPreviewing()) << "the song takes over";
        EXPECT_EQ(f.log.stops, 1);
    }
}

TEST(DrumMachinePlayClipTest, DeletingTheClipStopsTheLoop) {
    PlayFixture f;
    f.log.beat = 16.0;
    f.clickPlayClip();
    ASSERT_TRUE(f.doc.removeClip(f.clip));
    f.grid.refreshFromDoc();
    EXPECT_FALSE(f.grid.isOpen());
    EXPECT_FALSE(f.grid.isClipPreviewing());
    EXPECT_EQ(f.log.stops, 1);
}

TEST(DrumMachinePlayClipTest, MovingOrResizingTheClipWhileLoopingLoopsTheNewRange) {
    PlayFixture f;
    f.log.beat = 16.0;
    f.clickPlayClip();
    ASSERT_TRUE(f.doc.moveClip(f.clip, 24.0));
    f.grid.refreshFromDoc();
    ASSERT_EQ(f.log.starts.size(), 2u);
    EXPECT_EQ(f.log.starts[1], std::make_pair(24.0, 28.0));
    EXPECT_TRUE(f.grid.isClipPreviewing());

    f.grid.refreshFromDoc();
    EXPECT_EQ(f.log.starts.size(), 2u) << "an unchanged clip is not restarted";
}

TEST(DrumMachinePlayClipTest, ALoopThatEndedByItselfIsNoticedFromThePlayheadFrame) {
    PlayFixture f;
    f.log.beat = 17.0;
    f.clickPlayClip();
    f.grid.setPlayheadBeat(0.0);
    EXPECT_TRUE(f.grid.isClipPreviewing());
    f.log.beat = std::nan(""); // the module ended it: the song started, or the track went
    f.grid.setPlayheadBeat(0.0);
    EXPECT_FALSE(f.grid.isClipPreviewing());
    EXPECT_EQ(f.log.changes, (std::vector<bool>{true, false}));
}

TEST(DrumMachinePlayClipTest, WhileLoopingThePlayheadLineFollowsTheLoopNotTheStoppedSong) {
    PlayFixture f;
    f.log.beat = 18.0;
    f.clickPlayClip();
    f.grid.setPlayheadBeat(0.0); // the overlay hands over the (stopped) song's position

    const auto render = [&f] {
        juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        f.grid.paintEntireComponent(g, false);
        return image;
    };
    const auto looping = render();
    f.log.beat = 17.0;
    f.grid.setPlayheadBeat(0.0);
    const auto moved = render();
    const int y = f.grid.gridRegion().getCentreY();
    const int x = (int)std::lround(f.grid.beatToX(18.0));
    const int elsewhere = (int)std::lround(f.grid.beatToX(16.5));
    EXPECT_GT(rgbDistance(looping.getPixelAt(x, y), moved.getPixelAt(x, y)), 20.0) << "the line left beat 18 for 17";
    EXPECT_LT(rgbDistance(looping.getPixelAt(elsewhere, y), moved.getPixelAt(elsewhere, y)), 1.0);
}

//==============================================================================
// Wired into the panel, with a host that records the app-side calls.

namespace {

struct LoopHost : AuditionRecordingHost {
    synth::TrackId drumTrack;
    struct Start {
        synth::TrackId track;
        double start, end;
    };
    std::vector<Start> starts;
    std::vector<synth::TrackId> stops;
    double beat = std::nan("");
    bool drumKitRows(synth::TrackId t, synth::ui::KitRows*) const override { return t == drumTrack; }
    bool startClipPreview(synth::TrackId t, double a, double b) override {
        starts.push_back({t, a, b});
        return true;
    }
    void stopClipPreview(synth::TrackId t) override { stops.push_back(t); }
    double clipPreviewBeat(synth::TrackId) override { return beat; }
};

struct LoopPanel {
    TimelineDoc doc;
    AppUndoManager undo;
    LoopHost host;
    synth::TransportService transport;
    synth::ui::TimelinePanelComponent panel;
    synth::TrackId drums;
    ClipId clip;

    LoopPanel() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        panel.setSize(1200, 360);
        panel.getViewState().pixelsPerBeat = 40.0;
        transport.prepare(48000.0, 512);
        panel.setTransport(&transport);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTrackHeaderHost(&host);
        drums = doc.addTrack(TrackKind::Midi, "Drums");
        host.drumTrack = drums;
        panel.resized();
    }
    ~LoopPanel() { panel.setTrackHeaderHost(nullptr); }

    void locate(double beat) {
        transport.locateBeat(beat);
        transport.tick(512);
    }
    void clickPlayClip() {
        auto& button = panel.getDrumMachine().getPlayClipButton();
        button.onClick();
    }
    int clipCount() { return (int)doc.getTrack(drums)->clips.size(); }
};

} // namespace

TEST(DrumMachinePanelPlayClipTest, PlayClipAsksTheHostToLoopTheClipOnItsTrackAndStopsWhenTheGridCloses) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 8.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    f.host.beat = 8.0;
    f.clickPlayClip();
    ASSERT_EQ(f.host.starts.size(), 1u);
    EXPECT_EQ(f.host.starts[0].track, f.drums);
    EXPECT_DOUBLE_EQ(f.host.starts[0].start, 8.0);
    EXPECT_DOUBLE_EQ(f.host.starts[0].end, 12.0);

    f.panel.closeDrumMachine();
    ASSERT_EQ(f.host.stops.size(), 1u);
    EXPECT_EQ(f.host.stops[0], f.drums);
}

TEST(DrumMachinePanelPlayClipTest, SwitchingToThePianoRollStopsTheLoop) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 0.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    f.host.beat = 0.0;
    f.clickPlayClip();
    f.panel.getDrumMachine().onPianoRollRequested(f.clip);
    EXPECT_EQ(f.host.stops.size(), 1u);
    EXPECT_TRUE(f.panel.isPianoRollOpen());
}

TEST(DrumMachinePanelPlayClipTest, TheTransportPlayingStopsTheLoopAtTheHost) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 0.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    f.host.beat = 0.0;
    f.clickPlayClip();
    ASSERT_TRUE(f.panel.getDrumMachine().isClipPreviewing());

    auto snapshot = f.transport.getPositionSnapshot();
    snapshot.playing = true;
    f.panel.updateFromTransport(snapshot, 0.0);
    EXPECT_FALSE(f.panel.getDrumMachine().isClipPreviewing());
    EXPECT_EQ(f.host.stops.size(), 1u);
}

TEST(DrumMachinePanelPlayClipTest, DeletingTheClipStopsTheLoopOnTheTrackItStartedOn) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 0.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    f.host.beat = 0.0;
    f.clickPlayClip();
    ASSERT_TRUE(f.doc.removeClip(f.clip));
    f.panel.getDrumMachine().refreshFromDoc();
    ASSERT_EQ(f.host.stops.size(), 1u);
    EXPECT_EQ(f.host.stops[0], f.drums) << "the track was latched at the start: the clip no longer says which";
}

TEST(DrumMachinePanelPlayClipTest, SwitchingProjectStopsTheLoop) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 0.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    f.host.beat = 0.0;
    f.clickPlayClip();
    TimelineDoc other;
    f.panel.setTimelineDoc(&other);
    EXPECT_FALSE(f.panel.getDrumMachine().isClipPreviewing());
    EXPECT_EQ(f.host.stops.size(), 1u);
    f.panel.setTimelineDoc(&f.doc);
}

TEST(DrumMachinePanelPlayClipTest, TheLoopKeepsThePlayheadFramesRunningWithTheTransportStopped) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 0.0, 4.0, "Beat 1");
    f.panel.openDrumMachine(f.clip);
    auto& playhead = f.panel.getPlayhead();
    EXPECT_FALSE(playhead.isPlayheadTimerRunning());
    f.host.beat = 0.0;
    f.clickPlayClip();
    EXPECT_TRUE(playhead.isPlayheadTimerRunning()) << "the same 30 Hz frames the song uses, for the loop's beat";
    f.panel.getDrumMachine().stopClipPreview();
    EXPECT_FALSE(playhead.isPlayheadTimerRunning()) << "and they stop with it";
}

//==============================================================================
// "Drum machine" on a Drum Kit track.

TEST(DrumMachineOpenTest, OnAnEmptyTrackItAddsAOneBarBeatClipAtThePlayheadsBarAsOneUndoStep) {
    LoopPanel f;
    f.locate(5.5); // inside bar 2 of 4/4
    f.undo.clearUndoHistory();
    ASSERT_TRUE(f.panel.openDrumMachineOnTrack(f.drums));
    ASSERT_EQ(f.clipCount(), 1);
    const auto& clip = f.doc.getTrack(f.drums)->clips[0];
    EXPECT_EQ(clip.name, "Beat 1");
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0) << "the start of the bar the playhead is in";
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0) << "one bar";
    EXPECT_TRUE(f.panel.getDrumMachine().isOpen());
    EXPECT_EQ(f.panel.getDrumMachine().getClipId(), clip.id);

    f.undo.undo();
    EXPECT_EQ(f.clipCount(), 0) << "the whole add is one undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineOpenTest, TheNewClipTakesTheNextFreeBeatName) {
    LoopPanel f;
    f.doc.addClip(f.drums, 64.0, 4.0, "Beat 1");
    f.doc.addClip(f.drums, 72.0, 4.0, "Beat 2");
    f.locate(0.0);
    ASSERT_TRUE(f.panel.openDrumMachineOnTrack(f.drums));
    bool found = false;
    for (const auto& clip : f.doc.getTrack(f.drums)->clips)
        found = found || (clip.name == "Beat 3" && clip.startBeat == 0.0);
    EXPECT_TRUE(found);
}

TEST(DrumMachineOpenTest, WithAClipUnderThePlayheadItOpensThatClipAndAddsNothing) {
    LoopPanel f;
    f.clip = f.doc.addClip(f.drums, 8.0, 8.0, "Groove");
    f.doc.addClip(f.drums, 0.0, 4.0, "Intro");
    f.locate(10.0);
    f.undo.clearUndoHistory();
    ASSERT_TRUE(f.panel.openDrumMachineOnTrack(f.drums));
    EXPECT_EQ(f.clipCount(), 2);
    EXPECT_EQ(f.panel.getDrumMachine().getClipId(), f.clip);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineOpenTest, TheTrackHeaderButtonIsOnDrumKitTracksOnlyAndOpensTheGrid) {
    LoopPanel f;
    const auto keys = f.doc.addTrack(TrackKind::Midi, "Keys");
    f.panel.resized();
    synth::ui::TimelineTrackHeaderComponent *drumHeader = nullptr, *keysHeader = nullptr;
    for (int i = 0; i < f.panel.getTrackHeaderCount(); ++i) {
        auto* header = f.panel.getTrackHeaderAt(i);
        (header->getTrackId() == f.drums ? drumHeader : keysHeader) = header;
    }
    ASSERT_NE(drumHeader, nullptr);
    ASSERT_NE(keysHeader, nullptr);
    EXPECT_TRUE(drumHeader->getDrumMachineButton().isShowing() || drumHeader->getDrumMachineButton().isVisible());
    EXPECT_FALSE(keysHeader->getDrumMachineButton().isVisible()) << "a track that is not a Drum Kit has no button";
    EXPECT_EQ(keysHeader->getTrackId(), keys);

    auto& button = drumHeader->getDrumMachineButton();
    EXPECT_EQ(button.getTitle(), "Drum machine");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_FALSE(button.getTooltip().isEmpty());
    button.onClick();
    EXPECT_TRUE(f.panel.getDrumMachine().isOpen());
    EXPECT_EQ(f.clipCount(), 1) << "an empty track gets its first Beat";
}
