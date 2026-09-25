// Right-click MIDI Learn on Master's fader (FRO133,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage). Master has exactly one
// learnable control (its "gain" param -- no pan, no send list, and Mute is out of scope here, same
// as the mixer column's own row), so this mirrors MixerColumnMidiLearnTests.cpp's idiom on a much
// smaller surface. Direct is not covered at all: MixerDirectColumn has no fader/pan/M-S of its own
// (just "Make channel"), so there is nothing to register.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerMasterColumn.h"

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

juce::PopupMenu rightClickChild(synth::ui::MixerMasterColumn& column, juce::Component& child) {
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

struct MasterColumnFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::ui::MixerMasterColumn column;
    synth::MacroSet macros;
    juce::AudioProcessorGraph::Node::Ptr masterNode;

    MasterColumnFixture() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);

        masterNode = graph.addNode(std::make_unique<MasterModule>());
        column.configure(graph, undoManager, macros, editor);
        column.setSize(140, 300);
        column.setNodeId(masterNode->nodeID);
    }
};

} // namespace

TEST(MixerMasterColumnMidiLearnTests, RightClickFaderShowsUnmappedMidiLearnItem) {
    MasterColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};

    const auto menu = rightClickChild(fixture.column, fixture.column.getAccessibilityFocusTargetForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Gain'..."));
}

TEST(MixerMasterColumnMidiLearnTests, NoMidiLearnItemsWhenTheHostNeverWiredTheCallback) {
    MasterColumnFixture fixture;

    const auto menu = rightClickChild(fixture.column, fixture.column.getAccessibilityFocusTargetForTest());
    EXPECT_EQ(menuItemTexts(menu).size(), 0u);
}

TEST(MixerMasterColumnMidiLearnTests, LearnMenuItemFiresOnMidiLearnRequestedWithMastersNodeAndGain) {
    MasterColumnFixture fixture;
    juce::AudioProcessorGraph::NodeID requestedNode;
    juce::String requestedParamId;
    int callCount = 0;
    fixture.editor.onMidiLearnRequested = [&](juce::AudioProcessorGraph::NodeID n, const juce::String& p) {
        requestedNode = n;
        requestedParamId = p;
        ++callCount;
    };

    auto menu = rightClickChild(fixture.column, fixture.column.getAccessibilityFocusTargetForTest());
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
    EXPECT_EQ(requestedNode, fixture.masterNode->nodeID);
    EXPECT_EQ(requestedParamId, "gain");
}

TEST(MixerMasterColumnMidiLearnTests, MappedFaderShowsDisabledTitleLearnAgainAndForget) {
    MasterColumnFixture fixture;
    fixture.editor.onMidiLearnRequested = [](juce::AudioProcessorGraph::NodeID, const juce::String&) {};
    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };

    const auto menu = rightClickChild(fixture.column, fixture.column.getAccessibilityFocusTargetForTest());
    EXPECT_TRUE(menuContains(menu, "MIDI: Fader 1 on Launchkey Mini"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn again..."));
    EXPECT_TRUE(menuContains(menu, "Forget MIDI"));
}

TEST(MixerMasterColumnMidiLearnTests, ForgetMenuItemFiresOnMidiForgetRequested) {
    MasterColumnFixture fixture;
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

    auto menu = rightClickChild(fixture.column, fixture.column.getAccessibilityFocusTargetForTest());
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Forget MIDI")
            it.getItem().action();
    EXPECT_EQ(forgetCount, 1);
}

TEST(MixerMasterColumnMidiLearnTests, BadgePaintsOnlyAfterRefreshMeterObservesAMapping) {
    MasterColumnFixture fixture;
    auto& slider = fixture.column.getAccessibilityFocusTargetForTest();

    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&slider));

    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };
    EXPECT_FALSE(fixture.column.isMidiLearnBadgeMappedForTest(&slider))
        << "wiring the callback alone must not paint anything";

    fixture.column.refreshMeter(0.1f);
    EXPECT_TRUE(fixture.column.isMidiLearnBadgeMappedForTest(&slider));
}

TEST(MixerMasterColumnMidiLearnTests, UnbindFromGraphClearsTheRegistrySoARightClickFindsNothing) {
    MasterColumnFixture fixture;
    auto& slider = fixture.column.getAccessibilityFocusTargetForTest();
    ASSERT_NE(fixture.column.findMidiLearnableParamForTest(&slider), nullptr);

    fixture.column.unbindFromGraph();
    EXPECT_EQ(fixture.column.findMidiLearnableParamForTest(&slider), nullptr);
}

// ============================================================================
// FRO256: same coordinate-frame bug as MixerColumnMidiLearnTests.cpp's own "not the column's
// corner" tests -- paintMidiLearnOverlays() here passed the slider's PARENT (fader_, itself nested
// inside this column) as the getLocalArea() source but the slider's OWN local bounds as the area,
// so the badge landed at fader_'s origin translated into this column's frame, not on the slider.
// ============================================================================

TEST(MixerMasterColumnMidiLearnTests, BadgePaintsOnTheFaderItselfNotAtAWrongOffset) {
    MasterColumnFixture fixture;
    fixture.editor.onQueryMidiMappingsForNode =
        [](juce::AudioProcessorGraph::NodeID) -> std::map<juce::String, juce::String> {
        return {{"gain", "Fader 1 on Launchkey Mini"}};
    };
    fixture.column.refreshMeter(0.1f);

    auto& slider = fixture.column.getAccessibilityFocusTargetForTest();
    const auto sliderBoundsInColumn =
        fixture.column.getLocalArea(&slider, slider.getLocalBounds()); // the CORRECT conversion
    const auto image = fixture.column.createComponentSnapshot(fixture.column.getLocalBounds());

    const juce::Point<int> badgeCentre(sliderBoundsInColumn.getRight() - 3, sliderBoundsInColumn.getY() + 3);
    const juce::Colour target(0xffB48EF5u);
    bool found = false;
    for (int dx = -2; dx <= 2 && !found; ++dx)
        for (int dy = -2; dy <= 2 && !found; ++dy) {
            const auto p = badgeCentre + juce::Point<int>(dx, dy);
            if (image.getBounds().contains(p) && image.getPixelAt(p.x, p.y) == target)
                found = true;
        }
    EXPECT_TRUE(found) << "badge should be at the fader's own top-right corner, (" << badgeCentre.x << ", "
                       << badgeCentre.y << ")";
}

// ============================================================================
// FRO256: the armed breathing outline must keep repainting while armed, same fix as
// MixerColumnComponent -- refreshMeter() had no such repaint at all before this.
// ============================================================================

TEST(MixerMasterColumnMidiLearnTests, RefreshMeterKeepsRepaintingTheArmedOutlineWhileArmed) {
    MasterColumnFixture fixture;
    fixture.column.setMidiLearnArmedParam("gain");

    fixture.column.refreshMeter(0.1f);
    fixture.column.refreshMeter(0.1f);
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 3);

    fixture.column.setMidiLearnArmedParam({});
    fixture.column.refreshMeter(0.1f);
    EXPECT_EQ(fixture.column.getMidiLearnArmedRepaintCountForTest(), 3) << "no longer armed -- must stop repainting";
}
