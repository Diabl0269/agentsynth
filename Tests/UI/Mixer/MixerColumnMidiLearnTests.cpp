// Right-click MIDI Learn on a mixer column's controls (FRO133,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage). Same "drive a real right-click,
// capture via setShowContextMenuHookForTest()" idiom as
// Tests/UI/Graph/ModuleComponent/ModuleComponentMidiLearnTests.cpp -- see that file's header
// comment for why a direct mouseDown() call on both the child AND the column reproduces the real
// addMouseListener() dispatch a right-click delivers.
//
// Builds a minimal real graph (AudioEngine + AppUndoManager + GraphEditor, no MainComponent) the
// same way MixerColumnComponentTests.cpp does -- a mixer column's own right-click menu only needs
// GraphEditor's onMidiLearnRequested/onMidiForgetRequested/onQueryMidiMappingsForNode callbacks
// (manually wired here as test doubles, exactly like ModuleComponentMidiLearnTests.cpp does),
// never a real MidiLearnController/RemoteEngine.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

namespace {

juce::ModifierKeys rightClickMods() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

juce::MouseEvent realChildMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// See ModuleComponentMidiLearnTests.cpp's rightClickChild for why both calls are needed.
juce::PopupMenu rightClickChild(synth::ui::MixerColumnComponent& column, juce::Component& child) {
    juce::PopupMenu captured;
    column.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto downEvent = realChildMouseEvent(child, rightClickMods());
    child.mouseDown(downEvent);
    column.mouseDown(downEvent);
    child.mouseUp(realChildMouseEvent(child, rightClickMods()));
    column.setShowContextMenuHookForTest(nullptr);
    return captured;
}

std::vector<juce::String> menuItemTexts(const juce::PopupMenu& menu) {
    std::vector<juce::String> texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.text.isNotEmpty())
            texts.push_back(item.text);
    }
    return texts;
}

bool menuContains(const juce::PopupMenu& menu, const juce::String& text) {
    const auto texts = menuItemTexts(menu);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
}

/** A real ChannelStripModule node (via buildBusChannel, same helper
 *  MixerColumnComponentTests.cpp uses) plus a MixerColumnComponent bound to it -- `withSend` also
 *  activates send slot 0 (kMaxSends level params are always added, active or not -- see
 *  ChannelStripModule.h's own comment -- so a plain synth::MixerColumn value with one
 *  MixerSendEntry is enough; no real "add send" Core flow needed for this). */
struct ColumnFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::ui::MixerColumnComponent column;
    ChannelStripModule* strip = nullptr;

    explicit ColumnFixture(bool withSend = false) {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);

        const synth::DefaultChannelLayout layout{{0, 0}, {100, 0}, {200, 0}, {300, 0}};
        const auto channel = synth::buildBusChannel(graph, layout);
        strip = dynamic_cast<ChannelStripModule*>(channel.strip->getProcessor());

        column.configure(graph, undoManager, editor.getMacros(), editor, engine);
        column.setSize(140, 300);

        synth::MixerColumn model;
        model.nodeId = channel.strip->nodeID;
        model.name = "Test Strip";
        if (withSend) {
            synth::MixerSendEntry send;
            send.slot = 0;
            send.targetNodeId = channel.strip->nodeID; // dummy target -- rebuildKnobs() never resolves it
            send.targetName = "Bus";
            model.sends.push_back(send);
        }
        column.setColumn(model, "");
    }
};

} // namespace

// ============================================================================
// Coverage: right-click shows the MIDI block for fader/pan/mute/send.
// ============================================================================

TEST(MixerColumnMidiLearnTests, RightClickFaderShowsUnmappedMidiLearnItem) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    const auto menu = rightClickChild(fixture.column, fixture.column.getFaderForTest().getSlider());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Gain'..."));
}

TEST(MixerColumnMidiLearnTests, RightClickPanShowsUnmappedMidiLearnItem) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    const auto menu = rightClickChild(fixture.column, fixture.column.getPanSliderForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Pan'..."));
}

TEST(MixerColumnMidiLearnTests, RightClickMuteShowsMidiLearnAndDoesNotToggleMute) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    const bool before = fixture.strip->isMuted();

    const auto menu = rightClickChild(fixture.column, fixture.column.getMuteButtonForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Muted'..."));
    EXPECT_EQ(fixture.strip->isMuted(), before) << "a right click must never fire Mute's own click";
}

TEST(MixerColumnMidiLearnTests, RightClickSendKnobShowsMidiLearnForTheRightSendParam) {
    ColumnFixture fixture(/*withSend=*/true);
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    auto* knob = fixture.column.getSendListForTest().getKnobForTest(0);
    ASSERT_NE(knob, nullptr);

    const auto menu = rightClickChild(fixture.column, *knob);
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Send 1 Level'..."));
    EXPECT_EQ(fixture.column.findMidiLearnableParamForTest(knob), fixture.strip->getSendLevelParameter(0));
}

TEST(MixerColumnMidiLearnTests, NoMidiLearnItemsWhenTheHostNeverWiredTheCallback) {
    ColumnFixture fixture; // editor.onMidiLearnRequested left unset

    const auto menu = rightClickChild(fixture.column, fixture.column.getFaderForTest().getSlider());
    EXPECT_EQ(menuItemTexts(menu).size(), 0u);
}

// ============================================================================
// The learn/forget actions fire through GraphEditor's shared callbacks with the RIGHT (nodeId, paramId).
// ============================================================================

TEST(MixerColumnMidiLearnTests, LearnMenuItemFiresOnMidiLearnRequestedWithTheColumnsNodeAndParamId) {
    ColumnFixture fixture;
    juce::AudioProcessorGraph::NodeID requestedNode;
    juce::String requestedParamId;
    int callCount = 0;
    fixture.editor.onMidiLearnRequested = [&](juce::AudioProcessorGraph::NodeID n, const juce::String& p) {
        requestedNode = n;
        requestedParamId = p;
        ++callCount;
    };

    auto menu = rightClickChild(fixture.column, fixture.column.getFaderForTest().getSlider());
    juce::PopupMenu::MenuItemIterator it(menu);
    bool invoked = false;
    while (it.next()) {
        if (it.getItem().text == "MIDI Learn 'Gain'...") {
            it.getItem().action();
            invoked = true;
        }
    }
    ASSERT_TRUE(invoked);
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(requestedNode, fixture.column.getNodeId());
    EXPECT_EQ(requestedParamId, "gain");
}

TEST(MixerColumnMidiLearnTests, MappedFaderShowsDisabledTitleLearnAgainAndForget) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };

    const auto menu = rightClickChild(fixture.column, fixture.column.getFaderForTest().getSlider());
    EXPECT_TRUE(menuContains(menu, "MIDI: Fader 1 on Launchkey Mini"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn again..."));
    EXPECT_TRUE(menuContains(menu, "Forget MIDI"));
    EXPECT_FALSE(menuContains(menu, "Edit MIDI assignment...")) << "no dead menu item before FRO131 wires it";
}

TEST(MixerColumnMidiLearnTests, ForgetMenuItemFiresOnMidiForgetRequested) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };
    int forgetCount = 0;
    fixture.editor.onMidiForgetRequested = [&](juce::AudioProcessorGraph::NodeID, const juce::String& p) {
        ++forgetCount;
        EXPECT_EQ(p, "gain");
    };

    auto menu = rightClickChild(fixture.column, fixture.column.getFaderForTest().getSlider());
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Forget MIDI")
            it.getItem().action();
    EXPECT_EQ(forgetCount, 1);
}

// ============================================================================
// Badge: painted only when mapped, refreshed from refreshMeter() (the existing 10 Hz poll).
// ============================================================================

TEST(MixerColumnMidiLearnTests, BadgePaintsOnlyAfterRefreshMeterObservesAMapping) {
    ColumnFixture fixture;
    auto& slider = fixture.column.getFaderForTest().getSlider();

    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&slider)) << "unmapped and no query wired yet";

    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };
    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&slider))
        << "wiring the callback alone must not paint anything";

    fixture.column.refreshMeter(0.1f);
    EXPECT_TRUE(fixture.column.isMidiLearnBadgeMappedForTest(&slider));
}

// ============================================================================
// Unbind: FRO11's crash-fix invariant, extended to the MIDI-learn registry (Source/UI/CLAUDE.md).
// ============================================================================

TEST(MixerColumnMidiLearnTests, UnbindFromGraphClearsTheRegistrySoARightClickFindsNothing) {
    ColumnFixture fixture;
    auto& slider = fixture.column.getFaderForTest().getSlider();
    ASSERT_NE(fixture.column.findMidiLearnableParamForTest(&slider), nullptr);

    fixture.column.unbindFromGraph();
    EXPECT_EQ(fixture.column.findMidiLearnableParamForTest(&slider), nullptr);
}

// ============================================================================
// FRO253: Solo is a node command, not a parameter -- right-click opens ITS OWN "MIDI Learn
// 'Solo'..." menu (through onSoloMidiLearnRequested/onSoloMidiForgetRequested/
// onQuerySoloMidiMapping rather than GraphEditor's onMidiLearnRequested), never toggles solo, and
// a left click still does.
// ============================================================================

TEST(MixerColumnMidiLearnTests, RightClickSoloShowsMidiLearnAndDoesNotToggleSolo) {
    ColumnFixture fixture;
    fixture.column.onSoloMidiLearnRequested = [] {};
    const bool before = fixture.strip->isSoloed();

    const auto menu = rightClickChild(fixture.column, fixture.column.getSoloButtonForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Solo'..."));
    EXPECT_EQ(fixture.strip->isSoloed(), before) << "a right click must never fire Solo's own click";
}

TEST(MixerColumnMidiLearnTests, LeftClickSoloStillTogglesSolo) {
    ColumnFixture fixture;
    const bool before = fixture.strip->isSoloed();

    // Not a synthesized mouseDown/mouseUp pair: juce::Button::mouseUp()'s click completion gates
    // on Component::isMouseOver(), which reads the REAL desktop cursor position, not the position
    // carried by a synthetic juce::MouseEvent -- headless, that is never inside the button, so no
    // test in this codebase drives a real Button click that way (juce::Button::triggerClick()
    // fails the same way for a different reason: it posts via a juce::Timer this headless suite
    // never pumps). Firing onClick() directly is the established substitute -- see
    // MixerAccessibilityTests.cpp's own "MuteSoloButtonsMirrorToggleState", which calls
    // column->toggleSoloed() directly for the same reason.
    fixture.column.getSoloButtonForTest().onClick();

    EXPECT_NE(fixture.strip->isSoloed(), before) << "left-click must still toggle solo through the right-click guard";
}

TEST(MixerColumnMidiLearnTests, NoSoloMidiLearnItemWhenTheHostNeverWiredTheCallback) {
    ColumnFixture fixture; // onSoloMidiLearnRequested left unset

    const auto menu = rightClickChild(fixture.column, fixture.column.getSoloButtonForTest());
    EXPECT_EQ(menuItemTexts(menu).size(), 0u);
}

TEST(MixerColumnMidiLearnTests, SoloLearnMenuItemFiresOnSoloMidiLearnRequested) {
    ColumnFixture fixture;
    int callCount = 0;
    fixture.column.onSoloMidiLearnRequested = [&] { ++callCount; };

    auto menu = rightClickChild(fixture.column, fixture.column.getSoloButtonForTest());
    juce::PopupMenu::MenuItemIterator it(menu);
    bool invoked = false;
    while (it.next()) {
        if (it.getItem().text == "MIDI Learn 'Solo'...") {
            it.getItem().action();
            invoked = true;
        }
    }
    ASSERT_TRUE(invoked);
    EXPECT_EQ(callCount, 1);
}

TEST(MixerColumnMidiLearnTests, MappedSoloShowsDisabledTitleLearnAgainAndForget) {
    ColumnFixture fixture;
    fixture.column.onSoloMidiLearnRequested = [] {};
    fixture.column.onQuerySoloMidiMapping = [] { return juce::String("Pad 1 on Launchkey Mini"); };

    const auto menu = rightClickChild(fixture.column, fixture.column.getSoloButtonForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI: Pad 1 on Launchkey Mini"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn again..."));
    EXPECT_TRUE(menuContains(menu, "Forget MIDI"));
}

TEST(MixerColumnMidiLearnTests, SoloForgetMenuItemFiresOnSoloMidiForgetRequested) {
    ColumnFixture fixture;
    fixture.column.onSoloMidiLearnRequested = [] {};
    fixture.column.onQuerySoloMidiMapping = [] { return juce::String("Pad 1 on Launchkey Mini"); };
    int forgetCount = 0;
    fixture.column.onSoloMidiForgetRequested = [&] { ++forgetCount; };

    auto menu = rightClickChild(fixture.column, fixture.column.getSoloButtonForTest());
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Forget MIDI")
            it.getItem().action();
    EXPECT_EQ(forgetCount, 1);
}

TEST(MixerColumnMidiLearnTests, SoloBadgePaintsOnlyAfterRefreshMeterObservesAMapping) {
    ColumnFixture fixture;
    auto& soloButton = fixture.column.getSoloButtonForTest();

    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&soloButton)) << "unmapped and no query wired yet";

    fixture.column.onQuerySoloMidiMapping = [] { return juce::String("Pad 1 on Launchkey Mini"); };
    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&soloButton))
        << "wiring the callback alone must not paint anything";

    fixture.column.refreshMeter(0.1f);
    EXPECT_TRUE(fixture.column.isMidiLearnBadgeMappedForTest(&soloButton));
}

// ============================================================================
// FRO256: the badge/armed outline must paint AT the control's own on-screen position, not at this
// column's top-left corner. paintMidiLearnOverlays() used to call
// getLocalArea(control->getParentComponent(), control->getLocalBounds()) -- passing the control's
// PARENT as the coordinate-frame source but the control's OWN local bounds (always (0,0,w,h)) as
// the area mixes two different frames and, since the source equals `this` for a direct child,
// silently collapses to "the control's own size, positioned at this column's origin" -- nowhere
// near the M/S row these controls actually sit in. isMidiLearnBadgeMappedForTest() (used by every
// other badge test in this file) only reads the cached bool, so it can't see this: rendering to a
// real image is the only way to catch it.
// ============================================================================

namespace {
// The lavender fallback synth::ui::midilearn::paintMidiMappedBadge/paintMidiLearnArmedOutline use
// when no AppLookAndFeel is installed (MidiLearnMenu.cpp's own fallback constants) -- exactly what
// a column built with the stock default juce::LookAndFeel (as every fixture here is) resolves to.
constexpr juce::uint32 kBadgeColourArgb = 0xffB48EF5;
constexpr juce::uint32 kArmedColourArgb = 0xff00D1FF;

bool imageHasColourNear(const juce::Image& image, juce::Point<int> centre, juce::uint32 argb, int searchRadius = 2) {
    const juce::Colour target(argb);
    for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
            const auto p = centre + juce::Point<int>(dx, dy);
            if (!image.getBounds().contains(p))
                continue;
            if (image.getPixelAt(p.x, p.y) == target)
                return true;
        }
    return false;
}
} // namespace

TEST(MixerColumnMidiLearnTests, SoloBadgePaintsOnTheSoloButtonItselfNotTheColumnsCorner) {
    ColumnFixture fixture;
    fixture.column.onQuerySoloMidiMapping = [] { return juce::String("Pad 1 on Launchkey Mini"); };
    fixture.column.refreshMeter(0.1f);

    const auto soloBounds = fixture.column.getSoloButtonForTest().getBounds(); // relative to the column itself
    const auto image = fixture.column.createComponentSnapshot(fixture.column.getLocalBounds());

    const juce::Point<int> badgeCentre(soloBounds.getRight() - 3, soloBounds.getY() + 3);
    EXPECT_TRUE(imageHasColourNear(image, badgeCentre, kBadgeColourArgb))
        << "badge should be at the solo button's own top-right corner, (" << badgeCentre.x << ", " << badgeCentre.y
        << ")";

    // The pre-fix code painted a badge sized to the solo button but positioned at the COLUMN's own
    // (0, 0) origin instead -- assert that corner is clean so a regression can't slip back in by
    // accident (e.g. reintroducing the bug for a DIFFERENT control that happens to paint over it).
    EXPECT_FALSE(imageHasColourNear(image, {3, 3}, kBadgeColourArgb, /*searchRadius=*/0))
        << "no badge should ever land at the column's own top-left corner";
}

TEST(MixerColumnMidiLearnTests, MuteBadgePaintsOnTheMuteButtonItselfNotTheColumnsCorner) {
    ColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"muted", "Pad 2 on Launchkey Mini"}};
    };
    fixture.column.refreshMeter(0.1f);

    const auto muteBounds = fixture.column.getMuteButtonForTest().getBounds();
    const auto image = fixture.column.createComponentSnapshot(fixture.column.getLocalBounds());

    const juce::Point<int> badgeCentre(muteBounds.getRight() - 3, muteBounds.getY() + 3);
    EXPECT_TRUE(imageHasColourNear(image, badgeCentre, kBadgeColourArgb))
        << "badge should be at the mute button's own top-right corner, (" << badgeCentre.x << ", " << badgeCentre.y
        << ")";
}

// ============================================================================
// FRO256: the armed breathing outline must actually keep repainting while armed -- it froze at its
// first-paint alpha (0.4, the dimmest point of the breath cycle) because nothing asked for a
// repaint after the initial arm edge. refreshMeter() rides the same 10 Hz tick every other
// per-column visual uses (Source/UI/CLAUDE.md's no-unconditional-repaint rule: only while
// something is actually armed).
// ============================================================================

TEST(MixerColumnMidiLearnTests, RefreshMeterKeepsRepaintingTheArmedSoloOutlineWhileArmed) {
    ColumnFixture fixture;
    fixture.column.setMidiLearnArmedSolo(true);
    ASSERT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 0)
        << "arming itself repaints once via setMidiLearnArmedSolo, not this counter";

    fixture.column.refreshMeter(0.1f);
    fixture.column.refreshMeter(0.1f);
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 3)
        << "every tick while armed must repaint the breathing outline's region, or it freezes";

    fixture.column.setMidiLearnArmedSolo(false);
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 3) << "no longer armed -- must stop repainting";
}

TEST(MixerColumnMidiLearnTests, RefreshMeterKeepsRepaintingTheArmedFaderOutlineWhileArmed) {
    ColumnFixture fixture;
    fixture.column.setMidiLearnArmedParam("gain");

    fixture.column.refreshMeter(0.1f);
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 2);

    fixture.column.setMidiLearnArmedParam({});
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 2);
}
