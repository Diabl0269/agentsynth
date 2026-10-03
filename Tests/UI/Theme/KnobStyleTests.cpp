// KnobStyleTests.cpp -- the knob look chosen in Settings > Appearance > Knobs: persisted ids, the six
// painters (rendered into software images -- the platform default reads back zeros on the Windows CI),
// the family colour reaching the value arc, and the theme tokens it relies on.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/KnobPainter.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/ThemeLoader.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace {

using namespace synth::theme;

int channelDistance(juce::Colour a, juce::Colour b) {
    return std::abs((int)a.getRed() - (int)b.getRed()) + std::abs((int)a.getGreen() - (int)b.getGreen()) +
           std::abs((int)a.getBlue() - (int)b.getBlue());
}

juce::Image makeImage(int w, int h) { return juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType()); }

std::unique_ptr<juce::PropertiesFile> makeProps(const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

juce::Image renderKnob(const Theme& theme, KnobStyle style, float pos) {
    auto img = makeImage(60, 60);
    juce::Graphics g(img);
    paintKnob(g, theme, style, juce::Rectangle<float>(0, 0, 60, 60), pos, theme.colors.accent);
    return img;
}

int differingPixels(const juce::Image& a, const juce::Image& b) {
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++count;
    return count;
}

// A slider inside a parent that may carry the family property, painted through a real AppLookAndFeel.
struct KnobRig {
    KnobRig(KnobAppearance appearance, int family) {
        laf.applyTheme(makeObsidian());
        laf.setKnobAppearance(appearance);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setValue(1.0, juce::dontSendNotification);
        parent.setLookAndFeel(&laf);
        if (family >= 0)
            parent.getProperties().set(kKnobFamilyProperty, family);
        parent.addAndMakeVisible(slider);
        parent.setBounds(0, 0, 60, 60);
        slider.setBounds(0, 0, 60, 60);
    }
    ~KnobRig() { parent.setLookAndFeel(nullptr); }

    // A pixel on the value arc near its end (the value is 1.0), clear of every style's rider/tip.
    juce::Colour arcPixel() {
        auto img = makeImage(60, 60);
        juce::Graphics g(img);
        slider.paintEntireComponent(g, false);
        const float angle =
            AppLookAndFeel::kRotaryStart + 0.9f * (AppLookAndFeel::kRotaryEnd - AppLookAndFeel::kRotaryStart);
        const float radius = 30.0f - laf.getTheme().metrics.knobTrackWidth;
        return img.getPixelAt((int)std::lround(30.0f + std::sin(angle) * radius),
                              (int)std::lround(30.0f - std::cos(angle) * radius));
    }

    AppLookAndFeel laf;
    juce::Component parent;
    juce::Slider slider;
};

} // namespace

TEST(KnobStyleTest, IdsAndLabelsRoundTrip) {
    for (int i = 0; i < kKnobStyleCount; ++i) {
        const auto style = (KnobStyle)i;
        EXPECT_EQ(knobStyleFromId(knobStyleId(style)), style);
        const juce::String label = knobStyleLabel(style);
        EXPECT_EQ(label, label.substring(0, 1).toUpperCase() + label.substring(1).toLowerCase())
            << "sentence case, never all caps";
    }
    EXPECT_STREQ(knobStyleId(KnobStyle::Polished), "polished");
}

TEST(KnobStyleTest, UnknownIdLoadsAsPolished) { EXPECT_EQ(knobStyleFromId("sparkly"), KnobStyle::Polished); }

TEST(KnobStyleTest, MissingKeysLoadPolishedWithFamilyOn) {
    auto props = makeProps("KnobStyleMissing");
    const auto appearance = loadKnobAppearance(*props);
    EXPECT_EQ(appearance.style, KnobStyle::Polished);
    EXPECT_TRUE(appearance.colourByFamily);
}

TEST(KnobStyleTest, WriteThenLoadRoundTrips) {
    auto props = makeProps("KnobStyleRoundTrip");
    writeKnobAppearance(*props, {KnobStyle::Neon, false});
    const auto appearance = loadKnobAppearance(*props);
    EXPECT_EQ(appearance.style, KnobStyle::Neon);
    EXPECT_FALSE(appearance.colourByFamily);
    props->setValue(knobStyleKey(), "nonsense");
    EXPECT_EQ(loadKnobAppearance(*props).style, KnobStyle::Polished);
}

TEST(KnobStyleTest, EveryStylePaintsDifferently) {
    const auto theme = makeObsidian();
    juce::Image images[kKnobStyleCount];
    for (int i = 0; i < kKnobStyleCount; ++i)
        images[i] = renderKnob(theme, (KnobStyle)i, 0.62f);
    for (int a = 0; a < kKnobStyleCount; ++a)
        for (int b = a + 1; b < kKnobStyleCount; ++b)
            EXPECT_GT(differingPixels(images[a], images[b]), 20)
                << knobStyleId((KnobStyle)a) << " and " << knobStyleId((KnobStyle)b) << " look the same";
}

TEST(KnobStyleTest, EveryStyleMovesWithTheValue) {
    const auto theme = makeObsidian();
    for (int i = 0; i < kKnobStyleCount; ++i)
        EXPECT_GT(differingPixels(renderKnob(theme, (KnobStyle)i, 0.2f), renderKnob(theme, (KnobStyle)i, 0.8f)), 20)
            << knobStyleId((KnobStyle)i);
}

TEST(KnobStyleTest, FamilyColourReachesTheValueArc) {
    const auto theme = makeObsidian();
    for (int i = 0; i < kKnobStyleCount; ++i) {
        const auto style = (KnobStyle)i;
        KnobRig onRig({style, true}, /*sources*/ 0);
        EXPECT_LE(channelDistance(onRig.arcPixel(), theme.colors.hueAmber), 30) << knobStyleId(style);

        KnobRig offRig({style, false}, 0);
        EXPECT_LE(channelDistance(offRig.arcPixel(), theme.colors.accent), 30) << knobStyleId(style) << " family off";

        KnobRig noFamily({style, true}, -1);
        EXPECT_LE(channelDistance(noFamily.arcPixel(), theme.colors.accent), 30) << knobStyleId(style) << " no family";
    }
}

TEST(KnobStyleTest, FamilyHueMapping) {
    const auto c = makeObsidian().colors;
    EXPECT_EQ(familyHue(c, 0), c.hueAmber);
    EXPECT_EQ(familyHue(c, 1), c.hueGreen);
    EXPECT_EQ(familyHue(c, 2), c.hueGreen);
    EXPECT_EQ(familyHue(c, 3), c.accent);
    EXPECT_EQ(familyHue(c, 4), c.hueRose);
    EXPECT_EQ(familyHue(c, 5), c.hueRose);
    EXPECT_EQ(familyHue(c, 6), c.hueRose);
    EXPECT_EQ(familyHue(c, 7), c.accent);
    EXPECT_EQ(familyHue(c, 99), c.accent);
}

TEST(KnobStyleTest, BuiltInThemesDefineTheNewTokens) {
    struct Expect {
        Theme theme;
        juce::uint32 amber, green, rose, violet, skirt, ink, paper;
    };
    const Expect expected[] = {
        {makeObsidian(), 0xffF5C542, 0xff4ADE80, 0xffFF6FA8, 0xffB48EF5, 0xff0E1014, 0xff0B0D10, 0xffF4F6F8},
        {makeNeon(), 0xffFFD166, 0xff46E0A0, 0xffFF8C69, 0xffC9A7FF, 0xff0D0818, 0xff0A0612, 0xffFBF7FF},
        {makeWarm(), 0xffFFD27A, 0xff9BBF6A, 0xffF28AB2, 0xffC6A2E8, 0xff120D09, 0xff161310, 0xffFBF3E4},
        {makeDaylight(), 0xff946200, 0xff2B8A3E, 0xffC2255C, 0xff7048E8, 0xffADB5BD, 0xffFFFFFF, 0xffFFFFFF},
    };
    for (const auto& e : expected) {
        EXPECT_EQ(e.theme.colors.hueAmber.getARGB(), e.amber) << e.theme.name;
        EXPECT_EQ(e.theme.colors.hueGreen.getARGB(), e.green) << e.theme.name;
        EXPECT_EQ(e.theme.colors.hueRose.getARGB(), e.rose) << e.theme.name;
        EXPECT_EQ(e.theme.colors.hueViolet.getARGB(), e.violet) << e.theme.name;
        EXPECT_EQ(e.theme.colors.knobSkirt.getARGB(), e.skirt) << e.theme.name;
        EXPECT_EQ(e.theme.colors.iconInk.getARGB(), e.ink) << e.theme.name;
        EXPECT_EQ(e.theme.colors.iconPaper.getARGB(), e.paper) << e.theme.name;
    }
    EXPECT_NEAR(makeObsidian().colors.knobCapHighlight.getFloatAlpha(), 0.18f, 0.01f);
    EXPECT_NEAR(makeNeon().colors.knobCapHighlight.getFloatAlpha(), 0.22f, 0.01f);
    EXPECT_NEAR(makeWarm().colors.knobCapHighlight.getFloatAlpha(), 0.14f, 0.01f);
    EXPECT_NEAR(makeDaylight().colors.knobCapHighlight.getFloatAlpha(), 0.70f, 0.01f);
}

TEST(KnobStyleTest, ThemeLoaderParsesAndRoundTripsTheNewTokens) {
    const auto original = makeDaylight();
    const auto parsed = ThemeLoader::parseTheme(ThemeLoader::themeToJson(original));
    ASSERT_TRUE(parsed.has_value()) << ThemeLoader::getLastError();
    EXPECT_EQ(parsed->colors.hueAmber, original.colors.hueAmber);
    EXPECT_EQ(parsed->colors.hueViolet, original.colors.hueViolet);
    EXPECT_EQ(parsed->colors.knobSkirt, original.colors.knobSkirt);
    EXPECT_EQ(parsed->colors.knobCapHighlight, original.colors.knobCapHighlight);
    EXPECT_EQ(parsed->colors.iconInk, original.colors.iconInk);
    EXPECT_EQ(parsed->colors.iconPaper, original.colors.iconPaper);

    // A theme file that omits them gets the struct defaults, which are the Obsidian values.
    auto bare = ThemeLoader::themeToJson(makeDaylight());
    auto* colours = bare["colors"].getDynamicObject();
    ASSERT_NE(colours, nullptr);
    colours->removeProperty("hueAmber");
    colours->setProperty("hueRose", "#FF112233");
    const auto theme = ThemeLoader::parseTheme(bare, "bare");
    ASSERT_TRUE(theme.has_value()) << ThemeLoader::getLastError();
    EXPECT_EQ(theme->colors.hueRose.getARGB(), 0xff112233u);
    EXPECT_EQ(theme->colors.hueAmber, makeObsidian().colors.hueAmber);
}
