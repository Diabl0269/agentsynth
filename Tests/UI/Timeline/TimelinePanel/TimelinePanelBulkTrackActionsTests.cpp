// TimelinePanelBulkTrackActionsTests.cpp: with several tracks selected, Cmd+D, Cmd+C / Cmd+V, Cmd+Backspace, the
// M / S buttons, the colour picker and a header drag act on every selected track, each as ONE undo step
// (docs/timeline/tracks.md#selecting-several-tracks). Real paths on a real MainComponent: selection by Cmd-click
// MouseEvents on the rows, keys through the row's keyPressed, M / S by MouseEvents on the hit-tested buttons, the
// colour picker's own commit, the drag by hand-built mouse events.
#include "../../../App/MainComponent/MainComponentTestFixture.h"
#include "AI/AIStateMapper/GraphRebuildBatch.h"
#include "AudioEngine/AudioEngine.h"
#include "TimelinePanelTestEvents.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

namespace {

using Tracks = std::vector<synth::TrackId>;
const juce::ModifierKeys kCmd(juce::ModifierKeys::commandModifier);
const juce::KeyPress kCmdD('d', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmdC('c', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmdV('v', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kCmdBackspace(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0);

} // namespace

class BulkTrackActionsTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        synth::ui::test_hooks::deleteTrackConfirmHookForTest() = nullptr;
        mc.reset();
        MainComponentTest::TearDown();
    }

    // Audio tracks are bound to a node and linked to a mixer channel; MIDI tracks are loose rows (their M / S live on
    // the doc).
    void addTracks(int n, bool midi = false) {
        for (int i = 0; i < n; ++i)
            if (midi)
                doc().addTrack(synth::TrackKind::Midi, "Loose " + juce::String(i + 1));
            else
                mc->simulateAddAudioTrackClick();
        panel().setSize(1400, 700); // tall: a drag stays clear of the autoscroll edge zones
    }
    synth::ui::TimelinePanelComponent& panel() { return mc->getTimelinePanel(); }
    synth::TimelineDoc& doc() { return mc->getTimelineDoc(); }
    juce::AudioProcessorGraph& graph() { return mc->getAudioEngine().getGraph(); }
    Tracks ids() {
        Tracks out;
        for (const auto& t : doc().getTracks())
            out.push_back(t.id);
        return out;
    }
    juce::StringArray names() {
        juce::StringArray out;
        for (const auto& t : doc().getTracks())
            out.add(t.name);
        return out;
    }
    synth::ui::TimelineTrackHeaderComponent& row(int index) { return *panel().getTrackHeaderAt(index); }

    // A real click, found through the header list's hit test (trackNumber is 1-based).
    void click(int trackNumber, juce::ModifierKeys mods = {}) {
        auto& header = row(trackNumber - 1);
        auto* hit = panel().getTrackHeaderListForTest().getComponentAt(header.getX() + 1,
                                                                       header.getY() + header.getHeight() / 2);
        auto* target = dynamic_cast<synth::ui::TimelineTrackHeaderComponent*>(hit);
        ASSERT_NE(target, nullptr);
        const juce::Point<float> pos(1.0f, static_cast<float>(target->getHeight()) * 0.5f);
        target->mouseDown(makeClickEvent(*target, pos, mods.withFlags(juce::ModifierKeys::leftButtonModifier)));
        target->mouseUp(makeClickEvent(*target, pos, mods.withFlags(juce::ModifierKeys::leftButtonModifier)));
    }
    void select(std::initializer_list<int> trackNumbers) {
        bool first = true;
        for (const int n : trackNumbers) {
            click(n, first ? juce::ModifierKeys() : kCmd);
            first = false;
        }
    }
    // A MouseEvent pair on whatever the header list's hit test finds at the button's centre.
    void clickButton(int trackNumber, bool solo) {
        auto& header = row(trackNumber - 1);
        auto& button = solo ? header.getSoloButton() : header.getMuteButton();
        auto& list = panel().getTrackHeaderListForTest();
        const auto centre = list.getLocalPoint(&button, button.getLocalBounds().getCentre());
        auto* hit = list.getComponentAt(centre);
        ASSERT_EQ(hit, &button) << "the hit test lands on the button";
        // A headless MouseEvent pair never sees the button as "mouse over" (JUCE's triggerClick posts a message), so
        // the hit-tested button's own click handler runs, as the existing channel-link tests do.
        ASSERT_TRUE(button.onClick);
        button.onClick();
    }

    std::unique_ptr<MainComponent> mc;
};

TEST_F(BulkTrackActionsTest, CmdDOnThreeSelectedTracksCopiesAllThreeAfterTheLastWithOneRebuildAndOneUndo) {
    addTracks(4);
    const auto before = ids();
    const auto namesBefore = names();
    const auto nodesBefore = graph().getNodes().size();
    select({1, 2, 3});
    ASSERT_EQ(panel().selectedTracks().size(), 3u);

    const int rebuilds = synth::GraphRebuildBatch::rebuildCountForTest();
    ASSERT_TRUE(row(2).keyPressed(kCmdD));
    EXPECT_EQ(synth::GraphRebuildBatch::rebuildCountForTest() - rebuilds, 1) << "three copies, one audio-graph rebuild";

    ASSERT_EQ(doc().getTracks().size(), 7u);
    const auto after = ids();
    EXPECT_EQ(Tracks(after.begin(), after.begin() + 3), Tracks(before.begin(), before.begin() + 3));
    EXPECT_EQ(after[6], before[3]) << "the copies sit after the LAST selected track, ahead of the unselected one";
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(doc().getTracks()[static_cast<size_t>(3 + i)].name, namesBefore[i] + " copy")
            << "in the same relative order";
    EXPECT_EQ(panel().selectedTracks(), Tracks(after.begin() + 3, after.begin() + 6)) << "the copies are selected";
    EXPECT_EQ(panel().getSelectedTrackId(), after[5]);
    const auto nodesAfter = graph().getNodes().size();
    EXPECT_GT(nodesAfter, nodesBefore);

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(ids(), before) << "ONE Cmd+Z removes all three";
    EXPECT_EQ(graph().getNodes().size(), nodesBefore);
    ASSERT_TRUE(mc->getUndoManager().redo());
    EXPECT_EQ(ids(), after) << "ONE redo brings all three back";
    EXPECT_EQ(graph().getNodes().size(), nodesAfter);
}

TEST_F(BulkTrackActionsTest, CmdDOnASingleSelectedTrackStillMakesOneCopyBelowIt) {
    addTracks(3);
    const auto before = ids();
    const auto namesBefore = names();
    select({1});
    ASSERT_TRUE(row(0).keyPressed(kCmdD));
    ASSERT_EQ(doc().getTracks().size(), 4u);
    EXPECT_EQ(ids()[0], before[0]);
    EXPECT_EQ(doc().getTracks()[1].name, namesBefore[0] + " copy");
    EXPECT_EQ(ids()[2], before[1]);
}

TEST_F(BulkTrackActionsTest, CopyThenPasteOfTwoTracksPutsBothAfterThePrimaryTrackInOneUndo) {
    addTracks(3);
    const auto before = ids();
    const auto namesBefore = names();
    select({1});
    EXPECT_FALSE(row(0).keyPressed(kCmdC)) << "one selected track: Cmd+C is not claimed";
    select({1, 3});
    ASSERT_TRUE(row(2).keyPressed(kCmdC));
    click(2); // the primary track is now the middle one

    ASSERT_TRUE(row(1).keyPressed(kCmdV));
    ASSERT_EQ(doc().getTracks().size(), 5u);
    const auto after = ids();
    EXPECT_EQ(after[0], before[0]);
    EXPECT_EQ(after[1], before[1]);
    EXPECT_EQ(after[4], before[2]);
    EXPECT_EQ(doc().getTracks()[2].name, namesBefore[0] + " copy");
    EXPECT_EQ(doc().getTracks()[3].name, namesBefore[2] + " copy");
    EXPECT_EQ(panel().selectedTracks(), Tracks({after[2], after[3]}));

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(ids(), before);
}

TEST_F(BulkTrackActionsTest, CmdBackspaceOnThreeSelectedTracksAsksOnceAndOneUndoBringsBackOrderAndContent) {
    addTracks(4);
    const auto before = ids();
    const auto namesBefore = names();
    std::vector<juce::String> bindings;
    for (const auto& t : doc().getTracks())
        bindings.push_back(t.bindingUuid);
    const auto nodesBefore = graph().getNodes().size();
    int asked = 0;
    juce::String title;
    synth::ui::test_hooks::deleteTrackConfirmHookForTest() = [&](const auto& text, auto done) {
        ++asked;
        title = text.title;
        done(true, false);
    };
    select({1, 2, 4});

    ASSERT_TRUE(row(3).keyPressed(kCmdBackspace));
    EXPECT_EQ(asked, 1) << "one question for all three";
    EXPECT_EQ(title, "Delete 3 tracks?");
    EXPECT_EQ(ids(), Tracks({before[2]}));
    EXPECT_LT(graph().getNodes().size(), nodesBefore);

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(ids(), before) << "ONE Cmd+Z restores all three, in order";
    EXPECT_EQ(names(), namesBefore);
    for (size_t i = 0; i < before.size(); ++i)
        EXPECT_EQ(doc().getTracks()[i].bindingUuid, bindings[i]);
    EXPECT_EQ(graph().getNodes().size(), nodesBefore);
}

TEST_F(BulkTrackActionsTest, BulkDeleteTakesEachTracksMacroAndOneUndoBringsTracksAndMacrosBack) {
    addTracks(3);
    auto& macros = mc->getGraphEditor().getMacros();
    const int macrosBefore = macros.size();
    const auto before = ids();
    EXPECT_GE(macrosBefore, 3) << "every audio track lives in its own macro";
    int asked = 0;
    synth::ui::test_hooks::deleteTrackConfirmHookForTest() = [&](const auto&, auto done) {
        ++asked;
        done(true, false);
    };
    select({1, 2});

    ASSERT_TRUE(row(1).keyPressed(kCmdBackspace));
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(ids(), Tracks({before[2]}));
    EXPECT_EQ(macros.size(), macrosBefore - 2) << "both tracks' macros left with them";

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(ids(), before) << "ONE Cmd+Z restores both tracks";
    EXPECT_EQ(mc->getGraphEditor().getMacros().size(), macrosBefore) << "...and both macros";
}

TEST_F(BulkTrackActionsTest, MuteOnASelectedTrackMutesEverySelectedTrackAndOnAnUnselectedOneOnlyThatTrack) {
    addTracks(4, /*midi=*/true);
    select({1, 2, 3});
    clickButton(2, /*solo=*/false);
    EXPECT_TRUE(doc().getTracks()[0].muted);
    EXPECT_TRUE(doc().getTracks()[1].muted);
    EXPECT_TRUE(doc().getTracks()[2].muted);
    EXPECT_FALSE(doc().getTracks()[3].muted);

    ASSERT_TRUE(mc->getUndoManager().undo());
    for (const auto& t : doc().getTracks())
        EXPECT_FALSE(t.muted) << "one Cmd+Z unmutes all of them";

    clickButton(4, /*solo=*/false); // the fourth track is not selected: only it is muted
    EXPECT_FALSE(doc().getTracks()[0].muted);
    EXPECT_TRUE(doc().getTracks()[3].muted);
}

TEST_F(BulkTrackActionsTest, TheClickedTracksNewStateIsTakenByEverySelectedTrackWhateverTheirOwnWas) {
    addTracks(3, /*midi=*/true);
    doc().setTrackMuted(doc().getTracks()[0].id, true);
    select({1, 2, 3});
    clickButton(2, /*solo=*/false); // track 2 goes muted: so does track 3, track 1 stays muted
    EXPECT_TRUE(doc().getTracks()[0].muted);
    EXPECT_TRUE(doc().getTracks()[1].muted);
    EXPECT_TRUE(doc().getTracks()[2].muted);
    clickButton(2, /*solo=*/false); // and back: all unmuted
    for (const auto& t : doc().getTracks())
        EXPECT_FALSE(t.muted);
}

TEST_F(BulkTrackActionsTest, SoloOnASelectedTrackSolosEverySelectedTrackInOneUndo) {
    addTracks(3, /*midi=*/true);
    select({1, 3});
    clickButton(1, /*solo=*/true);
    EXPECT_TRUE(doc().getTracks()[0].soloed);
    EXPECT_FALSE(doc().getTracks()[1].soloed);
    EXPECT_TRUE(doc().getTracks()[2].soloed);
    ASSERT_TRUE(mc->getUndoManager().undo());
    for (const auto& t : doc().getTracks())
        EXPECT_FALSE(t.soloed);
}

TEST_F(BulkTrackActionsTest, AColourChosenOnASelectedTrackColoursEverySelectedTrackInOneUndo) {
    addTracks(4);
    std::vector<juce::uint32> original;
    for (const auto& t : doc().getTracks())
        original.push_back(t.colourArgb);
    select({1, 2, 4});
    auto picker = row(1).createColourPickerForTest();
    ASSERT_NE(picker, nullptr);
    picker->setCurrentColourForTest(juce::Colours::hotpink);
    EXPECT_EQ(doc().getTracks()[0].colourArgb, juce::Colours::hotpink.getARGB()) << "the preview shows on all of them";
    picker->commitForTest();

    for (const int i : {0, 1, 3})
        EXPECT_EQ(doc().getTracks()[static_cast<size_t>(i)].colourArgb, juce::Colours::hotpink.getARGB());
    EXPECT_EQ(doc().getTracks()[2].colourArgb, original[2]) << "the unselected track keeps its colour";

    ASSERT_TRUE(mc->getUndoManager().undo());
    for (size_t i = 0; i < original.size(); ++i)
        EXPECT_EQ(doc().getTracks()[i].colourArgb, original[i]) << "one Cmd+Z restores every colour";
}

TEST_F(BulkTrackActionsTest, DraggingOneOfTwoSeparatedSelectedTracksMovesBothAsABlockInOneUndo) {
    addTracks(4);
    const auto original = ids();
    select({1, 3}); // tracks 1 and 3, with 2 and 4 between and after
    const int rh = row(0).getHeight();
    auto& list = panel().getTrackHeaderListForTest();
    auto& pressed = row(0);
    const int pressY = pressed.getY() + 10;
    const auto local = [&](int listY) { return pressed.getLocalPoint(&list, juce::Point<float>(3.0f, (float)listY)); };

    pressed.mouseDown(
        makeClickEvent(pressed, local(pressY), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    EXPECT_EQ(panel().selectedTracks().size(), 2u) << "pressing a selected row keeps the selection for the drag";
    const int dropY = pressY + 4 * rh; // below the last row
    pressed.mouseDrag(makeDragEvent(pressed, local(pressY + rh + 6), local(pressY)));
    pressed.mouseDrag(makeDragEvent(pressed, local(dropY), local(pressY)));
    pressed.mouseUp(makeClickEvent(pressed, local(dropY)));

    EXPECT_EQ(ids(), Tracks({original[1], original[3], original[0], original[2]}))
        << "both selected tracks move together, in their relative order, to where the dragged one dropped";
    EXPECT_EQ(panel().selectedTracks().size(), 2u);

    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(ids(), original) << "one undo step puts both back";
}

TEST_F(BulkTrackActionsTest, ASimpleClickOnASelectedRowCollapsesTheSelectionToItOnRelease) {
    addTracks(3);
    select({1, 2, 3});
    click(2);
    EXPECT_EQ(panel().selectedTracks(), Tracks({ids()[1]}));
}

TEST_F(BulkTrackActionsTest, MuteOnALinkedTrackMutesEverySelectedChannelInOneUndo) {
    addTracks(4);
    select({1, 2, 3});
    clickButton(2, /*solo=*/false);
    EXPECT_TRUE(row(0).getMuteButton().getToggleState());
    EXPECT_TRUE(row(1).getMuteButton().getToggleState());
    EXPECT_TRUE(row(2).getMuteButton().getToggleState());
    EXPECT_FALSE(row(3).getMuteButton().getToggleState());
    ASSERT_TRUE(mc->getUndoManager().undo());
    for (int i = 0; i < 4; ++i)
        EXPECT_FALSE(row(i).getMuteButton().getToggleState()) << "one Cmd+Z unmutes the lot";
}
