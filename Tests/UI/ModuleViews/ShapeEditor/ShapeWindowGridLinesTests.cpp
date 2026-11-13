// ShapeWindowGridLinesTests.cpp -- how the shape window's grid reads: three tiers of line (labelled guides, snap lines)
// that differ in brightness and not only hue in every built-in theme, the Guides switch, and the line under the
// pointer lighting up with its name.
#include "../../../TestSettingsHelpers.h"
#include "../../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/ShapeEditor/PitchGridRows.h"
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

        const juce::Point<float> pointer((float)column + 2.0f, (float)kProbeRow + 60.0f); // between level lines
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

namespace {

/** The strongest difference from column `bgX` over the rows between the labels and the curve's top. */
float peakOver(const juce::Image& image, int x, int bgX) {
    float best = 0.0f;
    for (int row = 16; row < 26; ++row)
        best = std::max(best, std::abs(image.getPixelAt(x, row).getPerceivedBrightness() -
                                       image.getPixelAt(bgX, row).getPerceivedBrightness()));
    return best;
}

/** The summed per-channel distance between two colours. */
float colourDistance(juce::Colour a, juce::Colour b) {
    return std::abs(a.getFloatRed() - b.getFloatRed()) + std::abs(a.getFloatGreen() - b.getFloatGreen()) +
           std::abs(a.getFloatBlue() - b.getFloatBlue());
}

/** The brightest of the three pixels around column `x` on row 20 (a line may straddle two columns). */
juce::Colour brightestNear(const juce::Image& image, int x) {
    auto best = image.getPixelAt(x, 20);
    for (const int dx : {-1, 1})
        if (image.getPixelAt(x + dx, 20).getPerceivedBrightness() > best.getPerceivedBrightness())
            best = image.getPixelAt(x + dx, 20);
    return best;
}

double noteHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

} // namespace

TEST(ShapeWindowGridLines, TheNotesGridReadsCThenNotesThenDottedOutOfKeyNotesInEveryTheme) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemedEditor themed(theme);
        auto& rows = *themed.editor->getPitchRows();
        rows.getKeyField().onPickOwn(synth::MusicalScale{0, 0xAB5, "Major"});
        themed.editor->setGridName("Notes");
        auto& curve = themed.editor->getCurveEditor();
        const auto image = synth::test::theme_sweep::renderToImage(curve);
        synth::test::theme_sweep::writeSweepPng("shape-grid-notes", theme.id, image);

        // Around C6 the semitones are far enough apart to all be drawn.
        const int bg = columnOf(curve, (float)(noteHz(84) * std::pow(2.0, 0.5 / 12.0)));
        const float c = peakOver(image, columnOf(curve, (float)noteHz(84)), bg);
        const float d = peakOver(image, columnOf(curve, (float)noteHz(86)), bg);
        const float dSharp = peakOver(image, columnOf(curve, (float)noteHz(87)), bg);
        EXPECT_GT(dSharp, 0.0f) << theme.name << ": the out-of-key note is still drawn";
        EXPECT_GT(d, dSharp) << theme.name << ": an in-key note outshines an out-of-key one";
        EXPECT_GT(c, d + 0.04f) << theme.name << ": the C is the landmark, by brightness";
    }
}

TEST(ShapeWindowGridLines, TheHarmonicsGridLightsThePowersOfTwoAndLeansToTheWarningHue) {
    juce::MessageManager::getInstance();
    GuidesKeysGuard keys;
    for (const auto& theme : synth::theme::builtInThemes()) {
        ThemedEditor themed(theme);
        themed.editor->setGridName("Harmonics");
        themed.editor->getPitchRows()->setRootId(PitchGridRows::rootIdOf(45)); // A2
        auto& curve = themed.editor->getCurveEditor();
        const auto image = synth::test::theme_sweep::renderToImage(curve);
        synth::test::theme_sweep::writeSweepPng("shape-grid-harmonics", theme.id, image);

        const int bg = columnOf(curve, 110.0f * 3.5f);
        const float x4 = peakOver(image, columnOf(curve, 440.0f), bg);
        const float x3 = peakOver(image, columnOf(curve, 330.0f), bg);
        EXPECT_GT(x3, 0.0f) << theme.name;
        EXPECT_GT(x4, x3 + 0.04f) << theme.name << ": x4 is a landmark, by brightness";

        // Hue is the second cue: of two landmarks, the harmonic one leans toward the warning colour, the note one
        // toward the accent.
        const auto& colours = themed.lookAndFeel.getTheme().colors;
        const auto harmonic = brightestNear(image, columnOf(curve, 440.0f)); // x4, a landmark
        themed.editor->setGridName("Notes");
        const auto notes = synth::test::theme_sweep::renderToImage(curve);
        const auto note = brightestNear(notes, columnOf(curve, (float)noteHz(84))); // C6, a landmark
        const auto lean = [&](juce::Colour c) {
            return colourDistance(c, colours.warning) - colourDistance(c, colours.accent);
        };
        if (colours.accent == colours.warning)
            continue; // a theme whose accent is its warning colour (Warm Console) has brightness and width only
        EXPECT_GT(lean(note), lean(harmonic)) << theme.name;
    }
}
