// EQBandTypeGlyphSurfacesTests.cpp -- a Parametric EQ band's type is a response-curve glyph on every surface that shows
// it (strip chip, readout, type pill, type menu); its name is for tooltips and screen readers. These tests fail when
// a surface starts drawing the type's name again, and check the chip's glyph in every built-in theme.
#include "../SourceScan.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/ModuleViews/EQBandStrip.h"
#include "UI/ModuleViews/EQBandTypePill.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::eq::BandType;

namespace {

// Every Source file that calls eqBandTypeName( (the declaration and definition included), how many times, and why
// none of them draws the name: a tooltip, a spoken title or a menu row (the menu keeps glyph + name).
const std::map<std::string, std::pair<int, const char*>>& allowedCallSites() {
    static const std::map<std::string, std::pair<int, const char*>> sites = {
        {"Source/UI/ModuleViews/EQBandKinds.h", {1, "the declaration"}},
        {"Source/UI/ModuleViews/EQBandKinds.cpp", {1, "the definition"}},
        {"Source/UI/ModuleViews/EQBandTypePill.cpp", {1, "the accessible title and the tooltip's first words"}},
        {"Source/UI/ModuleViews/EQBandStrip.cpp",
         {2, "the chip's tooltip, its test-mode label and screen-reader title"}},
        {"Source/UI/ModuleViews/EQBandTypeMenu.cpp", {2, "the menu row's name beside its glyph, and its tooltip"}},
        {"Source/UI/ModuleViews/EQCurveKeyboard.cpp", {1, "the curve's spoken description of the selected band"}},
    };
    return sites;
}

struct Rig {
    explicit Rig(int bands) {
        for (int b = 0; b < bands; ++b) {
            eq.setBandFreq(b, 100.0f * std::pow(2.0f, (float)b));
            eq.setBandEnabled(b, true);
        }
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setBounds(0, 0, 480, 200);
        strip = std::make_unique<synth::ui::EQBandStrip>(*curve);
        strip->setBounds(0, 0, 480, strip->heightForWidth(480));
    }
    ParametricEQModule eq;
    std::unique_ptr<EQCurveComponent> curve;
    std::unique_ptr<synth::ui::EQBandStrip> strip;
};

juce::Image snapshotOf(juce::Component& c) { return c.createComponentSnapshot(c.getLocalBounds(), false, 1.0f); }

// The pixels of `a` that differ from `b`: how many and their bounding box.
struct Diff {
    int count = 0;
    juce::Rectangle<int> box;
};
Diff diffOf(const juce::Image& a, const juce::Image& b) {
    Diff d;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) {
                ++d.count;
                d.box = d.box.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1) : d.box.getUnion({x, y, 1, 1});
            }
    return d;
}

} // namespace

TEST(EQBandTypeGlyphSurfaces, NoSurfaceCallsEqBandTypeNameOutsideTheTooltipAndSpokenUses) {
    std::map<std::string, int> registered;
    for (const auto& [file, entry] : allowedCallSites()) {
        EXPECT_NE(std::string(entry.second), "") << file << " needs a written reason";
        registered[file] = entry.first;
    }
    synth::test::source_scan::expectCountsMatch(
        "eqBandTypeName(",
        synth::test::source_scan::countCallSites(std::regex(R"(eqBandTypeName\s*\()"), "Source/Modules"), registered,
        "EQBandTypeGlyphSurfacesTests.cpp (allowedCallSites)");
}

TEST(EQBandTypeGlyphSurfaces, TheChipKeepsTheNameInItsTitleAndTooltipButChangesOnlyItsGlyphWithTheType) {
    Rig rig(3);
    auto* chip = rig.strip->getChip(1);
    ASSERT_NE(chip, nullptr);
    EXPECT_NE(chip->getTitle().indexOf("Bell"), -1);
    EXPECT_NE(chip->getTooltip().indexOf("Bell"), -1);
    const auto bell = snapshotOf(*chip);

    rig.curve->setBandTypeAndSlope(1, BandType::Notch);
    chip = rig.strip->getChip(1);
    EXPECT_NE(chip->getTitle().indexOf("Notch"), -1);
    EXPECT_NE(chip->getTooltip().indexOf("Notch"), -1);
    const auto diff = diffOf(bell, snapshotOf(*chip));
    // The kind colour changes too, so compare only where the glyph is: the right half, after the number.
    EXPECT_GT(diff.count, 0);
    EXPECT_LE(diff.box.getWidth(), chip->getWidth());
}

// The glyph's ink reaches the chip fill in every built-in theme (light and Neon included): the type's glyph is the only
// thing that differs between two chips of one kind, and its pixels are not the fill's.
TEST(EQBandTypeGlyphSurfaces, TheChipGlyphHasInkAgainstItsFillInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        Rig rig(3);
        rig.strip->setLookAndFeel(&lookAndFeel);
        rig.curve->setBandTypeAndSlope(1, BandType::LowShelf);
        auto* chip = rig.strip->getChip(1);
        ASSERT_NE(chip, nullptr);
        const auto shelf = snapshotOf(*chip);
        rig.curve->setBandTypeAndSlope(1, BandType::HighShelf); // same kind, so only the glyph changes
        const auto other = snapshotOf(*rig.strip->getChip(1));
        const auto diff = diffOf(shelf, other);
        ASSERT_GT(diff.count, 8) << theme.id << ": the two shelves look alike";
        const auto fill = shelf.getPixelAt(diff.box.getCentreX(), 3);
        float widest = 0.0f;
        for (int y = diff.box.getY(); y < diff.box.getBottom(); ++y)
            for (int x = diff.box.getX(); x < diff.box.getRight(); ++x) {
                const auto px = shelf.getPixelAt(x, y);
                widest = std::max(widest, std::abs(px.getFloatRed() - fill.getFloatRed()) +
                                              std::abs(px.getFloatGreen() - fill.getFloatGreen()) +
                                              std::abs(px.getFloatBlue() - fill.getFloatBlue()));
            }
        EXPECT_GT(widest, 0.6f) << theme.id << ": the glyph ink does not stand out from the chip fill";
        rig.strip->setLookAndFeel(nullptr);
    }
}

TEST(EQBandTypeGlyphSurfaces, TheTypePillTooltipAndTitleNameTheType) {
    Rig rig(2);
    synth::ui::EQBandTypePill pill(*rig.curve);
    rig.curve->setSelectedBand(0);
    rig.curve->setBandTypeAndSlope(0, BandType::HighCut);
    EXPECT_EQ(pill.getTitle(), "Band 1 type, High Cut");
    EXPECT_TRUE(pill.getTooltip().startsWith("High Cut. "));
}
