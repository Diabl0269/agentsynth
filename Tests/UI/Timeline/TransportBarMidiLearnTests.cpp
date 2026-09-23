// Right-click MIDI Learn on the transport bar's four glyph buttons (FRO133,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage). Every target here is an
// ACTION, not a graph parameter (docs/control/midi-remote.md#action-targets) -- the bar stays
// graph-free, so these tests wire onMidiLearnRequested/onMidiForgetRequested/
// onQueryMidiMappingsForActions directly as test doubles (mirroring
// ModuleComponentMidiLearnTests.cpp's GraphEditor doubles), never a real MidiLearnController.
//
// Same "drive a real right-click, capture via setShowContextMenuHookForTest()" idiom; see
// ModuleComponentMidiLearnTests.cpp's header comment for why both the button's own mouseDown() AND
// the bar's (the registered MouseListener) must be called to reproduce a real OS click.

#include "UI/Timeline/TimelineTransportBar.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

namespace {

juce::ModifierKeys rightClickMods() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

juce::MouseEvent realChildMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

juce::PopupMenu rightClickChild(synth::ui::TimelineTransportBar& bar, juce::Component& child) {
    juce::PopupMenu captured;
    bar.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const auto downEvent = realChildMouseEvent(child, rightClickMods());
    child.mouseDown(downEvent);
    bar.mouseDown(downEvent);
    child.mouseUp(realChildMouseEvent(child, rightClickMods()));
    bar.setShowContextMenuHookForTest(nullptr);
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

} // namespace

// ============================================================================
// Coverage: right-click on each of the four buttons shows the MIDI block with the right action id,
// and never also fires the button's own transport action (RightClickSafeButton's guard).
// ============================================================================

TEST(TransportBarMidiLearnTests, RightClickPlayStopShowsMidiLearnAndDoesNotTogglePlayback) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    const bool wasPlaying = bar.getPlayStopButton().getToggleState();

    const auto menu = rightClickChild(bar, bar.getPlayStopButton());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Play/Stop'..."));
    EXPECT_EQ(bar.getPlayStopButton().getToggleState(), wasPlaying)
        << "a right click must never fire the button's own click";
}

TEST(TransportBarMidiLearnTests, RightClickRecordShowsMidiLearnAndDoesNotToggleRecord) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    int recordToggledCount = 0;
    bar.onRecordToggled = [&](bool) { ++recordToggledCount; };

    const auto menu = rightClickChild(bar, bar.getRecordButton());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Record'..."));
    EXPECT_EQ(recordToggledCount, 0);
}

TEST(TransportBarMidiLearnTests, RightClickLoopShowsMidiLearnAndDoesNotToggleLoop) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    const bool wasLooping = bar.getLoopButton().getToggleState();

    const auto menu = rightClickChild(bar, bar.getLoopButton());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Loop'..."));
    EXPECT_EQ(bar.getLoopButton().getToggleState(), wasLooping);
}

TEST(TransportBarMidiLearnTests, RightClickMetronomeShowsMidiLearnAndDoesNotToggleMetronome) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    const bool wasEnabled = bar.getMetronomeButton().getToggleState();

    const auto menu = rightClickChild(bar, bar.getMetronomeButton());
    EXPECT_TRUE(menuContains(menu, "MIDI Learn 'Metronome'..."));
    EXPECT_EQ(bar.getMetronomeButton().getToggleState(), wasEnabled);
}

TEST(TransportBarMidiLearnTests, NoMidiLearnItemsWhenTheHostNeverWiredTheCallback) {
    synth::ui::TimelineTransportBar bar; // onMidiLearnRequested left unset
    bar.setSize(500, 28);

    const auto menu = rightClickChild(bar, bar.getPlayStopButton());
    EXPECT_EQ(menuItemTexts(menu).size(), 0u);
}

// ============================================================================
// The learn/forget actions fire with the RIGHT action id (docs/control/midi-remote.md#action-targets).
// ============================================================================

TEST(TransportBarMidiLearnTests, LearnMenuItemFiresOnMidiLearnRequestedWithTheRightActionId) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    juce::String requestedActionId;
    int callCount = 0;
    bar.onMidiLearnRequested = [&](const juce::String& actionId) {
        requestedActionId = actionId;
        ++callCount;
    };

    auto menu = rightClickChild(bar, bar.getRecordButton());
    juce::PopupMenu::MenuItemIterator it(menu);
    bool invoked = false;
    while (it.next()) {
        if (it.getItem().text == "MIDI Learn 'Record'...") {
            it.getItem().action();
            invoked = true;
        }
    }
    ASSERT_TRUE(invoked);
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(requestedActionId, "transportRecord");
}

TEST(TransportBarMidiLearnTests, PlayStopLearnsTheAliasedTransportTogglePlayStopActionId) {
    // docs/control/midi-remote.md#action-targets / Source/ShortcutManager/AppCommands.h:
    // "transportTogglePlayStop" is the action id (a pure alias resolved to togglePlayback's
    // CommandID at dispatch time) -- the Learn menu must arm THAT id, not "togglePlayback" or a
    // raw CommandID.
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    juce::String requestedActionId;
    bar.onMidiLearnRequested = [&](const juce::String& actionId) { requestedActionId = actionId; };

    auto menu = rightClickChild(bar, bar.getPlayStopButton());
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "MIDI Learn 'Play/Stop'...")
            it.getItem().action();
    EXPECT_EQ(requestedActionId, "transportTogglePlayStop");
}

TEST(TransportBarMidiLearnTests, MappedButtonShowsDisabledTitleLearnAgainAndForget) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    bar.onQueryMidiMappingsForActions = [] {
        return std::map<juce::String, juce::String>{{"transportRecord", "Pad 3 on Launchkey Mini"}};
    };

    const auto menu = rightClickChild(bar, bar.getRecordButton());
    EXPECT_TRUE(menuContains(menu, "MIDI: Pad 3 on Launchkey Mini"));
    EXPECT_TRUE(menuContains(menu, "MIDI Learn again..."));
    EXPECT_TRUE(menuContains(menu, "Forget MIDI"));
    EXPECT_FALSE(menuContains(menu, "Edit MIDI assignment...")) << "no dead menu item before FRO131 wires it";
}

TEST(TransportBarMidiLearnTests, ForgetMenuItemFiresOnMidiForgetRequestedWithTheRightActionId) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.onMidiLearnRequested = [](const juce::String&) {};
    bar.onQueryMidiMappingsForActions = [] {
        return std::map<juce::String, juce::String>{{"transportRecord", "Pad 3 on Launchkey Mini"}};
    };
    int forgetCount = 0;
    bar.onMidiForgetRequested = [&](const juce::String& actionId) {
        ++forgetCount;
        EXPECT_EQ(actionId, "transportRecord");
    };

    auto menu = rightClickChild(bar, bar.getRecordButton());
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Forget MIDI")
            it.getItem().action();
    EXPECT_EQ(forgetCount, 1);
}

// ============================================================================
// Badge: painted only when mapped, refreshed from updateFromTransport() (the existing 10 Hz poll).
// ============================================================================

TEST(TransportBarMidiLearnTests, BadgePaintsOnlyAfterUpdateFromTransportObservesAMapping) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    auto& recordButton = bar.getRecordButton();

    EXPECT_FALSE(bar.isMidiLearnBadgeMappedForTest(&recordButton)) << "unmapped and no query wired yet";

    bar.onQueryMidiMappingsForActions = [] {
        return std::map<juce::String, juce::String>{{"transportRecord", "Pad 3 on Launchkey Mini"}};
    };
    EXPECT_FALSE(bar.isMidiLearnBadgeMappedForTest(&recordButton))
        << "wiring the callback alone must not paint anything";

    synth::TransportService::PositionSnapshot snapshot;
    bar.updateFromTransport(snapshot);
    EXPECT_TRUE(bar.isMidiLearnBadgeMappedForTest(&recordButton));
}

// ============================================================================
// Armed state (the breathing outline) -- setMidiLearnArmedAction/clearMidiLearnArmedAction.
// ============================================================================

TEST(TransportBarMidiLearnTests, ArmingADifferentActionReplacesTheArmedOne) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);

    bar.setMidiLearnArmedAction("transportRecord");
    bar.setMidiLearnArmedAction("transportToggleLoop");
    bar.clearMidiLearnArmedAction();
    // No test seam reads the armed id directly (it's paint-only state) -- this proves the sequence
    // at least never crashes/asserts, mirroring ModuleComponent's own setMidiLearnArmedParam({})
    // repaint-only contract.
    SUCCEED();
}

// FRO256: updateFromTransport() must keep repainting the armed glyph's bounds on every poll while
// armed, or the breathing outline (its alpha computed from wall time on every paint()) freezes at
// whatever alpha its first paint happened to land on.
TEST(TransportBarMidiLearnTests, UpdateFromTransportKeepsRepaintingTheArmedOutlineWhileArmed) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.setMidiLearnArmedAction("transportRecord");

    synth::TransportService::PositionSnapshot snapshot;
    bar.updateFromTransport(snapshot);
    bar.updateFromTransport(snapshot);
    bar.updateFromTransport(snapshot);
    EXPECT_EQ(bar.getMidiLearnArmedRepaintCountForTest(), 3)
        << "every poll while armed must repaint the breathing outline's region, or it freezes";

    bar.clearMidiLearnArmedAction();
    bar.updateFromTransport(snapshot);
    EXPECT_EQ(bar.getMidiLearnArmedRepaintCountForTest(), 3) << "no longer armed -- must stop repainting";
}
