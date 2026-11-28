// MixerSourcesBadgeTests.cpp (docs/mixer/panel.md#the-sources-badge): what plays into a channel is a small badge in the
// column header, not a row under it. There is no blank row on any column; the badge shows only on a channel with a
// source that differs from its name; hovering it, its screen-reader title and its description list every source; it
// is a Tab stop that acts on Return. A real off-screen MainComponent and its mixer panel.
#include "../../StubPluginInstance.h"
#include "../../TestWait.h"
#include "../Layout/FadeVisibilityTestGuard.h"
#include "MixerHeaderTestRig.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Layout/HelperTooltip.h"
#include "UI/Mixer/MixerHeader/PluginOffBadge.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {

using mixer_header_test::HeaderRig;
using synth::ui::MixerColumnComponent;
using synth::ui::MixerSection;

// Tracks 0 and 2 now feed one strip, so that strip's source differs from its own name.
void shareStripBetweenFirstAndLastTrack(HeaderRig& rig) {
    rig.mc.simulateAddAudioTrackClick();
    auto& doc = rig.doc();
    const auto& tracks = doc.getTracks();
    ASSERT_GE(tracks.size(), 3u);
    doc.setTrackBinding(tracks[2].id, tracks[0].bindingUuid);
    rig.panel().rebuild();
}

} // namespace

TEST(MixerSourcesBadgeTest, NoColumnReservesARowUnderItsHeaderWhateverItsSources) {
    HeaderRig rig;
    shareStripBetweenFirstAndLastTrack(rig);

    int checked = 0;
    for (int i = 0; rig.panel().getStripColumnForTest(i) != nullptr; ++i) {
        auto& column = *rig.panel().getStripColumnForTest(i);
        EXPECT_EQ(column.getHeaderForTest().getBottom(), column.getSectionViewportForTest(MixerSection::Inserts).getY())
            << "the first section starts right under the header, column " << i;
        EXPECT_EQ(column.getFaderForTest().getY(), rig.firstColumn().getFaderForTest().getY())
            << "faders stay level without any reserved row, column " << i;
        ++checked;
    }
    EXPECT_GE(checked, 3);
    ASSERT_NE(rig.panel().getMasterColumnForTest(), nullptr);
    EXPECT_EQ(rig.panel().getMasterColumnForTest()->getFaderForTest().getY(),
              rig.firstColumn().getFaderForTest().getY());
}

TEST(MixerSourcesBadgeTest, ASharedSourceShowsABadgeListingEveryTrackInItsTooltipTitleAndDescription) {
    HeaderRig rig;
    shareStripBetweenFirstAndLastTrack(rig);
    const auto first = rig.doc().getTracks()[0].name;
    const auto last = rig.doc().getTracks()[2].name;

    auto& column = rig.firstColumn();
    ASSERT_TRUE(column.hasSources());
    auto& badge = column.getHeaderForTest().getSourcesButtonForTest();
    ASSERT_TRUE(badge.isVisible());
    const auto words = "Plays into this channel: " + first + ", " + last;
    EXPECT_EQ(badge.getTooltip(), words);
    EXPECT_EQ(badge.getTitle(), words);
    EXPECT_EQ(badge.getDescription(), words);
    EXPECT_EQ(column.getHeaderForTest().getSources(), first + ", " + last);

    EXPECT_FALSE(badge.getBounds().isEmpty());
    EXPECT_TRUE(column.getHeaderForTest().getLocalBounds().contains(badge.getBounds()));
}

TEST(MixerSourcesBadgeTest, ABusListsItsFeedingStripsAndALinkedChannelNamedAfterItsTrackHasNoBadge) {
    HeaderRig rig;
    auto* bus = rig.busColumn();
    ASSERT_NE(bus, nullptr);
    ASSERT_TRUE(bus->hasSources());
    const auto sender = rig.firstColumn().getHeaderForTest().getDisplayName();
    EXPECT_EQ(bus->getHeaderForTest().getSourcesButtonForTest().getTooltip(), "Plays into this channel: " + sender);
    EXPECT_TRUE(synth::ui::isHelperTooltip(bus->getHeaderForTest().getSourcesButtonForTest()))
        << "a helper tip: it keeps showing when \"Show info tooltips\" is off";

    EXPECT_FALSE(rig.firstColumn().hasSources()) << "a linked channel named after its one track says nothing more";
    EXPECT_FALSE(rig.firstColumn().getHeaderForTest().getSourcesButtonForTest().isVisible());
    EXPECT_TRUE(rig.firstColumn().getHeaderForTest().getSourcesButtonForTest().getBounds().isEmpty());
}

TEST(MixerSourcesBadgeTest, TheBadgeIsAKeyboardReachableButtonThatSelectsTheChannel) {
    HeaderRig rig;
    auto& badge = rig.busColumn()->getHeaderForTest().getSourcesButtonForTest();
    EXPECT_TRUE(badge.getWantsKeyboardFocus());
    EXPECT_FALSE(badge.getTooltip().isEmpty());
    EXPECT_FALSE(badge.getTitle().isEmpty());

    int selected = 0;
    rig.busColumn()->onColumnClicked = [&selected] { ++selected; };
    EXPECT_TRUE(static_cast<juce::Component&>(badge).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)))
        << "Return acts on it";
    synth::test::waitUntil([&] { return (selected) == (1); });
    EXPECT_EQ(selected, 1) << "activating it selects the channel, like a click on the header";

    mixer_header_test::clickThroughMouse(badge);
    EXPECT_EQ(selected, 2);
}

TEST(MixerSourcesBadgeTest, TheHeaderDescriptionAlsoListsTheSources) {
    HeaderRig rig;
    auto* bus = rig.busColumn();
    ASSERT_NE(bus, nullptr);
    EXPECT_TRUE(bus->getHeaderForTest().getDescription().contains("Plays into this channel: "))
        << bus->getHeaderForTest().getDescription();
}

TEST(MixerSourcesBadgeTest, TheNameKeepsRoomAndTheBadgeNeverOverlapsIt) {
    HeaderRig rig;
    auto& header = rig.busColumn()->getHeaderForTest();
    const auto badge = header.getSourcesButtonForTest().getBounds();
    const auto name = header.getNameLabelForTest().getBounds();
    ASSERT_FALSE(badge.isEmpty());
    EXPECT_FALSE(badge.intersects(name));
    EXPECT_GE(name.getWidth(), 40) << "even on a bus (BUS tag and sources badge) the name still has room";
}

// The switched-off plugin badge (docs/mixer/plugin-off-badge.md): the same row of the header as the sources badge, and
// on the timeline track header.
namespace {

using mixer_header_test::HeaderRig;
using synth::HostedPluginModule;
using synth::ui::PluginOffBadge;

PluginOffBadge* trackBadgeIn(juce::Component& header) {
    return dynamic_cast<PluginOffBadge*>(header.findChildWithID("trackPluginOffBadge"));
}
PluginOffBadge* mixerBadgeIn(juce::Component& header) {
    return dynamic_cast<PluginOffBadge*>(header.findChildWithID("mixerPluginOffBadge"));
}

struct SwitchedOffPluginRig {
    HeaderRig rig;
    synth::test::StubBackend backend;
    synth::HostedPluginBackend::ScopedDefault installed{&backend}; // Turn on loads through the default backend
    HostedPluginModule* module = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId;

    /** A hosted plugin that "Open without it" switched off, wired into the first track's channel strip. */
    SwitchedOffPluginRig() {
        backend.skipPluginForNextLoad("Crashy Synth", "/Library/Audio/Plug-Ins/VST3/Crashy Synth.vst3");
        module = new HostedPluginModule();
        auto& graph = rig.mc.getAudioEngine().getGraph();
        auto node = graph.addNode(std::unique_ptr<juce::AudioProcessor>(module));
        node->properties.set("uuid", "crashy-uuid");
        nodeId = node->nodeID;
        module->prepareToPlay(48000.0, 64);
        // Wired in before the plugin is switched off: the badge re-reads the graph only when the switched-off count
        // moves, and the wait below pumps the message loop (a header refresh can run in it).
        EXPECT_TRUE(graph.addConnection({{nodeId, 0}, {rig.firstColumn().getNodeId(), 0}}));
        synth::PluginIdentity identity;
        identity.name = "Crashy Synth";
        identity.format = "VST3";
        identity.uid = 0xC4A5;
        module->loadPlugin(identity, backend);
        EXPECT_TRUE(synth::test::waitUntil([this] { return !module->isLoading(); }));
    }

    synth::ui::TimelineTrackHeaderComponent& header() { return *rig.mc.getTimelinePanel().getTrackHeaderAt(0); }
    void tickBoth() {
        rig.firstColumn().refreshMeter(0.1f);
        header().tickChannelMeter();
    }
    void settleFades() { synth::ui::FadeVisibility::stepAllForTest(1.0f); }
};

} // namespace

TEST(PluginOffBadgeTest, TheTrackHeaderAndTheMixerColumnBothShowItAfterTheTickAndNameThePlugin) {
    FadeAnimateGuard animate;
    SwitchedOffPluginRig f;
    ASSERT_NE(trackBadgeIn(f.header()), nullptr);
    ASSERT_NE(mixerBadgeIn(f.rig.firstColumn().getHeaderForTest()), nullptr);
    EXPECT_FALSE(trackBadgeIn(f.header())->isVisible()) << "no badge until the tick sees the switched-off plugin";

    f.tickBoth();
    f.settleFades();
    f.header().setSize(260, 60); // the rig's panel may not have laid the header out
    f.rig.firstColumn().getHeaderForTest().setSize(160, 90);
    f.header().resized();

    auto* onTrack = trackBadgeIn(f.header());
    auto* onStrip = mixerBadgeIn(f.rig.firstColumn().getHeaderForTest());
    for (auto* badge : {onTrack, onStrip}) {
        ASSERT_TRUE(badge->isVisible()) << (badge == onTrack ? "track header" : "mixer column");
        EXPECT_EQ(badge->getTooltip(), PluginOffBadge::wordsFor({"Crashy Synth"}));
        EXPECT_NE(badge->getTooltip().indexOf("Crashy Synth"), -1);
        EXPECT_EQ(badge->getTitle(), badge->getTooltip()) << "the screen reader says what the tooltip says";
        EXPECT_TRUE(badge->getWantsKeyboardFocus()) << "a Tab stop";
        EXPECT_TRUE(synth::ui::isHelperTooltip(*badge)) << "it shows even with info tooltips off";
        EXPECT_FALSE(badge->getBounds().isEmpty()) << (badge == onTrack ? "track header" : "mixer column");
    }
    EXPECT_TRUE(f.header().getLocalBounds().contains(onTrack->getBounds()));
    EXPECT_TRUE(f.header().getNameLabel().getRight() <= onTrack->getX()) << "the name makes room, nothing overlaps";
}

TEST(PluginOffBadgeTest, ItGoesAwayWhenThePluginIsTurnedBackOnWithoutAnyDocEdit) {
    FadeAnimateGuard animate;
    SwitchedOffPluginRig f;
    f.tickBoth();
    f.settleFades();
    ASSERT_TRUE(trackBadgeIn(f.header())->isVisible());

    f.module->turnBackOn(f.backend);
    ASSERT_TRUE(synth::test::waitUntil([&] { return f.module->hasInstance(); }));
    f.tickBoth();
    EXPECT_TRUE(trackBadgeIn(f.header())->isVisible()) << "fading out: still drawn until the fade ends";
    EXPECT_FALSE(trackBadgeIn(f.header())->isShownLogically());
    f.settleFades();
    EXPECT_FALSE(trackBadgeIn(f.header())->isVisible());
    EXPECT_FALSE(mixerBadgeIn(f.rig.firstColumn().getHeaderForTest())->isVisible());
}

TEST(PluginOffBadgeTest, PressingItShowsThePluginsCardFromTheTrackAndFromTheMixer) {
    FadeAnimateGuard animate;
    SwitchedOffPluginRig f;
    juce::String shown;
    f.rig.firstColumn().onEditOnCanvas = [&](const juce::String& uuid) { shown = uuid; };
    f.tickBoth();
    f.settleFades();

    mixer_header_test::clickThroughMouse(*mixerBadgeIn(f.rig.firstColumn().getHeaderForTest()));
    EXPECT_EQ(shown, "crashy-uuid") << "the mixer shows the switched-off plugin's own card";
    // The track header hops to its module through the same host call as the Show Module button; here that is the
    // first track's module on the canvas, the way the button does it.
    EXPECT_TRUE(trackBadgeIn(f.header())->onClick != nullptr);
}

TEST(PluginOffBadgeTest, AColumnBuiltWhileAPluginIsOffShowsTheBadgeAtOnce) {
    SwitchedOffPluginRig f;
    f.rig.panel().rebuild();
    f.rig.panel().getStripColumnForTest(0)->getHeaderForTest().resized();
    auto* badge = mixerBadgeIn(f.rig.panel().getStripColumnForTest(0)->getHeaderForTest());
    ASSERT_NE(badge, nullptr);
    EXPECT_TRUE(badge->isVisible());
    EXPECT_EQ(badge->getAlpha(), 1.0f) << "a column built with the panel does not fade in";
}

TEST(PluginOffBadgeTest, ItFadesWithTheAppsMotionSettingAndLandsAtOnceWhenAnimationsAreOff) {
    juce::Component parent;
    parent.setSize(100, 40);
    PluginOffBadge badge;
    parent.addChildComponent(badge);
    badge.setBounds(0, 0, 20, 20);
    {
        FadeAnimateGuard full(synth::ui::AnimationMode::full);
        badge.setPlugins({"A"});
        EXPECT_TRUE(badge.isVisible());
        EXPECT_EQ(badge.getAlpha(), 0.0f) << "frame 0 of the fade in";
        synth::ui::FadeVisibility::stepAllForTest(0.5f);
        EXPECT_NEAR(badge.getAlpha(), 0.5f, 0.01f);
        synth::ui::FadeVisibility::stepAllForTest(1.0f);
        EXPECT_EQ(badge.getAlpha(), 1.0f);
        EXPECT_FLOAT_EQ(badge.progress(), 1.0f);
    }
    {
        FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
        badge.setPlugins({});
        EXPECT_TRUE(badge.isVisible()) << "a plain fade under Reduce Motion, not a pop";
        synth::ui::FadeVisibility::stepAllForTest(1.0f);
        EXPECT_FALSE(badge.isVisible());
    }
    {
        FadeAnimateGuard off(synth::ui::AnimationMode::off);
        badge.setPlugins({"A"});
        EXPECT_TRUE(badge.isVisible());
        EXPECT_EQ(badge.getAlpha(), 1.0f) << "Animations off: it lands whole";
        badge.setPlugins({});
        EXPECT_FALSE(badge.isVisible());
    }
}

TEST(PluginOffBadgeTest, TheWordsListEveryPluginAndSayWhatToDo) {
    EXPECT_TRUE(PluginOffBadge::wordsFor({}).isEmpty());
    const auto one = PluginOffBadge::wordsFor({"Serum"});
    EXPECT_TRUE(one.startsWith("Plugin switched off after a crash: Serum"));
    EXPECT_NE(one.indexOf("Turn on"), -1);
    EXPECT_TRUE(
        PluginOffBadge::wordsFor({"Serum", "Diva"}).startsWith("Plugins switched off after a crash: Serum, Diva"));
}
