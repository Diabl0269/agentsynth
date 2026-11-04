// ActionGlyphsTests.cpp -- the shared action painters (UI/Layout/ActionGlyphs.h), the guard of the action-drift kind:
// one action is drawn by one painter on every surface, so these tests compare the surfaces' pixels with the painter's,
// and fail when a second painter for the same action appears in Source/.
#include "../SourceScan.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include "UI/Timeline/TrackShowModuleButton.h"
#include <gtest/gtest.h>

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
