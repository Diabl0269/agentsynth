// ControlStyleOverrideTests.cpp -- the knob/fader style a module can choose for itself or for its type
// (Source/UI/Theme/ControlStyleOverride.*) and how AppLookAndFeel paints a slider inside a card that
// carries one.

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ControlStyleOverride.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using namespace synth::theme;

std::unique_ptr<juce::PropertiesFile> makeProps() {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("ControlStyleOverrideRig-" + juce::Uuid().toString() + ".settings");
    juce::PropertiesFile::Options opts;
    opts.applicationName = "ControlStyleOverrideRig";
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

juce::Image render(juce::Component& target) {
    juce::Image image(juce::Image::ARGB, target.getWidth(), target.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    target.paintEntireComponent(g, false);
    return image;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

// A card-like parent holding one slider, painted by an AppLookAndFeel whose app-wide style is Classic.
struct SliderRig {
    explicit SliderRig(juce::Slider::SliderStyle style) {
        laf.applyTheme(makeObsidian());
        laf.setKnobAppearance({KnobStyle::Classic, true, KnobStyle::Classic});
        card.setLookAndFeel(&laf);
        card.addAndMakeVisible(slider);
        slider.setSliderStyle(style);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setValue(0.6, juce::dontSendNotification);
        const bool linear = style == juce::Slider::LinearHorizontal;
        card.setBounds(0, 0, linear ? 120 : 64, linear ? 22 : 64);
        slider.setBounds(card.getLocalBounds());
    }
    ~SliderRig() { card.setLookAndFeel(nullptr); }
    AppLookAndFeel laf;
    juce::Component card;
    juce::Slider slider;
};

} // namespace

TEST(ControlStyleOverrideTest, TheVarHoldsOnlyTheSetKeysAndRoundTrips) {
    EXPECT_TRUE(toVar({}).isVoid());
    const ControlStyleOverride onlyKnob{KnobStyle::Neon, std::nullopt};
    const auto var = toVar(onlyKnob);
    ASSERT_NE(var.getDynamicObject(), nullptr);
    EXPECT_TRUE(var.getDynamicObject()->hasProperty("knob"));
    EXPECT_FALSE(var.getDynamicObject()->hasProperty("fader"));
    EXPECT_EQ(controlStyleOverrideFromVar(var), onlyKnob);

    const ControlStyleOverride both{KnobStyle::Hardware, KnobStyle::Ring};
    EXPECT_EQ(controlStyleOverrideFromVar(toVar(both)), both);
    EXPECT_EQ(toVar(both).getProperty("knob", {}).toString(), "hardware");
    EXPECT_EQ(toVar(both).getProperty("fader", {}).toString(), "ring");
}

TEST(ControlStyleOverrideTest, UnknownIdsAndShapesAreReadAsUnset) {
    const auto parsed = juce::JSON::parse("{\"knob\":\"sparkle\",\"fader\":\"polished\"}");
    const auto style = controlStyleOverrideFromVar(parsed);
    EXPECT_FALSE(style.knob) << "an id this build does not know follows the next level";
    EXPECT_EQ(style.fader, KnobStyle::Analog) << "a retired id loads as its successor";
    EXPECT_TRUE(controlStyleOverrideFromVar(juce::var("neon")).isEmpty());
    EXPECT_TRUE(controlStyleOverrideFromVar({}).isEmpty());
    EXPECT_TRUE(controlStyleOverrideFromVar(juce::JSON::parse("{\"knob\":7}")).isEmpty());
}

TEST(ControlStyleOverrideTest, ResolutionPrefersTheInstanceThenTheTypeThenTheApp) {
    EXPECT_EQ(resolveControlStyle(KnobStyle::Neon, KnobStyle::Ring, KnobStyle::Classic), KnobStyle::Neon);
    EXPECT_EQ(resolveControlStyle(std::nullopt, KnobStyle::Ring, KnobStyle::Classic), KnobStyle::Ring);
    EXPECT_EQ(resolveControlStyle(std::nullopt, std::nullopt, KnobStyle::Analog), KnobStyle::Analog);
    EXPECT_EQ(resolveControlStyle(KnobStyle::Neon, std::nullopt, KnobStyle::Analog), KnobStyle::Neon);
}

TEST(ControlStyleOverrideTest, TypeStylesAreWrittenReadAndRemovedPerType) {
    auto props = makeProps();
    EXPECT_TRUE(loadTypeControlStyle(*props, "Oscillator").isEmpty());

    writeTypeControlStyle(*props, "Oscillator", {KnobStyle::Ring, std::nullopt});
    writeTypeControlStyle(*props, "Filter", {std::nullopt, KnobStyle::Neon});
    EXPECT_EQ(loadTypeControlStyle(*props, "Oscillator"), (ControlStyleOverride{KnobStyle::Ring, std::nullopt}));
    EXPECT_EQ(loadTypeControlStyle(*props, "Filter"), (ControlStyleOverride{std::nullopt, KnobStyle::Neon}));
    EXPECT_TRUE(loadTypeControlStyle(*props, "Reverb").isEmpty());

    writeTypeControlStyle(*props, "Oscillator", {});
    EXPECT_TRUE(loadTypeControlStyle(*props, "Oscillator").isEmpty());
    EXPECT_FALSE(loadTypeControlStyle(*props, "Filter").isEmpty()) << "only that type's entry goes";

    writeTypeControlStyle(*props, "Filter", {});
    EXPECT_FALSE(props->containsKey(controlStyleByTypeKey())) << "the last entry removes the key";
}

TEST(ControlStyleOverrideTest, ADamagedSettingReadsAsNoOverrides) {
    auto props = makeProps();
    props->setValue(controlStyleByTypeKey(), "{not json");
    EXPECT_TRUE(loadTypeControlStyle(*props, "Oscillator").isEmpty());
    writeTypeControlStyle(*props, "Oscillator", {KnobStyle::Neon, std::nullopt});
    EXPECT_EQ(loadTypeControlStyle(*props, "Oscillator").knob, KnobStyle::Neon);
}

TEST(ControlStyleOverrideTest, AKnobInsideACardWithAStylePaintsItAndOthersFollowTheApp) {
    SliderRig plain(juce::Slider::RotaryHorizontalVerticalDrag);
    SliderRig styled(juce::Slider::RotaryHorizontalVerticalDrag);
    styled.card.getProperties().set(kKnobStyleProperty, (int)KnobStyle::Neon);
    SliderRig neonApp(juce::Slider::RotaryHorizontalVerticalDrag);
    neonApp.laf.setKnobAppearance({KnobStyle::Neon, true, KnobStyle::Classic});

    const auto plainImage = render(plain.card);
    const auto styledImage = render(styled.card);
    EXPECT_FALSE(sameImage(plainImage, styledImage));
    EXPECT_TRUE(sameImage(styledImage, render(neonApp.card)))
        << "the card's own style paints as the app-wide style would";
    EXPECT_EQ(plain.laf.knobStyleFor(plain.slider), KnobStyle::Classic);
    EXPECT_EQ(styled.laf.knobStyleFor(styled.slider), KnobStyle::Neon);
}

TEST(ControlStyleOverrideTest, AFaderInsideACardWithAStylePaintsItAndAKnobStyleDoesNotLeakIn) {
    SliderRig plain(juce::Slider::LinearHorizontal);
    SliderRig styled(juce::Slider::LinearHorizontal);
    styled.card.getProperties().set(kFaderStyleProperty, (int)KnobStyle::Ring);
    SliderRig knobOnly(juce::Slider::LinearHorizontal);
    knobOnly.card.getProperties().set(kKnobStyleProperty, (int)KnobStyle::Ring);

    EXPECT_FALSE(sameImage(render(plain.card), render(styled.card)));
    EXPECT_TRUE(sameImage(render(plain.card), render(knobOnly.card))) << "a knob style does not restyle a fader";
    EXPECT_EQ(styled.laf.faderStyleFor(styled.slider), KnobStyle::Ring);
}

TEST(ControlStyleOverrideTest, ASliderWithoutACardFollowsTheAppStyle) {
    AppLookAndFeel laf;
    laf.applyTheme(makeObsidian());
    laf.setKnobAppearance({KnobStyle::Hardware, true, KnobStyle::Analog});
    juce::Slider loose;
    EXPECT_EQ(laf.knobStyleFor(loose), KnobStyle::Hardware);
    EXPECT_EQ(laf.faderStyleFor(loose), KnobStyle::Analog);
}
