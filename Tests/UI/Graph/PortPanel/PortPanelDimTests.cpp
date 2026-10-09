// PortPanelDimTests.cpp: while a port connections panel row is highlighted the canvas paints every OTHER cable dimmed
// (kPortPanelDimAlpha), wire and travelling dots alike. Rendered into a software image and compared as pixels.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelTestFixture.h"

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

} // namespace

TEST_F(GraphEditorTest, WhileARowIsHighlightedTheOtherCablesArePaintedDimmed) {
    PortFixture f(2);
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

    const auto bg =
        juce::Colour(renderCanvas(*f.editor).getPixelAt(f.editor->getWidth() - 2, f.editor->getHeight() - 2));
    const int normal = peakInk(renderCanvas(*f.editor), box, bg);
    ASSERT_GT(normal, 100) << "the cable is painted at all";

    f.controller().setHighlightedCable(first.id);
    const int dimmed = peakInk(renderCanvas(*f.editor), box, bg);
    EXPECT_LT(dimmed, normal * 0.5) << "the wire and its dots fade together (alpha x " << synth::ui::kPortPanelDimAlpha
                                    << ")";
    EXPECT_GT(dimmed, 0) << "dimmed, not hidden";

    f.controller().clearHighlight();
    EXPECT_EQ(peakInk(renderCanvas(*f.editor), box, bg), normal) << "clearing the highlight restores it";
}
