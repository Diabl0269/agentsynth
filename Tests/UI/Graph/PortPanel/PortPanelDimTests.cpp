// PortPanelDimTests.cpp: while a port connections panel row is highlighted the canvas paints every OTHER cable dimmed
// (kPortPanelDimAlpha), wire and travelling dots alike. Rendered into a software image and compared as pixels.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelTestFixture.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <algorithm>
#include <map>

namespace {

juce::Image renderCanvas(GraphEditor& editor) {
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    editor.paintEntireComponent(g, false);
    return image;
}

int distanceFrom(juce::Colour c, juce::Colour bg) {
    return std::abs((int)c.getRed() - (int)bg.getRed()) + std::abs((int)c.getGreen() - (int)bg.getGreen()) +
           std::abs((int)c.getBlue() - (int)bg.getBlue());
}

// The strongest pixel in `box`, as a distance from the background.
int peakInk(const juce::Image& image, juce::Rectangle<int> box, juce::Colour bg) {
    int peak = 0;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            peak = std::max(peak, distanceFrom(image.getPixelAt(x, y), bg));
    return peak;
}

// The colour most pixels of `box` have: the canvas, since the cable covers a thin part of the box.
juce::Colour commonColourIn(const juce::Image& image, juce::Rectangle<int> box) {
    std::map<juce::uint32, int> counts;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            ++counts[image.getPixelAt(x, y).getARGB()];
    const auto best = std::max_element(counts.begin(), counts.end(),
                                       [](const auto& a, const auto& b) { return a.second < b.second; });
    return juce::Colour(best->first);
}

} // namespace

namespace {
void expectOtherCablesDim(bool themed) {
    synth::theme::AppLookAndFeel lf; // outlives the editor
    PortFixture f(2);
    if (themed) {
        lf.applyTheme(synth::theme::makeNeon()); // glow > 0: the halo and casing are part of what must dim
        f.editor->setLookAndFeel(&lf);
    }
    f.refresh();
    const auto cables = f.editor->buildVisibleCables();
    ASSERT_EQ(cables.size(), 2u);
    const auto& first = cables[0].id.dstUid == f.vcaIds[0].uid ? cables[0] : cables[1];
    const auto& second = cables[0].id.dstUid == f.vcaIds[0].uid ? cables[1] : cables[0];

    // A box over the middle of the second cable that holds neither a card nor the first cable, and (being most of the
    // cable) at least one of its travelling dots.
    const auto curve = GraphEditor::buildCablePath(second.p1, second.p2);
    const auto from = curve.getPointAlongPath(curve.getLength() * 0.3f);
    const auto to = curve.getPointAlongPath(curve.getLength() * 0.8f);
    const auto box = juce::Rectangle<float>(from, to).expanded(6.0f).toNearestInt();
    for (auto* card : f.editor->getModuleComponents())
        ASSERT_FALSE(card->getBounds().intersects(box)) << "the box is clear of the cards";
    const auto other = GraphEditor::buildCablePath(first.p1, first.p2);
    for (float t = 0.0f; t <= 1.0f; t += 0.02f)
        ASSERT_FALSE(box.toFloat().contains(other.getPointAlongPath(other.getLength() * t)))
            << "clear of the first cable";

    const auto before = renderCanvas(*f.editor);
    const auto bg = commonColourIn(before, box); // the canvas the cable lies on, whatever the theme paints it
    const int normal = peakInk(before, box, bg);
    ASSERT_GT(normal, 100) << "the cable is painted at all";

    f.controller().setHighlightedCable(first.id);
    const int dimmed = peakInk(renderCanvas(*f.editor), box, bg);
    EXPECT_LT(dimmed, normal * (themed ? 0.6 : 0.5))
        << "the wire and its dots fade together (alpha x " << synth::ui::kPortPanelDimAlpha
        << "; a dot over the wire's own core keeps a little more, hence the margin)";
    EXPECT_GT(dimmed, 0) << "dimmed, not hidden";

    f.controller().clearHighlight();
    EXPECT_EQ(peakInk(renderCanvas(*f.editor), box, bg), normal) << "clearing the highlight restores it";
    f.editor->setLookAndFeel(nullptr);
}
} // namespace

TEST_F(GraphEditorTest, WhileARowIsHighlightedTheOtherCablesArePaintedDimmed) { expectOtherCablesDim(false); }

// Regression test for FRO791: the dim must also show with the app's own look (its wire stroke, glow and casing), which
// is what runs on screen; headless tests otherwise paint the plain fallback wire.
TEST_F(GraphEditorTest, WhileARowIsHighlightedTheOtherCablesArePaintedDimmedInTheAppsOwnLook) {
    expectOtherCablesDim(true);
}

// Regression test for FRO791: a neon wire's glow and its casing were drawn at full strength under a dimmed cable, so
// on screen the "dimmed" cable still glowed.
TEST(PortPanelDim, ADimmedNeonWireTakesItsGlowAndCasingDownWithIt) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeNeon());
    ASSERT_GT(lf.getTheme().treatment.glow, 0.0f);
    const auto bg = lf.getTheme().colors.bg1;
    auto ink = [&](float alpha) {
        juce::Image img(juce::Image::ARGB, 120, 60, true, juce::SoftwareImageType());
        juce::Graphics g(img);
        g.fillAll(bg);
        const juce::Path wire = GraphEditor::buildCablePath({10.0f, 30.0f}, {110.0f, 30.0f});
        lf.drawConnectionWire(g, {10.0f, 30.0f}, {110.0f, 30.0f}, wire, lf.getTheme().colors.audioWire.withAlpha(alpha),
                              false, 0.0f, false);
        long total = 0; // everything the wire put on the canvas, glow halo included
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 20; x < 100; ++x)
                total += distanceFrom(img.getPixelAt(x, y), bg);
        return total;
    };
    const long full = ink(1.0f);
    const long dimmed = ink(synth::ui::kPortPanelDimAlpha);
    ASSERT_GT(full, 0);
    EXPECT_LT(dimmed, full * 0.5) << "the whole wire, glow and casing included, fades with its alpha";
}
