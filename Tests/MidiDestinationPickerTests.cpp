// MidiDestinationPickerTests.cpp
//
// synth::ui::MidiDestinationPicker in isolation, against a fake provider/apply pair — no
// juce::CallOutBox, no MainComponent, no juce::AudioProcessorGraph. Mirrors
// TimelineTrackHeaderTests.cpp's own "talk to the app only through callbacks, stub the rest"
// approach (see that file's header comment).

#include "../Source/UI/MidiDestinationPicker.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::MidiDestinationPicker;

namespace {

// A fake destination list the picker's provider reads, and the apply callback mutates — so a
// toggle's effect on `connected` is visible the moment the picker re-pulls the provider, exactly
// like the real graph is the truth for MainComponent::getMidiDestinationOptions().
struct FakeGraph {
    std::vector<MidiDestinationPicker::Option> options;

    std::vector<MidiDestinationPicker::Option> refresh() const { return options; }

    void apply(juce::uint32 nodeUid, bool connect) {
        for (auto& option : options)
            if (option.nodeUid == nodeUid)
                option.connected = connect;
        ++applyCalls;
        lastNodeUid = nodeUid;
        lastConnect = connect;
    }

    int applyCalls = 0;
    juce::uint32 lastNodeUid = 0;
    bool lastConnect = false;
};

// A fake whose apply() deliberately does NOT mutate `options` — models a stale/no-op apply (the
// bound Track In node or the target vanished between the list being built and the click), so the
// picker's post-apply refresh must show the UNCHANGED (pre-click) state, not what the click asked
// for.
struct StubbornFakeGraph {
    std::vector<MidiDestinationPicker::Option> options;
    int applyCalls = 0;

    std::vector<MidiDestinationPicker::Option> refresh() const { return options; }
    void apply(juce::uint32, bool) { ++applyCalls; }
};

std::unique_ptr<MidiDestinationPicker> makePicker(FakeGraph& graph) {
    auto picker = std::make_unique<MidiDestinationPicker>([&graph] { return graph.refresh(); },
                                                           [&graph](juce::uint32 uid, bool connect) {
                                                               graph.apply(uid, connect);
                                                           });
    picker->setSize(260, 320);
    return picker;
}

} // namespace

TEST(MidiDestinationPickerTest, RowsBuiltFromTheProvider) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}};
    auto picker = makePicker(graph);

    const auto names = picker->getVisibleRowNamesForTest();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "Oscillator 1");
    EXPECT_EQ(names[1], "Sampler");
}

TEST(MidiDestinationPickerTest, EmptyOptionsShowTheBindFirstHint) {
    FakeGraph graph; // no options at all
    auto picker = makePicker(graph);

    const auto names = picker->getVisibleRowNamesForTest();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Bind this track to a Track In node first");
}

TEST(MidiDestinationPickerTest, SearchFiltersRowsCaseInsensitiveSubstring) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}, {"Wavetable", 3, false}};
    auto picker = makePicker(graph);

    picker->setSearchTextForTest("sam");
    auto names = picker->getVisibleRowNamesForTest();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Sampler");

    picker->setSearchTextForTest("WAVE");
    names = picker->getVisibleRowNamesForTest();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "Wavetable");
}

TEST(MidiDestinationPickerTest, SearchClearsBackToTheFullList) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}};
    auto picker = makePicker(graph);

    picker->setSearchTextForTest("sam");
    ASSERT_EQ(picker->getVisibleRowNamesForTest().size(), 1u);

    picker->setSearchTextForTest("");
    EXPECT_EQ(picker->getVisibleRowNamesForTest().size(), 2u);
}

TEST(MidiDestinationPickerTest, JunkSearchTextMatchesNothing) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}};
    auto picker = makePicker(graph);

    picker->setSearchTextForTest("zzzznotarealdestination");
    EXPECT_TRUE(picker->getVisibleRowNamesForTest().empty());
}

TEST(MidiDestinationPickerTest, ToggleCallsApplyWithTheRightNodeUidAndDirection) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}};
    auto picker = makePicker(graph);

    picker->toggleRowForTest(0); // "Oscillator 1": currently disconnected -> connect
    EXPECT_EQ(graph.applyCalls, 1);
    EXPECT_EQ(graph.lastNodeUid, 1u);
    EXPECT_TRUE(graph.lastConnect);

    picker->toggleRowForTest(1); // "Sampler": currently connected -> disconnect
    EXPECT_EQ(graph.applyCalls, 2);
    EXPECT_EQ(graph.lastNodeUid, 2u);
    EXPECT_FALSE(graph.lastConnect);
}

TEST(MidiDestinationPickerTest, ToggleRebuildsRowsFromThePostApplyProviderState) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}};
    auto picker = makePicker(graph);

    picker->toggleRowForTest(0);
    // graph.apply() flipped options[0].connected to true — the picker must have re-pulled that,
    // not just assumed the click succeeded. There's no direct "is this row checked" getter, but a
    // second identical toggle (which computes wantConnect from the CURRENT connected flag) proves
    // it: if the picker still thought it was disconnected, this second toggle would ask to CONNECT
    // again instead of disconnecting.
    picker->toggleRowForTest(0);
    EXPECT_EQ(graph.applyCalls, 2);
    EXPECT_FALSE(graph.lastConnect) << "the picker must have seen connected==true after the first "
                                       "toggle to compute a disconnect on the second";
}

TEST(MidiDestinationPickerTest, ToggleWithANoOpApplyLeavesTheGraphsRealStateShowing) {
    // The apply callback here never actually mutates `connected` — models a stale binding, where
    // MainComponent::setMidiDestinationConnected silently no-ops. The picker's post-apply refresh
    // must reflect that: a second toggle should still ask for the SAME direction as the first,
    // because nothing actually changed.
    StubbornFakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}};

    juce::uint32 lastUid = 0;
    bool lastConnect = false;
    auto picker = std::make_unique<MidiDestinationPicker>(
        [&graph] { return graph.refresh(); },
        [&graph, &lastUid, &lastConnect](juce::uint32 uid, bool connect) {
            lastUid = uid;
            lastConnect = connect;
            graph.apply(uid, connect);
        });
    picker->setSize(260, 320);

    picker->toggleRowForTest(0);
    EXPECT_EQ(lastUid, 1u);
    EXPECT_TRUE(lastConnect); // was false, so the first click asks to connect

    picker->toggleRowForTest(0);
    EXPECT_TRUE(lastConnect) << "the apply never actually flipped `connected`, so a second click "
                                "must ask for the SAME thing again, not a disconnect";
}

TEST(MidiDestinationPickerTest, ToggleOnAFilteredRowIndexTargetsTheVisibleRow) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}, {"Sampler", 2, true}, {"Oscillator 2", 3, false}};
    auto picker = makePicker(graph);

    picker->setSearchTextForTest("oscillator");
    ASSERT_EQ(picker->getVisibleRowNamesForTest().size(), 2u);

    picker->toggleRowForTest(1); // second VISIBLE row is "Oscillator 2" (nodeUid 3)
    EXPECT_EQ(graph.lastNodeUid, 3u);
}

TEST(MidiDestinationPickerTest, ToggleOutOfRangeIsANoOp) {
    FakeGraph graph;
    graph.options = {{"Oscillator 1", 1, false}};
    auto picker = makePicker(graph);

    picker->toggleRowForTest(5); // no such row
    EXPECT_EQ(graph.applyCalls, 0);
}

TEST(MidiDestinationPickerTest, ToggleOnTheHintRowIsANoOp) {
    FakeGraph graph; // empty -> hint row only, non-interactive
    auto picker = makePicker(graph);

    picker->toggleRowForTest(0);
    EXPECT_EQ(graph.applyCalls, 0);
}
