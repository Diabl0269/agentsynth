// SnapToggleButtonTests.cpp -- the guard of the snap-switch kind: one shared button (SnapToggleButton in
// UI/Chrome/ToolbarButton/ToolbarButton.h) is the switch on every surface that has one. The scan fails when a second
// kind of button named "Snap" appears in Source/; the surface tests check each switch is the shared button, is named
// and Tab-reachable, and flips its surface.
#include "../../TestWait.h"
#include "../SourceScan.h"
#include "SnapButtonExpect.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <regex>
#include <type_traits>

using synth::ui::SnapToggleButton;
using synth::ui::test::expectSharedSnapButton;

TEST(SnapToggleButton, NoSourceFileBuildsItsOwnSnapSwitch) {
    // A toggle, text, toolbar or drawable button constructed with a "Snap..." name, outside the shared class.
    const std::regex ownSnapButton(
        R"((ToggleButton|TextButton|ToolbarButton|DrawableButton)(>|\s+\w+)?\s*[{(]\s*"Snap)");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file.rfind("Source/UI/Chrome/ToolbarButton/ToolbarButton.", 0) == 0)
            continue;
        EXPECT_FALSE(std::regex_search(text, ownSnapButton))
            << file << " builds its own Snap button: use synth::ui::SnapToggleButton";
    }
}

TEST(SnapToggleButton, TheTimelineSwitchIsTheSharedButtonAndFlipsTheViewState) {
    static_assert(std::is_same_v<decltype(std::declval<synth::ui::TimelinePanelComponent&>().getSnapToggleButton()),
                                 SnapToggleButton&>);
    synth::ui::TimelinePanelComponent panel;
    auto& button = panel.getSnapToggleButton();
    expectSharedSnapButton(button, "timeline");
    const bool before = panel.getViewState().snapEnabled;
    button.triggerClick();
    synth::test::flushMessageQueue();
    EXPECT_NE(panel.getViewState().snapEnabled, before);
    EXPECT_EQ(button.getToggleState(), panel.getViewState().snapEnabled);
}
