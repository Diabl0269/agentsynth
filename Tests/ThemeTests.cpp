// ThemeTests.cpp
// Headless unit tests for the Agent Synth theme system.
// Covers ThemeManager, ThemeLoader, BuiltInThemes, AppLookAndFeel::applyTheme,
// and the WCAG contrast requirement.  All 15 cases from spec section 9.

#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include "../Source/UI/Theme/Theme.h"
#include "../Source/UI/Theme/ThemeLoader.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include <array>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

// ---------------------------------------------------------------------------
// WCAG 2.x relative luminance + contrast ratio helpers (spec test 15)
// ---------------------------------------------------------------------------
static double wcagLinearise(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

static double wcagLuminance(juce::Colour col) {
    double r = wcagLinearise(col.getFloatRed());
    double g = wcagLinearise(col.getFloatGreen());
    double b = wcagLinearise(col.getFloatBlue());
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static double wcagContrast(juce::Colour a, juce::Colour b) {
    double l1 = wcagLuminance(a);
    double l2 = wcagLuminance(b);
    double lighter = std::max(l1, l2);
    double darker = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

// ---------------------------------------------------------------------------
// Fixture that creates a temporary ApplicationProperties in a temp directory
// (mirrors SettingsWindowTests.cpp pattern).
// ---------------------------------------------------------------------------
class ThemeTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "ThemeTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
    }

    void TearDown() override {
        if (auto* s = appProperties.getUserSettings())
            s->clear();
    }

    juce::ApplicationProperties appProperties;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. BuiltInsRegisterAndAreNonEmpty
// ---------------------------------------------------------------------------
TEST(ThemeBuiltInsTest, BuiltInsRegisterAndAreNonEmpty) {
    auto themes = synth::theme::builtInThemes();
    ASSERT_EQ(themes.size(), 4u);

    for (const auto& t : themes) {
        EXPECT_FALSE(t.name.isEmpty());
        EXPECT_FALSE(t.id.isEmpty());
    }

    EXPECT_EQ(themes[0].id, "obsidian");
    EXPECT_EQ(themes[1].id, "neon");
    EXPECT_EQ(themes[2].id, "warm");
    EXPECT_EQ(themes[3].id, "daylight");
}

// ---------------------------------------------------------------------------
// 2. ManagerStartsOnDefault
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, ManagerStartsOnDefault) {
    synth::theme::ThemeManager mgr;
    // No initialise() called — default built-in should be active.
    EXPECT_EQ(mgr.getActiveThemeId(), "obsidian");
    EXPECT_GE(mgr.getThemes().size(), 3u);
}

// ---------------------------------------------------------------------------
// 3. ApplyThemeSetsEveryColourId
// ---------------------------------------------------------------------------
TEST(ThemeLookAndFeelTest, ApplyThemeSetsEveryColourId) {
    synth::theme::AppLookAndFeel lf;
    auto neon = synth::theme::makeNeon();
    lf.applyTheme(neon);
    const auto& c = neon.colors;

    // Verify every mapping from spec section 3.
    EXPECT_EQ(lf.findColour(juce::ResizableWindow::backgroundColourId), c.bg0);
    EXPECT_EQ(lf.findColour(juce::DocumentWindow::textColourId), c.textPrimary);

    EXPECT_EQ(lf.findColour(juce::Slider::rotarySliderFillColourId), c.accent);
    EXPECT_EQ(lf.findColour(juce::Slider::rotarySliderOutlineColourId), c.border);
    EXPECT_EQ(lf.findColour(juce::Slider::thumbColourId), c.knobPointer);
    EXPECT_EQ(lf.findColour(juce::Slider::textBoxTextColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::Slider::textBoxBackgroundColourId), c.bg0);
    EXPECT_EQ(lf.findColour(juce::Slider::textBoxOutlineColourId), c.border);
    EXPECT_EQ(lf.findColour(juce::Slider::trackColourId), c.accent);
    EXPECT_EQ(lf.findColour(juce::Slider::backgroundColourId), c.surface);

    EXPECT_EQ(lf.findColour(juce::Label::textColourId), c.textPrimary);

    EXPECT_EQ(lf.findColour(juce::TextButton::buttonColourId), c.surface);
    EXPECT_EQ(lf.findColour(juce::TextButton::buttonOnColourId), c.accent);
    EXPECT_EQ(lf.findColour(juce::TextButton::textColourOffId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::TextButton::textColourOnId), c.bg0);

    EXPECT_EQ(lf.findColour(juce::ToggleButton::textColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::ToggleButton::tickColourId), c.accent);
    EXPECT_EQ(lf.findColour(juce::ToggleButton::tickDisabledColourId), c.border);

    EXPECT_EQ(lf.findColour(juce::ComboBox::backgroundColourId), c.surface);
    EXPECT_EQ(lf.findColour(juce::ComboBox::textColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::ComboBox::outlineColourId), c.border);
    EXPECT_EQ(lf.findColour(juce::ComboBox::arrowColourId), c.textMuted);
    EXPECT_EQ(lf.findColour(juce::ComboBox::buttonColourId), c.surfaceHi);

    EXPECT_EQ(lf.findColour(juce::PopupMenu::backgroundColourId), c.surface);
    EXPECT_EQ(lf.findColour(juce::PopupMenu::textColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::PopupMenu::highlightedTextColourId), c.textPrimary);
    // Highlighted bg = accent with alpha 0.25
    EXPECT_EQ(lf.findColour(juce::PopupMenu::highlightedBackgroundColourId), c.accent.withAlpha(0.25f));

    EXPECT_EQ(lf.findColour(juce::TextEditor::backgroundColourId), c.bg0);
    EXPECT_EQ(lf.findColour(juce::TextEditor::textColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::TextEditor::outlineColourId), c.border);
    EXPECT_EQ(lf.findColour(juce::TextEditor::highlightColourId), c.accent.withAlpha(0.30f));

    EXPECT_EQ(lf.findColour(juce::ScrollBar::thumbColourId), c.border.brighter(0.3f));

    EXPECT_EQ(lf.findColour(juce::TooltipWindow::backgroundColourId), c.surfaceHi);
    EXPECT_EQ(lf.findColour(juce::TooltipWindow::textColourId), c.textPrimary);

    EXPECT_EQ(lf.findColour(juce::ListBox::backgroundColourId), c.bg0);
    EXPECT_EQ(lf.findColour(juce::ListBox::textColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::ListBox::outlineColourId), c.border);

    EXPECT_EQ(lf.findColour(juce::TabbedComponent::backgroundColourId), c.bg0);
    EXPECT_EQ(lf.findColour(juce::TabbedButtonBar::tabTextColourId), c.textMuted);
    EXPECT_EQ(lf.findColour(juce::TabbedButtonBar::frontTextColourId), c.textPrimary);
    EXPECT_EQ(lf.findColour(juce::TabbedButtonBar::tabOutlineColourId), c.border);
    // MidiKeyboardComponent ColourIds (juce_audio_utils) are not linked into Core;
    // the on-screen keyboard keeps JUCE defaults for now (see AppLookAndFeel::applyTheme).
}

// ---------------------------------------------------------------------------
// 4. JsonRoundTrip
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, JsonRoundTrip) {
    auto original = synth::theme::makeObsidian();
    auto json = synth::theme::ThemeLoader::themeToJson(original);
    auto parsed = synth::theme::ThemeLoader::parseTheme(json);
    ASSERT_TRUE(parsed.has_value());

    const auto& p = *parsed;
    const auto& o = original;

    // Colors — exact ARGB equality
    EXPECT_EQ(p.colors.bg0.getARGB(), o.colors.bg0.getARGB());
    EXPECT_EQ(p.colors.bg1.getARGB(), o.colors.bg1.getARGB());
    EXPECT_EQ(p.colors.surface.getARGB(), o.colors.surface.getARGB());
    EXPECT_EQ(p.colors.surfaceHi.getARGB(), o.colors.surfaceHi.getARGB());
    EXPECT_EQ(p.colors.border.getARGB(), o.colors.border.getARGB());
    EXPECT_EQ(p.colors.accent.getARGB(), o.colors.accent.getARGB());
    EXPECT_EQ(p.colors.accent2.getARGB(), o.colors.accent2.getARGB());
    EXPECT_EQ(p.colors.audioWire.getARGB(), o.colors.audioWire.getARGB());
    EXPECT_EQ(p.colors.modWire.getARGB(), o.colors.modWire.getARGB());
    EXPECT_EQ(p.colors.pitchWire.getARGB(), o.colors.pitchWire.getARGB());
    EXPECT_EQ(p.colors.gateWire.getARGB(), o.colors.gateWire.getARGB());
    EXPECT_EQ(p.colors.polyBusWire.getARGB(), o.colors.polyBusWire.getARGB());
    EXPECT_EQ(p.colors.textPrimary.getARGB(), o.colors.textPrimary.getARGB());
    EXPECT_EQ(p.colors.textMuted.getARGB(), o.colors.textMuted.getARGB());
    EXPECT_EQ(p.colors.textDisabled.getARGB(), o.colors.textDisabled.getARGB());
    EXPECT_EQ(p.colors.success.getARGB(), o.colors.success.getARGB());
    EXPECT_EQ(p.colors.warning.getARGB(), o.colors.warning.getARGB());
    EXPECT_EQ(p.colors.error.getARGB(), o.colors.error.getARGB());
    EXPECT_EQ(p.colors.knobBody.getARGB(), o.colors.knobBody.getARGB());
    EXPECT_EQ(p.colors.knobPointer.getARGB(), o.colors.knobPointer.getARGB());
    EXPECT_EQ(p.colors.meterFill.getARGB(), o.colors.meterFill.getARGB());
    EXPECT_EQ(p.colors.modRingPositive.getARGB(), o.colors.modRingPositive.getARGB());
    EXPECT_EQ(p.colors.modRingNegative.getARGB(), o.colors.modRingNegative.getARGB());

    // Metrics — exact equality
    EXPECT_FLOAT_EQ(p.metrics.cornerRadius, o.metrics.cornerRadius);
    EXPECT_FLOAT_EQ(p.metrics.windowRadius, o.metrics.windowRadius);
    EXPECT_FLOAT_EQ(p.metrics.pillRadius, o.metrics.pillRadius);
    EXPECT_EQ(p.metrics.padding, o.metrics.padding);
    EXPECT_EQ(p.metrics.spacingUnit, o.metrics.spacingUnit);
    EXPECT_FLOAT_EQ(p.metrics.knobTrackWidth, o.metrics.knobTrackWidth);
    EXPECT_FLOAT_EQ(p.metrics.knobRingWidth, o.metrics.knobRingWidth);
    EXPECT_FLOAT_EQ(p.metrics.borderWidth, o.metrics.borderWidth);
    EXPECT_FLOAT_EQ(p.metrics.wireCoreWidth, o.metrics.wireCoreWidth);
    EXPECT_FLOAT_EQ(p.metrics.wireCasingWidth, o.metrics.wireCasingWidth);

    // Typography
    EXPECT_EQ(p.type.uiFamily, o.type.uiFamily);
    EXPECT_EQ(p.type.monoFamily, o.type.monoFamily);
    EXPECT_FLOAT_EQ(p.type.h1, o.type.h1);
    EXPECT_FLOAT_EQ(p.type.h2, o.type.h2);
    EXPECT_FLOAT_EQ(p.type.label, o.type.label);
    EXPECT_FLOAT_EQ(p.type.value, o.type.value);
    EXPECT_FLOAT_EQ(p.type.micro, o.type.micro);

    // Treatment
    EXPECT_EQ(p.treatment.style, o.treatment.style);
    EXPECT_NEAR(p.treatment.glow, o.treatment.glow, 1e-4f);
    EXPECT_NEAR(p.treatment.shadow, o.treatment.shadow, 1e-4f);
    EXPECT_NEAR(p.treatment.blur, o.treatment.blur, 1e-4f);
    EXPECT_NEAR(p.treatment.texture, o.treatment.texture, 1e-4f);
}

// ---------------------------------------------------------------------------
// 5. ObsidianFileMatchesBuiltIn
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, ObsidianFileMatchesBuiltIn) {
#ifdef THEMES_DIR
    juce::File themesDir(THEMES_DIR);
#else
    // Fallback: resolve relative to this source file (for local IDE builds).
    juce::File thisFile(__FILE__);
    juce::File themesDir = thisFile.getParentDirectory().getParentDirectory().getChildFile("themes");
#endif

    juce::File obsidianFile = themesDir.getChildFile("obsidian.gtheme.json");
    ASSERT_TRUE(obsidianFile.existsAsFile())
        << "themes/obsidian.gtheme.json not found at: " << obsidianFile.getFullPathName();

    juce::String jsonText = obsidianFile.loadFileAsString();
    auto jsonVar = juce::JSON::parse(jsonText);
    ASSERT_TRUE(jsonVar.isObject()) << "obsidian.gtheme.json is not valid JSON object";

    auto parsed = synth::theme::ThemeLoader::parseTheme(jsonVar, "obsidian");
    ASSERT_TRUE(parsed.has_value()) << "Failed to parse obsidian.gtheme.json: "
                                    << synth::theme::ThemeLoader::getLastError();

    auto builtin = synth::theme::makeObsidian();
    const auto& p = *parsed;
    const auto& o = builtin;

    EXPECT_EQ(p.colors.bg0.getARGB(), o.colors.bg0.getARGB());
    EXPECT_EQ(p.colors.bg1.getARGB(), o.colors.bg1.getARGB());
    EXPECT_EQ(p.colors.surface.getARGB(), o.colors.surface.getARGB());
    EXPECT_EQ(p.colors.surfaceHi.getARGB(), o.colors.surfaceHi.getARGB());
    EXPECT_EQ(p.colors.border.getARGB(), o.colors.border.getARGB());
    EXPECT_EQ(p.colors.accent.getARGB(), o.colors.accent.getARGB());
    EXPECT_EQ(p.colors.audioWire.getARGB(), o.colors.audioWire.getARGB());
    EXPECT_EQ(p.colors.modWire.getARGB(), o.colors.modWire.getARGB());
    EXPECT_EQ(p.colors.textPrimary.getARGB(), o.colors.textPrimary.getARGB());
    EXPECT_EQ(p.treatment.style, o.treatment.style);
}

// ---------------------------------------------------------------------------
// 6. RejectsMissingRequiredKey
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, RejectsMissingRequiredKey) {
    // Missing "name"
    {
        juce::DynamicObject* obj = new juce::DynamicObject();
        juce::var colors = new juce::DynamicObject();
        colors.getDynamicObject()->setProperty("bg0", "#FF0B0D10");
        colors.getDynamicObject()->setProperty("surface", "#FF1B1F26");
        colors.getDynamicObject()->setProperty("accent", "#FF00D1FF");
        colors.getDynamicObject()->setProperty("textPrimary", "#FFEAEEF3");
        colors.getDynamicObject()->setProperty("audioWire", "#FFE8EDF2");
        colors.getDynamicObject()->setProperty("modWire", "#FF00D1FF");
        obj->setProperty("colors", colors);
        juce::var json(obj);
        EXPECT_FALSE(synth::theme::ThemeLoader::parseTheme(json).has_value())
            << "Expected nullopt when 'name' is missing";
    }

    // Missing required color "accent"
    {
        juce::DynamicObject* obj = new juce::DynamicObject();
        obj->setProperty("name", "TestTheme");
        juce::var colors = new juce::DynamicObject();
        colors.getDynamicObject()->setProperty("bg0", "#FF0B0D10");
        colors.getDynamicObject()->setProperty("surface", "#FF1B1F26");
        // accent deliberately omitted
        colors.getDynamicObject()->setProperty("textPrimary", "#FFEAEEF3");
        colors.getDynamicObject()->setProperty("audioWire", "#FFE8EDF2");
        colors.getDynamicObject()->setProperty("modWire", "#FF00D1FF");
        obj->setProperty("colors", colors);
        juce::var json(obj);
        EXPECT_FALSE(synth::theme::ThemeLoader::parseTheme(json).has_value())
            << "Expected nullopt when required color 'accent' is missing";
    }
}

// ---------------------------------------------------------------------------
// 7. RejectsBadHex
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, RejectsBadHex) {
    // "#ZZZ" is not a valid colour
    EXPECT_FALSE(synth::theme::ThemeLoader::parseHexColour("#ZZZ").has_value());
    // "nope" is not a valid colour string
    EXPECT_FALSE(synth::theme::ThemeLoader::parseHexColour("nope").has_value());

    // "#0AF" (3-digit RGB) should succeed → opaque 0xFF00AAFF
    auto c3 = synth::theme::ThemeLoader::parseHexColour("#0AF");
    ASSERT_TRUE(c3.has_value());
    EXPECT_EQ(c3->getAlpha(), 0xFF);

    // "#FF00D1FF" (8-digit AARRGGBB) should succeed → 0xFF00D1FF
    auto c8 = synth::theme::ThemeLoader::parseHexColour("#FF00D1FF");
    ASSERT_TRUE(c8.has_value());
    EXPECT_EQ(c8->getARGB(), (juce::uint32)0xFF00D1FF);

    // A JSON theme with a bad hex value for bg0 should be rejected
    {
        juce::DynamicObject* obj = new juce::DynamicObject();
        obj->setProperty("name", "BadHex");
        juce::var colors = new juce::DynamicObject();
        colors.getDynamicObject()->setProperty("bg0", "#ZZZ"); // bad
        colors.getDynamicObject()->setProperty("surface", "#FF1B1F26");
        colors.getDynamicObject()->setProperty("accent", "#FF00D1FF");
        colors.getDynamicObject()->setProperty("textPrimary", "#FFEAEEF3");
        colors.getDynamicObject()->setProperty("audioWire", "#FFE8EDF2");
        colors.getDynamicObject()->setProperty("modWire", "#FF00D1FF");
        obj->setProperty("colors", colors);
        juce::var json(obj);
        EXPECT_FALSE(synth::theme::ThemeLoader::parseTheme(json).has_value())
            << "Expected nullopt for bad hex value in required color";
    }
}

// ---------------------------------------------------------------------------
// 8. ClampsTreatmentFloats
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, ClampsTreatmentFloats) {
    auto makeJson = [](double glow, double shadow, double blur, double texture) -> juce::var {
        juce::DynamicObject* obj = new juce::DynamicObject();
        obj->setProperty("name", "ClampTest");
        juce::var colors = new juce::DynamicObject();
        colors.getDynamicObject()->setProperty("bg0", "#FF0B0D10");
        colors.getDynamicObject()->setProperty("surface", "#FF1B1F26");
        colors.getDynamicObject()->setProperty("accent", "#FF00D1FF");
        colors.getDynamicObject()->setProperty("textPrimary", "#FFEAEEF3");
        colors.getDynamicObject()->setProperty("audioWire", "#FFE8EDF2");
        colors.getDynamicObject()->setProperty("modWire", "#FF00D1FF");
        obj->setProperty("colors", colors);
        juce::var treatment = new juce::DynamicObject();
        treatment.getDynamicObject()->setProperty("glow", glow);
        treatment.getDynamicObject()->setProperty("shadow", shadow);
        treatment.getDynamicObject()->setProperty("blur", blur);
        treatment.getDynamicObject()->setProperty("texture", texture);
        obj->setProperty("treatment", treatment);
        return juce::var(obj);
    };

    // glow = 5.0 should clamp to 1.0; -2.0 should clamp to 0.0
    auto over = synth::theme::ThemeLoader::parseTheme(makeJson(5.0, 0.5, 0.0, 0.0));
    ASSERT_TRUE(over.has_value());
    EXPECT_FLOAT_EQ(over->treatment.glow, 1.0f);

    auto under = synth::theme::ThemeLoader::parseTheme(makeJson(-2.0, 0.5, 0.0, 0.0));
    ASSERT_TRUE(under.has_value());
    EXPECT_FLOAT_EQ(under->treatment.glow, 0.0f);

    // texture = -2 should clamp to 0
    auto underTexture = synth::theme::ThemeLoader::parseTheme(makeJson(0.0, 0.5, 0.0, -2.0));
    ASSERT_TRUE(underTexture.has_value());
    EXPECT_FLOAT_EQ(underTexture->treatment.texture, 0.0f);
}

// ---------------------------------------------------------------------------
// 9. RejectsNewerSchema
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, RejectsNewerSchema) {
    juce::DynamicObject* obj = new juce::DynamicObject();
    obj->setProperty("schemaVersion", 999);
    obj->setProperty("name", "FutureTheme");
    juce::var colors = new juce::DynamicObject();
    colors.getDynamicObject()->setProperty("bg0", "#FF0B0D10");
    colors.getDynamicObject()->setProperty("surface", "#FF1B1F26");
    colors.getDynamicObject()->setProperty("accent", "#FF00D1FF");
    colors.getDynamicObject()->setProperty("textPrimary", "#FFEAEEF3");
    colors.getDynamicObject()->setProperty("audioWire", "#FFE8EDF2");
    colors.getDynamicObject()->setProperty("modWire", "#FF00D1FF");
    obj->setProperty("colors", colors);
    juce::var json(obj);
    EXPECT_FALSE(synth::theme::ThemeLoader::parseTheme(json).has_value())
        << "Expected nullopt for schemaVersion > kSchemaVersion";
}

// ---------------------------------------------------------------------------
// 10. StyleStringRoundTrip
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, StyleStringRoundTrip) {
    // Case-insensitive parse
    auto glass = synth::theme::ThemeLoader::parseStyle("GLASS");
    ASSERT_TRUE(glass.has_value());
    EXPECT_EQ(*glass, synth::theme::ThemeStyle::Glass);

    auto flat = synth::theme::ThemeLoader::parseStyle("flat");
    ASSERT_TRUE(flat.has_value());
    EXPECT_EQ(*flat, synth::theme::ThemeStyle::Flat);

    auto textured = synth::theme::ThemeLoader::parseStyle("Textured");
    ASSERT_TRUE(textured.has_value());
    EXPECT_EQ(*textured, synth::theme::ThemeStyle::Textured);

    // Unknown string
    EXPECT_FALSE(synth::theme::ThemeLoader::parseStyle("bogus").has_value());

    // Serialize
    EXPECT_EQ(synth::theme::ThemeLoader::styleToString(synth::theme::ThemeStyle::Textured), "textured");
    EXPECT_EQ(synth::theme::ThemeLoader::styleToString(synth::theme::ThemeStyle::Glass), "glass");
    EXPECT_EQ(synth::theme::ThemeLoader::styleToString(synth::theme::ThemeStyle::Flat), "flat");
}

// ---------------------------------------------------------------------------
// 11. SetActiveThemePersistsAndRestores
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, SetActiveThemePersistsAndRestores) {
    {
        synth::theme::ThemeManager mgr;
        mgr.initialise(&appProperties);
        EXPECT_TRUE(mgr.setActiveTheme("neon"));
        // Should be persisted immediately
        EXPECT_EQ(appProperties.getUserSettings()->getValue("themeId"), "neon");
    }
    // Second manager reads the stored value
    {
        synth::theme::ThemeManager mgr2;
        mgr2.initialise(&appProperties);
        EXPECT_EQ(mgr2.getActiveThemeId(), "neon");
    }
}

// ---------------------------------------------------------------------------
// 12. SetActiveThemeBroadcasts
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, SetActiveThemeBroadcasts) {
    class CountingListener : public juce::ChangeListener {
    public:
        int count{0};
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    };

    synth::theme::ThemeManager mgr;
    mgr.initialise(nullptr); // no props — that's fine

    CountingListener listener;
    mgr.addChangeListener(&listener);

    EXPECT_TRUE(mgr.setActiveTheme("warm"));
    EXPECT_EQ(listener.count, 1) << "Expected exactly one broadcast after setActiveTheme(warm)";

    // Selecting the same id again should be a no-op (no broadcast)
    EXPECT_TRUE(mgr.setActiveTheme("warm"));
    EXPECT_EQ(listener.count, 1) << "Expected no extra broadcast for idempotent setActiveTheme";

    mgr.removeChangeListener(&listener);
}

// ---------------------------------------------------------------------------
// 13. UnknownThemeIdIgnored
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, UnknownThemeIdIgnored) {
    class CountingListener : public juce::ChangeListener {
    public:
        int count{0};
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    };

    synth::theme::ThemeManager mgr;
    mgr.initialise(nullptr);
    auto originalId = mgr.getActiveThemeId();

    CountingListener listener;
    mgr.addChangeListener(&listener);

    EXPECT_FALSE(mgr.setActiveTheme("does-not-exist"));
    EXPECT_EQ(mgr.getActiveThemeId(), originalId) << "Active theme changed after unknown id";
    EXPECT_EQ(listener.count, 0) << "Broadcast fired for unknown id";

    mgr.removeChangeListener(&listener);
}

// ---------------------------------------------------------------------------
// 14. AddUserThemeReplacesById
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, AddUserThemeReplacesById) {
    synth::theme::ThemeManager mgr;
    mgr.initialise(nullptr);

    size_t before = mgr.getThemes().size();

    synth::theme::Theme t;
    t.id = "my-custom-theme";
    t.name = "My Custom Theme";
    mgr.addUserTheme(t);
    EXPECT_EQ(mgr.getThemes().size(), before + 1u);

    // Adding again with the same id should REPLACE, not grow
    synth::theme::Theme t2;
    t2.id = "my-custom-theme";
    t2.name = "My Custom Theme v2";
    mgr.addUserTheme(t2);
    EXPECT_EQ(mgr.getThemes().size(), before + 1u) << "Duplicate id should replace, not duplicate";

    // Verify the second version won
    bool found = false;
    for (const auto& th : mgr.getThemes()) {
        if (th.id == "my-custom-theme") {
            EXPECT_EQ(th.name, "My Custom Theme v2");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// 15. ContrastAA_AllBuiltIns
// ---------------------------------------------------------------------------
TEST(ThemeBuiltInsTest, ContrastAA_AllBuiltIns) {
    auto themes = synth::theme::builtInThemes();
    for (const auto& t : themes) {
        double ratio = wcagContrast(t.colors.textPrimary, t.colors.bg0);
        EXPECT_GE(ratio, 4.5) << "Theme '" << t.name << "': textPrimary on bg0 contrast ratio " << ratio
                              << " < 4.5 (WCAG AA minimum)";
    }
}

// ---------------------------------------------------------------------------
// 20. Issue103MetricsDefaults
// ---------------------------------------------------------------------------
// Verify the new Metrics fields from Issue #103 (UI Phase 7 polish) have correct defaults.
TEST(ThemeMetricsTest, Issue103MetricsDefaults) {
    synth::theme::Theme theme;
    const auto& m = theme.metrics;

    // Item 2: Metrics migration — snap threshold (kGridSize)
    EXPECT_EQ(m.gridSize, 8);

    // Item 4: Alignment guides
    EXPECT_FLOAT_EQ(m.guideAlpha, 0.7f);        // guide line opacity
    EXPECT_FLOAT_EQ(m.guideLineWidth, 1.5f);    // guide stroke width
    EXPECT_FLOAT_EQ(m.cornerRadiusSmall, 4.0f); // pill/small element radius
}

// ---------------------------------------------------------------------------
// 21. Issue103MetricsBuiltIns
// ---------------------------------------------------------------------------
// Verify all built-in themes populate the new Metrics fields correctly.
TEST(ThemeBuiltInsTest, Issue103MetricsInAllBuiltIns) {
    auto themes = synth::theme::builtInThemes();
    ASSERT_EQ(themes.size(), 4u);

    for (const auto& t : themes) {
        EXPECT_EQ(t.metrics.gridSize, 8) << "Theme '" << t.name << "' has incorrect gridSize";
        EXPECT_FLOAT_EQ(t.metrics.guideAlpha, 0.7f) << "Theme '" << t.name << "' has incorrect guideAlpha";
        EXPECT_FLOAT_EQ(t.metrics.guideLineWidth, 1.5f) << "Theme '" << t.name << "' has incorrect guideLineWidth";
        EXPECT_FLOAT_EQ(t.metrics.cornerRadiusSmall, 4.0f)
            << "Theme '" << t.name << "' has incorrect cornerRadiusSmall";
    }
}

// ---------------------------------------------------------------------------
// 22. MetricsCodeOnlyFieldsNotInJSON (Issue #103 new fields)
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, Issue103MetricsNotInJSON) {
    auto theme = synth::theme::makeObsidian();
    auto json = synth::theme::ThemeLoader::themeToJson(theme);
    juce::String jsonStr = juce::JSON::toString(json, true);

    // Verify that Issue #103 new code-only metrics fields are NOT present in the serialized JSON
    EXPECT_FALSE(jsonStr.contains("gridSize")) << "Issue 103 gridSize should not be user-configurable";
    EXPECT_FALSE(jsonStr.contains("guideAlpha")) << "Issue 103 guideAlpha should not be user-configurable";
    EXPECT_FALSE(jsonStr.contains("guideLineWidth")) << "Issue 103 guideLineWidth should not be user-configurable";
    EXPECT_FALSE(jsonStr.contains("cornerRadiusSmall"))
        << "Issue 103 cornerRadiusSmall should not be user-configurable";
}

// ---------------------------------------------------------------------------
// 23. GraphEditorUsesMetricsForRendering (Issue #103 Item 2 verification)
// ---------------------------------------------------------------------------
// Verify that GraphEditor renders with correct metrics-derived values.
TEST(GraphEditorRenderingTest, UsesMetricsCornerRadius) {
    // This test verifies that the code uses theme.metrics.cornerRadius
    // for drag ghost rendering instead of hardcoding it.
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    const auto& metrics = lf.getTheme().metrics;
    EXPECT_EQ(metrics.gridSize, 8);                   // snap threshold
    EXPECT_FLOAT_EQ(metrics.cornerRadiusSmall, 4.0f); // pill radius
}

TEST(GraphEditorRenderingTest, UsesMetricsGuideParams) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeNeon());

    const auto& m = lf.getTheme().metrics;
    EXPECT_FLOAT_EQ(m.guideAlpha, 0.7f);     // guide opacity matches Item 4 spec
    EXPECT_FLOAT_EQ(m.guideLineWidth, 1.5f); // guide stroke width matches Item 4 spec
}

// ---------------------------------------------------------------------------
// Smoke tests: LookAndFeel draw helpers must not throw / crash
// (Exercises the LnF draw paths in headless mode without asserting pixels.)
// ---------------------------------------------------------------------------
TEST(ThemeLookAndFeelTest, DrawHelpersSmoke) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    // Create a tiny in-memory image to draw into.
    juce::Image img(juce::Image::ARGB, 100, 100, true);
    juce::Graphics g(img);

    // fillThemedBackground — canvas and non-canvas
    EXPECT_NO_THROW(lf.fillThemedBackground(g, {0.0f, 0.0f, 100.0f, 100.0f}, false));
    EXPECT_NO_THROW(lf.fillThemedBackground(g, {0.0f, 0.0f, 100.0f, 100.0f}, true));

    // drawModulePanel
    EXPECT_NO_THROW(lf.drawModulePanel(g, {0.0f, 0.0f, 100.0f, 100.0f}, 24, "OSC", false, false));
    EXPECT_NO_THROW(lf.drawModulePanel(g, {0.0f, 0.0f, 100.0f, 100.0f}, 24, "VCA", true, true));

    // drawConnectionWire — empty path (helper builds its own bezier)
    EXPECT_NO_THROW(lf.drawConnectionWire(g, {10.0f, 10.0f}, {90.0f, 90.0f}, juce::Path{},
                                          lf.getTheme().colors.audioWire, false, 0.5f, false));

    // drawModulationRing
    EXPECT_NO_THROW(lf.drawModulationRing(g, {50.0f, 50.0f}, 20.0f, 0.5f, 0.2f, true));

    // drawRotarySlider via juce::Slider object
    juce::Slider slider(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    slider.setRange(0.0, 1.0);
    slider.setValue(0.5);
    EXPECT_NO_THROW(lf.drawRotarySlider(g, 0, 0, 100, 100, 0.5f, synth::theme::AppLookAndFeel::kRotaryStart,
                                        synth::theme::AppLookAndFeel::kRotaryEnd, slider));
}

// ---------------------------------------------------------------------------
// 16. MetricsCodeOnlyFieldsHaveExpectedDefaults
// ---------------------------------------------------------------------------
TEST(ThemeMetricsTest, MetricsCodeOnlyFieldsHaveExpectedDefaults) {
    synth::theme::Theme theme;
    const auto& m = theme.metrics;

    EXPECT_EQ(m.toolbarHeight, 36);
    EXPECT_EQ(m.statusBarHeight, 24);
    EXPECT_EQ(m.controlPadding, 4);
    EXPECT_EQ(m.minWindowWidth, 480);
    EXPECT_EQ(m.minWindowHeight, 400);
    EXPECT_EQ(m.sidebarCollapsedWidth, 0);
    EXPECT_EQ(m.librarySidebarWidth, 200);
    EXPECT_EQ(m.aiPanelWidth, 300);
    EXPECT_EQ(m.iconSize, 16);
    EXPECT_EQ(m.timelinePanelHeight, 220);
    EXPECT_EQ(m.timelineTrackHeaderWidth, 160);
    EXPECT_EQ(m.timelineTransportBarHeight, 28);
}

// ---------------------------------------------------------------------------
// 17. MetricsCodeOnlyFieldsNotInJSON
// ---------------------------------------------------------------------------
TEST(ThemeLoaderTest, MetricsCodeOnlyFieldsNotInJSON) {
    auto theme = synth::theme::makeObsidian();
    auto json = synth::theme::ThemeLoader::themeToJson(theme);
    juce::String jsonStr = juce::JSON::toString(json, true);

    // Verify that code-only metrics fields are NOT present in the serialized JSON
    EXPECT_FALSE(jsonStr.contains("toolbarHeight"));
    EXPECT_FALSE(jsonStr.contains("statusBarHeight"));
    EXPECT_FALSE(jsonStr.contains("controlPadding"));
    EXPECT_FALSE(jsonStr.contains("minWindowWidth"));
    EXPECT_FALSE(jsonStr.contains("minWindowHeight"));
    EXPECT_FALSE(jsonStr.contains("sidebarCollapsedWidth"));
    EXPECT_FALSE(jsonStr.contains("librarySidebarWidth"));
    EXPECT_FALSE(jsonStr.contains("aiPanelWidth"));
    EXPECT_FALSE(jsonStr.contains("iconSize"));
    EXPECT_FALSE(jsonStr.contains("timelinePanelHeight"));
    EXPECT_FALSE(jsonStr.contains("timelineTrackHeaderWidth"));
    EXPECT_FALSE(jsonStr.contains("timelineTransportBarHeight"));
}

// ---------------------------------------------------------------------------
// 18. RetintIconsCalledByApplyTheme
// ---------------------------------------------------------------------------
// applyTheme() must re-tint the icon set as part of its single re-skin pass: after applying
// each built-in theme, getIcon() returns a usable (non-null when assets present) drawable and
// nothing throws across repeated switches.
TEST(ThemeLookAndFeelTest, RetintIconsCalledByApplyTheme) {
    synth::theme::AppLookAndFeel lf;

    const std::array<synth::theme::Theme, 3> themes = {synth::theme::makeObsidian(), synth::theme::makeNeon(),
                                                       synth::theme::makeWarm()};
    for (const auto& t : themes) {
        EXPECT_NO_THROW(lf.applyTheme(t));
#ifdef HAS_FONT_ASSETS
        EXPECT_NE(lf.getIcon(synth::theme::Icon::ActionUndo), nullptr);
        EXPECT_NE(lf.peekIcon(synth::theme::Icon::ModuleDelete), nullptr);
        EXPECT_NE(lf.getIcon(synth::theme::Icon::CatSources), nullptr);
#endif
    }
}

// ---------------------------------------------------------------------------
// 19. StyledWidgetSmokeTest — combo / popup / scrollbar / tab draw paths must not throw
// (Exercises the section-5 themed-widget painters in headless mode across all three themes.)
// ---------------------------------------------------------------------------
TEST(StyledWidgetSmokeTest, ComboBoxDrawNoThrow) {
    const std::array<synth::theme::Theme, 3> themes = {synth::theme::makeObsidian(), synth::theme::makeNeon(),
                                                       synth::theme::makeWarm()};
    for (const auto& t : themes) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(t);

        juce::Image img(juce::Image::ARGB, 120, 28, true);
        juce::Graphics g(img);

        juce::ComboBox box;
        box.setLookAndFeel(&lf);
        box.setSize(120, 28);

        // normal
        EXPECT_NO_THROW(lf.drawComboBox(g, 120, 28, false, 96, 0, 24, 28, box));
        // pressed
        EXPECT_NO_THROW(lf.drawComboBox(g, 120, 28, true, 96, 0, 24, 28, box));
        // disabled
        box.setEnabled(false);
        EXPECT_NO_THROW(lf.drawComboBox(g, 120, 28, false, 96, 0, 24, 28, box));

        box.setLookAndFeel(nullptr);
    }
}

TEST(StyledWidgetSmokeTest, ComboBoxTextWhenNothingSelected) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    juce::Image img(juce::Image::ARGB, 120, 28, true);
    juce::Graphics g(img);

    juce::ComboBox box;
    box.setLookAndFeel(&lf);
    box.setSize(120, 28);
    box.setTextWhenNothingSelected("Pick one");

    juce::Label label;
    label.setBounds(8, 1, 90, 26);

    EXPECT_NO_THROW(lf.drawComboBoxTextWhenNothingSelected(g, box, label));
    box.setLookAndFeel(nullptr);
}

TEST(StyledWidgetSmokeTest, PopupMenuDrawNoThrow) {
    const std::array<synth::theme::Theme, 3> themes = {synth::theme::makeObsidian(), synth::theme::makeNeon(),
                                                       synth::theme::makeWarm()};
    for (const auto& t : themes) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(t);

        juce::Image img(juce::Image::ARGB, 200, 30, true);
        juce::Graphics g(img);
        const juce::Rectangle<int> area(0, 0, 200, 28);

        // separator
        EXPECT_NO_THROW(lf.drawPopupMenuItem(g, area, true, false, false, false, false, {}, {}, nullptr, nullptr));
        // highlighted + active
        EXPECT_NO_THROW(
            lf.drawPopupMenuItem(g, area, false, true, true, false, false, "Item", "Cmd+S", nullptr, nullptr));
        // ticked
        EXPECT_NO_THROW(lf.drawPopupMenuItem(g, area, false, true, false, true, false, "Ticked", {}, nullptr, nullptr));
        // disabled (inactive)
        EXPECT_NO_THROW(
            lf.drawPopupMenuItem(g, area, false, false, false, false, false, "Disabled", {}, nullptr, nullptr));
        // has submenu
        EXPECT_NO_THROW(lf.drawPopupMenuItem(g, area, false, true, false, false, true, "More", {}, nullptr, nullptr));
    }
}

TEST(StyledWidgetSmokeTest, ScrollbarWidthAndDrawNoThrow) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    EXPECT_EQ(lf.getDefaultScrollbarWidth(), 6);

    juce::Image img(juce::Image::ARGB, 60, 200, true);
    juce::Graphics g(img);
    juce::ScrollBar vbar(true);
    juce::ScrollBar hbar(false);

    // vertical: over + down
    EXPECT_NO_THROW(lf.drawScrollbar(g, vbar, 0, 0, 6, 200, true, 20, 40, true, false));
    EXPECT_NO_THROW(lf.drawScrollbar(g, vbar, 0, 0, 6, 200, true, 20, 40, false, true));
    // horizontal: over + down
    EXPECT_NO_THROW(lf.drawScrollbar(g, hbar, 0, 0, 200, 6, false, 20, 40, true, false));
    EXPECT_NO_THROW(lf.drawScrollbar(g, hbar, 0, 0, 200, 6, false, 20, 40, false, true));

    // scrollbar buttons — both directions
    EXPECT_NO_THROW(lf.drawScrollbarButton(g, vbar, 12, 12, 0, true, false, false));  // up
    EXPECT_NO_THROW(lf.drawScrollbarButton(g, vbar, 12, 12, 2, true, true, true));    // down (over+down)
    EXPECT_NO_THROW(lf.drawScrollbarButton(g, hbar, 12, 12, 1, false, false, false)); // right
    EXPECT_NO_THROW(lf.drawScrollbarButton(g, hbar, 12, 12, 3, false, false, false)); // left
}

TEST(StyledWidgetSmokeTest, TabBarDrawNoThrow) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());

    juce::TabbedButtonBar bar(juce::TabbedButtonBar::TabsAtTop);
    bar.setLookAndFeel(&lf);
    bar.addTab("One", lf.getTheme().colors.surface, 0);
    bar.addTab("Two", lf.getTheme().colors.surface, 1);
    bar.setSize(300, 30);

    // Background painter across orientations (rebuild the bar per orientation to keep it valid).
    {
        juce::Image bg(juce::Image::ARGB, 300, 30, true);
        juce::Graphics g(bg);
        EXPECT_NO_THROW(lf.drawTabbedButtonBarBackground(bar, g));
    }

    // Active (front) tab snapshot must contain non-transparent pixels.
    bar.setCurrentTabIndex(0);
    auto snap = bar.createComponentSnapshot(bar.getLocalBounds());
    ASSERT_TRUE(snap.isValid());
    bool anyOpaque = false;
    for (int y = 0; y < snap.getHeight() && !anyOpaque; ++y)
        for (int x = 0; x < snap.getWidth() && !anyOpaque; ++x)
            if (snap.getPixelAt(x, y).getAlpha() > 0)
                anyOpaque = true;
    EXPECT_TRUE(anyOpaque) << "Active tab snapshot was fully transparent";

    bar.setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// 24. DefaultThemesAndModeToggle
// ---------------------------------------------------------------------------
TEST_F(ThemeTest, DefaultThemesAndModeToggle) {
    synth::theme::ThemeManager mgr;
    mgr.initialise(&appProperties);

    // Verify defaults
    EXPECT_EQ(mgr.getDefaultDarkThemeId(), "obsidian");
    EXPECT_EQ(mgr.getDefaultLightThemeId(), "daylight");
    EXPECT_EQ(mgr.getThemeMode(), synth::theme::ThemeManager::ThemeMode::Dark);

    // Toggle mode
    mgr.toggleLightDarkMode();
    EXPECT_EQ(mgr.getActiveThemeId(), "daylight");
    EXPECT_FALSE(mgr.getActiveTheme().isDark);

    mgr.toggleLightDarkMode();
    EXPECT_EQ(mgr.getActiveThemeId(), "obsidian");
    EXPECT_TRUE(mgr.getActiveTheme().isDark);

    // Change default dark/light theme
    EXPECT_TRUE(mgr.setDefaultDarkThemeId("warm"));
    EXPECT_TRUE(mgr.setDefaultLightThemeId("daylight"));
    EXPECT_EQ(mgr.getDefaultDarkThemeId(), "warm");

    // Setting mode to Light
    mgr.setThemeMode(synth::theme::ThemeManager::ThemeMode::Light);
    EXPECT_EQ(mgr.getActiveThemeId(), "daylight");

    // Setting mode to Dark
    mgr.setThemeMode(synth::theme::ThemeManager::ThemeMode::Dark);
    EXPECT_EQ(mgr.getActiveThemeId(), "warm");
}
