// WindowSnapKeyTests.cpp -- the Snap key (J, the app's "timelineSnapToggle") in the windows that have a Snap button of
// their own: the curve window (its test is with the curve window rigs, CurveEditorSnapLineTests.cpp) and the Filter
// shape editor. J flips Snap, lights the button, moves the grid and says
// "Snap on" / "Snap off" (or why nothing can snap); it never reaches the app's relay, so a pop-out never flips the
// timeline's Snap; a rebound key is the one the tooltip names. The scan fails when a surface owns a SnapToggleButton
// and does not handle the key.
#include "../../TestSettingsHelpers.h"
#include "../../TestWait.h"
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "../SourceScan.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include <set>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

const juce::KeyPress kJ('j', juce::ModifierKeys::noModifiers, 0);

struct FilterGridKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{shapewindowprefs::gridKey("filter"), shapewindowprefs::gridKindKey("filter"),
                          shapewindowprefs::gridStepsKey("filter"), shapewindowprefs::gridLevelKey("filter"),
                          shapewindowprefs::snapAcrossKey("filter"), shapewindowprefs::snapLevelKey("filter"),
                          shapewindowprefs::gridOwnKeyKey("filter"), shapewindowprefs::gridRootKey("filter")}};
};

} // namespace

TEST(WindowSnapKey, JFlipsBothSnapButtonsOfTheFilterShapeEditorAndSaysWhyWhenThereIsNoGrid) {
    FilterGridKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setSize(720, 460);
    int relayed = 0;
    editor.onAppShortcut = [&](const juce::KeyPress&) {
        ++relayed;
        return false;
    };
    auto* rows = editor.getPitchRows();
    ASSERT_NE(rows, nullptr);

    // Grid Off and Level Off: there is nothing to snap to, and the key says so.
    rows->getKindSwitch().setSelectedIndex((int)pitchgrid::Kind::off, juce::sendNotificationSync);
    rows->getLevelSwitch().setSelectedIndex(0, juce::sendNotificationSync);
    EXPECT_TRUE(editor.keyPressed(kJ));
    EXPECT_TRUE(editor.getStatusText().containsIgnoreCase("grid")) << editor.getStatusText();

    rows->getKindSwitch().setSelectedIndex((int)pitchgrid::Kind::octaves, juce::sendNotificationSync);
    rows->getLevelSwitch().setSelectedIndex(1, juce::sendNotificationSync);
    ASSERT_TRUE(rows->getSnapAcross().isEnabled());
    ASSERT_TRUE(rows->getSnapLevel().isEnabled());
    const bool before = rows->getSnapAcross().getToggleState() || rows->getSnapLevel().getToggleState();
    EXPECT_TRUE(editor.keyPressed(kJ));
    EXPECT_EQ(rows->getSnapAcross().getToggleState(), !before);
    EXPECT_EQ(rows->getSnapLevel().getToggleState(), !before);
    EXPECT_EQ(editor.getStatusText(), WindowSnapKey::message(!before));
    EXPECT_TRUE(editor.keyPressed(kJ));
    EXPECT_EQ(rows->getSnapAcross().getToggleState(), before);
    EXPECT_EQ(editor.getStatusText(), WindowSnapKey::message(before));
    EXPECT_EQ(relayed, 0);
    EXPECT_TRUE(rows->getSnapAcross().getTooltip().containsIgnoreCase("(j)")) << rows->getSnapAcross().getTooltip();
}

TEST(WindowSnapKey, AnUnboundTableLeavesTheDefaultAndTheMessagesAreFixed) {
    WindowSnapKey key;
    EXPECT_TRUE(key.matches(kJ));
    EXPECT_FALSE(key.matches(juce::KeyPress('k', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_EQ(WindowSnapKey::message(true), "Snap on");
    EXPECT_EQ(WindowSnapKey::message(false), "Snap off");
}

TEST(WindowSnapKey, EverySourceDirectoryThatOwnsASnapToggleButtonHandlesTheSnapKey) {
    // A button is declared in a header and wired in its owner's other files, so the key is looked for in the directory.
    const auto directoryOf = [](const std::string& file) { return file.substr(0, file.find_last_of('/')); };
    std::set<std::string> owners;
    std::set<std::string> handling;
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file.rfind("Source/UI/Chrome/ToolbarButton/ToolbarButton.", 0) == 0)
            continue;
        if (text.find("SnapToggleButton") != std::string::npos)
            owners.insert(directoryOf(file));
        if (text.find("timelineSnapToggle") != std::string::npos || text.find("WindowSnapKey") != std::string::npos)
            handling.insert(directoryOf(file));
    }
    for (const auto& directory : owners)
        EXPECT_TRUE(handling.count(directory) > 0)
            << directory
            << " has a SnapToggleButton but never handles the Snap key (timelineSnapToggle): use WindowSnapKey";
    EXPECT_GE(owners.size(), 3u);
}
