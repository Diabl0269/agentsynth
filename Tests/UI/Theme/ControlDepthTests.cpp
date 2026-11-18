// ControlDepthTests.cpp -- what a knob cap and a fader cap paint at each Depth level
// (docs/layout/theming.md#depth-levels).
//
// Soft is today's control: ControlDepthReference.h holds the painters as they were before the Depth setting reached
// knobs and faders, and Soft must match them pixel for pixel in every built-in theme (every knob style, every fader
// style and size). Flat drops the contact shadow and the bevel, rim and highlight; Rich adds a rim light on the top
// left and a deeper, larger shadow. A fader cap and a knob cap share the same numbers (ControlDepth), checked per
// level.
//
// Images use SoftwareImageType(): the default native image type reads back zeros on a Windows runner.

#include "../SourceScan.h"
#include "ControlDepthReference.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ControlDepth.h"
#include "UI/Theme/DepthLevel.h"
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <string>

namespace {

using namespace synth::theme;

const KnobStyle kAllStyles[] = {KnobStyle::Classic, KnobStyle::Hardware, KnobStyle::Analog, KnobStyle::Neon,
                                KnobStyle::Ring};
const DepthLevel kLevels[] = {DepthLevel::Flat, DepthLevel::Soft, DepthLevel::Rich};

Theme themeAt(Theme theme, DepthLevel level) {
    theme.treatment.depth = level;
    return theme;
}

juce::Image makeImage(int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType()); }

int countDifferent(const juce::Image& a, const juce::Image& b) {
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            n += a.getPixelAt(x, y) != b.getPixelAt(x, y) ? 1 : 0;
    return n;
}

// Within rounding: premultiplied pixels land a unit off a token colour.
int channelDistance(juce::Colour a, juce::Colour b) {
    return std::abs((int)a.getRed() - (int)b.getRed()) + std::abs((int)a.getGreen() - (int)b.getGreen()) +
           std::abs((int)a.getBlue() - (int)b.getBlue());
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

// ---- knobs ----

juce::Image renderKnob(const Theme& theme, KnobStyle style, int size, float pos, float origin = 0.0f) {
    auto image = makeImage(size, size);
    juce::Graphics g(image);
    paintKnob(g, theme, style, juce::Rectangle<float>(0, 0, (float)size, (float)size), pos, theme.colors.accent,
              origin);
    return image;
}

// What paintKnob drew before the Depth setting. Neon and Ring have no cap and never changed.
juce::Image renderReferenceKnob(const Theme& theme, KnobStyle style, int size, float pos, float origin) {
    auto image = makeImage(size, size);
    juce::Graphics g(image);
    const auto bounds = juce::Rectangle<float>(0, 0, (float)size, (float)size);
    const auto geo = knobs::makeGeo(theme, bounds, pos, origin);
    switch (style) {
    case KnobStyle::Classic:
        refknobs::paintClassicRef(g, geo, theme, theme.colors.accent);
        break;
    case KnobStyle::Hardware:
        refknobs::paintChunky_ref::paint(g, geo, theme, theme.colors.accent);
        break;
    case KnobStyle::Analog:
        refknobs::paintAnalog_ref::paint(g, geo, theme);
        break;
    case KnobStyle::Neon:
        knobs::paintNeon(g, geo, theme, theme.colors.accent);
        break;
    case KnobStyle::Ring:
        knobs::paintRing(g, geo, theme, theme.colors.accent);
        break;
    }
    return image;
}

// The lowest row of the knob's body in the centre column, read off the Flat knob (which has no shadow below it).
int bodyBottom(const juce::Image& flat) {
    const int cx = flat.getWidth() / 2;
    int bottom = flat.getHeight() / 2;
    for (int y = flat.getHeight() / 2; y < flat.getHeight(); ++y)
        if (flat.getPixelAt(cx, y).getAlpha() > 0)
            bottom = y;
    return bottom;
}

// The strongest and the longest shadow in the column under the knob, `from` rows below its body.
struct Shadow {
    int strongest = 0;
    int rows = 0;
};
Shadow shadowBelow(const juce::Image& image, int bottom) {
    Shadow s;
    const int cx = image.getWidth() / 2;
    for (int y = bottom + 1; y < image.getHeight(); ++y) {
        const int a = image.getPixelAt(cx, y).getAlpha();
        s.strongest = std::max(s.strongest, a);
        s.rows += a > 0 ? 1 : 0;
    }
    return s;
}

// Pixels that differ between two knobs inside radius `r` of the centre, split into the top-left and bottom-right halves
// along the light's diagonal.
struct Halves {
    int topLeft = 0;
    int bottomRight = 0;
};
Halves differenceHalves(const juce::Image& a, const juce::Image& b, const juce::Image& flat, float r) {
    Halves h;
    const float c = (float)a.getWidth() * 0.5f;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const float dx = (float)x + 0.5f - c, dy = (float)y + 0.5f - c;
            // Only where the flat knob is opaque: an edge pixel shows the shadow, which Rich deepens on every side.
            if (std::hypot(dx, dy) > r || flat.getPixelAt(x, y).getAlpha() < 255 ||
                a.getPixelAt(x, y) == b.getPixelAt(x, y))
                continue;
            (dx + dy < 0.0f ? h.topLeft : h.bottomRight)++;
        }
    return h;
}

} // namespace

TEST(ControlDepthTest, SoftKnobsAreTodaysKnobsPixelForPixelInEveryThemeAndStyle) {
    for (const auto& base : builtInThemes()) {
        const auto theme = themeAt(base, DepthLevel::Soft);
        for (const auto style : kAllStyles)
            for (const int size : {54, 96})
                for (const auto [pos, origin] : {std::pair{0.62f, 0.0f}, std::pair{0.2f, 0.5f}})
                    EXPECT_EQ(countDifferent(renderKnob(theme, style, size, pos, origin),
                                             renderReferenceKnob(theme, style, size, pos, origin)),
                              0)
                        << base.id << " style " << (int)style << " size " << size << " pos " << pos;
    }
}

TEST(ControlDepthTest, NeonAndRingHaveNoCapAndLookTheSameAtEveryLevel) {
    for (const auto& base : builtInThemes())
        for (const auto style : {KnobStyle::Neon, KnobStyle::Ring})
            for (const auto level : kLevels)
                EXPECT_EQ(countDifferent(renderKnob(themeAt(base, level), style, 54, 0.62f),
                                         renderKnob(themeAt(base, DepthLevel::Soft), style, 54, 0.62f)),
                          0)
                    << base.id << " style " << (int)style;
}

TEST(ControlDepthTest, FlatKnobHasNoShadowNoGradientAndNoBevel) {
    for (const auto& base : builtInThemes())
        for (const auto style : {KnobStyle::Classic, KnobStyle::Hardware, KnobStyle::Analog}) {
            const auto flat = renderKnob(themeAt(base, DepthLevel::Flat), style, 96, 0.5f);
            const auto soft = renderKnob(themeAt(base, DepthLevel::Soft), style, 96, 0.5f);
            const int bottom = bodyBottom(flat);
            EXPECT_EQ(shadowBelow(flat, bottom).rows, 0) << base.id << " style " << (int)style;
            EXPECT_GT(shadowBelow(soft, bottom).strongest, 0)
                << base.id << " style " << (int)style << ": Soft's shadow";
            EXPECT_GT(countDifferent(flat, soft), 0) << base.id << " style " << (int)style;
        }
    // Classic's cap is one plain surfaceHi fill: the top-left and bottom-right of the face read the same, where
    // Soft's gradient and bevel make them differ.
    for (const auto& base : builtInThemes()) {
        const float r = 96.0f * 0.26f - 3.0f;
        const auto at = [&](const juce::Image& image, float sign) {
            const float k = std::sqrt(0.5f) * r * sign;
            return image.getPixelAt((int)(48.0f + k), (int)(48.0f + k));
        };
        const auto flat = renderKnob(themeAt(base, DepthLevel::Flat), KnobStyle::Classic, 96, 0.5f);
        const auto soft = renderKnob(themeAt(base, DepthLevel::Soft), KnobStyle::Classic, 96, 0.5f);
        EXPECT_LE(channelDistance(at(flat, -1.0f), base.colors.surfaceHi), 3) << base.id;
        EXPECT_LE(channelDistance(at(flat, 1.0f), base.colors.surfaceHi), 3) << base.id;
        EXPECT_NE(at(soft, -1.0f), at(soft, 1.0f)) << base.id;
    }
}

TEST(ControlDepthTest, RichKnobAddsARimLightOnTheTopLeftAndADeeperLargerShadow) {
    for (const auto& base : builtInThemes())
        for (const auto style : {KnobStyle::Classic, KnobStyle::Hardware, KnobStyle::Analog}) {
            const auto flat = renderKnob(themeAt(base, DepthLevel::Flat), style, 96, 0.5f);
            const auto soft = renderKnob(themeAt(base, DepthLevel::Soft), style, 96, 0.5f);
            const auto rich = renderKnob(themeAt(base, DepthLevel::Rich), style, 96, 0.5f);
            const int bottom = bodyBottom(flat);
            const auto s = shadowBelow(soft, bottom), r = shadowBelow(rich, bottom);
            EXPECT_GT(r.strongest, s.strongest) << base.id << " style " << (int)style << ": deeper";
            EXPECT_GE(r.rows, s.rows) << base.id << " style " << (int)style << ": larger";
            // The rim is on the body, so look inside it: the light is on the top left, not the bottom right.
            const auto halves = differenceHalves(rich, soft, flat, (float)(bottom - 48));
            EXPECT_GT(halves.topLeft, 0) << base.id << " style " << (int)style;
            EXPECT_GT(halves.topLeft, halves.bottomRight) << base.id << " style " << (int)style << ": one light";
        }
}

// ---- faders ----

namespace {

struct FaderCase {
    bool vertical;
    int w, h;
};
const FaderCase kFaderCases[] = {{true, 80, 160}, {true, 40, 80}, {false, 120, 24}};

juce::Image renderFader(const Theme& theme, const FaderCase& c, KnobStyle style, bool hover, bool dragging, bool ref) {
    auto image = makeImage(c.w, c.h);
    juce::Graphics g(image);
    const auto m = fader::metricsFor(c.vertical, c.w, c.h);
    const int inset = fader::thumbRadiusFor(c.vertical, c.w, c.h);
    const auto travel = c.vertical ? juce::Rectangle<int>(0, inset, c.w, c.h - 2 * inset)
                                   : juce::Rectangle<int>(inset, 0, c.w - 2 * inset, c.h);
    const float pos =
        c.vertical ? (float)(travel.getY() + travel.getHeight() / 2) : (float)(travel.getX() + travel.getWidth() / 2);
    (void)m;
    fader::State state;
    state.style = style;
    state.hover = hover;
    state.dragging = dragging;
    state.valueColour = theme.colors.accent;
    if (ref)
        reffader::paint(g, theme, travel, {c.w, c.h}, pos, c.vertical, state);
    else
        fader::paint(g, theme, travel, {c.w, c.h}, pos, c.vertical, state);
    return image;
}

} // namespace

TEST(ControlDepthTest, SoftFaderIsTodaysFaderPixelForPixelInEveryThemeStyleAndSize) {
    for (const auto& base : builtInThemes()) {
        const auto theme = themeAt(base, DepthLevel::Soft);
        for (const auto style : kAllStyles)
            for (const auto& c : kFaderCases)
                for (const auto [hover, dragging] :
                     {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}})
                    EXPECT_EQ(countDifferent(renderFader(theme, c, style, hover, dragging, false),
                                             renderFader(theme, c, style, hover, dragging, true)),
                              0)
                        << base.id << " style " << (int)style << " " << c.w << "x" << c.h;
    }
}

TEST(ControlDepthTest, RingFaderPointerLooksTheSameAtEveryLevel) {
    for (const auto& base : builtInThemes())
        for (const auto level : kLevels)
            for (const auto& c : kFaderCases)
                EXPECT_EQ(countDifferent(
                              renderFader(themeAt(base, level), c, KnobStyle::Ring, false, false, false),
                              renderFader(themeAt(base, DepthLevel::Soft), c, KnobStyle::Ring, false, false, false)),
                          0)
                    << base.id;
}

namespace {

// The large vertical fader's cap at its middle: its box, and a column clear of the slot where the shadow can be read.
struct CapProbe {
    juce::Rectangle<int> cap;
};
CapProbe largeCap() {
    const FaderCase c{true, 80, 160};
    const auto m = fader::metricsFor(true, c.w, c.h);
    const int inset = fader::thumbRadiusFor(true, c.w, c.h);
    const int centreY = inset + (c.h - 2 * inset) / 2;
    return {juce::Rectangle<int>((int)(c.w / 2 - m.capW / 2), (int)(centreY - m.capH / 2), (int)m.capW, (int)m.capH)};
}

int faderShadowStrength(const juce::Image& image, const CapProbe& p) {
    int strongest = 0;
    const int x = p.cap.getCentreX() + 10;
    for (int y = p.cap.getBottom(); y < p.cap.getBottom() + 5; ++y)
        strongest = std::max(strongest, (int)image.getPixelAt(x, y).getAlpha());
    return strongest;
}

// The same measure on a knob: the strongest shadow in the column under its body.
int knobShadowStrength(const juce::Image& image, int bottom) { return shadowBelow(image, bottom).strongest; }

} // namespace

TEST(ControlDepthTest, AFaderCapAndAKnobCapShareTheSameLightAtEveryLevel) {
    const auto probe = largeCap();
    for (const auto& base : builtInThemes()) {
        const FaderCase fc{true, 80, 160};
        const auto flatKnob = renderKnob(themeAt(base, DepthLevel::Flat), KnobStyle::Classic, 96, 0.5f);
        const int bottom = bodyBottom(flatKnob);
        std::map<int, int> faderShadow, knobShadow;
        std::map<int, juce::Image> faders, knobs;
        for (const auto level : kLevels) {
            const auto theme = themeAt(base, level);
            faders[(int)level] = renderFader(theme, fc, KnobStyle::Classic, false, false, false);
            knobs[(int)level] = renderKnob(theme, KnobStyle::Classic, 96, 0.5f);
            faderShadow[(int)level] = faderShadowStrength(faders[(int)level], probe);
            knobShadow[(int)level] = knobShadowStrength(knobs[(int)level], bottom);
        }
        const int F = (int)DepthLevel::Flat, S = (int)DepthLevel::Soft, R = (int)DepthLevel::Rich;

        // Shadow: none at Flat on both, today's at Soft, and the same step deeper on both at Rich.
        EXPECT_EQ(faderShadow[F], 0) << base.id;
        EXPECT_EQ(knobShadow[F], 0) << base.id;
        ASSERT_GT(faderShadow[S], 0) << base.id;
        ASSERT_GT(knobShadow[S], 0) << base.id;
        const double faderStep = (double)faderShadow[R] / (double)faderShadow[S];
        const double knobStep = (double)knobShadow[R] / (double)knobShadow[S];
        // The same share deeper: the fader reads it straight off a fully covered row, the knob's fading shadow also
        // reaches further, so it reads at least that.
        const double share = capShadowAlpha(0.4f, themeAt(base, DepthLevel::Rich)) / 0.4f;
        EXPECT_NEAR(faderStep, share, 0.1) << base.id << ": the fader shadow steps up by the shared share";
        EXPECT_GE(knobStep, share - 0.05) << base.id << ": and so does the knob's";

        // Rim: the lit edge is the top left on both and nothing is added at the bottom right, at Rich only.
        const auto cap = probe.cap;
        const juce::Point<int> faderTL(cap.getX() + 6, cap.getY()), faderBR(cap.getRight() - 4, cap.getBottom() - 1);
        const float r = 96.0f * 0.26f - 0.6f, k = std::sqrt(0.5f) * r;
        const juce::Point<int> knobTL((int)(48.0f - k), (int)(48.0f - k)), knobBR((int)(48.0f + k), (int)(48.0f + k));
        EXPECT_NE(faders[R].getPixelAt(faderTL.x, faderTL.y), faders[S].getPixelAt(faderTL.x, faderTL.y)) << base.id;
        EXPECT_LE(
            channelDistance(faders[R].getPixelAt(faderBR.x, faderBR.y), faders[S].getPixelAt(faderBR.x, faderBR.y)), 3)
            << base.id;
        EXPECT_NE(knobs[R].getPixelAt(knobTL.x, knobTL.y), knobs[S].getPixelAt(knobTL.x, knobTL.y)) << base.id;
        EXPECT_LE(channelDistance(knobs[R].getPixelAt(knobBR.x, knobBR.y), knobs[S].getPixelAt(knobBR.x, knobBR.y)), 3)
            << base.id;

        // Flat: a plain surfaceHi face on both.
        EXPECT_LE(channelDistance(faders[F].getPixelAt(cap.getX() + 5, cap.getBottom() - 3), base.colors.surfaceHi), 3)
            << base.id;
    }
}

// ---- the pointer and the value arc stay readable ----

TEST(ControlDepthTest, PointerAndValueArcKeepTheirContrastAtEveryLevelInEveryTheme) {
    for (const auto& base : builtInThemes()) {
        const auto soft = renderKnob(themeAt(base, DepthLevel::Soft), KnobStyle::Classic, 96, 0.5f);
        const auto softFader =
            renderFader(themeAt(base, DepthLevel::Soft), {true, 80, 160}, KnobStyle::Classic, false, false, false);
        for (const auto level : kLevels) {
            const auto theme = themeAt(base, level);
            const auto knob = renderKnob(theme, KnobStyle::Classic, 96, 0.5f);
            // The pointer runs up from the centre; the face beside it is what it must read against.
            const juce::Point<int> beside(48 - 4, 48 - (int)(96.0f * 0.26f * 0.5f));
            const double got = contrast(theme.colors.knobPointer, knob.getPixelAt(beside.x, beside.y));
            const double was = contrast(theme.colors.knobPointer, soft.getPixelAt(beside.x, beside.y));
            EXPECT_TRUE(got >= 3.0 || got >= was - 0.05) << base.id << " level " << (int)level << " pointer " << got;

            // The value arc is not on the cap: every level paints it as Soft does.
            const float a =
                AppLookAndFeel::kRotaryStart + 0.25f * (AppLookAndFeel::kRotaryEnd - AppLookAndFeel::kRotaryStart);
            const float arcR = 48.0f - theme.metrics.knobTrackWidth;
            const juce::Point<int> arc((int)(48.0f + std::sin(a) * arcR), (int)(48.0f - std::cos(a) * arcR));
            EXPECT_EQ(knob.getPixelAt(arc.x, arc.y), soft.getPixelAt(arc.x, arc.y))
                << base.id << " level " << (int)level;

            // The fader's centre line against its cap face.
            const auto fader = renderFader(theme, {true, 80, 160}, KnobStyle::Classic, false, false, false);
            const auto cap = largeCap().cap;
            const juce::Point<int> face(cap.getCentreX() + 8, cap.getCentreY() - 3);
            const juce::Point<int> line(cap.getCentreX() + 8, cap.getCentreY());
            const double faderGot = contrast(fader.getPixelAt(line.x, line.y), fader.getPixelAt(face.x, face.y));
            const double faderWas =
                contrast(softFader.getPixelAt(line.x, line.y), softFader.getPixelAt(face.x, face.y));
            EXPECT_TRUE(faderGot >= 3.0 || faderGot >= faderWas - 0.05) << base.id << " level " << (int)level;
        }
    }
}

// ---- the sibling-gap guard ----

// Bug kind sibling-gap: every knob style and the fader's cap go through ControlDepth, so a new style cannot ship with
// Soft only. Neon and Ring have no cap to light and are exempt, by name, with the reason.
TEST(ControlDepthTest, EveryKnobStyleAndTheFaderGoThroughTheSharedCapLight) {
    const std::map<std::string, std::string> painter = {
        {"Classic", "Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStyles.cpp"},
        {"Hardware", "Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStylesChunky.cpp"},
        {"Analog", "Source/UI/Theme/AppLookAndFeel/AppLookAndFeelKnobStylesAnalog.cpp"},
    };
    const std::map<std::string, std::string> exempt = {
        {"Neon", "a glowing arc and a number: no cap to light"},
        {"Ring", "LEDs round a flat dark disc: no cap to light"},
    };
    const auto texts = synth::test::source_scan::sourceTexts();
    const auto header = texts.at("Source/UI/Theme/KnobStyle.h");
    std::smatch m;
    ASSERT_TRUE(std::regex_search(header, m, std::regex(R"(enum class KnobStyle\s*\{([^}]*)\})")));
    std::set<std::string> styles;
    const std::string list = m[1].str();
    const std::regex word(R"(\w+)");
    for (std::sregex_iterator it(list.begin(), list.end(), word), end; it != end; ++it)
        styles.insert(it->str());
    ASSERT_EQ(styles.size(), 5u);
    for (const auto& style : styles) {
        if (exempt.count(style) != 0)
            continue;
        ASSERT_NE(painter.count(style), 0u) << "knob style " << style << " has no cap light: add it to ControlDepth";
        const auto& text = texts.at(painter.at(style));
        EXPECT_NE(text.find("ControlDepth.h"), std::string::npos) << style;
        EXPECT_NE(text.find("capIsFlat"), std::string::npos) << style;
        EXPECT_NE(text.find("capShadowAlpha"), std::string::npos) << style;
        EXPECT_NE(text.find("capShadowReach"), std::string::npos) << style;
    }
    const auto& faderText = texts.at("Source/UI/Theme/AppLookAndFeel/AppLookAndFeelFader.cpp");
    for (const char* call : {"capIsFlat", "capShadowAlpha", "capShadowReach", "paintCapRimRect"})
        EXPECT_NE(faderText.find(call), std::string::npos) << "fader cap: " << call;

    // No painter draws a cap shadow of its own: every disc shadow in the look-and-feel is scaled by the shared reach.
    const std::regex discShadow(R"(fillDiscShadow\s*\([^;]*;)");
    for (const auto& [path, text] : texts) {
        if (path.find("Source/UI/Theme/AppLookAndFeel/") != 0 ||
            path.find("KnobPainterInternal.h") != std::string::npos)
            continue;
        for (std::sregex_iterator it(text.begin(), text.end(), discShadow), end; it != end; ++it) {
            const auto at = (size_t)it->position();
            if (at >= 5 && text.compare(at - 5, 5, "void ") == 0)
                continue; // the helper's own definition
            if (it->str().find("reach") == std::string::npos)
                ADD_FAILURE() << path << ": a disc shadow that ignores the Depth level: " << it->str();
        }
    }
}
