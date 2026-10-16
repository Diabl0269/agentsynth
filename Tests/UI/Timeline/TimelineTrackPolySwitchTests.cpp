// TimelineTrackPolySwitchTests.cpp
//
// The Mono | Poly switch on a track header, against a stub host: when it shows, which side is filled, that a press
// asks the host to switch that track's voice graph, and that it is reachable by keyboard and named for a screen
// reader. What the host does with the request (the whole voice graph, one undo step) is
// GraphEditorPolyChainTests.cpp's concern.

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

TEST(TimelineTrackPolySwitchTest, ItIsHiddenWhenTheTrackHasNothingPolyCapable) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    auto* header = f.header();
    ASSERT_NE(header, nullptr);
    EXPECT_FALSE(header->getPolySwitch().isVisible());
}

TEST(TimelineTrackPolySwitchTest, ItShowsTheCurrentModeAndLeavesBothSidesEmptyWhileHalfPoly) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::mono;
    f.header()->refreshFromDoc();
    auto& sw = f.header()->getPolySwitch();
    EXPECT_TRUE(sw.isVisible());
    EXPECT_TRUE(sw.getMonoButton().getToggleState());
    EXPECT_FALSE(sw.getPolyButton().getToggleState());

    f.host.states[lead] = TrackPolyState::poly;
    f.header()->refreshFromDoc();
    EXPECT_FALSE(sw.getMonoButton().getToggleState());
    EXPECT_TRUE(sw.getPolyButton().getToggleState());

    f.host.states[lead] = TrackPolyState::mixed;
    f.header()->refreshFromDoc();
    EXPECT_FALSE(sw.getMonoButton().getToggleState());
    EXPECT_FALSE(sw.getPolyButton().getToggleState());
    EXPECT_TRUE(sw.getPolyButton().getTooltip().containsIgnoreCase("some of its modules"));
}

TEST(TimelineTrackPolySwitchTest, PressingASideAsksTheHostToSwitchThatTrack) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    const auto b = f.doc.addTrack(TrackKind::Midi, "B");
    f.host.states[b] = TrackPolyState::mono;
    f.header(1)->refreshFromDoc();
    auto& sw = f.header(1)->getPolySwitch();

    sw.getPolyButton().triggerClick();
    sw.getMonoButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // triggerClick posts its click

    ASSERT_EQ(f.host.requests.size(), 2u);
    EXPECT_EQ(f.host.requests[0].first, b);
    EXPECT_TRUE(f.host.requests[0].second);
    EXPECT_FALSE(f.host.requests[1].second);
}

TEST(TimelineTrackPolySwitchTest, BothSidesAreTabStopsWithNamesAndATooltipThatKeepsFocusOnTheRow) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::mono;
    f.header()->refreshFromDoc();
    auto& sw = f.header()->getPolySwitch();
    for (auto* button : {&sw.getMonoButton(), &sw.getPolyButton()}) {
        EXPECT_TRUE(button->getWantsKeyboardFocus());
        EXPECT_FALSE(button->getMouseClickGrabsKeyboardFocus()) << "a click must leave focus on the row";
        EXPECT_TRUE(button->getTitle().contains("Lead")) << button->getTitle();
        EXPECT_TRUE(button->getTooltip().isNotEmpty());
    }
    EXPECT_EQ(sw.getMonoButton().getTitle(), "Mono, Lead");
    EXPECT_EQ(sw.getPolyButton().getTitle(), "Poly, Lead");
}

TEST(TimelineTrackPolySwitchTest, ItFitsTheRowBesideTheBindingChipAtTheMinimumHeaderWidth) {
    Fixture f;
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    f.host.states[lead] = TrackPolyState::poly;
    auto* header = f.header();
    header->refreshFromDoc();
    header->setSize(synth::ui::TimelinePanelComponent::kMinTrackHeaderWidth, 56);
    auto& sw = header->getPolySwitch();

    EXPECT_TRUE(header->getLocalBounds().contains(sw.getBounds())) << "overflows the row";
    EXPECT_GT(sw.getWidth(), 0);
    EXPECT_FALSE(sw.getBounds().intersects(header->getBindingChip().getBounds())) << "overlaps the binding chip";
    EXPECT_FALSE(sw.getBounds().intersects(header->getShowModuleButton().getBounds()));
    EXPECT_GE(header->getBindingChip().getWidth(), 30) << "the chip keeps some width";
}
