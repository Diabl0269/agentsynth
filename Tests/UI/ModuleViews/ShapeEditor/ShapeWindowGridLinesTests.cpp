// ShapeWindowGridLinesTests.cpp -- how the shape window's grid reads: three tiers of line (labelled guides, snap lines)
// that differ in brightness and not only hue in every built-in theme, the Guides switch, and the line under the
// pointer lighting up with its name.
#include "../../../TestSettingsHelpers.h"
#include "../../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

constexpr int kProbeRow = 3; // near the top: no curve, no label, no horizontal guide

struct GuidesKeysGuard {
    synth::test::PersistedKeysGuard guard{
        juce::StringArray{shapewindowprefs::gridKey("filter"), shapewindowprefs::guidesKey("filter")}};
};

int columnOf(CurveEditorComponent& curve, float hz) {
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), [] {
        CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return config;
    }());
    return (int)geometry.xForTime(CustomFilterShapes::hzToX(hz));
}

float brightnessOver(const juce::Image& image, int x, int bgX) {
    return std::abs(image.getPixelAt(x, kProbeRow).getPerceivedBrightness() -
                    image.getPixelAt(bgX, kProbeRow).getPerceivedBrightness());
}

/** A column between two 1/3 oct lines (so nothing is drawn on it). */
int emptyColumnOf(CurveEditorComponent& curve) { return columnOf(curve, 1000.0f * std::pow(2.0f, 1.0f / 6.0f)); }

struct ThemedEditor {
    synth::theme::AppLookAndFeel lookAndFeel;
    ShapeRig rig;
    std::unique_ptr<ShapeListEditor> editor;

    explicit ThemedEditor(const synth::theme::Theme& theme) {
        lookAndFeel.applyTheme(theme);
        editor = std::make_unique<ShapeListEditor>(rig.adapter, 0);
        editor->setLookAndFeel(&lookAndFeel);
        editor->setSize(ShapeListEditor::kWidth, ShapeListEditor::kPerNoteHeight);
    }
    ~ThemedEditor() { editor->setLookAndFeel(nullptr); }
};

} // namespace

TEST(ShapeWindowGridLines, ALandmarkLineIsClearlyBrighterThanASnapLineInEveryTheme) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemedEditor themed(theme);
        auto& curve = themed.editor->getCurveEditor();
        themed.editor->setGridName("1/3 oct");
        const auto image = synth::test::theme_sweep::renderToImage(curve);
        synth::test::theme_sweep::writeSweepPng("shape-grid-tiers", theme.id, image);

        const int bg = emptyColumnOf(curve);
        const float landmark = brightnessOver(image, columnOf(curve, 100.0f), bg);
        const float snap = brightnessOver(image, columnOf(curve, 1000.0f * std::pow(2.0f, 1.0f / 3.0f)), bg);
        EXPECT_GT(snap, 0.0f) << theme.name << ": a snap line is drawn";
        EXPECT_GT(landmark, snap + 0.04f) << theme.name << ": the labelled guide is brighter, not only another hue";
        EXPECT_GT(landmark, 2.0f * snap) << theme.name;
    }
}

TEST(ShapeWindowGridLines, GuidesOffKeepsTheSnapLinesAndDropsTheLandmarks) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemedEditor themed(theme);
        auto& curve = themed.editor->getCurveEditor();
        themed.editor->setGridName("1/3 oct");
        ASSERT_NE(themed.editor->getGuidesToggle(), nullptr);
        EXPECT_TRUE(themed.editor->getGuidesToggle()->getToggleState()) << "on by default";
        themed.editor->getGuidesToggle()->setToggleState(false, juce::sendNotificationSync);
        EXPECT_FALSE(curve.areGuidesVisible());

        const auto image = synth::test::theme_sweep::renderToImage(curve);
        synth::test::theme_sweep::writeSweepPng("shape-grid-guides-off", theme.id, image);
        const int bg = emptyColumnOf(curve);
        const float snapElsewhere = brightnessOver(image, columnOf(curve, 1000.0f * std::pow(2.0f, 1.0f / 3.0f)), bg);
        const float formerGuide = brightnessOver(image, columnOf(curve, 100.0f), bg);
        EXPECT_GT(snapElsewhere, 0.0f) << theme.name << ": snap lines stay";
        EXPECT_FLOAT_EQ(formerGuide, 0.0f) << theme.name << ": the 100 Hz guide is gone, not a snap line";
    }
}

TEST(ShapeWindowGridLines, TheGuidesChoiceIsRememberedAndTheSwitchIsANamedTabStop) {
    GuidesKeysGuard keys;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    shapewindowprefs::saveText(&props, shapewindowprefs::guidesKey("filter"), {});
    ShapeRig rig;
    rig.adapter->setApplicationProperties(&props);
    {
        ShapeListEditor editor(rig.adapter, 0);
        auto* toggle = editor.getGuidesToggle();
        ASSERT_NE(toggle, nullptr);
        EXPECT_EQ(toggle->getTitle(), "Guides");
        EXPECT_FALSE(toggle->getTooltip().isEmpty());
        EXPECT_TRUE(toggle->getWantsKeyboardFocus());
        toggle->setToggleState(false, juce::sendNotificationSync);
    }
    ShapeListEditor again(rig.adapter, 0);
    EXPECT_FALSE(again.getGuidesToggle()->getToggleState());
    EXPECT_FALSE(again.getCurveEditor().areGuidesVisible());
    again.getGuidesToggle()->setToggleState(true, juce::sendNotificationSync);
    ShapeListEditor third(rig.adapter, 0);
    EXPECT_TRUE(third.getCurveEditor().areGuidesVisible());
}

TEST(ShapeWindowGridLines, PointingAtALineLightsItInTheAccentWithItsName) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemedEditor themed(theme);
        auto& curve = themed.editor->getCurveEditor();
        themed.editor->setGridName("1/3 oct");
        const float hz = 1000.0f * std::pow(2.0f, 1.0f / 3.0f);
        const int column = columnOf(curve, hz);
        EXPECT_EQ(curve.getHighlightedLineName(), "");

        const juce::Point<float> pointer((float)column + 2.0f, (float)kProbeRow + 40.0f);
        curve.mouseMove(makeCurveMouseEvent(curve, pointer, {}, false, pointer));
        EXPECT_EQ(curve.getHighlightedLineName(), "1.26 kHz");
        const auto image = synth::test::theme_sweep::renderToImage(curve);
        synth::test::theme_sweep::writeSweepPng("shape-grid-hover", theme.id, image);
        const auto lit = image.getPixelAt(column, curve.getHeight() - 30);
        EXPECT_NEAR(lit.getRed(), themed.lookAndFeel.getTheme().colors.accent.getRed(), 2) << theme.name;
        EXPECT_NEAR(lit.getBlue(), themed.lookAndFeel.getTheme().colors.accent.getBlue(), 2) << theme.name;

        curve.mouseExit(makeCurveMouseEvent(curve, {0.0f, 0.0f}, {}, false, {0.0f, 0.0f}));
        EXPECT_EQ(curve.getHighlightedLineName(), "");
    }
}

TEST(ShapeWindowGridLines, ADraggedPointLightsTheLineItSnappedToAndALevelLineIsNamedInDb) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    editor.setGridName("1 oct");
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), config);
    const auto from = geometry.nodePosition(3);
    const juce::Point<float> to(geometry.xForTime(CustomFilterShapes::hzToX(1100.0f)),
                                geometry.yForLevel(CustomFilterShapes::dbToY(-4.0f)));
    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDrag(curve, to, from));
    EXPECT_TRUE(curve.getHighlightedLineName() == "1 kHz" || curve.getHighlightedLineName() == "-6 dB")
        << curve.getHighlightedLineName();
    curve.mouseUp(curveLeftDrag(curve, to, from));
}
