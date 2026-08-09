// IconLibraryTests.cpp
// Headless unit tests for the Agent Synth SVG icon registry (synth::theme::IconLibrary).
// Covers: enum coverage, headless null-fallback, clone independence, tint correctness across
// repeated theme switches, and the BinaryData symbol-naming convention guard.

#include "../Source/UI/Theme/IconLibrary.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

using synth::theme::Icon;
using synth::theme::IconLibrary;

namespace {

// True when Assets BinaryData is linked (the icons actually load). The headless
// test target links Core, which compiles IconLibrary with HAS_FONT_ASSETS,
// so this is normally true; the #else branch keeps the null-path test meaningful regardless.
constexpr bool kAssetsPresent =
#ifdef HAS_FONT_ASSETS
    true;
#else
    false;
#endif

// Render a Drawable into an ARGB image and report whether any pixel is "close enough" to the
// target colour (ignoring antialiased / transparent edges). Tolerance covers SVG edge blending.
bool imageContainsColour(const juce::Image& img, juce::Colour target, int tol = 24) {
    for (int y = 0; y < img.getHeight(); ++y) {
        for (int x = 0; x < img.getWidth(); ++x) {
            const auto p = img.getPixelAt(x, y);
            if (p.getAlpha() < 200)
                continue; // skip transparent / heavily-blended edge pixels
            if (std::abs((int)p.getRed() - (int)target.getRed()) <= tol &&
                std::abs((int)p.getGreen() - (int)target.getGreen()) <= tol &&
                std::abs((int)p.getBlue() - (int)target.getBlue()) <= tol)
                return true;
        }
    }
    return false;
}

// Snapshot an icon (held by a DrawableButton, per the spec) into an image for pixel inspection.
juce::Image snapshotIcon(const IconLibrary& lib, Icon id) {
    auto drawable = lib.getDrawable(id);
    if (drawable == nullptr)
        return {};

    juce::DrawableButton button("icon", juce::DrawableButton::ImageFitted);
    button.setImages(drawable.get());
    button.setSize(48, 48);
    return button.createComponentSnapshot(button.getLocalBounds());
}

} // namespace

// ---------------------------------------------------------------------------
// 1. AllIconEnumValuesHaveEntry
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, AllIconEnumValuesHaveEntry) {
    IconLibrary lib;
    for (int i = 0; i < (int)Icon::kCount; ++i) {
        auto d = lib.getDrawable(static_cast<Icon>(i));
        if (kAssetsPresent)
            EXPECT_NE(d, nullptr) << "Icon index " << i << " returned null with assets present";
        // With assets absent the contract is simply "no crash"; nullptr is acceptable.
    }
}

// ---------------------------------------------------------------------------
// 2. NullFallbackWhenAssetsAbsent
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, NullFallbackWhenAssetsAbsent) {
#ifndef HAS_FONT_ASSETS
    IconLibrary lib;
    for (int i = 0; i < (int)Icon::kCount; ++i)
        EXPECT_EQ(lib.getDrawable(static_cast<Icon>(i)), nullptr);
    EXPECT_EQ(lib.peekDrawable(Icon::ActionUndo), nullptr);
#else
    // Assets are present in this build; verify the guarded path simply doesn't crash and that
    // peek/get are consistent (both non-null) — the genuine no-asset path is compile-guarded.
    IconLibrary lib;
    EXPECT_NO_THROW({
        auto d = lib.getDrawable(Icon::ActionUndo);
        EXPECT_NE(d, nullptr);
        EXPECT_NE(lib.peekDrawable(Icon::ActionUndo), nullptr);
    });
#endif
}

// ---------------------------------------------------------------------------
// 3. ClonedDrawableIsIndependent
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, ClonedDrawableIsIndependent) {
    if (!kAssetsPresent)
        GTEST_SKIP() << "Requires embedded assets";

    IconLibrary lib;
    auto a = lib.getDrawable(Icon::ActionSave);
    auto b = lib.getDrawable(Icon::ActionSave);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get()) << "getDrawable must return distinct owned clones";
}

// ---------------------------------------------------------------------------
// 4. TintColourChangeApplied
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, TintColourChangeApplied) {
    if (!kAssetsPresent)
        GTEST_SKIP() << "Requires embedded assets";

    IconLibrary lib;
    const juce::Colour colour1 = juce::Colours::red;
    const juce::Colour colour2 = juce::Colours::lime; // pure green (0xff00ff00)

    lib.setTintColour(Icon::ActionSettings, colour1);
    {
        auto img = snapshotIcon(lib, Icon::ActionSettings);
        ASSERT_TRUE(img.isValid());
        EXPECT_TRUE(imageContainsColour(img, colour1)) << "First tint colour not present";
    }

    lib.setTintColour(Icon::ActionSettings, colour2);
    {
        auto img = snapshotIcon(lib, Icon::ActionSettings);
        ASSERT_TRUE(img.isValid());
        // The new colour must be present and the OLD colour gone (proves we re-tint from the
        // untinted original, not from an already-red drawable).
        EXPECT_TRUE(imageContainsColour(img, colour2)) << "Second tint colour not present";
        EXPECT_FALSE(imageContainsColour(img, colour1)) << "Stale first-tint colour still present";
    }
}

// ---------------------------------------------------------------------------
// 5. RetintMultipleSwitchesStable
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, RetintMultipleSwitchesStable) {
    if (!kAssetsPresent)
        GTEST_SKIP() << "Requires embedded assets";

    IconLibrary lib;
    const juce::Colour a = juce::Colours::cyan;
    const juce::Colour b = juce::Colours::magenta;

    juce::Colour last;
    for (int i = 0; i < 100; ++i) {
        last = (i % 2 == 0) ? a : b;
        EXPECT_NO_THROW(lib.setTintColour(Icon::ToggleMatrix, last));
    }

    // Final tint (i==99 → odd → magenta) must be the colour that renders.
    auto img = snapshotIcon(lib, Icon::ToggleMatrix);
    ASSERT_TRUE(img.isValid());
    EXPECT_TRUE(imageContainsColour(img, last)) << "Final tint colour not stable after 100 switches";
}

// ---------------------------------------------------------------------------
// 6. SvgBinaryDataNamingConvention
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, SvgBinaryDataNamingConvention) {
#ifdef HAS_FONT_ASSETS
    // JUCE's binary-data name mangler STRIPS hyphens (it does not convert them to underscores),
    // so 'action-undo.svg' becomes BinaryData::actionundo_svg. Guarding the exact symbol catches
    // any future CMake rename that would silently break the IconLibrary lookup table.
    EXPECT_NE(BinaryData::actionundo_svg, nullptr);
    EXPECT_GT(BinaryData::actionundo_svgSize, 0);
    EXPECT_NE(BinaryData::catmodulationfx_svg, nullptr);
    EXPECT_GT(BinaryData::catmodulationfx_svgSize, 0);
#else
    GTEST_SKIP() << "BinaryData not linked in this build";
#endif
}

// ---------------------------------------------------------------------------
// 7. TransportPlayIsScaffolding
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, TransportPlayIsScaffolding) {
    // TransportPlay is in the enum and loads without crashing; it is intentionally not wired to
    // any DrawableButton this phase (compile-level expectation — nothing to assert at runtime
    // beyond a clean load).
    IconLibrary lib;
    EXPECT_NO_THROW({
        auto d = lib.getDrawable(Icon::TransportPlay);
        if (kAssetsPresent)
            EXPECT_NE(d, nullptr);
    });
    static_assert((int)Icon::TransportPlay == 0, "TransportPlay must remain the first enum value");
}

// ---------------------------------------------------------------------------
// 8. WaveformIconsLoadAndAreNonNull
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, WaveformIconsLoadAndAreNonNull) {
    // All four waveform glyph icons must load successfully when assets are present.
    IconLibrary lib;
    const Icon kWaveformIcons[] = {Icon::WaveformSine, Icon::WaveformSaw, Icon::WaveformSquare, Icon::WaveformTriangle};
    for (const auto id : kWaveformIcons) {
        auto d = lib.getDrawable(id);
        if (kAssetsPresent)
            EXPECT_NE(d, nullptr) << "Waveform icon " << (int)id << " returned null with assets present";
        // No assets → nullptr is expected and acceptable (null fallback contract).
    }
}

// ---------------------------------------------------------------------------
// 9. WaveformIconTintStability
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, WaveformIconTintStability) {
    // Repeated setTintColour calls on a waveform icon must always produce the target colour
    // (no tint accumulation from re-tinting an already-tinted drawable).
    if (!kAssetsPresent)
        GTEST_SKIP() << "Requires embedded assets";

    IconLibrary lib;
    const juce::Colour colA = juce::Colours::orange;
    const juce::Colour colB = juce::Colours::deepskyblue;

    juce::Colour last;
    for (int i = 0; i < 100; ++i) {
        last = (i % 2 == 0) ? colA : colB;
        EXPECT_NO_THROW(lib.setTintColour(Icon::WaveformSine, last));
    }

    // Final tint (i==99 → odd → deepskyblue) must render correctly.
    auto img = snapshotIcon(lib, Icon::WaveformSine);
    ASSERT_TRUE(img.isValid());
    EXPECT_TRUE(imageContainsColour(img, last)) << "Final tint colour not stable after 100 switches";
}

// ---------------------------------------------------------------------------
// 10. WaveformIconBinaryDataSymbols
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, WaveformIconBinaryDataSymbols) {
    // Verify the exact BinaryData symbol names produced by JUCE's mangler for the four waveform
    // SVG files (hyphens stripped, dot-before-extension becomes '_').
    // 'waveform-sine.svg' → waveformsine_svg, etc.
#ifdef HAS_FONT_ASSETS
    EXPECT_NE(BinaryData::waveformsine_svg, nullptr);
    EXPECT_GT(BinaryData::waveformsine_svgSize, 0);
    EXPECT_NE(BinaryData::waveformsaw_svg, nullptr);
    EXPECT_GT(BinaryData::waveformsaw_svgSize, 0);
    EXPECT_NE(BinaryData::waveformsquare_svg, nullptr);
    EXPECT_GT(BinaryData::waveformsquare_svgSize, 0);
    EXPECT_NE(BinaryData::waveformtriangle_svg, nullptr);
    EXPECT_GT(BinaryData::waveformtriangle_svgSize, 0);
#else
    GTEST_SKIP() << "BinaryData not linked in this build";
#endif
}

// ---------------------------------------------------------------------------
// 11. WaveformIconEnumCountCoversNewIcons
// ---------------------------------------------------------------------------
TEST(IconLibraryTest, WaveformIconEnumCountCoversNewIcons) {
    // The enum must now contain 28 entries (22 Phase-3 + ActionNew + ThemeToggle + 4 waveform). The
    // static_assert in IconLibrary.cpp enforces kTable alignment at compile time; this runtime
    // check catches any mismatch that slips through without a rebuild.
    EXPECT_EQ((int)Icon::kCount, 28);
    // Spot-check ordinal positions of the new waveform icons (shifted +2 by ActionNew at index 6 and ThemeToggle at
    // index 11).
    EXPECT_EQ((int)Icon::WaveformSine, 24);
    EXPECT_EQ((int)Icon::WaveformSaw, 25);
    EXPECT_EQ((int)Icon::WaveformSquare, 26);
    EXPECT_EQ((int)Icon::WaveformTriangle, 27);
}
