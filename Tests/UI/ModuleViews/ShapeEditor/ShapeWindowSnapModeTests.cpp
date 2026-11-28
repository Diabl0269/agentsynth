// ShapeWindowSnapModeTests.cpp -- the Snap mode picker in the Filter shape window: what each mode lets a dragged
// point do through the real mouse path, J keeping the mode, the mode remembered across windows, and the picker's place
// in the Tab order.
#include "../../../TestSettingsHelpers.h"
#include "../../../TestWait.h"
#include "../../Accessibility/TabOrderHelpers.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

struct SnapModeKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{shapewindowprefs::gridKey("filter"), shapewindowprefs::gridKindKey("filter"),
                          shapewindowprefs::gridStepsKey("filter"), shapewindowprefs::gridLevelKey("filter"),
                          shapewindowprefs::snapKey("filter"), shapewindowprefs::snapModeKey("filter"),
                          shapewindowprefs::gridOwnKeyKey("filter"), shapewindowprefs::gridRootKey("filter")}};
};

pitchgrid::Choice octavesWithLevels(SnapMode mode) {
    pitchgrid::Choice choice;
    choice.kind = pitchgrid::Kind::octaves;
    choice.levelDb = 6.0f;
    choice.snapMode = mode;
    return choice;
}

void dragNode(CurveEditorComponent& curve, int index, double x, float level) {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    const CurveEditorGeometry geometry(curve.getModel(), curve.plotBounds(), config);
    const auto from = geometry.nodePosition(index);
    const juce::Point<float> to(geometry.xForTime(x), geometry.yForLevel(level));
    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDragPass(curve, to, from));
    curve.mouseUp(curveLeftDrag(curve, to, from));
}

bool nodeAt(const CurveEditorComponent& curve, double x, float* levelOut = nullptr) {
    for (int i = 0; i < curve.getModel().getNumNodes(); ++i)
        if (std::abs(curve.getModel().getNode(i).x - x) < 1e-4) {
            if (levelOut != nullptr)
                *levelOut = curve.getModel().getNode(i).y;
            return true;
        }
    return false;
}

bool onLevelLine(float y) {
    const double cells = (CustomFilterShapes::kMaxDb - CustomFilterShapes::kMinDb) / 6.0;
    return std::abs(y * cells - std::round(y * cells)) < 1e-3;
}

} // namespace

TEST(ShapeWindowSnapMode, TimeOnlySnapsAcrossAndLeavesTheLevelWhereItWasReleased) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(octavesWithLevels(SnapMode::TimeAndLevel));
    rows.getSnapMode().choose(SnapMode::TimeOnly);
    ASSERT_FALSE(rows.grid()->snapY);
    const float released = CustomFilterShapes::dbToY(-4.0f);
    ASSERT_FALSE(onLevelLine(released));
    dragNode(editor.getCurveEditor(), 3, CustomFilterShapes::hzToX(2060.0f), released);
    float level = 0.0f;
    ASSERT_TRUE(nodeAt(editor.getCurveEditor(), CustomFilterShapes::hzToX(2000.0f), &level))
        << "x lands on the 2 kHz line";
    EXPECT_NEAR(level, released, 1e-4) << "the level is free";
}

TEST(ShapeWindowSnapMode, LevelOnlySnapsUpAndDownAndLeavesTheFrequencyFree) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(octavesWithLevels(SnapMode::LevelOnly));
    ASSERT_FALSE(rows.grid()->snapX);
    const double x = CustomFilterShapes::hzToX(2060.0f);
    dragNode(editor.getCurveEditor(), 3, x, CustomFilterShapes::dbToY(-4.0f));
    float level = 0.0f;
    ASSERT_TRUE(nodeAt(editor.getCurveEditor(), x, &level)) << "x stays where it was released";
    EXPECT_TRUE(onLevelLine(level)) << "the level lands on a line";
}

TEST(ShapeWindowSnapMode, ThePickerMenuHasThreeModesAndChoosingOneDrivesTheGrid) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(octavesWithLevels(SnapMode::TimeAndLevel));
    CapturedMenu menu;
    rows.getSnapMode().showMenuHook = [&menu](juce::PopupMenu& m) { menu.capture(m); };
    rows.getSnapMode().showMenu();
    ASSERT_EQ(menu.items.size(), 3u);
    EXPECT_EQ(menu.items[1], "Time only");
    rows.getSnapMode().choose(SnapMode::TimeOnly);
    EXPECT_EQ(rows.getChoice().snapMode, SnapMode::TimeOnly);
    EXPECT_TRUE(rows.grid()->snapX);
    EXPECT_FALSE(rows.grid()->snapY);
}

TEST(ShapeWindowSnapMode, JTurnsSnapOffAndOnAndKeepsTheMode) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(octavesWithLevels(SnapMode::LevelOnly));
    const juce::KeyPress j('j', juce::ModifierKeys::noModifiers, 0);
    EXPECT_TRUE(editor.keyPressed(j));
    EXPECT_FALSE(rows.getSnap().getToggleState());
    EXPECT_EQ(rows.getSnapMode().getMode(), SnapMode::LevelOnly);
    EXPECT_TRUE(editor.keyPressed(j));
    EXPECT_TRUE(rows.getSnap().getToggleState());
    EXPECT_EQ(rows.getSnapMode().getMode(), SnapMode::LevelOnly);
    EXPECT_TRUE(rows.getSnapMode().getTooltip().containsIgnoreCase("(j)")) << rows.getSnapMode().getTooltip();
}

TEST(ShapeWindowSnapMode, ThePickerFollowsTheSnapButtonsAvailability) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& rows = *editor.getPitchRows();
    pitchgrid::Choice none;
    none.kind = pitchgrid::Kind::off;
    none.levelDb = 0.0f;
    rows.setChoice(none);
    EXPECT_FALSE(rows.getSnap().isEnabled());
    EXPECT_FALSE(rows.getSnapMode().isEnabled());
    rows.setChoice(octavesWithLevels(SnapMode::TimeAndLevel));
    EXPECT_TRUE(rows.getSnapMode().isEnabled());
    rows.getSnap().setToggleState(false, juce::sendNotificationSync);
    EXPECT_TRUE(rows.getSnapMode().isEnabled()) << "usable while Snap is off, so the mode can be chosen first";
}

TEST(ShapeWindowSnapMode, TheModeIsRememberedAcrossWindows) {
    SnapModeKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        editor.getPitchRows()->getSnapMode().choose(SnapMode::TimeOnly);
    }
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_EQ(again.getPitchRows()->getChoice().snapMode, SnapMode::TimeOnly);
    EXPECT_EQ(again.getPitchRows()->getSnapMode().getMode(), SnapMode::TimeOnly);
}

TEST(ShapeWindowSnapMode, ThePickerIsATabStopRightAfterSnapAndEnterOpensIt) {
    SnapModeKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    auto& rows = *editor.getPitchRows();
    rows.setChoice(octavesWithLevels(SnapMode::TimeAndLevel));
    const auto walk = synth::test::walkTabOrder(editor);
    const auto at = [&walk](juce::Component& c) {
        const auto it = std::find(walk.all.begin(), walk.all.end(), &c);
        return it == walk.all.end() ? -1 : (int)(it - walk.all.begin());
    };
    ASSERT_GE(at(rows.getSnap()), 0);
    EXPECT_EQ(at(rows.getSnapMode()), at(rows.getSnap()) + 1);
    int opened = 0;
    rows.getSnapMode().showMenuHook = [&opened](juce::PopupMenu&) { ++opened; };
    EXPECT_TRUE(
        static_cast<juce::Component&>(rows.getSnapMode()).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(opened, 1);
}
