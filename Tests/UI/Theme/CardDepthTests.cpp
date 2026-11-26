// CardDepthTests.cpp -- what a card paints at each Depth level (docs/layout/theming.md#depth-levels).
//
// Soft is today's card: ReferenceSoftPanel below is a copy of AppLookAndFeel::drawModulePanel as it was before the
// Depth setting reached cards, and Soft must match it pixel for pixel in every built-in theme. Flat only drops light
// and shade; Rich adds a lighter rim on the top edge, a deeper inner shade, the accent seam, a stronger shadow.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "../../TestWait.h"
#include "../SourceScan.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardOutline.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/CardDepth.h"
#include "UI/Theme/DepthLevel.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <regex>
#include <string>

namespace {

using synth::theme::DepthLevel;
using synth::theme::Theme;

constexpr int kHeaderHeight = 24;
constexpr int kW = 200;
constexpr int kH = 120;

// The pre-change drawModulePanel (unselected, not bypassed), kept here as the golden for Soft.
void referenceSoftPanel(juce::Graphics& g, juce::Rectangle<float> bounds, const Theme& theme,
                        const juce::String& title) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;
    const float radius = m.cornerRadius;
    auto body = synth::ui::cardBodyRect(bounds);
    if (tr.shadow > 0.0f) {
        for (int i = 3; i >= 1; --i) {
            const float grow = (float)i * 1.5f;
            g.setColour(juce::Colours::black.withAlpha(tr.shadow * 0.13f / (float)i));
            g.fillRoundedRectangle(body.expanded(grow).translated(0.0f, 2.0f + (float)i), radius + grow);
        }
    }
    const auto surfaceCol = c.surface;
    const auto surfaceHiCol = c.surfaceHi;
    {
        juce::Path bodyPath;
        bodyPath.addRoundedRectangle(body, radius);
        g.saveState();
        g.reduceClipRegion(bodyPath);
        if (tr.style == synth::theme::ThemeStyle::Glass) {
            g.setColour(surfaceCol);
            g.fillRect(body);
            const float hAlpha = 0.05f * tr.blur + 0.05f;
            juce::ColourGradient grad(juce::Colours::white.withAlpha(hAlpha), body.getX(), body.getY(),
                                      juce::Colours::transparentWhite, body.getX(),
                                      body.getY() + body.getHeight() * 0.4f, false);
            g.setGradientFill(grad);
            g.fillRect(body);
        } else if (tr.style == synth::theme::ThemeStyle::Textured) {
            juce::ColourGradient grad(surfaceHiCol, body.getX(), body.getY(), surfaceCol.darker(0.1f), body.getX(),
                                      body.getBottom(), false);
            grad.addColour(0.5, surfaceCol);
            g.setGradientFill(grad);
            g.fillRect(body);
            if (tr.texture > 0.0f) {
                const int step = 3;
                for (int xi = (int)body.getX(); xi < (int)body.getRight(); xi += step) {
                    g.setColour(((xi / step) % 2 == 0) ? juce::Colours::white.withAlpha(tr.texture * 0.025f)
                                                       : juce::Colours::black.withAlpha(tr.texture * 0.03f));
                    g.drawVerticalLine(xi, body.getY(), body.getBottom());
                }
            }
        } else {
            juce::ColourGradient grad(surfaceHiCol, body.getX(), body.getY(), surfaceCol, body.getX(), body.getBottom(),
                                      false);
            g.setGradientFill(grad);
            g.fillRect(body);
        }
        g.restoreState();
    }
    {
        auto header = body.withHeight((float)kHeaderHeight);
        juce::Path headerPath;
        headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius,
                                       radius, true, true, false, false);
        if (tr.style == synth::theme::ThemeStyle::Glass)
            g.setColour(juce::Colours::white.withAlpha(0.05f));
        else
            g.setColour(surfaceHiCol);
        g.fillPath(headerPath);
        g.setColour(c.border);
        g.drawHorizontalLine((int)header.getBottom(), header.getX(), header.getRight());
        g.setColour(c.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(theme.type.h2, juce::Font::bold)));
        g.drawText(title, header.withTrimmedLeft(22.0f).withTrimmedRight(10.0f).toNearestInt(),
                   juce::Justification::centredLeft, true);
    }
    g.setColour(c.border);
    g.drawRoundedRectangle(body, radius, m.borderWidth);
    if (tr.style == synth::theme::ThemeStyle::Glass) {
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine((int)body.getY() + 1, body.getX() + radius, body.getRight() - radius);
    }
}

Theme themeAt(Theme theme, DepthLevel level) {
    theme.treatment.depth = level;
    return theme;
}

juce::Image paintCard(const Theme& theme, const juce::String& title = "Filter") {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(theme);
    juce::Image image(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    lf.drawModulePanel(g, {0.0f, 0.0f, (float)kW, (float)kH}, kHeaderHeight, title, false, false);
    return image;
}

juce::Image paintReference(const Theme& theme, const juce::String& title = "Filter") {
    juce::Image image(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    referenceSoftPanel(g, {0.0f, 0.0f, (float)kW, (float)kH}, theme, title);
    return image;
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
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

TEST(CardDepthTest, SoftIsTodaysCardPixelForPixelInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto soft = themeAt(theme, DepthLevel::Soft);
        EXPECT_EQ(countDifferent(paintCard(soft), paintReference(soft)), 0) << theme.id;
    }
}

TEST(CardDepthTest, FlatDropsLightAndShadeAndDiffersFromSoft) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto flat = paintCard(themeAt(theme, DepthLevel::Flat));
        const auto soft = paintCard(themeAt(theme, DepthLevel::Soft));
        EXPECT_GT(countDifferent(flat, soft), 0) << theme.id << ": Flat is a different look from Soft";

        // No drop shadow: the 2px margin outside the body below the card is untouched, unlike Soft's.
        const int below = kH - 1;
        EXPECT_EQ(flat.getPixelAt(kW / 2, below).getAlpha(), 0) << theme.id << ": no shadow under a Flat card";
        EXPECT_GT(soft.getPixelAt(kW / 2, below).getAlpha(), 0) << theme.id << ": Soft keeps its shadow";

        // A plain fill: the body below the header is one colour top to bottom (no gradient, sheen or striations).
        const int xs[] = {kW / 3, kW / 2, kW - 40};
        const auto top = flat.getPixelAt(xs[0], kHeaderHeight + 8);
        for (int y = kHeaderHeight + 8; y < kH - 6; y += 7)
            for (int x : xs)
                EXPECT_EQ(flat.getPixelAt(x, y), top) << theme.id << " at " << x << "," << y;
    }
}

TEST(CardDepthTest, RichHasALighterRimOnTheTopOnlyOnDarkThemes) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        if (!theme.isDark)
            continue;
        const auto rich = paintCard(themeAt(theme, DepthLevel::Rich));
        const auto soft = paintCard(themeAt(theme, DepthLevel::Soft));
        const int topRimY = 3; // body is inset 2: the rim is the first row inside the border
        const int leftRimX = 3;
        EXPECT_GT(luma(rich.getPixelAt(kW / 2, topRimY)), luma(soft.getPixelAt(kW / 2, topRimY))) << theme.id;
        // No rim on the left or the right edge: a line inside one edge only made the left jacks read closer to the edge
        // than the right ones (JackInsetSweep). (A translucent glass body shows the stronger Rich shadow through it, so
        // only an opaque body can be compared pixel for pixel.)
        if (theme.colors.surface.isOpaque()) {
            EXPECT_EQ(rich.getPixelAt(leftRimX, kH / 2), soft.getPixelAt(leftRimX, kH / 2)) << theme.id;
            EXPECT_EQ(rich.getPixelAt(kW - 4, kH / 2), soft.getPixelAt(kW - 4, kH / 2)) << theme.id;
        }
    }
}

TEST(CardDepthTest, RichShadesTheBottomDeeperAndCastsAStrongerShadow) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto rich = paintCard(themeAt(theme, DepthLevel::Rich));
        const auto soft = paintCard(themeAt(theme, DepthLevel::Soft));
        EXPECT_LT(luma(rich.getPixelAt(kW / 2, kH - 5)), luma(soft.getPixelAt(kW / 2, kH - 5)))
            << theme.id << ": deeper inner shade";
        EXPECT_GT(rich.getPixelAt(kW / 2, kH - 1).getAlpha(), soft.getPixelAt(kW / 2, kH - 1).getAlpha())
            << theme.id << ": stronger drop shadow";
        // The shade leaves the middle of the card alone.
        if (theme.colors.surface.isOpaque())
            EXPECT_EQ(rich.getPixelAt(kW / 2, kH / 2 - 10), soft.getPixelAt(kW / 2, kH / 2 - 10)) << theme.id;
    }
}

TEST(CardDepthTest, RichAccentSeamUnderTheHeaderOnDarkThemesAndNoneOnLight) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto rich = paintCard(themeAt(theme, DepthLevel::Rich));
        const auto soft = paintCard(themeAt(theme, DepthLevel::Soft));
        const int seamY = 2 + kHeaderHeight + 1;
        const auto seam = rich.getPixelAt(kW / 2, seamY);
        const auto plain = soft.getPixelAt(kW / 2, seamY);
        if (theme.isDark) {
            EXPECT_NE(seam, plain) << theme.id;
            // It leans toward the accent: closer to it than the plain card is.
            const auto distance = [&](juce::Colour c) {
                return std::abs((int)c.getRed() - (int)theme.colors.accent.getRed()) +
                       std::abs((int)c.getGreen() - (int)theme.colors.accent.getGreen()) +
                       std::abs((int)c.getBlue() - (int)theme.colors.accent.getBlue());
            };
            EXPECT_LT(distance(seam), distance(plain)) << theme.id;
        } else {
            EXPECT_EQ(seam, plain) << theme.id << ": Daylight Rich stays neutral, no accent wash";
        }
    }
}

TEST(CardDepthTest, DaylightRichAddsNoColourCast) {
    const auto rich = paintCard(themeAt(synth::theme::makeDaylight(), DepthLevel::Rich));
    const auto soft = paintCard(themeAt(synth::theme::makeDaylight(), DepthLevel::Soft));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const auto a = rich.getPixelAt(x, y), b = soft.getPixelAt(x, y);
            if (a == b)
                continue;
            // Every changed pixel is a tint of grey/blue-grey: the red channel never exceeds blue (no orange).
            EXPECT_LE((int)a.getRed(), (int)a.getBlue() + 2) << x << "," << y;
        }
}

TEST(CardDepthTest, TheTitleStaysReadableAtEveryLevelInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes())
        for (int i = 0; i < synth::theme::kDepthLevelCount; ++i) {
            const auto t = themeAt(theme, (DepthLevel)i);
            const auto image = paintCard(t);
            // The header band as painted (rim, seam and shade included) against the title colour, composited on the
            // page.
            for (int x : {10, 60, kW / 2, kW - 12})
                for (int y : {3, 4, 12, 22, 26}) {
                    const auto bg = t.colors.bg0.withAlpha(1.0f).overlaidWith(image.getPixelAt(x, y));
                    EXPECT_GE(contrast(t.colors.textPrimary, bg), 4.5)
                        << t.id << " level " << i << " at " << x << "," << y;
                }
        }
}

TEST(CardDepthTest, MacroAndMixerChromeUseTheSameRichLightAndNothingBelowRich) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        for (int i = 0; i < synth::theme::kDepthLevelCount; ++i) {
            const auto t = themeAt(theme, (DepthLevel)i);
            juce::Image image(juce::Image::ARGB, 80, 60, true, juce::SoftwareImageType());
            juce::Graphics g(image);
            g.fillAll(t.colors.surface);
            const auto before = image.createCopy();
            synth::theme::paintCardRichLight(g, {0.0f, 0.0f, 80.0f, 60.0f}, 0.0f, t);
            if (t.treatment.depth == DepthLevel::Rich)
                EXPECT_GT(countDifferent(before, image), 0) << t.id;
            else
                EXPECT_EQ(countDifferent(before, image), 0) << t.id << ": only Rich paints the light";
        }
    }
}

// The sibling-gap guard: a card that paints its own chrome must go through the Depth painter, so a new card kind (or a
// column) cannot ship with Soft only. Discovery is by class name; a class that is not a card chrome is exempt with a
// reason.
TEST(CardDepthTest, EveryCardAndColumnPainterUsesTheDepthPainter) {
    const std::map<std::string, std::string> exempt = {
        {"MixerColumnHeader", "the header strip inside a column, not a card"},
    };
    const std::regex painter(R"(\b(Mixer\w*Column\w*|\w*CardComponent)::paint\s*\(\s*juce::Graphics)");
    int found = 0;
    for (const auto& [path, text] : synth::test::source_scan::sourceTexts()) {
        std::smatch m;
        if (path.size() < 4 || path.substr(path.size() - 4) != ".cpp" || !std::regex_search(text, m, painter))
            continue;
        if (exempt.count(m[1].str()) != 0)
            continue;
        ++found;
        EXPECT_NE(text.find("paintCardRichLight"), std::string::npos)
            << path << ": " << m[1].str() << "::paint draws card chrome without the Depth painter (CardDepth.h)";
    }
    EXPECT_GE(found, 4) << "the macro card and the three mixer columns";
}

// A card never moves under the pointer at any level (a card that shifts on hover also shifts its hit
// area, so the pointer at its bottom edge flickered between hovered and not). The scan covers the module card and the
// macro card, the two cards that were lifted; the next test hovers a real module card at every level.
TEST(CardDepthTest, NoCardMovesOnHoverAtAnyLevel) {
    int found = 0;
    for (const auto& [path, text] : synth::test::source_scan::sourceTexts()) {
        const bool module = path.find("ModuleComponent/ModuleComponent.cpp") != std::string::npos;
        const bool macro = path.find("MacroCardComponent/MacroCardComponent.cpp") != std::string::npos;
        const bool lift = path.find("CardHoverLift") != std::string::npos;
        EXPECT_FALSE(lift) << path << ": the hover lift was removed, do not bring it back";
        if (!module && !macro)
            continue;
        ++found;
        const auto code = synth::test::source_scan::stripComments(text);
        EXPECT_EQ(code.find("CardHoverLift"), std::string::npos) << path;
        EXPECT_EQ(code.find("setTransform"), std::string::npos) << path << ": a card never moves itself on hover";
    }
    EXPECT_EQ(found, 2);
}

TEST(CardDepthTest, AHoveredModuleCardKeepsItsTransformAndBoundsAtEveryLevel) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto processor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(processor, nullptr);
    ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    card.setBounds(10, 10, 260, 180);
    for (const auto level : {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich}) {
        synth::theme::AppLookAndFeel lf;
        auto theme = synth::theme::makeObsidian();
        theme.treatment.depth = level;
        lf.applyTheme(theme);
        card.setLookAndFeel(&lf);
        const auto before = card.getBounds();
        const auto transformBefore = card.getTransform();
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const juce::Point<float> pos(130.0f, 178.0f);
        const juce::MouseEvent event(source, pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &card, &card,
                                     juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 0, false);
        card.mouseEnter(event);
        card.mouseMove(event);
        synth::test::flushMessageQueue();
        EXPECT_EQ(card.getBounds(), before) << (int)level;
        EXPECT_TRUE(card.getTransform() == transformBefore) << (int)level << ": hovering moves no card";
        card.mouseExit(event);
        card.setLookAndFeel(nullptr);
    }
}

// A selected card's glow stays inside the card's bounds and the Glass highlight line is not drawn under the outline.
// Regression test for FRO2365: the glow was stroked past the bounds and cut off square at the rounded corner, and the
// white line under the top edge showed as a grey stripe beneath the accent outline.
TEST(CardDepthTest, ASelectedCardPaintsNothingOnTheOuterPixelOfItsBoundsAndNoHighlightLine) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        for (const auto level : {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich}) {
            const auto t = themeAt(theme, level);
            synth::theme::AppLookAndFeel lf;
            lf.applyTheme(t);
            const auto render = [&lf](bool selected) {
                juce::Image image(juce::Image::ARGB, kW, kH, true, juce::SoftwareImageType());
                juce::Graphics g(image);
                lf.drawModulePanel(g, {0.0f, 0.0f, (float)kW, (float)kH}, kHeaderHeight, "Filter", selected, false);
                return image;
            };
            const auto plain = render(false);
            const auto selected = render(true);
            // Selecting adds the accent outline inside the body and a halo, never anything on the bounds' own edge
            // pixels (that is where a halo wider than the 2 px inset is cut off).
            for (int i = 0; i < kW; ++i) {
                EXPECT_EQ(selected.getPixelAt(i, 0), plain.getPixelAt(i, 0)) << theme.id << " top " << i;
                EXPECT_EQ(selected.getPixelAt(i, kH - 1), plain.getPixelAt(i, kH - 1)) << theme.id << " bottom " << i;
            }
            for (int j = 0; j < kH; ++j) {
                EXPECT_EQ(selected.getPixelAt(0, j), plain.getPixelAt(0, j)) << theme.id << " left " << j;
                EXPECT_EQ(selected.getPixelAt(kW - 1, j), plain.getPixelAt(kW - 1, j)) << theme.id << " right " << j;
            }
            EXPECT_FALSE(synth::theme::cardPaintsTopHighlight(t, true)) << theme.id;
            EXPECT_EQ(synth::theme::cardPaintsTopHighlight(t, false),
                      t.treatment.style == synth::theme::ThemeStyle::Glass && level != DepthLevel::Flat)
                << theme.id;
        }
    }
}
