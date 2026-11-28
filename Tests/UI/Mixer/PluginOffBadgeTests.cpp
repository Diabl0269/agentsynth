// PluginOffBadgeTests.cpp: the "a plugin on this track was switched off after a crash" badge on the timeline track
// header and the mixer column header (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down). A real
// off-screen MainComponent: the badge appears from the live graph, follows a Turn on, reads the same on both surfaces,
// is a Tab stop with a screen-reader name, goes to the plugin's card when pressed, and fades (or not) with the app's
// motion setting.

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
        synth::PluginIdentity identity;
        identity.name = "Crashy Synth";
        identity.format = "VST3";
        identity.uid = 0xC4A5;
        module->loadPlugin(identity, backend);
        EXPECT_TRUE(synth::test::waitUntil([this] { return !module->isLoading(); }));
        EXPECT_TRUE(graph.addConnection({{nodeId, 0}, {rig.firstColumn().getNodeId(), 0}}));
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
