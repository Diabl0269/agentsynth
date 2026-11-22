// AlertIconDepthTests.cpp -- what the alert box's severity icon paints at each Depth level
// (docs/layout/theming.md#depth-levels, Source/UI/Layout/AlertIconPainter.h).
//
// Soft is today's lit disc: the old call (paintSeverityDisc with no theme) is the golden, and Soft must match it pixel
// for pixel in every built-in theme and glyph. Flat is a bare glyph (no disc); Rich is Soft's disc and glyph plus the
// shared tile layers (rim, shadow), never a new shape; the hue is Soft's in every theme.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "../Layout/NativeWindowTestGuard.h"
#include "../SourceScan.h"
#include "DepthSameShape.h"
#include "TestPaths.h"
#include "ThemeSweep/ThemeSweepSupport.h"
#include "ThemeSweep/ThemeSweepSurfaces.h"
#include "UI/Layout/AlertIconPainter.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AlertIconBounds.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/DepthLevel.h"
#include <cmath>
#include <gtest/gtest.h>
#include <regex>
#include <string>

namespace {

using synth::test::theme_sweep::contrastRatio;
using synth::theme::DepthLevel;
using synth::theme::Theme;
using synth::ui::SeverityGlyph;

constexpr int kSide = synth::ui::kAlertIconSize;
constexpr int kImage = kSide + 40; // room round the icon for the Rich shadow and glow
const juce::Rectangle<float> kArea(20.0f, 20.0f, (float)kSide, (float)kSide);

constexpr SeverityGlyph kGlyphs[] = {SeverityGlyph::warning, SeverityGlyph::info, SeverityGlyph::question};

Theme themeAt(Theme theme, DepthLevel level) {
    theme.treatment.depth = level;
    return theme;
}

juce::Colour inkFor(const Theme& theme, SeverityGlyph glyph) {
    return glyph == SeverityGlyph::warning ? theme.colors.warning : theme.colors.accent;
}

juce::Colour surfaceOf(const Theme& theme) { return synth::theme::windowBackgroundFor(theme); }

juce::Image newImage(juce::Colour surface) {
    juce::Image image(juce::Image::ARGB, kImage, kImage, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.fillAll(surface);
    return image;
}

juce::Image paintIcon(const Theme& theme, SeverityGlyph glyph) {
    const auto surface = surfaceOf(theme);
    auto image = newImage(surface);
    juce::Graphics g(image);
    synth::ui::paintAlertIcon(g, kArea, inkFor(theme, glyph), glyph, surface, theme);
    return image;
}

juce::Image paintOldCall(const Theme& theme, SeverityGlyph glyph) {
    const auto surface = surfaceOf(theme);
    auto image = newImage(surface);
    juce::Graphics g(image);
    synth::ui::paintSeverityDisc(g, kArea, inkFor(theme, glyph), glyph, surface);
    return image;
}

int countDifferent(const juce::Image& a, const juce::Image& b) {
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            n += a.getPixelAt(x, y) != b.getPixelAt(x, y) ? 1 : 0;
    return n;
}

bool near(juce::Colour a, juce::Colour b, int tolerance) {
    return std::abs(a.getRed() - b.getRed()) <= tolerance && std::abs(a.getGreen() - b.getGreen()) <= tolerance &&
           std::abs(a.getBlue() - b.getBlue()) <= tolerance;
}

juce::Point<int> pixelAt(juce::Point<float> p) { return {(int)std::floor(p.x), (int)std::floor(p.y)}; }

// The side of the glyph the painter draws at `level` (the filled body).
float glyphSideAt(DepthLevel level, SeverityGlyph glyph) {
    const float soft = (float)kSide * synth::ui::kAlertGlyphShare;
    if (level == DepthLevel::Flat)
        return soft * synth::ui::kFlatGlyphScale;
    return soft;
}

// A point of the glyph's mark (the hole in it) and a point of its body beside it, on the glyph's 24-unit grid.
struct MarkProbe {
    juce::Point<float> mark, body;
};
MarkProbe probeFor(SeverityGlyph glyph) {
    switch (glyph) {
    case SeverityGlyph::warning:
        return {{12.0f, 10.6f}, {15.2f, 10.6f}};
    case SeverityGlyph::info:
        return {{12.0f, 14.0f}, {16.0f, 14.0f}};
    default:
        return {{12.0f, 17.6f}, {16.0f, 17.6f}};
    }
}

juce::Point<float> onGrid(juce::Point<float> gridPoint, float glyphSide) {
    const auto origin = kArea.getCentre() - juce::Point<float>(glyphSide, glyphSide) * 0.5f;
    return origin + gridPoint * (glyphSide / 24.0f);
}

// Rows and columns of the pixels that are close to `ink`: the glyph's extent.
juce::Rectangle<int> inkExtent(const juce::Image& image, juce::Colour ink) {
    juce::Rectangle<int> box;
    bool any = false;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (near(image.getPixelAt(x, y), ink, 6)) {
                const juce::Rectangle<int> px(x, y, 1, 1);
                box = any ? box.getUnion(px) : px;
                any = true;
            }
    return box;
}

} // namespace

TEST(AlertIconDepthTest, SoftIsTodaysIconPixelForPixelInEveryThemeAndGlyph) {
    for (const auto& base : synth::theme::builtInThemes()) {
        const auto soft = themeAt(base, DepthLevel::Soft);
        for (const auto glyph : kGlyphs)
            EXPECT_EQ(countDifferent(paintIcon(soft, glyph), paintOldCall(soft, glyph)), 0)
                << base.id << " glyph " << (int)glyph;
    }
}

TEST(AlertIconDepthTest, FlatIsABareGlyphWithNoDiscRimOrShadow) {
    for (const auto& base : synth::theme::builtInThemes()) {
        const auto flat = themeAt(base, DepthLevel::Flat);
        const auto soft = themeAt(base, DepthLevel::Soft);
        const auto surface = surfaceOf(flat);
        for (const auto glyph : kGlyphs) {
            const auto image = paintIcon(flat, glyph);
            const auto lit = paintIcon(soft, glyph);
            // The corners of the 64 px box and the middle of each edge (where the Soft disc's rim runs) are surface.
            for (const auto p :
                 {juce::Point<float>(0.1f, 0.1f), juce::Point<float>(0.9f, 0.1f), juce::Point<float>(0.1f, 0.9f),
                  juce::Point<float>(0.9f, 0.9f), juce::Point<float>(0.5f, 0.02f), juce::Point<float>(0.02f, 0.5f),
                  juce::Point<float>(0.98f, 0.5f), juce::Point<float>(0.5f, 0.98f)}) {
                const auto px = pixelAt(kArea.getTopLeft() + juce::Point<float>(p.x, p.y) * (float)kSide);
                EXPECT_EQ(image.getPixelAt(px.x, px.y), surface) << base.id << " " << (int)glyph << " bare";
            }
            // Nothing of a disc: the whole image outside the glyph's own extent is surface.
            const auto ink = juce::Colour(inkFor(flat, glyph));
            const auto extent = inkExtent(image, ink);
            EXPECT_FALSE(extent.isEmpty()) << base.id;
            const auto glyphBox = (float)extent.getWidth();
            // Bigger than the glyph inside a Soft disc (about 1.4x), and still inside the old disc.
            const auto softExtent = inkExtent(lit, ink);
            EXPECT_GT(glyphBox, (float)softExtent.getWidth() * 1.25f) << base.id << " " << (int)glyph;
            EXPECT_LT(glyphBox, (float)kSide) << base.id << " " << (int)glyph;
            EXPECT_NEAR(extent.getCentreX(), kArea.getCentreX(), 3.0f) << base.id << " " << (int)glyph;
            const int margin = 2;
            for (int y = 0; y < kImage; ++y)
                for (int x = 0; x < kImage; ++x) {
                    if (extent.expanded(margin + 4).contains(x, y))
                        continue;
                    ASSERT_EQ(image.getPixelAt(x, y), surface)
                        << base.id << " " << (int)glyph << ": paints outside the bare glyph at " << x << "," << y;
                }
        }
    }
}

TEST(AlertIconDepthTest, RichIsALitDiscWithAShadowAndADarkThemeWash) {
    for (const auto& base : synth::theme::builtInThemes()) {
        const auto rich = themeAt(base, DepthLevel::Rich);
        const auto soft = themeAt(base, DepthLevel::Soft);
        const auto surface = surfaceOf(rich);
        for (const auto glyph : kGlyphs) {
            const auto r = paintIcon(rich, glyph);
            const auto s = paintIcon(soft, glyph);
            EXPECT_GT(countDifferent(r, s), 400) << base.id << " " << (int)glyph;
            // Drop shadow: just under the disc's bottom edge, outside it, the surface is darkened (Soft paints none).
            const auto below = pixelAt({kArea.getCentreX(), kArea.getBottom() + 1.5f});
            EXPECT_NE(r.getPixelAt(below.x, below.y), surface) << base.id << " " << (int)glyph << ": no drop shadow";
            EXPECT_EQ(s.getPixelAt(below.x, below.y), surface) << base.id << " " << (int)glyph;
        }
    }
}

// Rich keeps Soft's disc, hue and glyph in every theme (bug kind look-level-reshapes): the silhouette, the glyph box
// and the body colour hold (DepthSameShape.h), and no extra stroke of the icon colour appears anywhere in the disc
// beyond Soft's own glyph (a Rich-only outline would fail the stroke check, whatever the mean colour delta says).
TEST(AlertIconDepthTest, RichAlertIconKeepsSoftsShapeInEveryThemeAndGlyph) {
    for (const auto& base : synth::theme::builtInThemes())
        for (const auto glyph : kGlyphs) {
            const auto soft = themeAt(base, DepthLevel::Soft);
            const auto rich = themeAt(base, DepthLevel::Rich);
            const auto centre = kArea.getCentre();
            const float softGlyph = (float)kSide * synth::ui::kAlertGlyphShare;
            const float inner = (float)kSide * 0.5f - (float)depthshape::kRimBand;
            const auto inDisc = [=](int x, int y) {
                return std::hypot((float)x + 0.5f - centre.x, (float)y + 0.5f - centre.y) < inner;
            };
            depthshape::Surface surface{"alert icon",
                                        juce::Rectangle<float>(softGlyph, softGlyph)
                                            .withCentre(centre)
                                            .getSmallestIntegerContainer()
                                            .expanded(1),
                                        inDisc};
            const auto softImage = paintIcon(soft, glyph);
            const auto richImage = paintIcon(rich, glyph);
            depthshape::expectSameShape(softImage, richImage, surface, base.id + " glyph " + juce::String((int)glyph));

            // No new stroke: every icon-coloured pixel Rich paints inside the disc is within 2 px of one Soft paints.
            const auto ink = inkFor(rich, glyph);
            int extra = 0;
            for (int y = 0; y < kImage; ++y)
                for (int x = 0; x < kImage; ++x) {
                    if (!inDisc(x, y) || !near(richImage.getPixelAt(x, y), ink, 6))
                        continue;
                    bool nearSoftInk = false;
                    for (int dy = -2; dy <= 2 && !nearSoftInk; ++dy)
                        for (int dx = -2; dx <= 2 && !nearSoftInk; ++dx)
                            nearSoftInk = near(softImage.getPixelAt(juce::jlimit(0, kImage - 1, x + dx),
                                                                    juce::jlimit(0, kImage - 1, y + dy)),
                                               ink, 12);
                    extra += nearSoftInk ? 0 : 1;
                }
            EXPECT_EQ(extra, 0) << base.id << " glyph " << (int)glyph << ": Rich paints a stroke Soft does not";
        }
}

// The glyph's mark reads against the glyph's own body at every level in every theme (the 3:1 bar the sweep uses for
// the icon colour on the surface).
TEST(AlertIconDepthTest, TheMarkReadsAgainstTheGlyphAtEveryLevelInEveryTheme) {
    for (const auto& base : synth::theme::builtInThemes())
        for (int i = 0; i < synth::theme::kDepthLevelCount; ++i) {
            const auto level = (DepthLevel)i;
            const auto theme = themeAt(base, level);
            for (const auto glyph : kGlyphs) {
                const auto ink =
                    synth::ui::alertIconColour(glyph == SeverityGlyph::warning ? juce::MessageBoxIconType::WarningIcon
                                                                               : juce::MessageBoxIconType::InfoIcon,
                                               theme.colors.accent, theme.colors.warning, surfaceOf(theme));
                const auto surface = surfaceOf(theme);
                auto image = newImage(surface);
                {
                    juce::Graphics g(image);
                    synth::ui::paintAlertIcon(g, kArea, ink, glyph, surface, theme);
                }
                const auto probe = probeFor(glyph);
                const float side = glyphSideAt(level, glyph);
                const auto m = pixelAt(onGrid(probe.mark, side));
                const auto b = pixelAt(onGrid(probe.body, side));
                EXPECT_GE(contrastRatio(image.getPixelAt(m.x, m.y), image.getPixelAt(b.x, b.y)), 3.0)
                    << theme.id << " level " << i << " glyph " << (int)glyph;
                EXPECT_TRUE(near(image.getPixelAt(b.x, b.y), ink, 12))
                    << theme.id << " level " << i << " glyph " << (int)glyph << ": the body is not the icon colour";
            }
        }
}

// Windows that open for real need a display (and not the ASAN job's Xvfb).
class AlertIconDepthWindows : public NeedsDisplayTest {};

// The guard that drawAlertBox reads the Depth level when it paints: an alert built after a switch looks different at
// each level, and the Soft one is the old disc.
TEST_F(AlertIconDepthWindows, AnAlertOpenedAtEachLevelPaintsItsOwnIcon) {
    using namespace synth::test::theme_sweep;
    for (const auto& base : synth::theme::builtInThemes())
        for (const auto type : {juce::MessageBoxIconType::WarningIcon, juce::MessageBoxIconType::InfoIcon}) {
            std::vector<juce::Image> icons;
            for (const auto level : {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich}) {
                const auto theme = themeAt(base, level);
                synth::test::theme_sweep::SweepEnvironment env(theme);
                std::unique_ptr<juce::AlertWindow> alert(
                    env.lookAndFeel().createAlertWindow("Title", "A message.", "OK", {}, {}, type, 1, nullptr));
                ASSERT_NE(alert, nullptr);
                const auto window = renderToImage(*alert);
                const auto bounds = env.lookAndFeel().getLastAlertIconBounds();
                icons.push_back(window.getClippedImage(bounds));
                if (level == DepthLevel::Soft &&
                    base.colors.surface.getAlpha() == 0xff) { // a glass sheen tints the icon area
                    const auto surface = alert->findColour(juce::AlertWindow::backgroundColourId).withAlpha(1.0f);
                    juce::Image expected(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true,
                                         juce::SoftwareImageType());
                    juce::Graphics g(expected);
                    g.fillAll(surface);
                    g.addTransform(juce::AffineTransform::translation((float)-bounds.getX(), (float)-bounds.getY()));
                    synth::ui::paintSeverityDisc(g, bounds.toFloat(), env.lookAndFeel().getLastAlertIconColour(),
                                                 type == juce::MessageBoxIconType::WarningIcon ? SeverityGlyph::warning
                                                                                               : SeverityGlyph::info,
                                                 surface);
                    // The window's surface under the icon is the same colour; the icon is the old disc.
                    // Painted in a window the glyph's blurred shadow can land one level off in a channel; the
                    // pixel-for-pixel pin is SoftIsTodaysIconPixelForPixelInEveryThemeAndGlyph.
                    int off = 0;
                    for (int y = 0; y < expected.getHeight(); ++y)
                        for (int x = 0; x < expected.getWidth(); ++x)
                            off += near(icons.back().getPixelAt(x, y), expected.getPixelAt(x, y), 2) ? 0 : 1;
                    EXPECT_EQ(off, 0) << base.id << ": Soft is not today's disc";
                }
            }
            EXPECT_GT(countDifferent(icons[0], icons[1]), 200) << base.id << ": Flat paints what Soft does";
            EXPECT_GT(countDifferent(icons[1], icons[2]), 200) << base.id << ": Rich paints what Soft does";
            EXPECT_GT(countDifferent(icons[0], icons[2]), 200) << base.id << ": Flat paints what Rich does";
        }
}

// Bug kind sibling-gap: the alert box paints its icon through the Depth-aware painter and hands it the theme.
TEST(AlertIconDepthTest, TheAlertBoxPaintsItsIconThroughTheDepthAwarePainter) {
    const auto texts = synth::test::source_scan::sourceTexts();
    const auto it = texts.find("Source/UI/Theme/AppLookAndFeel/AppLookAndFeelAlertBox.cpp");
    ASSERT_NE(it, texts.end());
    std::smatch m;
    ASSERT_TRUE(std::regex_search(it->second, m, std::regex(R"(paintAlertIcon\s*\([^;]*;)")));
    EXPECT_NE(m[0].str().find("theme"), std::string::npos) << "drawAlertBox does not pass the theme";
    EXPECT_EQ(it->second.find("paintSeverityDisc"), std::string::npos) << "the alert box bypasses the Depth look";
}

// Guard for bug kind "window-needs-display": an alert window lays itself out against the
// monitor it sits on, so building one on a headless runner (no display) segfaults. Every test file that builds an
// alert (createAlertWindow, buildConfirmChoiceWindow, buildAlertWindow, a juce::AlertWindow subclass) must skip
// without a display, through NeedsDisplayTest or an explicit primary-display check.
TEST(AlertDisplayFixtureGuard, EveryTestFileThatBuildsAnAlertSkipsWithoutADisplay) {
    const juce::File tests(TESTS_ROOT_DIR);
    const std::regex builds(
        R"(createAlertWindow\s*\(|buildConfirmChoiceWindow\s*\(|buildAlertWindow\s*\(|new\s+\w*Alert\s*\()");
    const std::regex skips(R"(NeedsDisplayTest|getPrimaryDisplay\s*\(\s*\)\s*==\s*nullptr)");
    // The helpers that define the builders hold no test; this file's own patterns are not a use.
    const juce::StringArray exempt{"ThemeSweepSurfaces.cpp", "ThemeSweepSurfaces.h", "AlertIconDepthTests.cpp"};
    int scanned = 0;
    for (const auto& file : tests.findChildFiles(juce::File::findFiles, true, "*.cpp")) {
        if (exempt.contains(file.getFileName()))
            continue;
        const std::string text = file.loadFileAsString().toStdString();
        ++scanned;
        if (std::regex_search(text, builds))
            EXPECT_TRUE(std::regex_search(text, skips))
                << file.getFullPathName() << " builds an alert but never skips without a display (derive its "
                << "fixture from NeedsDisplayTest, Tests/UI/Layout/NativeWindowTestGuard.h)";
    }
    EXPECT_GT(scanned, 100) << "the scan found no test files: TESTS_ROOT_DIR is wrong";
}
