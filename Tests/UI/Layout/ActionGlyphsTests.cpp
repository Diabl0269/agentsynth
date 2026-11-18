// ActionGlyphsTests.cpp -- the shared action painters (UI/Layout/ActionGlyphs.h), the guard of the action-drift kind:
// one action is drawn by one painter on every surface, so these tests compare the surfaces' pixels with the painter's,
// and fail when a second painter for the same action appears in Source/.
#include "../SourceScan.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include "UI/Timeline/TrackShowModuleButton.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

juce::Image render(int size, const std::function<void(juce::Graphics&)>& draw) {
    juce::Image image(juce::Image::ARGB, size, size, true);
    juce::Graphics g(image);
    draw(g);
    return image;
}

bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

bool drewSomething(const juce::Image& image) {
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt(x, y).getAlpha() > 0)
                return true;
    return false;
}

} // namespace

TEST(ActionGlyphs, TheLaneChipDrawsShowOnCanvasWithTheSharedPainter) {
    const auto area = juce::Rectangle<float>(3.0f, 3.0f, 14.0f, 14.0f);
    const auto ink = juce::Colour(0xff3b82f6);
    const auto chip = render(20, [&](juce::Graphics& g) {
        synth::ui::paintLaneControlIcon(g, synth::ui::LaneControl::ShowInCanvas, area, ink);
    });
    const auto shared = render(20, [&](juce::Graphics& g) { synth::ui::paintShowOnCanvasGlyph(g, area, ink); });
    EXPECT_TRUE(drewSomething(shared));
    EXPECT_TRUE(samePixels(chip, shared));
}

TEST(ActionGlyphs, TheTrackHeaderButtonDrawsShowOnCanvasWithTheSharedPainter) {
    synth::ui::TrackShowModuleButton button;
    button.setSize(synth::ui::TrackShowModuleButton::kSize, synth::ui::TrackShowModuleButton::kSize);
    const auto snapshot = button.createComponentSnapshot(button.getLocalBounds(), false, 1.0f);
    const auto ink = synth::theme::themeOf(button).colors.textMuted;
    const auto shared = render(synth::ui::TrackShowModuleButton::kSize, [&](juce::Graphics& g) {
        synth::ui::paintShowOnCanvasGlyph(g, juce::Rectangle<float>(12.0f, 12.0f).withCentre({10.0f, 10.0f}), ink);
    });
    EXPECT_TRUE(samePixels(snapshot, shared));
}

TEST(ActionGlyphs, TheStrokeScaleThickensTheLines) {
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, 20.0f, 20.0f);
    auto coverage = [&](float scale) {
        const auto image = render(
            20, [&](juce::Graphics& g) { synth::ui::paintShowOnCanvasGlyph(g, area, juce::Colours::white, scale); });
        float sum = 0.0f;
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                sum += image.getPixelAt(x, y).getFloatAlpha();
        return sum;
    };
    EXPECT_GT(coverage(2.0f), coverage(1.0f));
}

// Source guard: the ring-and-ticks drawing lives in ActionGlyphs.cpp only. ModDotGlyph::Crosshair is the one other
// ring-with-ticks (it means "aim at a card", not "show on canvas"), listed here with that reason; a new file that
// draws one fails until it calls the shared painter or is listed with a written reason.
TEST(ActionGlyphs, ThereIsOneShowOnCanvasPainterInSource) {
    const std::regex ellipse(R"((drawEllipse|addEllipse))");
    const std::regex tick(R"(float\s+tick\b)"); // the tick length of a ring-with-ticks drawing
    const std::set<std::string> allowed = {
        "Source/UI/Layout/ActionGlyphs.cpp",
        "Source/UI/Graph/ModDot/ModDotGlyphs.cpp", // ModDotGlyph::Crosshair: "pick on canvas" (aim), not "show"
    };
    std::set<std::string> found;
    std::map<std::string, int> callers;
    const std::regex oldChipPainter(R"(\bpaintCanvas\s*\()");
    const std::regex call(R"(paintShowOnCanvasGlyph\s*\()");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (std::regex_search(text, ellipse) && std::regex_search(text, tick))
            found.insert(file);
        if (file != "Source/UI/Layout/ActionGlyphs.cpp" && file != "Source/UI/Layout/ActionGlyphs.h") {
            const auto n =
                (int)std::distance(std::sregex_iterator(text.begin(), text.end(), call), std::sregex_iterator());
            if (n > 0)
                callers[file] = n;
        }
        EXPECT_FALSE(std::regex_search(text, oldChipPainter)) << file << " keeps the old lane-chip canvas painter";
    }
    for (const auto& file : found)
        EXPECT_TRUE(allowed.count(file) > 0) << file << " draws a ring with ticks: call paintShowOnCanvasGlyph";
    synth::test::source_scan::expectCountsMatch(
        "paintShowOnCanvasGlyph", callers,
        {{"Source/UI/Timeline/TrackShowModuleButton.cpp", 1},
         {"Source/UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlIcons.cpp", 1}},
        "ActionGlyphsTests.cpp");
}

// The severity disc (alerts at 64 px, toasts at 22 px): every kind draws, the four differ, the disc is lit from the top
// left, and the mark inside the glyph shows the surface colour (a hole), at both sizes.
TEST(ActionGlyphs, TheSeverityDiscIsLitFromTheTopLeftAndEveryKindDiffers) {
    using synth::ui::SeverityGlyph;
    const juce::Colour ink(0xff00D1FF), surface(0xff1B1F26);
    const SeverityGlyph kinds[] = {SeverityGlyph::info, SeverityGlyph::warning, SeverityGlyph::error,
                                   SeverityGlyph::question};
    for (const int size : {22, 64}) {
        std::vector<juce::Image> images;
        for (const auto kind : kinds) {
            juce::Image image(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
            juce::Graphics g(image);
            synth::ui::paintSeverityDisc(g, image.getBounds().toFloat(), ink, kind, surface);
            EXPECT_TRUE(drewSomething(image)) << size;
            // Light from the top left: the disc is more opaque near its top-left than near its bottom-right.
            EXPECT_GT(image.getPixelAt(size * 3 / 10, size * 3 / 10).getAlpha() +
                          (kind == SeverityGlyph::warning ? 255 : 0),
                      image.getPixelAt(size * 7 / 10, size * 8 / 10).getAlpha() - 1)
                << size;
            // The disc's corners stay clear.
            EXPECT_EQ(image.getPixelAt(0, 0).getAlpha(), 0);
            images.push_back(image);
        }
        for (size_t a = 0; a < images.size(); ++a)
            for (size_t b = a + 1; b < images.size(); ++b)
                EXPECT_FALSE(samePixels(images[a], images[b])) << size << ": kinds " << a << " and " << b << " match";
    }
    // The warning's exclamation bar is cut out in the surface colour at the large size.
    juce::Image warning(juce::Image::ARGB, 64, 64, true, juce::SoftwareImageType());
    juce::Graphics g(warning);
    synth::ui::paintSeverityDisc(g, warning.getBounds().toFloat(), ink, SeverityGlyph::warning, surface);
    EXPECT_EQ(warning.getPixelAt(32, 30).getRed(), surface.getRed());
    EXPECT_EQ(warning.getPixelAt(32, 30).getBlue(), surface.getBlue());
}

// One painter for the severity disc: the toast and the alert icon's Soft look (AlertIconPainter, which the alert box
// calls) use it, and neither keeps a glyph path of its own.
TEST(ActionGlyphs, TheAlertBoxAndTheToastDrawTheSameSeverityDisc) {
    std::map<std::string, int> callers;
    const std::regex call(R"(paintSeverityDisc\s*\()");
    const std::regex ownGlyph(R"(kWarningGlyph|kInfoGlyph|kQuestionGlyph|paintSeverityGlyph)");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        EXPECT_FALSE(std::regex_search(text, ownGlyph)) << file << " draws its own severity glyph";
        if (file != "Source/UI/Layout/ActionGlyphs.cpp" && file != "Source/UI/Layout/ActionGlyphs.h") {
            const auto n =
                (int)std::distance(std::sregex_iterator(text.begin(), text.end(), call), std::sregex_iterator());
            if (n > 0)
                callers[file] = n;
        }
    }
    synth::test::source_scan::expectCountsMatch(
        "paintSeverityDisc", callers,
        {{"Source/UI/Chrome/Toast/ToastComponent.cpp", 1}, {"Source/UI/Layout/AlertIconPainter.cpp", 1}},
        "ActionGlyphsTests.cpp");
}

// Regression test for FRO1578: the poly and mixed voice glyph read as three dots because the stem barely rose above
// its heads. The stem now stands clearly above them, so it reads as notes. Painted on a 12-unit grid scaled by 4: the
// stem (the columns right of the heads' left part) reaches the top edge, the heads' own top sits at least 3 units
// lower, and the mono glyph keeps its one short stem.
TEST(ActionGlyphs, ThePolyAndMixedVoiceGlyphsHaveAStemClearlyAboveTheirHeads) {
    constexpr int scale = 4;
    constexpr int size = 12 * scale;
    const auto topOfInk = [](const juce::Image& image, int x0, int x1) {
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = x0; x < x1; ++x)
                if (image.getPixelAt(x, y).getAlpha() > 100)
                    return y;
        return image.getHeight();
    };
    for (const auto voice : {synth::ui::VoiceGlyph::poly, synth::ui::VoiceGlyph::mixed}) {
        juce::Image image(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        synth::ui::paintVoiceGlyph(g, juce::Rectangle<float>(0.0f, 0.0f, (float)size, (float)size),
                                   juce::Colours::white, voice, 1.0f);
        const int stemTop = topOfInk(image, 0, size);
        const int headsTop = topOfInk(image, 0, 5 * scale); // left of the stem: only heads live here
        EXPECT_LE(stemTop, scale) << "the stem reaches the top of the grid";
        EXPECT_GE(headsTop - stemTop, 3 * scale) << "the stem stands at least 3 units above the top head";
        for (int i = 0; i < size; ++i) { // inside the grid: nothing on the left or bottom edge, nothing clipped
            EXPECT_EQ(image.getPixelAt(0, i).getAlpha(), 0);
            EXPECT_EQ(image.getPixelAt(i, size - 1).getAlpha(), 0);
        }
    }
    juce::Image mono(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g(mono);
    synth::ui::paintVoiceGlyph(g, juce::Rectangle<float>(0.0f, 0.0f, (float)size, (float)size), juce::Colours::white,
                               synth::ui::VoiceGlyph::mono, 1.0f);
    EXPECT_EQ(topOfInk(mono, 0, size), scale) << "mono keeps its stem top at 1 unit";
}

// All eight EQ band types draw something, and every pair of them differs, at the size of a chip glyph.
TEST(ActionGlyphs, EveryEqBandTypeHasItsOwnGlyph) {
    using synth::eq::BandType;
    const BandType types[] = {BandType::LowShelf, BandType::Peak,  BandType::HighShelf, BandType::LowCut,
                              BandType::HighCut,  BandType::Notch, BandType::BandPass,  BandType::Tilt};
    std::vector<juce::Image> images;
    for (const auto type : types) {
        juce::Image image(juce::Image::ARGB, 28, 20, true, juce::SoftwareImageType());
        juce::Graphics g(image);
        synth::ui::paintEqBandTypeGlyph(g, juce::Rectangle<float>(14.0f, 10.0f).withCentre({14.0f, 10.0f}),
                                        juce::Colours::white, type);
        images.push_back(image);
        EXPECT_TRUE(drewSomething(image)) << (int)type;
    }
    for (size_t a = 0; a < images.size(); ++a)
        for (size_t b = a + 1; b < images.size(); ++b) {
            int differing = 0;
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 28; ++x)
                    if (std::abs(images[a].getPixelAt(x, y).getAlpha() - images[b].getPixelAt(x, y).getAlpha()) > 40)
                        ++differing;
            EXPECT_GT(differing, 12) << "glyphs " << a << " and " << b << " look alike";
        }
}

// Source guard: a band type's response drawing lives in ActionGlyphs.cpp only. A file that matches on BandType::LowCut
// and BandType::Notch and strokes a path is drawing its own response icon; it calls paintEqBandTypeGlyph instead.
TEST(ActionGlyphs, ThereIsOneEqBandTypeGlyphPainterInSource) {
    const std::regex cases(R"(case\s+(synth::eq::)?BandType::Notch)");
    const std::regex stroke(R"(strokePath|drawLine)");
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        if (file == "Source/UI/Layout/ActionGlyphs.cpp" || file.rfind("Source/UI/", 0) != 0)
            continue;
        EXPECT_FALSE(std::regex_search(text, cases) && std::regex_search(text, stroke))
            << file << " draws per-band-type shapes: call paintEqBandTypeGlyph";
    }
}
