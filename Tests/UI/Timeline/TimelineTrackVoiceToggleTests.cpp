// TimelineTrackVoiceToggleTests.cpp
//
// The voice toggle (mono / poly icon button) on a track header, against a stub host: when it shows, its state and
// glyph, that a press asks the host to switch that track's voice graph, and that it is reachable by keyboard and
// named for a screen reader. Whether it fits the row at every height is TimelineTrackHeaderRowFitTests.cpp. What the
// host does with the request (the whole voice graph, one undo step) is GraphEditorPolyChainTests.cpp's concern.

#include "../../TestWait.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <map>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TrackPolyState;

namespace {

struct PolyHost : synth::ui::TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    TrackPolyState getTrackPolyState(TrackId track) override {
        const auto found = states.find(track);
        return found != states.end() ? found->second : TrackPolyState::none;
    }
    void setTrackPoly(TrackId track, bool poly) override { requests.push_back({track, poly}); }

    std::map<TrackId, TrackPolyState> states;
    std::vector<std::pair<TrackId, bool>> requests;
};

struct Fixture {
    TimelineDoc doc;
    PolyHost host;
    synth::ui::TimelinePanelComponent panel;

    Fixture() {
        panel.setSize(1200, 320);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
    }
    synth::ui::TimelineTrackHeaderComponent* header(int i = 0) { return panel.getTrackHeaderAt(i); }
};

} // namespace

TEST(TimelineTrackVoiceToggleTest, ItIsHiddenWhenTheTrackHasNothingPolyCapable) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    auto* header = f.header();
    ASSERT_NE(header, nullptr);
    EXPECT_FALSE(header->getVoiceToggle().isVisible());
}

TEST(TimelineTrackVoiceToggleTest, ItShowsTheCurrentModeAndMixedIsOffWithATooltipThatSaysSo) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::mono;
    f.header()->refreshFromDoc();
    auto& toggle = f.header()->getVoiceToggle();
    EXPECT_TRUE(toggle.isVisible());
    EXPECT_FALSE(toggle.getToggleState());
    EXPECT_EQ(toggle.getState(), TrackPolyState::mono);
    EXPECT_TRUE(toggle.getTooltip().containsIgnoreCase("one note at a time"));

    f.host.states[lead] = TrackPolyState::poly;
    f.header()->refreshFromDoc();
    EXPECT_TRUE(toggle.getToggleState());
    EXPECT_TRUE(toggle.getTooltip().containsIgnoreCase("several notes at once"));

    f.host.states[lead] = TrackPolyState::mixed;
    f.header()->refreshFromDoc();
    EXPECT_FALSE(toggle.getToggleState());
    EXPECT_EQ(toggle.getState(), TrackPolyState::mixed);
    EXPECT_TRUE(toggle.getTooltip().containsIgnoreCase("some of lead's modules are mono and some poly"));
    EXPECT_TRUE(toggle.getTooltip().containsIgnoreCase("switch its whole voice chain to poly"));
}

TEST(TimelineTrackVoiceToggleTest, AClickAsksForTheOtherModeAndHalfPolyBecomesPoly) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    const auto b = f.doc.addTrack(TrackKind::Midi, "B");
    f.host.states[b] = TrackPolyState::mono;
    f.header(1)->refreshFromDoc();
    auto& toggle = f.header(1)->getVoiceToggle();

    auto press = [&](TrackPolyState next, size_t expectedRequests) {
        toggle.triggerClick(); // the click is posted; the host then answers with the graph's new state
        synth::test::waitUntil([&] { return f.host.requests.size() == expectedRequests; });
        f.host.states[b] = next;
        f.header(1)->refreshFromDoc();
    };
    press(TrackPolyState::poly, 1u);  // mono -> poly
    press(TrackPolyState::mixed, 2u); // poly -> mono
    press(TrackPolyState::poly, 3u);  // half poly -> poly

    ASSERT_EQ(f.host.requests.size(), 3u);
    for (const auto& request : f.host.requests)
        EXPECT_EQ(request.first, b);
    EXPECT_TRUE(f.host.requests[0].second);
    EXPECT_FALSE(f.host.requests[1].second);
    EXPECT_TRUE(f.host.requests[2].second);
}

TEST(TimelineTrackVoiceToggleTest, ItIsATabStopNamedForAScreenReaderThatKeepsFocusOnTheRow) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::mono;
    f.header()->refreshFromDoc();
    auto& toggle = f.header()->getVoiceToggle();
    EXPECT_TRUE(toggle.getWantsKeyboardFocus());
    EXPECT_FALSE(toggle.getMouseClickGrabsKeyboardFocus()) << "a click must leave focus on the row";
    EXPECT_EQ(toggle.getTitle(), "Poly voices, Lead");
    EXPECT_TRUE(toggle.isToggleable()) << "a screen reader announces on or off";
    EXPECT_FALSE(toggle.getClickingTogglesState()) << "the graph is the truth, a click does not flip it";
    EXPECT_TRUE(toggle.getTooltip().isNotEmpty());

    f.host.states[lead] = TrackPolyState::poly;
    f.header()->refreshFromDoc();
    EXPECT_TRUE(toggle.getToggleState()) << "the state a screen reader reads as on";

    // Return and Space press it, like the show-module button next to it.
    f.host.requests.clear();
    EXPECT_TRUE(toggle.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(toggle.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_EQ(f.host.requests.size(), 2u);
    EXPECT_FALSE(f.host.requests[0].second);
}

TEST(TimelineTrackVoiceToggleTest, ItIsASquareIconTheSizeOfTheShowModuleButtonBesideTheBindingChip) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::poly;
    auto* header = f.header();
    header->refreshFromDoc();
    header->setSize(synth::ui::TimelinePanelComponent::kMinTrackHeaderWidth, 56);
    auto& toggle = header->getVoiceToggle();

    EXPECT_EQ(toggle.getWidth(), synth::ui::TrackShowModuleButton::kSize);
    EXPECT_EQ(toggle.getHeight(), synth::ui::TrackShowModuleButton::kSize);
    EXPECT_TRUE(header->getLocalBounds().contains(toggle.getBounds())) << "overflows the row";
    EXPECT_FALSE(toggle.getBounds().intersects(header->getBindingChip().getBounds()));
    EXPECT_FALSE(toggle.getBounds().intersects(header->getShowModuleButton().getBounds()));
    EXPECT_GE(header->getBindingChip().getWidth(), 60) << "the chip keeps most of the row";
}

// A header is built on the doc change that adds its track, before the track's graph exists, and shown whole by the
// refresh that follows in the same gesture: those controls land with the row. A change on a row that has been on
// screen since still fades.
