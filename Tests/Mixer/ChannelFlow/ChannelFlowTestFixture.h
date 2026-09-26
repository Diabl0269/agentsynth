#pragma once

// ChannelFlowTestFixture.h
//
// Shared fixtures and helpers for the ChannelFlow test suite
// (Tests/Mixer/ChannelFlow/ChannelFlow*Tests.cpp). Header-only; not compiled on its own and not
// registered in Tests/CMakeLists.txt. Engine-level rig helpers that don't need a MainComponent
// live in ChannelFlowTestRigs.h (FRO307) — included below.

#include "ChannelFlowTestRigs.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UserSettings.h"

class ChannelFlowTest : public ::testing::Test {
protected:
    // Same settings-file hygiene as AudioClipPlaybackTests.cpp's AddAudioTrackFlowTest /
    // RecordTapTests.cpp / TimelinePanelTests.cpp: the delegating MainComponent ctor reads/writes
    // the shared on-disk "Agent Synth" settings, so pin the keys this flow depends on before AND
    // after every test.
    void resetKeys() {
        juce::PropertiesFile::Options opts = synth::userSettingsOptions();

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("bottomDockVisible", "0");
            // T184: pinned ON (the default) so an earlier PreferencesSettingsTabTests run that
            // persisted "0" to this same shared on-disk settings file can't silently flip these
            // tests' trigger condition off.
            s->setValue("mixerAutoCreateChannelOnConnect", "1");
            // FRO42: the plugin-instrument tests below drive a REAL PluginScanService::ensureScanned()
            // through MainComponent, and a completed scan's pluginScanCompleted() unconditionally
            // persists the scan list (MainComponent::savePluginScanList()) to this SAME shared
            // on-disk file — same convention as PluginScanTests.cpp's PluginScanPersistenceTest::
            // clearScanList(). Left uncleared, a fake candidate path ("/plugins/Slow.vst3" etc.)
            // scanned once survives as a blacklist/known-plugin entry into every later run on this
            // machine, so a later test's "fresh" scan silently skips its own candidate as
            // already-known/blacklisted instead of actually invoking its child launcher.
            s->removeValue(MainComponent::kPluginScanListKey);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetKeys(); }
    void TearDown() override { resetKeys(); }

    // The menu hook, not the async PopupMenu — the same headless seam the binding chip uses.
    static void addAudioTrack(MainComponent& mc) {
        mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }

    // T183: the Instrument submenu's headless seam, keyed by module type name rather than the raw
    // menu id — matches how the picker itself is spelled everywhere else in this file. FRO48
    // (P9-3k): `poly` picks the "(Poly)" menu entry instead, for Oscillator/Wavetable (Sampler has
    // no poly parameter and no poly menu entry — see TimelinePanelComponent::openAddTrackMenu).
    static void addInstrumentTrack(MainComponent& mc, const juce::String& instrumentModuleType, bool poly = false) {
        int menuId = synth::ui::TimelinePanelComponent::kAddInstrumentSamplerMenuId;
        if (instrumentModuleType == "Oscillator")
            menuId = poly ? synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorPolyMenuId
                          : synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId;
        else if (instrumentModuleType == "Wavetable")
            menuId = poly ? synth::ui::TimelinePanelComponent::kAddInstrumentWavetablePolyMenuId
                          : synth::ui::TimelinePanelComponent::kAddInstrumentWavetableMenuId;
        mc.getTimelinePanel().applyAddTrackMenuChoice(menuId);
    }
};

// ============================================================================
// Real-mouse-gesture helpers (T184/FRO25): a hand-built MouseEvent and the module-component
// lookup they're driven against. dragRealMidiCableBetweenCFT is local to
// ChannelFlowAutoChannelTests.cpp (its only caller).
// ============================================================================

// mouseDownLocalPos is the FIXED press-point (local to eventComp), unchanged across an entire
// gesture; localPos is where the cursor is RIGHT NOW for this specific event. Same shape as
// MacroPortRealMouseDragTests.cpp's own helper — duplicated here since that one is scoped to its
// own translation unit's anonymous namespace.
inline juce::MouseEvent realMouseEventCFT(juce::Component& eventComp, juce::Point<int> localPos,
                                          juce::Point<int> mouseDownLocalPos, juce::ModifierKeys mods,
                                          bool wasDragged = false) {
    const auto pos = localPos.toFloat();
    const auto downPos = mouseDownLocalPos.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &eventComp, &eventComp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(),
                            1, wasDragged);
}

inline ModuleComponent* compForCFT(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

struct McRigCFT {
    LegacyRigCFT rig;
    synth::TrackId trackA, trackB;
};

// The same legacy patch inside a real MainComponent, each Track In bound to its own track.
inline McRigCFT buildMcRigCFT(MainComponent& mc, RigShapeCFT shape) {
    McRigCFT setup;
    auto& graph = mc.getAudioEngine().getGraph();
    auto* output = findNodeNamedCFT(graph, "Audio Output");
    if (output == nullptr)
        return setup;
    // Park Audio Output bottom-left so the canvas's top-right corner stays empty of cards and cables
    // (the canvas right-click test below clicks there).
    output->properties.set("x", 0);
    output->properties.set("y", 900);
    setup.rig = buildLegacyRigCFT(mc.getAudioEngine(), output, shape);
    auto& doc = mc.getTimelineDoc();
    setup.trackA = doc.addTrack(synth::TrackKind::Midi, "Lead");
    setup.trackB = doc.addTrack(synth::TrackKind::Midi, "Pad");
    doc.setTrackBinding(setup.trackA, setup.rig.trackInAUuid);
    doc.setTrackBinding(setup.trackB, setup.rig.trackInBUuid);
    mc.getGraphEditor().updateComponents();
    return setup;
}

inline synth::ui::TimelineTrackHeaderComponent* headerForCFT(MainComponent& mc, synth::TrackId track) {
    auto& panel = mc.getTimelinePanel();
    for (int i = 0; i < panel.getTrackHeaderCount(); ++i)
        if (auto* header = panel.getTrackHeaderAt(i); header != nullptr && header->getTrackId() == track)
            return header;
    return nullptr;
}

// A real right-click on the track header — mouseDown() builds the context menu and hands it to the
// test hook instead of showing it.
inline juce::PopupMenu rightClickHeaderMenuCFT(synth::ui::TimelineTrackHeaderComponent& header) {
    juce::PopupMenu captured;
    header.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> point(4, 4);
    header.mouseDown(
        realMouseEventCFT(header, point, point, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    header.setShowContextMenuHookForTest(nullptr);
    return captured;
}

inline juce::PopupMenu rightClickModuleMenuCFT(ModuleComponent& comp) {
    juce::PopupMenu captured;
    comp.setShowContextMenuHookForTest([&captured](juce::PopupMenu& menu) { captured = menu; });
    const juce::Point<int> body(comp.getWidth() / 2, comp.getHeight() - 10);
    comp.mouseDown(realMouseEventCFT(comp, body, body, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    return captured;
}

// Chooses "Make Channel" from `track`'s header menu (asserting it is offered and enabled).
inline void makeChannelFromHeaderCFT(MainComponent& mc, synth::TrackId track) {
    auto* header = headerForCFT(mc, track);
    ASSERT_NE(header, nullptr);
    const auto menu = rightClickHeaderMenuCFT(*header);
    const auto* item = findMenuItemByTextCFT(menu, "Make Channel");
    ASSERT_NE(item, nullptr) << "the track header menu must offer Make Channel";
    EXPECT_TRUE(item->isEnabled);
    header->applyContextMenuChoice(item->itemID);
}

struct SnapshotCFT {
    juce::String graph, doc, macros;
};

inline SnapshotCFT snapshotCFT(MainComponent& mc) {
    return {juce::JSON::toString(synth::AIStateMapper::graphToJSON(mc.getAudioEngine().getGraph())),
            juce::JSON::toString(mc.getTimelineDoc().toVar()),
            juce::JSON::toString(mc.getGraphEditor().getMacros().toVar())};
}

inline void expectSameSnapshotCFT(const SnapshotCFT& actual, const SnapshotCFT& expected) {
    EXPECT_EQ(actual.graph, expected.graph);
    EXPECT_EQ(actual.doc, expected.doc);
    EXPECT_EQ(actual.macros, expected.macros);
}

// One undo fully reverts the action to `before`; redo restores `after`.
inline void expectOneUndoStepCFT(MainComponent& mc, const SnapshotCFT& before) {
    const auto after = snapshotCFT(mc);
    EXPECT_NE(after.graph, before.graph) << "the action must have changed the graph";
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    ASSERT_TRUE(mc.getUndoManager().redo());
    expectSameSnapshotCFT(snapshotCFT(mc), after);
}
