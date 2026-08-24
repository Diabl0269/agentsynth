// Tests for piano-roll note colour resolution (Theme foundations, task A1).
//
// Modeled on CableColourTests.cpp:
//   1. The pure resolver (precedence, velocity, selection, muting) — no GUI, no LookAndFeel.
//   2. Persistence round-trip for the per-pitch-class override layer.
//   3. A contrast guard over every built-in theme's new note/piano-key tokens.

#include "../Source/UI/NoteColour.h"
#include "../Source/UI/Theme/BuiltInThemes.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace synth::ui;

namespace {

juce::File tempSettingsFile(const juce::String& name) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
}

std::unique_ptr<juce::PropertiesFile> makeProps(const juce::String& name) {
    auto file = tempSettingsFile(name);
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

// WCAG relative luminance / contrast ratio (same formula used across the theme test suites).
double relativeLuminance(juce::Colour c) {
    auto channel = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

double contrastRatio(juce::Colour a, juce::Colour b) {
    const auto l1 = relativeLuminance(a);
    const auto l2 = relativeLuminance(b);
    const auto hi = std::max(l1, l2);
    const auto lo = std::min(l1, l2);
    return (hi + 0.05) / (lo + 0.05);
}

// Composite a possibly-translucent colour over an opaque background (Neon's surfaceHi is the
// only built-in surface with alpha < 1; every other theme's composite is a no-op).
juce::Colour compositeOver(juce::Colour fg, juce::Colour bg) {
    const float a = fg.getFloatAlpha();
    auto mix = [a](float f, float b) { return f * a + b * (1.0f - a); };
    return juce::Colour::fromFloatRGBA(mix(fg.getFloatRed(), bg.getFloatRed()),
                                       mix(fg.getFloatGreen(), bg.getFloatGreen()),
                                       mix(fg.getFloatBlue(), bg.getFloatBlue()), 1.0f);
}

// Plain Euclidean distance in 8-bit RGB space. Two saturated colours at similar luminance can
// have a WCAG contrast ratio near 1:1 while still reading as obviously different hues (e.g.
// electric green vs orange-red) — this is the metric that actually tells them apart.
double rgbDistance(juce::Colour a, juce::Colour b) {
    const double dr = (double)a.getRed() - (double)b.getRed();
    const double dg = (double)a.getGreen() - (double)b.getGreen();
    const double db = (double)a.getBlue() - (double)b.getBlue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

synth::theme::Colors obsidianColors() { return synth::theme::makeObsidian().colors; }

} // namespace

//==============================================================================
// 1. Resolver precedence
//==============================================================================

TEST(NoteColourResolveTest, OutOfScaleBeatsOverrideBeatsTheme) {
    const auto colors = obsidianColors();
    NoteColourOverrides overrides;
    overrides.set(0, juce::Colour(0xff00FF00)); // pitch class C overridden to pure green

    // Theme default (no override, no out-of-scale): pitch class D (2), unaffected by the C override.
    const auto themeOnly = resolveNoteColour(colors, /*pitch*/ 2, 100, false, false, false, overrides);
    EXPECT_NE(themeOnly.fill.withAlpha(1.0f), juce::Colour(0xff00FF00));

    // Override wins over theme for pitch class C (0), when not out-of-scale.
    const auto overridden = resolveNoteColour(colors, /*pitch*/ 12, 100, false, false, false, overrides);
    // withMultipliedBrightness/alpha are applied on top, so compare hue family via RGB distance
    // to the raw override rather than exact equality.
    EXPECT_LT(rgbDistance(overridden.fill.withAlpha(1.0f), juce::Colour(0xff00FF00)), 40.0);

    // outOfScale forces the warning family even though pitch class C has an override pinned.
    const auto outOfScale = resolveNoteColour(colors, /*pitch*/ 12, 100, false, false, true, overrides);
    EXPECT_GT(rgbDistance(outOfScale.fill.withAlpha(1.0f), juce::Colour(0xff00FF00)), 40.0);
    EXPECT_LT(rgbDistance(outOfScale.fill.withAlpha(1.0f), colors.noteOutOfScale), 60.0);
}

TEST(NoteColourResolveTest, NoOverrideFallsBackToThemeNoteFill) {
    const auto colors = obsidianColors();
    NoteColourOverrides empty;
    const auto paint = resolveNoteColour(colors, 5, 100, false, false, false, empty);
    EXPECT_LT(rgbDistance(paint.fill.withAlpha(1.0f), colors.noteFill), 40.0);
}

//==============================================================================
// 2. Velocity, selection, muting
//==============================================================================

TEST(NoteColourResolveTest, VelocityBrightnessIsMonotonic) {
    const auto colors = obsidianColors();
    NoteColourOverrides none;

    double prevBrightness = -1.0;
    for (int velocity : {1, 32, 64, 96, 127}) {
        const auto paint = resolveNoteColour(colors, 3, velocity, false, false, false, none);
        const double brightness = (double)paint.fill.getBrightness();
        EXPECT_GE(brightness, prevBrightness) << "velocity " << velocity;
        prevBrightness = brightness;
    }
}

TEST(NoteColourResolveTest, SelectedUsesNoteSelectedBorder) {
    const auto colors = obsidianColors();
    NoteColourOverrides none;

    const auto unselected = resolveNoteColour(colors, 3, 100, false, false, false, none);
    EXPECT_EQ(unselected.border, colors.noteBorder);

    const auto selected = resolveNoteColour(colors, 3, 100, true, false, false, none);
    EXPECT_EQ(selected.border, colors.noteSelected);

    // Selection also raises fill alpha (0.95 vs 0.8), matching the pre-existing paintNote formula.
    EXPECT_GT(selected.fill.getFloatAlpha(), unselected.fill.getFloatAlpha());
}

TEST(NoteColourResolveTest, MutedDimsFillAndBorder) {
    const auto colors = obsidianColors();
    NoteColourOverrides none;

    const auto normal = resolveNoteColour(colors, 3, 100, false, false, false, none);
    const auto muted = resolveNoteColour(colors, 3, 100, false, true, false, none);

    EXPECT_LT(muted.fill.getFloatAlpha(), normal.fill.getFloatAlpha());
    EXPECT_LT(muted.border.getFloatAlpha(), normal.border.getFloatAlpha());
    EXPECT_LE(muted.fill.getSaturation(), normal.fill.getSaturation() + 1e-4f);
}

TEST(NoteColourResolveTest, MutedSelectionStillUsesSelectedBorderFamily) {
    // Muting a selected note must not read as deselecting it: the border still comes from
    // noteSelected (just dimmed), not from noteBorder.
    const auto colors = obsidianColors();
    NoteColourOverrides none;

    const auto mutedSelected = resolveNoteColour(colors, 3, 100, true, true, false, none);
    const auto mutedUnselected = resolveNoteColour(colors, 3, 100, false, true, false, none);
    EXPECT_NE(mutedSelected.border, mutedUnselected.border);
}

//==============================================================================
// 3. Persistence round-trip
//==============================================================================

TEST(NoteColourPersistenceTest, RoundTripsAndAbsentKeyMeansNoOverrides) {
    auto props = makeProps("AgentSynthNoteColourAbsent");
    EXPECT_FALSE(props->containsKey(noteColourOverridesKey()));
    const auto loaded = loadNoteColourOverrides(*props);
    EXPECT_FALSE(loaded.hasAny());
    tempSettingsFile("AgentSynthNoteColourAbsent").deleteFile();
}

TEST(NoteColourPersistenceTest, RoundTripsSparseOverrides) {
    auto props = makeProps("AgentSynthNoteColourRoundTrip");

    NoteColourOverrides o;
    o.set(0, juce::Colour(0xff112233));  // C
    o.set(11, juce::Colour(0xff445566)); // B
    saveNoteColourOverrides(*props, o);

    const auto loaded = loadNoteColourOverrides(*props);
    ASSERT_TRUE(loaded.perPitchClass[0].has_value());
    EXPECT_EQ(*loaded.perPitchClass[0], juce::Colour(0xff112233));
    ASSERT_TRUE(loaded.perPitchClass[11].has_value());
    EXPECT_EQ(*loaded.perPitchClass[11], juce::Colour(0xff445566));

    // Untouched slots stay unset.
    for (int i = 1; i < 11; ++i)
        EXPECT_FALSE(loaded.perPitchClass[(size_t)i].has_value()) << "slot " << i;

    // Clearing and re-saving removes the entry from the loaded result too.
    NoteColourOverrides cleared = loaded;
    cleared.clear(0);
    saveNoteColourOverrides(*props, cleared);
    EXPECT_FALSE(loadNoteColourOverrides(*props).perPitchClass[0].has_value());
    EXPECT_TRUE(loadNoteColourOverrides(*props).perPitchClass[11].has_value());

    tempSettingsFile("AgentSynthNoteColourRoundTrip").deleteFile();
}

TEST(NoteColourPersistenceTest, MalformedSingleSlotValueDoesNotThrow) {
    auto props = makeProps("AgentSynthNoteColourBadSlot");

    // Exactly 12 slots (correct shape), but one slot is not a colour string at all — a
    // hand-edited settings file rather than a truncated one. Must not throw; juce::Colour's own
    // fromString is what decides the fallback value, this just guards the call site.
    juce::StringArray tokens;
    for (int i = 0; i < 12; ++i)
        tokens.add(i == 4 ? juce::String("not-a-colour") : juce::String());
    props->setValue(noteColourOverridesKey(), tokens.joinIntoString(","));

    NoteColourOverrides loaded;
    EXPECT_NO_THROW(loaded = loadNoteColourOverrides(*props));
    // Every other slot stays unset regardless of what the malformed one produced.
    for (int i = 0; i < 12; ++i)
        if (i != 4)
            EXPECT_FALSE(loaded.perPitchClass[(size_t)i].has_value()) << "slot " << i;

    tempSettingsFile("AgentSynthNoteColourBadSlot").deleteFile();
}

TEST(NoteColourPersistenceTest, MalformedSlotCountIsTreatedAsAbsent) {
    auto props = makeProps("AgentSynthNoteColourMalformed");

    // Only 3 slots instead of 12 — a hand-edited or truncated settings file.
    props->setValue(noteColourOverridesKey(), "#FF112233,,");
    const auto loaded = loadNoteColourOverrides(*props);
    EXPECT_FALSE(loaded.hasAny()) << "wrong slot count must not be half-applied";

    tempSettingsFile("AgentSynthNoteColourMalformed").deleteFile();
}

//==============================================================================
// 4. Contrast guard for every built-in theme
//==============================================================================

TEST(NoteColourThemeTest, NoteFillIsLegibleAgainstBothRowBackgrounds) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        const auto surfaceHiOverBg0 = compositeOver(c.surfaceHi, c.bg0);

        EXPECT_GE(contrastRatio(c.noteFill, c.bg1), 1.5) << "Theme '" << t.name << "': noteFill vs bg1";
        EXPECT_GE(contrastRatio(c.noteFill, surfaceHiOverBg0), 1.5)
            << "Theme '" << t.name << "': noteFill vs surfaceHi-over-bg0";
    }
}

TEST(NoteColourThemeTest, NoteFillIsClearlyDistinctFromOutOfScaleWarning) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        EXPECT_GT(rgbDistance(c.noteFill, c.noteOutOfScale), 60.0)
            << "Theme '" << t.name << "': noteFill too close to noteOutOfScale";
    }
}

TEST(NoteColourThemeTest, OutOfScaleIsARedOrangeFamilyInEveryTheme) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors.noteOutOfScale;
        // Red/orange family: red channel clearly dominant over blue.
        EXPECT_GT((int)c.getRed(), (int)c.getBlue()) << "Theme '" << t.name << "'";
        EXPECT_GT((int)c.getRed(), 120) << "Theme '" << t.name << "'";
    }
}

TEST(NoteColourThemeTest, PianoKeysAreClearlySeparated) {
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        EXPECT_GE(contrastRatio(c.pianoKeyWhite, c.pianoKeyBlack), 5.0)
            << "Theme '" << t.name << "': pianoKeyWhite vs pianoKeyBlack";
    }
}

TEST(NoteColourThemeTest, NeonNoteFillIsNotInThePurpleFamily) {
    // Neon's bg0/bg1/midiWire are all near-black purple / purple — the note colour must not
    // stay in that family, or notes disappear into the rest of the theme.
    const auto neon = synth::theme::makeNeon();
    EXPECT_GT(rgbDistance(neon.colors.noteFill, neon.colors.midiWire), 80.0);
    // A green/cyan hue: green or blue channel should dominate red.
    EXPECT_GT((int)neon.colors.noteFill.getGreen(), (int)neon.colors.noteFill.getRed());
}

TEST(NoteColourThemeTest, NoteBorderIsNotJustNoteFillDarkened) {
    // The spec explicitly calls out "contrast, not just .darker(0.5)" — guard against a
    // regression that ties noteBorder to noteFill by construction rather than picking it.
    for (const auto& t : synth::theme::builtInThemes()) {
        const auto& c = t.colors;
        EXPECT_NE(c.noteBorder, c.noteFill.darker(0.5f)) << "Theme '" << t.name << "'";
    }
}
