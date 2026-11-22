// ToolbarLitInkSweepTests.cpp -- theme sweep of the lit toolbar toggles and the menu notch: every toolbar glyph, in
// every colour group, lit / hovered / pressed, in all four built-in themes, keeps its ink at 3:1 against the chip's
// fill, and a glyph with two tones (the Snap magnet's body and pole tips) keeps them 3:1 apart. Every menu notch (Draw
// pen, Sizing chip, Snap mode picker), at rest, hovered, pressed and lit, is 3:1 against the chip pixels just beside
// it. Regression test for the Daylight lit Snap toggle (muddy fill, magnet losing its pole tips) and the Draw pen's
// notch vanishing when the pen is lit. Images use SoftwareImageType() so the pixels read back on every platform.
#include "ToolbarButtonTestHelpers.h"
#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Timeline/TrackColour.h"
#include <cmath>
#include <map>

using namespace toolbartest;
using synth::theme::DepthLevel;
using synth::ui::colourContrast;
using synth::ui::ToolbarButtonLayout;

namespace {

constexpr float kMinContrast = 3.0f;
constexpr ToolbarGroup kGroups[] = {ToolbarGroup::File, ToolbarGroup::Edit,         ToolbarGroup::View,
                                    ToolbarGroup::AI,   ToolbarGroup::Housekeeping, ToolbarGroup::Feedback};

// Every glyph a toggle or button of the app puts on a toolbar chip. Save is a floppy in fixed colours and the AI plug's
// pulse is painted in code, so neither takes the lit roles.
std::vector<Icon> litGlyphs() {
    std::vector<Icon> icons;
    for (const auto& spec : allToolbarIcons())
        if (spec.icon != Icon::ActionSave && spec.icon != Icon::ToggleAI)
            icons.push_back(spec.icon);
    for (const auto icon : {Icon::ToolbarSnap, Icon::ToolbarAddTrack, Icon::ToolbarSearch, Icon::SnapModeBoth,
                            Icon::SnapModeTime, Icon::SnapModeLevel})
        icons.push_back(icon);
    return icons;
}

enum class State { Rest, Hover, Pressed };
const char* name(State s) { return s == State::Rest ? "rest" : (s == State::Hover ? "hover" : "pressed"); }

void drive(juce::Button& b, State s) {
    b.setState(s == State::Rest ? juce::Button::buttonNormal
                                : (s == State::Hover ? juce::Button::buttonOver : juce::Button::buttonDown));
}

// The opaque colours of the glyph's solid areas: those with at least `minPixels` interior pixels (all four neighbours
// the same colour, so no anti-aliased edge blend counts), other than the chip's own.
std::map<juce::uint32, int> solidTones(const juce::Image& img, juce::Rectangle<int> area, juce::Colour chip,
                                       int minPixels) {
    std::map<juce::uint32, int> counts;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            const auto c = img.getPixelAt(x, y);
            if (img.getPixelAt(x - 1, y) == c && img.getPixelAt(x + 1, y) == c && img.getPixelAt(x, y - 1) == c &&
                img.getPixelAt(x, y + 1) == c)
                ++counts[c.getARGB()];
        }
    std::map<juce::uint32, int> tones;
    for (const auto& [argb, n] : counts)
        if (n >= minPixels && !near(juce::Colour(argb), chip, 2))
            tones[argb] = n;
    return tones;
}

} // namespace

TEST_F(ToolbarButtonTest, ALitGlyphReadsAgainstItsChipInEveryGroupStateAndTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        useTheme(theme);
        for (const auto group : kGroups)
            for (const auto icon : litGlyphs()) {
                auto& b = make(icon, group, "Cap");
                b.setToggleState(true, juce::dontSendNotification);
                for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
                    drive(b, state);
                    const auto img = render(b);
                    const auto chip = img.getPixelAt(chipOnlyPixel(b).x, chipOnlyPixel(b).y);
                    for (const auto& [argb, n] : solidTones(img, iconArea(b), chip, 3)) {
                        const juce::Colour tone(argb);
                        // A soft shape is the ink at 40 percent over the chip on purpose: a back shape, not a mark.
                        if (near(tone, chip.interpolatedWith(synth::ui::toolbarLitInk(theme, group), 0.4f), 3))
                            continue;
                        EXPECT_GE(colourContrast(tone, chip), kMinContrast)
                            << theme.name << " group " << (int)group << " icon " << (int)icon << " " << name(state)
                            << ": lit glyph tone " << tone.toDisplayString(false) << " on chip "
                            << chip.toDisplayString(false);
                    }
                }
            }
    }
}

TEST_F(ToolbarButtonTest, ALitSnapMagnetKeepsItsPoleTipsApartFromItsBodyAndTheChip) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        useTheme(theme);
        for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
            auto& b = make(Icon::ToolbarSnap, ToolbarGroup::Edit, "Snap");
            b.setToggleState(true, juce::dontSendNotification);
            drive(b, state);
            const auto img = render(b);
            const auto chip = img.getPixelAt(chipOnlyPixel(b).x, chipOnlyPixel(b).y);
            const auto squash = b.getChipSquash();
            const auto chipBounds = synth::theme::toolbarChipBounds(b);
            const auto at = [&](juce::Point<float> p) {
                const auto q = p.transformedBy(synth::theme::toolbarIconTransform(b).translated(0.0f, -b.getIconLift()))
                                   .transformedBy(juce::AffineTransform::scale(
                                       squash.x, squash.y, chipBounds.getCentreX(), chipBounds.getCentreY()));
                return img.getPixelAt((int)std::floor(q.x), (int)std::floor(q.y));
            };
            const auto body = at({12.0f, 18.0f}); // the bottom of the U
            const auto tip = at({6.0f, 5.5f});    // the left pole's tip
            EXPECT_GE(colourContrast(body, chip), kMinContrast) << theme.name << " " << name(state) << " body";
            EXPECT_GE(colourContrast(tip, body), kMinContrast) << theme.name << " " << name(state) << " tip vs body";
            EXPECT_GE(colourContrast(tip, chip), kMinContrast) << theme.name << " " << name(state) << " tip vs chip";
        }
    }
}

TEST_F(ToolbarButtonTest, DaylightsLitSnapFillIsAClearAmberNotAMuddyOlive) {
    useTheme(synth::theme::makeDaylight());
    auto& b = make(Icon::ToolbarSnap, ToolbarGroup::Edit, "Snap");
    b.setToggleState(true, juce::dontSendNotification);
    const auto chip = render(b).getPixelAt(chipOnlyPixel(b).x, chipOnlyPixel(b).y);
    EXPECT_GT(chip.getBrightness(), 0.85f) << chip.toDisplayString(false);
    EXPECT_GT(chip.getSaturation(), 0.8f) << chip.toDisplayString(false);
    EXPECT_NEAR(chip.getHue() * 360.0f, 42.0f, 12.0f) << "amber, not olive: " << chip.toDisplayString(false);
}

// A lit chip's menu notch is the lit glyph's ink family. In a dark theme it is a softer tone of it (clearly lighter
// than the near-black glyph ink, still 3:1 on the chip); in Daylight it is the glyph ink itself. It is pure black or
// white only where that glyph ink is.
TEST_F(ToolbarButtonTest, ALitChipsNotchInkIsASofterToneOfTheLitGlyphInkInDarkThemes) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto& c = theme.colors;
        for (const auto group : kGroups) {
            const auto fill = synth::ui::toolbarLitFill(theme, group);
            const auto glyphInk = synth::ui::toolbarLitInk(theme, group);
            const auto ink = synth::ui::sideMenuNotchInk(theme, {c.bg0.overlaidWith(fill), c.bg1.overlaidWith(fill)},
                                                         glyphInk, 1.0f);
            const juce::String what = theme.name + " group " + juce::String((int)group);
            EXPECT_GE(colourContrast(ink, fill), kMinContrast) << what;
            if (theme.isDark)
                EXPECT_GT(std::abs(ink.getPerceivedBrightness() - glyphInk.getPerceivedBrightness()), 0.08f)
                    << what << ": notch " << ink.toDisplayString(false) << " glyph " << glyphInk.toDisplayString(false);
            else if (colourContrast(glyphInk, fill) >= 3.5f)
                EXPECT_EQ(ink, glyphInk) << what << ": Daylight's notch is the glyph ink";
            for (const auto pure : {juce::Colour(0xff000000), juce::Colour(0xffffffff)})
                if (ink == pure)
                    EXPECT_EQ(ink, glyphInk) << what << ": notch is pure " << pure.toDisplayString(false);
        }
    }
}

// When no theme ink reaches 3.5:1 on the fill (every ink a mid grey, on a mid-grey fill), the notch is toned toward
// black or white only as far as it needs, never the pure colour.
TEST_F(ToolbarButtonTest, ANotchOnAFillNoThemeInkReadsOnIsTonedNotPureBlackOrWhite) {
    auto theme = synth::theme::makeObsidian();
    auto& colours = theme.colors;
    const juce::Colour grey(0xff808080);
    colours.iconInk = colours.iconPaper = colours.textPrimary = colours.bg0 = colours.bg1 = grey;
    for (const float lit : {0.0f, 1.0f}) {
        const auto ink = synth::ui::sideMenuNotchInk(theme, {grey, grey}, grey, lit);
        EXPECT_NE(ink, juce::Colour(0xff000000)) << "lit " << lit;
        EXPECT_NE(ink, juce::Colour(0xffffffff)) << "lit " << lit;
        EXPECT_GE(colourContrast(ink, grey), 3.5f) << "lit " << lit;
    }
}

// ---- Bug kind focus-ring-box: nothing a toolbar button paints lies outside its focus ring ----

namespace {

constexpr float kRingInset = 1.0f; // the ring's outer edge stands 1 px in from the button's bounds
// A pixel counts as outside when its whole square is: its centre is at least this far (half a pixel's diagonal, and a
// little more) beyond the edge.
constexpr float kOutsideBy = 0.8f;

enum class State { Rest, Hover, Pressed };
const char* name(State s) { return s == State::Rest ? "rest" : (s == State::Hover ? "hover" : "pressed"); }

struct RingsOn {
    RingsOn() { synth::ui::setFocusRingsShownForTest(true); }
    ~RingsOn() { synth::ui::setFocusRingsShownForTest(false); }
};

// How far `p` is outside the rounded rectangle (negative inside).
float distanceOutside(juce::Point<float> p, juce::Rectangle<float> box, float radius) {
    const float qx = std::abs(p.x - box.getCentreX()) - (box.getWidth() * 0.5f - radius);
    const float qy = std::abs(p.y - box.getCentreY()) - (box.getHeight() * 0.5f - radius);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
}

const char* layoutName(ToolbarButtonLayout l) {
    switch (l) {
    case ToolbarButtonLayout::Stacked:
        return "stacked";
    case ToolbarButtonLayout::Inline:
        return "inline";
    case ToolbarButtonLayout::InlineCompact:
        return "inline-compact";
    case ToolbarButtonLayout::InlineStrip:
        return "inline-strip";
    }
    return "?";
}

} // namespace

TEST_F(ToolbarButtonTest, NothingAToolbarButtonPaintsLiesOutsideItsFocusRing) {
    RingsOn rings;
    for (auto theme : synth::theme::builtInThemes())
        for (const auto depth : {DepthLevel::Soft, DepthLevel::Rich}) {
            theme.treatment.depth = depth;
            useTheme(theme);
            for (const auto layout : {ToolbarButtonLayout::Stacked, ToolbarButtonLayout::Inline,
                                      ToolbarButtonLayout::InlineCompact, ToolbarButtonLayout::InlineStrip})
                for (const bool captioned : {true, false})
                    for (const bool lit : {false, true})
                        for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
                            auto& b = make(Icon::ToggleMinimap, ToolbarGroup::View, captioned ? "Minimap" : "");
                            b.setLayout(layout);
                            b.setSize(layout == ToolbarButtonLayout::Stacked ? 88 : b.getPreferredWidth() + 12,
                                      layout == ToolbarButtonLayout::Stacked ? 44 : 28);
                            b.setContentCentred(!captioned || lit);
                            b.setToggleState(lit, juce::dontSendNotification);
                            b.setState(state == State::Rest    ? juce::Button::buttonNormal
                                       : state == State::Hover ? juce::Button::buttonOver
                                                               : juce::Button::buttonDown);
                            juce::Image img(juce::Image::ARGB, b.getWidth(), b.getHeight(), true,
                                            juce::SoftwareImageType());
                            {
                                juce::Graphics g(img);
                                b.paintEntireComponent(g, false);
                            }
                            const auto box = b.getLocalBounds().toFloat().reduced(kRingInset);
                            int stray = 0;
                            juce::Point<int> first;
                            for (int y = 0; y < img.getHeight(); ++y)
                                for (int x = 0; x < img.getWidth(); ++x)
                                    if (distanceOutside({x + 0.5f, y + 0.5f}, box, theme.metrics.pillRadius) >
                                            kOutsideBy &&
                                        img.getPixelAt(x, y).getAlpha() != 0) {
                                        if (stray++ == 0)
                                            first = {x, y};
                                    }
                            EXPECT_EQ(stray, 0)
                                << theme.name << (depth == DepthLevel::Rich ? " rich " : " soft ") << layoutName(layout)
                                << (captioned ? " captioned " : " icon-only ") << (lit ? "lit " : "unlit ")
                                << name(state) << ": " << stray << " painted pixels outside the ring, first at ("
                                << first.x << "," << first.y << ")";
                        }
        }
}
