// SnapToggleButtonTests.cpp -- the guard of the snap-switch kind: one shared button (UI/Layout/SnapToggleButton.h)
// is the switch on every surface that has one. The scan fails when a second kind of button named "Snap" appears in
// Source/; the surface tests check each switch is the shared button, is named and Tab-reachable, and flips its surface.
#include "../../TestWait.h"
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../SourceScan.h"
#include "UI/Layout/SnapToggleButton.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <regex>
#include <type_traits>

namespace {

using synth::theme::Icon;
using synth::ui::SnapToggleButton;

void expectSharedSnapButton(SnapToggleButton& button, const char* surface) {
    EXPECT_EQ(button.getIcon(), Icon::ToolbarSnap) << surface;
    EXPECT_FALSE(button.getTitle().isEmpty()) << surface;
    EXPECT_FALSE(button.getTooltip().isEmpty()) << surface;
    EXPECT_NE(button.getTooltip(), button.getTitle()) << surface << ": a tooltip says more than the name";
    EXPECT_TRUE(button.getWantsKeyboardFocus()) << surface;
}

} // namespace

TEST(SnapToggleButton, NoSourceFileBuildsItsOwnSnapSwitch) {
    // A toggle, text, toolbar or drawable button constructed with a "Snap..." name, outside the shared class.
    const std::regex ownSnapButton(
        R"((ToggleButton|TextButton|ToolbarButton|DrawableButton)(>|\s+\w+)?\s*[{(]\s*"Snap)");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file.rfind("Source/UI/Layout/SnapToggleButton.", 0) == 0)
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

TEST(SnapToggleButton, TheShapeEditorSwitchesAreTheSharedButtonAndFlipTheirAxis) {
    static_assert(
        std::is_same_v<decltype(std::declval<synth::ui::PitchGridRows&>().getSnapAcross()), SnapToggleButton&>);
    static_assert(
        std::is_same_v<decltype(std::declval<synth::ui::PitchGridRows&>().getSnapLevel()), SnapToggleButton&>);
    synth::ui::test::ShapeRig rig;
    synth::ui::ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    synth::ui::pitchgrid::Choice choice;
    choice.kind = synth::ui::pitchgrid::Kind::notes;
    choice.levelDb = 6.0f; // a Snap button is disabled while its axis is Off
    rows.setChoice(choice);
    for (auto* button : {&rows.getSnapAcross(), &rows.getSnapLevel()}) {
        expectSharedSnapButton(*button, button == &rows.getSnapAcross() ? "snap across" : "snap level");
        const bool before = button->getToggleState();
        button->triggerClick();
        synth::test::flushMessageQueue();
        EXPECT_NE(button->getToggleState(), before) << button->getTitle();
    }
}
