// MixerTrackDeleteStripTests.cpp: deleting a track also deletes its mixer strip, in the same undo step, and Cmd+Z
// brings both back with the strip's settings, sends and place; the mixer column leaves and returns through the column
// motion. Drives a real off-screen MainComponent: the timeline row's Delete Track menu item, then the app's undo
// manager. The strips' track selection, Cmd+Backspace and Option+Left/Right on them are at the end.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/DeleteTrackMacroAutoAnswer.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesPane.h"
#include "UI/Mixer/MixerZonesPane/MixerZonesRow.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::ExitEnterTimeline;

class MockProviderTDS : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTDS"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

struct Rig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    DeleteTrackMacroAutoRemove removeMacroWhenAsked;
    MainComponent mc{std::make_unique<MockProviderTDS>()};

    explicit Rig(bool animate = false) {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
        mixer().setSize(1400, 360);
        mixer().rebuild();
        mc.getTimelinePanel().setSize(1400, 360);
        if (animate) {
            mixer().forceColumnMotionForTest(true);
            mc.getTimelinePanel().forceTrackGlideForTest(true);
        }
    }
    synth::ui::MixerPanelComponent& mixer() { return mc.getBottomDock().getMixerPanel(); }
    auto& motion() { return mixer().getColumnMotionForTest(); }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    synth::MixerSnapshot snapshot() {
        return synth::buildMixerSnapshot(graph(), mc.getTimelineDoc(), mc.getGraphEditor().getMacros());
    }
    // The track channels (not buses), in mixer order.
    std::vector<synth::MixerColumn> strips() {
        std::vector<synth::MixerColumn> out;
        for (const auto& column : snapshot().columns)
            if (column.kind == synth::MixerColumn::Kind::Strip)
                out.push_back(column);
        return out;
    }
    static const synth::MixerColumn* columnWithUuid(const juce::String& uuid,
                                                    const std::vector<synth::MixerColumn>& all) {
        for (const auto& column : all)
            if (column.uuid == uuid)
                return &column;
        return nullptr;
    }
    juce::RangedAudioParameter* findParam(juce::AudioProcessorGraph::NodeID strip, const juce::String& id) {
        for (auto* p : graph().getNodeForId(strip)->getProcessor()->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged && ranged->paramID == id)
                return ranged;
        return nullptr;
    }
    float param(juce::AudioProcessorGraph::NodeID strip, const juce::String& id) {
        auto* p = findParam(strip, id);
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : -1000.0f;
    }
    void setParam(juce::AudioProcessorGraph::NodeID strip, const juce::String& id, float value) {
        if (auto* p = findParam(strip, id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    }
    void deleteTrackRow(int index) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(index);
        ASSERT_NE(header, nullptr);
        header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    }
    float panelX(int columnIndex) {
        auto* column = mixer().getStripColumnForTest(columnIndex);
        return static_cast<float>(mixer().getViewportForTest().getX() + column->getX());
    }
};

} // namespace

TEST(MixerTrackDeleteStrip, DeletingATrackDeletesItsStripAndNoOrphanIsLeft) {
    Rig rig;
    const auto before = rig.strips();
    ASSERT_EQ(before.size(), 3u);
    const auto gone = before[1].uuid;

    rig.deleteTrackRow(1);

    const auto after = rig.strips();
    ASSERT_EQ(after.size(), 2u);
    EXPECT_EQ(Rig::columnWithUuid(gone, after), nullptr) << "the strip went with its track";
    for (const auto& column : after)
        EXPECT_FALSE(column.feedingTracks.empty()) << "no strip is left without a track";
    EXPECT_EQ(rig.mc.getTimelineDoc().getTracks().size(), 2u);
    EXPECT_EQ(rig.mixer().getStripColumnForTest(2), nullptr);
}

TEST(MixerTrackDeleteStrip, UndoBringsTheStripBackWithItsSettingsAndPlaceAndRedoRemovesItAgain) {
    Rig rig;
    const auto victim = rig.strips()[1];
    rig.setParam(victim.nodeId, "gain", -6.0f);
    rig.setParam(victim.nodeId, "pan", 0.5f);
    const auto settled = rig.strips();

    rig.deleteTrackRow(1);
    ASSERT_EQ(rig.strips().size(), 2u);
    ASSERT_TRUE(rig.mc.getUndoManager().undo()) << "one step brings back the track and its strip";

    const auto restored = rig.strips();
    ASSERT_EQ(restored.size(), 3u);
    EXPECT_EQ(restored[1].uuid, victim.uuid) << "back in its old position";
    EXPECT_EQ(restored[1].name, settled[1].name);
    EXPECT_EQ(restored[1].colour, settled[1].colour);
    EXPECT_EQ(restored[1].feedingTracks.size(), 1u);
    ASSERT_FALSE(settled[1].inserts.empty());
    EXPECT_EQ(restored[1].inserts.size(), settled[1].inserts.size()) << "inserts, in order";
    for (size_t i = 0; i < restored[1].inserts.size(); ++i)
        EXPECT_EQ(restored[1].inserts[i].uuid, settled[1].inserts[i].uuid);
    EXPECT_NEAR(rig.param(restored[1].nodeId, "gain"), -6.0f, 0.15f);
    EXPECT_NEAR(rig.param(restored[1].nodeId, "pan"), 0.5f, 0.01f);
    EXPECT_EQ(rig.mc.getTimelineDoc().getTracks().size(), 3u);

    ASSERT_TRUE(rig.mc.getUndoManager().redo());
    EXPECT_EQ(rig.strips().size(), 2u);
    EXPECT_EQ(Rig::columnWithUuid(victim.uuid, rig.strips()), nullptr);
}

TEST(MixerTrackDeleteStrip, ASendFromAnotherChannelIntoTheDeletedStripGoesWithItAndComesBackOnUndo) {
    Rig rig;
    const auto before = rig.strips();
    ASSERT_EQ(synth::addSend(rig.graph(), before[0].nodeId, before[1].nodeId), 0);
    rig.mixer().rebuild();
    ASSERT_EQ(rig.strips()[0].sends.size(), 1u);

    rig.deleteTrackRow(1);

    const auto after = rig.strips();
    ASSERT_EQ(after.size(), 2u);
    EXPECT_TRUE(after[0].sends.empty()) << "no send is left pointing at nothing";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    const auto restored = rig.strips();
    ASSERT_EQ(restored.size(), 3u);
    ASSERT_EQ(restored[0].sends.size(), 1u);
    EXPECT_EQ(restored[0].sends[0].targetNodeId, restored[1].nodeId) << "the send feeds the returned strip again";
    EXPECT_EQ(restored[1].receivesFrom.size(), 1u);
}

TEST(MixerTrackDeleteStrip, AChannelWithoutATrackKeepsItsColumnWhenATrackIsDeleted) {
    Rig rig;
    synth::DefaultChannelLayout layout;
    rig.mc.getUndoManager().recordStructuralChange(rig.graph(), [&] { synth::buildBusChannel(rig.graph(), layout); });
    rig.mixer().rebuild();
    juce::String bus;
    for (const auto& column : rig.snapshot().columns)
        if (column.kind == synth::MixerColumn::Kind::Bus)
            bus = column.uuid;
    ASSERT_TRUE(bus.isNotEmpty());

    rig.deleteTrackRow(0);

    EXPECT_EQ(rig.strips().size(), 2u);
    bool busStillThere = false;
    for (const auto& column : rig.snapshot().columns)
        busStillThere = busStillThere || (column.kind == synth::MixerColumn::Kind::Bus && column.uuid == bus);
    EXPECT_TRUE(busStillThere) << "a channel nothing owns is not a track's strip";
}

TEST(MixerTrackDeleteStrip, TheColumnShrinksAwayOnDeleteThenGrowsBackOnUndoInStepWithTheRow) {
    ReducedMotionGuard guard(false);
    Rig rig(true);
    const auto victim = rig.strips()[1].uuid;
    const float victimX = rig.panelX(1);
    const float afterX = rig.panelX(2);
    const float pitch = afterX - victimX;

    rig.deleteTrackRow(1);

    ASSERT_TRUE(rig.motion().isRunning()) << "the column leaves, it does not just vanish";
    EXPECT_EQ(rig.motion().exitGhostCount(), 1);
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning()) << "the row shrinks beside it";
    rig.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(victim).has_value());
    EXPECT_LT(rig.motion().drawnRectFor(victim)->getWidth(), pitch);
    rig.motion().applyAtMs(rig.motion().timeline().totalMs());
    EXPECT_FLOAT_EQ(rig.panelX(1), afterX - pitch) << "the next column closed the gap";
    rig.motion().finishNow();

    ASSERT_TRUE(rig.mc.getUndoManager().undo());

    ASSERT_TRUE(rig.motion().isRunning());
    EXPECT_TRUE(rig.motion().timeline().hasEnter);
    EXPECT_FALSE(rig.motion().drawnRectFor(victim).has_value()) << "the gap opens first";
    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(victim).has_value());
    EXPECT_LT(rig.motion().drawnRectFor(victim)->getWidth(), pitch);
    rig.motion().applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs +
                           ExitEnterTimeline::kOutlineMs * 0.25);
    EXPECT_GT(rig.motion().outlineAlphaFor(victim), 0.0f);
    EXPECT_EQ(rig.mixer().getStripColumnForTest(1)->getUuid(), victim);
}

TEST(MixerTrackDeleteStrip, ReduceMotionFadesTheColumnInPlace) {
    ReducedMotionGuard guard(true);
    Rig rig(true);
    const auto victim = rig.strips()[1].uuid;
    const float width = rig.panelX(2) - rig.panelX(1);
    rig.deleteTrackRow(1);
    ASSERT_TRUE(rig.motion().isRunning());
    rig.motion().applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    ASSERT_TRUE(rig.motion().drawnRectFor(victim).has_value());
    EXPECT_FLOAT_EQ(rig.motion().drawnRectFor(victim)->getWidth(), width) << "a fade, not a shrink";
}

TEST(MixerTrackDeleteStrip, OffScreenTheColumnLeavesAndReturnsAtOnce) {
    ReducedMotionGuard guard(false);
    Rig rig(false);
    rig.deleteTrackRow(1);
    EXPECT_FALSE(rig.motion().isRunning());
    EXPECT_EQ(rig.strips().size(), 2u);
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_FALSE(rig.motion().isRunning());
    EXPECT_EQ(rig.strips().size(), 3u);
}

namespace selection {

class MockProviderMTS : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMTS"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

constexpr const char* kSelAskKey = "timelineAskBeforeDeletingTrack";
using Tracks = std::vector<synth::TrackId>;

struct SelRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    DeleteTrackMacroAutoRemove removeMacroWhenAsked;
    MainComponent mc{std::make_unique<MockProviderMTS>()};
    int macroAsked = 0;

    SelRig() {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true); // a hidden mixer defers its rebuilds
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < 4; ++i)
            mc.simulateAddAudioTrackClick();
        mixer().setSize(1400, 360);
        mixer().rebuild();
        mc.getTimelinePanel().setSize(1400, 360);
        settings().removeValue(kSelAskKey);
        synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest() = [this](const auto&, auto done) {
            ++macroAsked;
            done(synth::ui::DeleteTrackMacroChoice::removeMacro, false);
        };
    }
    ~SelRig() { settings().removeValue(kSelAskKey); }

    synth::ui::MixerPanelComponent& mixer() { return mc.getBottomDock().getMixerPanel(); }
    juce::PropertiesFile& settings() { return *mc.getAppPropertiesForTest().getUserSettings(); }
    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }
    AppUndoManager& undo() { return mc.getUndoManager(); }
    Tracks ids() {
        Tracks out;
        for (const auto& t : doc().getTracks())
            out.push_back(t.id);
        return out;
    }
    Tracks selected() { return mc.getTimelinePanel().selectedTracks(); }
    bool key(int code, int mods) { return mixer().keyPressed(juce::KeyPress(code, juce::ModifierKeys(mods), 0)); }
    // Press strip `index`'s header (a real press and release, through the panel's own listener).
    void press(int index, int mods = 0) { mixer().pressStripHeaderForTest(index, juce::ModifierKeys(mods)); }
    bool stripSelected(int index) { return mixer().getStripColumnForTest(index)->isTrackSelectedForTest(); }
    // A press also moves the panel's focus onto the strip, so the last press is the focused strip.
    juce::String channelOf(int index) {
        int seen = 0;
        auto& pane = mixer().getZonesPaneForTest();
        for (int i = 0; i < pane.getRowCountForTest(); ++i) {
            const auto& channel = pane.getRowForTest(i)->getChannel();
            if (channel.kind == synth::ui::MixerZoneChannelKind::Track && seen++ == index)
                return channel.id;
        }
        return {};
    }
};

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

} // namespace selection

using namespace selection;

TEST(MixerTrackSelection, HeaderPressesFollowTheTimelinesRulesAndTheStripsShowIt) {
    SelRig rig;
    const auto t = rig.ids();
    rig.press(0);
    EXPECT_EQ(rig.selected(), Tracks({t[0]})) << "a plain press selects the strip's track";
    rig.press(2, kCmd);
    EXPECT_EQ(rig.selected(), Tracks({t[0], t[2]})) << "Cmd adds one";
    EXPECT_TRUE(rig.stripSelected(0));
    EXPECT_FALSE(rig.stripSelected(1));
    EXPECT_TRUE(rig.stripSelected(2));
    rig.press(2, kCmd);
    EXPECT_EQ(rig.selected(), Tracks({t[0]})) << "Cmd on a selected one takes it out";
    rig.press(3, kShift);
    EXPECT_EQ(rig.selected(), Tracks({t[0], t[2], t[3]})) << "Shift extends from the last-clicked anchor";
    EXPECT_TRUE(rig.stripSelected(3));
    EXPECT_FALSE(rig.stripSelected(1));
    rig.press(1);
    EXPECT_EQ(rig.selected(), Tracks({t[1]})) << "a plain press collapses the set to that strip";
    EXPECT_FALSE(rig.stripSelected(0));
    EXPECT_TRUE(rig.mc.getTimelinePanel().isTrackSelected(t[1])) << "the timeline row shows the same selection";
}

TEST(MixerTrackSelection, ACmdOrShiftPressOnlyEditsTheSelectionAndNeverPicksTheMacroOnTheCanvas) {
    SelRig rig;
    int revealed = 0;
    rig.mixer().onRevealCanvas = [&] { ++revealed; };
    rig.press(0);
    EXPECT_EQ(revealed, 1) << "a plain press still picks the macro on the canvas, as before";
    rig.press(1, kCmd);
    rig.press(2, kShift);
    EXPECT_EQ(revealed, 1) << "a modified press leaves the canvas alone";
    rig.press(3);
    EXPECT_EQ(revealed, 2) << "and the next plain press is back to normal";
}

TEST(MixerTrackSelection, TheArrowKeysCarryTheSelectionAndCmdSpaceTogglesTheFocusedStrip) {
    SelRig rig;
    const auto t = rig.ids();
    rig.press(0, kCmd);
    rig.press(0, kCmd); // focus on strip 0, nothing selected
    ASSERT_TRUE(rig.key(juce::KeyPress::rightKey, 0));
    EXPECT_EQ(rig.selected(), Tracks({t[1]})) << "the selection follows the focused strip";
    ASSERT_TRUE(rig.key(juce::KeyPress::spaceKey, kCmd));
    EXPECT_EQ(rig.selected(), Tracks()) << "Cmd+Space takes the focused strip's track out";
    ASSERT_TRUE(rig.key(juce::KeyPress::spaceKey, kCmd));
    EXPECT_EQ(rig.selected(), Tracks({t[1]})) << "and puts it back";
}

TEST(MixerTrackSelection, CmdBackspaceAsksOnceThenDeletesTheSetInOneUndoStep) {
    SelRig rig;
    const auto t = rig.ids();
    rig.press(2);
    rig.press(1, kCmd);
    ASSERT_TRUE(rig.key(juce::KeyPress::backspaceKey, kCmd));
    EXPECT_EQ(rig.macroAsked, 1) << "one question for the set";
    EXPECT_EQ(rig.ids(), Tracks({t[0], t[3]}));
    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.ids(), t) << "ONE Cmd+Z brings both back";
}

TEST(MixerTrackSelection, CmdBackspaceOnAnUnselectedStripDeletesOnlyThatOneAndTheAskSettingIsHonoured) {
    SelRig rig;
    const auto t = rig.ids();
    rig.press(3);
    ASSERT_TRUE(rig.key(juce::KeyPress::leftKey, 0)); // 3 -> 2: the arrow selects it
    ASSERT_EQ(rig.selected(), Tracks({t[2]}));
    rig.settings().setValue(kSelAskKey, "0"); // "ask before deleting" off: the stored answer (remove) applies
    ASSERT_TRUE(rig.key(juce::KeyPress::backspaceKey, kCmd));
    EXPECT_EQ(rig.macroAsked, 0) << "the preference switched the question off";
    EXPECT_EQ(rig.ids(), Tracks({t[0], t[1], t[3]}));
}

TEST(MixerTrackSelection, TheHeaderMenuOffersDeleteTrackOrDeleteNTracksAndDeletesThroughTheSameQuestion) {
    SelRig rig;
    const auto t = rig.ids();
    juce::PopupMenu captured;
    rig.mixer().setShowZoneMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto channelOf = [&](int index) { return rig.channelOf(index); };
    const auto itemNamed = [&](const juce::String& startsWith) {
        juce::PopupMenu::MenuItemIterator it(captured);
        while (it.next())
            if (it.getItem().text.startsWith(startsWith))
                return it.getItem();
        return juce::PopupMenu::Item();
    };

    rig.press(0);
    rig.mixer().showChannelMenuForTest(channelOf(0));
    auto single = itemNamed("Delete");
    ASSERT_EQ(single.text, "Delete Track");
    ASSERT_TRUE(single.action != nullptr);
    single.action();
    EXPECT_EQ(rig.ids(), Tracks({t[1], t[2], t[3]})) << "the menu's single Delete asks only when a macro would go";
    EXPECT_EQ(rig.macroAsked, 1);
    ASSERT_TRUE(rig.undo().undo());

    rig.press(1);
    rig.press(2, kCmd);
    rig.mixer().showChannelMenuForTest(channelOf(1));
    auto several = itemNamed("Delete");
    EXPECT_EQ(several.text, "Delete 2 Tracks");
    several.action();
    EXPECT_EQ(rig.ids(), Tracks({t[0], t[3]}));
    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.ids(), t);
}

TEST(MixerTrackSelection, OptionArrowsMoveTheSetOnePlaceInOneUndoStepAndStopAtTheEdge) {
    SelRig rig;
    synth::ui::setReducedMotionForTest(true);
    const auto t = rig.ids();
    rig.press(1);
    rig.press(0, kCmd);
    rig.press(1, kCmd);
    rig.press(1, kCmd); // strips 0 and 1 selected, focus on 1
    ASSERT_TRUE(rig.key(juce::KeyPress::rightKey, kAlt));
    EXPECT_EQ(rig.ids(), Tracks({t[2], t[0], t[1], t[3]})) << "both selected strips moved one place right";
    EXPECT_EQ(rig.selected(), Tracks({t[0], t[1]})) << "and stay selected";
    ASSERT_TRUE(rig.key(juce::KeyPress::rightKey, kAlt));
    EXPECT_EQ(rig.ids(), Tracks({t[2], t[3], t[0], t[1]}));
    ASSERT_TRUE(rig.key(juce::KeyPress::rightKey, kAlt)) << "at the right edge the key is still taken";
    EXPECT_EQ(rig.ids(), Tracks({t[2], t[3], t[0], t[1]})) << "and nothing moves";
    ASSERT_TRUE(rig.undo().undo());
    EXPECT_EQ(rig.ids(), Tracks({t[2], t[0], t[1], t[3]})) << "each key press is one undo step";
    ASSERT_TRUE(rig.key(juce::KeyPress::leftKey, kAlt));
    EXPECT_EQ(rig.ids(), Tracks({t[0], t[1], t[2], t[3]}));
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(MixerTrackSelection, OptionArrowOnAStripOutsideTheSelectionMovesOnlyThatStrip) {
    SelRig rig;
    synth::ui::setReducedMotionForTest(true);
    const auto t = rig.ids();
    rig.press(0);
    rig.press(1, kCmd);
    rig.press(3, kCmd);
    rig.press(3, kCmd); // strips 0 and 1 selected; focus on 3, outside the set: it moves alone
    ASSERT_TRUE(rig.key(juce::KeyPress::leftKey, kAlt));
    EXPECT_EQ(rig.ids(), Tracks({t[0], t[1], t[3], t[2]}));
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(MixerTrackSelection, OptionArrowGlidesTheStripsThroughTheColumnDragMotion) {
    SelRig rig;
    synth::ui::setReducedMotionForTest(false);
    rig.mixer().forceColumnMotionForTest(true);
    rig.press(0);
    ASSERT_TRUE(rig.key(juce::KeyPress::rightKey, kAlt));
    EXPECT_TRUE(rig.mixer().isColumnReorderActiveForTest()) << "the strips glide, as after a column drop";
    synth::ui::setReducedMotionForTest(std::nullopt);
}
