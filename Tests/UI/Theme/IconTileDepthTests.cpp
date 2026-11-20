// IconTileDepthTests.cpp -- what an icon tile paints at each Depth level (docs/layout/theming.md#depth-levels): the top
// bar's chips, the framed icon buttons of card headers and the toast's severity disc.
//
// Rich keeps every tile's shape (bug kind look-level-reshapes): DepthSameShape.h renders each surface at Soft
// and Rich in every theme and holds the silhouette, the glyph box and the body colour.
//
// Soft is today's tile: the reference painters below are copies of what the surfaces painted before the Depth setting
// reached icons, and Soft must match them pixel for pixel in every built-in theme. Flat drops the light (the severity
// disc is a plain disc); Rich adds the lit tile (ring, top highlight, drop shadow) and a glow on an active tool.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "../Chrome/ToolbarButton/ToolbarButtonTestHelpers.h"
#include "../SourceScan.h"
#include "DepthSameShape.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/IconButton.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Theme/IconTileDepth.h"
#include <cmath>
#include <map>
#include <regex>
#include <string>

namespace {

using synth::theme::DepthLevel;
using synth::theme::Theme;
using namespace toolbartest;

Theme themeAt(Theme theme, DepthLevel level) {
    theme.treatment.depth = level;
    return theme;
}

int countDifferent(const juce::Image& a, const juce::Image& b) {
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            n += a.getPixelAt(x, y) != b.getPixelAt(x, y) ? 1 : 0;
    return n;
}

float luma(juce::Colour c) { return 0.299f * c.getFloatRed() + 0.587f * c.getFloatGreen() + 0.114f * c.getFloatBlue(); }

double relativeLuminance(juce::Colour c) {
    auto channel = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

double contrast(juce::Colour a, juce::Colour b) {
    const double la = relativeLuminance(a.withAlpha(1.0f)), lb = relativeLuminance(b.withAlpha(1.0f));
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// ---- the severity disc ----

constexpr int kDiscImage = 40;
constexpr float kDiscSide = 22.0f;
const juce::Rectangle<float> kDiscArea(9.0f, 9.0f, kDiscSide, kDiscSide);

juce::Image paintDisc(const Theme* theme) {
    juce::Image image(juce::Image::ARGB, kDiscImage, kDiscImage, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    synth::ui::paintSeverityDisc(g, kDiscArea, juce::Colour(0xfff0b040), synth::ui::SeverityGlyph::warning,
                                 juce::Colour(0xff202428), theme);
    return image;
}

// The pre-change disc body (gradient, rim) with no glyph.
juce::Image referenceSoftDiscBody() {
    juce::Image image(juce::Image::ARGB, kDiscImage, kDiscImage, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    const auto ink = juce::Colour(0xfff0b040);
    const auto disc = kDiscArea;
    const float side = disc.getWidth();
    const auto lit = juce::ColourGradient(ink.withMultipliedAlpha(0.38f), disc.getX() + side * 0.35f,
                                          disc.getY() + side * 0.28f, ink.withMultipliedAlpha(0.10f),
                                          disc.getX() + side * 0.35f + side * 0.8f, disc.getY() + side * 0.28f, true);
    g.setGradientFill(lit);
    g.fillEllipse(disc.reduced(0.5f));
    g.setColour(ink.withMultipliedAlpha(0.45f));
    g.drawEllipse(disc.reduced(0.5f), 1.0f);
    return image;
}

// Pixels of the disc outside the glyph and its shadow (which fill the middle 80%): the band the body paints alone.
std::vector<juce::Point<int>> discBand() {
    std::vector<juce::Point<int>> out;
    const auto c = kDiscArea.getCentre();
    for (int y = 0; y < kDiscImage; ++y)
        for (int x = 0; x < kDiscImage; ++x) {
            const float d = std::hypot((float)x + 0.5f - c.x, (float)y + 0.5f - c.y) / kDiscSide;
            if (d > 0.40f && d < 0.5f)
                out.push_back({x, y});
        }
    return out;
}

} // namespace

TEST(IconTileDepthTest, SoftSeverityDiscIsTodaysDiscPixelForPixelInEveryTheme) {
    const auto reference = referenceSoftDiscBody();
    const auto band = discBand();
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto soft = themeAt(theme, DepthLevel::Soft);
        for (const Theme* t : {&soft, static_cast<const Theme*>(nullptr)}) {
            const auto image = paintDisc(t);
            int different = 0;
            for (const auto p : band)
                different += image.getPixelAt(p.x, p.y) != reference.getPixelAt(p.x, p.y) ? 1 : 0;
            EXPECT_EQ(different, 0) << theme.id;
        }
    }
}

TEST(IconTileDepthTest, FlatSeverityDiscIsAPlainDiscWithNoRimOrShadow) {
    const auto flatTheme = themeAt(synth::theme::makeObsidian(), DepthLevel::Flat);
    const auto flat = paintDisc(&flatTheme);
    const auto soft = paintDisc(nullptr);
    EXPECT_GT(countDifferent(flat, soft), 0);
    const auto band = discBand();
    const auto first = flat.getPixelAt((int)(kDiscArea.getCentreX() - kDiscSide * 0.42f), (int)kDiscArea.getCentreY());
    int different = 0;
    // One fill: every pixel well inside the band (clear of the anti-aliased edge) is the same colour, no gradient.
    for (const auto p : band) {
        const float d =
            std::hypot((float)p.x + 0.5f - kDiscArea.getCentreX(), (float)p.y + 0.5f - kDiscArea.getCentreY());
        if (d < kDiscSide * 0.45f && flat.getPixelAt(p.x, p.y) != first)
            ++different;
    }
    EXPECT_EQ(different, 0) << "no radial light, no ring";
    // No shadow outside the disc's bottom edge.
    EXPECT_EQ(flat.getPixelAt(20, 33).getAlpha(), 0);
}

TEST(IconTileDepthTest, RichSeverityDiscAddsADropShadowAndATopHighlight) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto richTheme = themeAt(theme, DepthLevel::Rich);
        const auto rich = paintDisc(&richTheme);
        const auto soft = paintDisc(nullptr);
        EXPECT_EQ(soft.getPixelAt(20, 33).getAlpha(), 0) << theme.id;
        EXPECT_GT(rich.getPixelAt(20, 33).getAlpha(), 0) << theme.id << ": a drop shadow under the disc";
        EXPECT_NE(rich.getPixelAt(20, 10), soft.getPixelAt(20, 10)) << theme.id << ": the top highlight";
    }
}

// ---- the top bar's chips ----

namespace {

// Pixels of the chip inside the glyph's box, at its edges, where the glyph's neighbours are.
std::vector<juce::Point<int>> chipMargin(const ToolbarButton& b) {
    const auto box = iconArea(b);
    const int cx = box.getCentreX(), cy = box.getCentreY();
    return {{cx, box.getY() + 1},     {cx, box.getBottom() - 2},        {box.getX() + 1, cy},
            {box.getRight() - 2, cy}, {box.getX() + 2, box.getY() + 2}, {box.getRight() - 3, box.getBottom() - 3}};
}

// Soft's chip before the Depth setting: a rounded rect of the group hue at the chip alpha, then nothing.
juce::Image referenceChip(const ToolbarButton& b, const Theme& theme, float alpha) {
    juce::Image image(juce::Image::ARGB, b.getWidth(), b.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.fillAll(theme.colors.bg0);
    g.setColour(synth::ui::toolbarGroupHue(theme, b.getGroup()).withAlpha(alpha));
    g.fillRoundedRectangle(synth::theme::toolbarChipBounds(b), synth::ui::toolbarChipSpec(b.getLayout()).chipRadius);
    return image;
}

bool insideGlyph(const ToolbarButton& b, int x, int y) { return iconArea(b).contains(x, y); }

} // namespace

TEST_F(ToolbarButtonTest, SoftChipIsTodaysChipPixelForPixelInEveryTheme) {
    for (const auto& base : synth::theme::builtInThemes()) {
        for (const bool lit : {false, true}) {
            useTheme(themeAt(base, DepthLevel::Soft));
            auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
            b.setToggleState(lit, juce::dontSendNotification);
            const auto image = render(b);
            const auto reference = referenceChip(b, theme(), lit ? 1.0f : (theme().isDark ? 0.16f : 0.10f));
            int different = 0;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    if (!insideGlyph(b, x, y) && y < (int)synth::theme::toolbarChipBounds(b).getBottom() + 3 &&
                        image.getPixelAt(x, y) != reference.getPixelAt(x, y))
                        ++different;
            EXPECT_EQ(different, 0) << base.id << (lit ? " lit" : " idle");
            buttons_.clear();
        }
    }
}

TEST_F(ToolbarButtonTest, FlatChipIsAPlainFillLikeSoft) {
    for (const auto& base : synth::theme::builtInThemes()) {
        useTheme(themeAt(base, DepthLevel::Flat));
        auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
        const auto flat = render(b);
        useTheme(themeAt(base, DepthLevel::Soft));
        EXPECT_EQ(countDifferent(flat, render(b)), 0) << base.id << ": Soft's chip is already a flat fill";
        buttons_.clear();
    }
}

TEST_F(ToolbarButtonTest, RichChipIsLitWithARingAHighlightAndAShadow) {
    for (const auto& base : synth::theme::builtInThemes()) {
        useTheme(themeAt(base, DepthLevel::Soft));
        auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
        const auto soft = render(b);
        useTheme(themeAt(base, DepthLevel::Rich));
        const auto rich = render(b);
        const auto chip = synth::theme::toolbarChipBounds(b);
        const int cx = (int)chip.getCentreX();
        EXPECT_NE(rich.getPixelAt(cx, (int)chip.getY() + 1), soft.getPixelAt(cx, (int)chip.getY() + 1))
            << base.id << ": top highlight";
        EXPECT_NE(rich.getPixelAt((int)chip.getX(), (int)chip.getCentreY()),
                  soft.getPixelAt((int)chip.getX(), (int)chip.getCentreY()))
            << base.id << ": hue ring";
        const int below = (int)chip.getBottom() + 1;
        EXPECT_NE(rich.getPixelAt(cx, below), soft.getPixelAt(cx, below)) << base.id << ": drop shadow";
        buttons_.clear();
    }
}

TEST_F(ToolbarButtonTest, AnActiveToolGlowsBrighterThanSoftAtRich) {
    for (const auto& base : synth::theme::builtInThemes()) {
        if (!base.isDark)
            continue; // a glow of hue over a light bar reads as a tint, not brighter; its change is checked below
        useTheme(themeAt(base, DepthLevel::Soft));
        auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
        b.setToggleState(true, juce::dontSendNotification);
        const auto soft = render(b);
        useTheme(themeAt(base, DepthLevel::Rich));
        const auto rich = render(b);
        const auto chip = synth::theme::toolbarChipBounds(b);
        const int x = (int)chip.getX() - 2, y = (int)chip.getCentreY();
        EXPECT_GT(luma(rich.getPixelAt(x, y)), luma(soft.getPixelAt(x, y))) << base.id << ": glow beside the chip";
        buttons_.clear();
    }
    useTheme(themeAt(synth::theme::makeDaylight(), DepthLevel::Soft));
    auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
    b.setToggleState(true, juce::dontSendNotification);
    const auto soft = render(b);
    useTheme(themeAt(synth::theme::makeDaylight(), DepthLevel::Rich));
    const auto chip = synth::theme::toolbarChipBounds(b);
    EXPECT_NE(render(b).getPixelAt((int)chip.getX() - 2, (int)chip.getCentreY()),
              soft.getPixelAt((int)chip.getX() - 2, (int)chip.getCentreY()));
}

TEST_F(ToolbarButtonTest, TheGlyphReadsOnItsChipAtEveryLevelInEveryTheme) {
    // The ThemeSweep floor for an icon: 3:1 against what it sits on. The chip's margin as painted (ring, highlight,
    // shade included) is the worst place for it.
    for (const auto& base : synth::theme::builtInThemes())
        for (int level = 0; level < synth::theme::kDepthLevelCount; ++level)
            for (const bool lit : {false, true})
                for (const auto& spec : allToolbarIcons()) {
                    if (spec.icon == Icon::ActionSave)
                        continue; // a floppy in its own colours
                    useTheme(themeAt(base, DepthLevel::Soft));
                    auto& b = make(spec.icon, spec.group, spec.caption);
                    b.setToggleState(lit, juce::dontSendNotification);
                    const auto soft = render(b);
                    useTheme(themeAt(base, (DepthLevel)level));
                    const auto image = render(b);
                    const auto glyph = lit ? theme().colors.iconInk : synth::ui::toolbarGroupHue(theme(), spec.group);
                    for (const auto p : chipMargin(b)) {
                        const double got = contrast(glyph, image.getPixelAt(p.x, p.y));
                        const double was = contrast(glyph, soft.getPixelAt(p.x, p.y));
                        EXPECT_TRUE(got >= 3.0 || got >= was - 0.05)
                            << base.id << " level " << level << (lit ? " lit " : " idle ") << spec.caption << " at "
                            << p.x << "," << p.y << ": " << got << " (Soft " << was << ")";
                    }
                    buttons_.clear();
                }
}

// ---- framed icon buttons (card headers) ----

namespace {
juce::Image renderIconButton(synth::ui::IconButton& button, const Theme& theme, synth::theme::AppLookAndFeel& lf) {
    lf.applyTheme(theme);
    button.setSize(24, 24);
    juce::Image image(juce::Image::ARGB, 40, 40, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.fillAll(theme.colors.surface);
    g.setOrigin(8, 8);
    button.paintEntireComponent(g, false);
    return image;
}
} // namespace

TEST(IconTileDepthTest, FramedIconButtonIsSoftAtFlatAndLitAtRichWithAGlowWhenOn) {
    for (const auto& base : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lf;
        synth::ui::IconButton button("b", synth::theme::Glyph::Close, synth::ui::IconButton::Style::Framed);
        button.setLookAndFeel(&lf);
        button.setClickingTogglesState(true);
        button.setOnColour(base.colors.accent);
        for (const bool on : {false, true}) {
            button.setToggleState(on, juce::dontSendNotification);
            const auto soft = renderIconButton(button, themeAt(base, DepthLevel::Soft), lf);
            const auto flat = renderIconButton(button, themeAt(base, DepthLevel::Flat), lf);
            const auto rich = renderIconButton(button, themeAt(base, DepthLevel::Rich), lf);
            EXPECT_EQ(countDifferent(soft, flat), 0) << base.id << ": Flat's tile is Soft's plain tile";
            EXPECT_GT(countDifferent(soft, rich), 0) << base.id << ": Rich lights the tile";
            // The button clips its own paint, so the shadow and the glow show in the 1px margin around the tile.
            if (!on)
                EXPECT_LT(luma(rich.getPixelAt(20, 8 + 23)), luma(soft.getPixelAt(20, 8 + 23)))
                    << base.id << ": shadow";
            if (on && base.isDark)
                EXPECT_GT(luma(rich.getPixelAt(8, 20)), luma(soft.getPixelAt(8, 20))) << base.id << ": glow";
        }
        button.setLookAndFeel(nullptr);
    }
}

// The sibling-gap guard (bug kind sibling-gap): every surface that paints an icon tile goes through the shared tile
// painter, so a new tile cannot ship with Soft only. The toolbar chip and the framed icon button call it directly; a
// severity disc caller passes the theme so the disc follows the level (the alert icon painter is the one exempt: it
// calls the disc without a theme for its Soft look and with one for Rich, AlertIconDepthTests.cpp).
TEST(IconTileDepthTest, EveryIconTilePainterUsesTheSharedTilePainter) {
    const std::map<std::string, std::string> discExempt = {
        {"Source/UI/Layout/AlertIconPainter.cpp",
         "the alert icon picks the level itself (Soft: no theme, Rich: the theme)"},
    };
    int tilePainters = 0, discCallers = 0;
    const std::regex discCall(R"(paintSeverityDisc\s*\([^;]*;)");
    for (const auto& [path, text] : synth::test::source_scan::sourceTexts()) {
        if (path.size() < 4 || path.substr(path.size() - 4) != ".cpp")
            continue;
        if (path.find("AppLookAndFeelToolbarButton.cpp") != std::string::npos ||
            path.find("AppLookAndFeelButtons.cpp") != std::string::npos) {
            ++tilePainters;
            EXPECT_NE(text.find("paintIconTileUnder"), std::string::npos) << path;
            EXPECT_NE(text.find("paintIconTileOver"), std::string::npos) << path;
        }
        std::smatch m;
        if (path == "Source/UI/Layout/ActionGlyphs.cpp" || !std::regex_search(text, m, discCall) ||
            discExempt.count(path) != 0)
            continue;
        ++discCallers;
        EXPECT_NE(m[0].str().find("heme"), std::string::npos)
            << path << ": paintSeverityDisc is called without the theme, so the disc ignores the Depth level";
    }
    EXPECT_EQ(tilePainters, 2);
    EXPECT_GE(discCallers, 1) << "the toast";
}

// ---- Rich keeps every tile's shape (kind look-level-reshapes) ----

TEST(IconTileDepthTest, RichSeverityDiscKeepsSoftsShapeInEveryTheme) {
    for (const auto& base : synth::theme::builtInThemes()) {
        const auto softTheme = themeAt(base, DepthLevel::Soft);
        const auto richTheme = themeAt(base, DepthLevel::Rich);
        const auto c = kDiscArea.getCentre();
        const float inner = kDiscSide * 0.5f - (float)depthshape::kRimBand;
        depthshape::Surface surface{
            "toast disc", juce::Rectangle<float>(12.0f, 12.0f).withCentre(c).toNearestInt(),
            [c, inner](int x, int y) { return std::hypot((float)x + 0.5f - c.x, (float)y + 0.5f - c.y) < inner; }};
        depthshape::expectSameShape(paintDisc(&softTheme), paintDisc(&richTheme), surface, base.id);
    }
}

TEST_F(ToolbarButtonTest, RichChipKeepsSoftsShapeInEveryThemeIdleAndLit) {
    for (const auto& base : synth::theme::builtInThemes())
        for (const bool lit : {false, true}) {
            useTheme(themeAt(base, DepthLevel::Soft));
            auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
            b.setToggleState(lit, juce::dontSendNotification);
            const auto soft = render(b);
            useTheme(themeAt(base, DepthLevel::Rich));
            const auto rich = render(b);
            const auto body = synth::theme::toolbarChipBounds(b).reduced((float)depthshape::kRimBand);
            depthshape::Surface surface{
                "top-bar chip", iconArea(b).getIntersection(body.toNearestInt()),
                [body](int x, int y) { return body.contains((float)x + 0.5f, (float)y + 0.5f); }};
            depthshape::expectSameShape(soft, rich, surface, base.id + (lit ? " lit" : " idle"));
            buttons_.clear();
        }
}

TEST(IconTileDepthTest, RichFramedAndLitIconButtonsKeepSoftsShapeInEveryTheme) {
    using Style = synth::ui::IconButton::Style;
    for (const auto& base : synth::theme::builtInThemes())
        for (const Style style : {Style::Framed, Style::Bare})
            for (const bool on : {false, true}) {
                synth::theme::AppLookAndFeel lf;
                synth::ui::IconButton button("b", synth::theme::Glyph::Close, style);
                button.setLookAndFeel(&lf);
                button.setClickingTogglesState(true);
                if (style == Style::Framed)
                    button.setOnColour(base.colors.accent);
                button.setToggleState(on, juce::dontSendNotification);
                const auto soft = renderIconButton(button, themeAt(base, DepthLevel::Soft), lf);
                const auto rich = renderIconButton(button, themeAt(base, DepthLevel::Rich), lf);
                const auto body = juce::Rectangle<float>(8.0f, 8.0f, 24.0f, 24.0f).reduced((float)depthshape::kRimBand);
                depthshape::Surface surface{style == Style::Framed ? "framed icon button" : "lit tool button",
                                            juce::Rectangle<int>(20, 20, 0, 0).expanded(7), [body](int x, int y) {
                                                return body.contains((float)x + 0.5f, (float)y + 0.5f);
                                            }};
                depthshape::expectSameShape(soft, rich, surface, base.id + (on ? " on" : " off"));
                button.setLookAndFeel(nullptr);
            }
}

// Registry (kind look-level-reshapes): every file that paints an icon tile through the shared tile painter is listed
// here with the test above that holds its shape. A new Depth-aware painter that is not listed fails this, so it cannot
// ship without joining the Soft-vs-Rich guard. The alert icon paints through the severity disc (ActionGlyphs.cpp) and
// holds its own shape in AlertIconDepthTest.RichAlertIconKeepsSoftsShapeInEveryThemeAndGlyph.
TEST(IconTileDepthTest, EveryDepthAwareTilePainterIsRegisteredInTheSameShapeGuard) {
    const std::map<std::string, std::string> registered = {
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeelToolbarButton.cpp",
         "RichChipKeepsSoftsShapeInEveryThemeIdleAndLit"},
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeelButtons.cpp",
         "RichFramedAndLitIconButtonsKeepSoftsShapeInEveryTheme"},
        {"Source/UI/Layout/ActionGlyphs.cpp", "RichSeverityDiscKeepsSoftsShapeInEveryTheme"},
    };
    int found = 0;
    for (const auto& [path, text] : synth::test::source_scan::sourceTexts()) {
        if (path.size() < 4 || path.substr(path.size() - 4) != ".cpp" ||
            path.find("Source/UI/Theme/IconTileDepth.cpp") != std::string::npos)
            continue;
        if (text.find("paintIconTileUnder(") == std::string::npos &&
            text.find("paintIconTileOver(") == std::string::npos)
            continue;
        ++found;
        EXPECT_EQ(registered.count(path), 1u)
            << path
            << " paints a Depth-aware icon tile but is not registered in the same-shape guard (DepthSameShape.h)";
    }
    EXPECT_EQ(found, (int)registered.size());
}
