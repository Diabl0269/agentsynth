// The keyboard-focus ring on the Sounds view's filter chips (the panel's chip groups and the row of active chips):
// drawn on the chip's own bounds with the chip's pill radius, so no edge of the component cuts it off.
#include "UI/Layout/FocusRing.h"
#include "UI/Library/SoundBrowser/SoundFilterChipGroup.h"
#include "UI/Library/SoundBrowser/SoundFilterChipRow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

using synth::ui::SoundFilterChipGroup;
using synth::ui::SoundFilterChipRow;

namespace {

struct RingFixture {
    synth::theme::AppLookAndFeel laf;
    RingFixture() {
        for (const auto& theme : synth::theme::builtInThemes())
            if (theme.id == "obsidian")
                laf.applyTheme(theme);
        synth::ui::setFocusRingsShownForTest(true);
    }
    ~RingFixture() { synth::ui::setFocusRingsShownForTest(false); }
    juce::Colour accent() const { return laf.getTheme().colors.accent; }
    juce::Image render(juce::Component& c) const {
        juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        c.paintEntireComponent(g, true);
        return image;
    }
};

// True when the pixel is the ring: the accent at close to full strength (the ring is solid, the chip's fill is 22 %).
bool isRing(const juce::Image& image, int x, int y, juce::Colour accent) {
    const auto p = image.getPixelAt(x, y);
    const auto near = [](juce::uint8 a, juce::uint8 b) { return std::abs((int)a - (int)b) <= 24; };
    return p.getAlpha() > 220 && near(p.getRed(), accent.getRed()) && near(p.getGreen(), accent.getGreen()) &&
           near(p.getBlue(), accent.getBlue());
}

// The ring covers the chip's outermost pixel rows at its centre and is rounded at the corners with the pill radius.
void expectWholeRing(const juce::Image& image, juce::Rectangle<int> chip, juce::Colour accent) {
    const int cx = chip.getCentreX();
    EXPECT_TRUE(isRing(image, cx, chip.getY(), accent)) << "top edge";
    EXPECT_TRUE(isRing(image, cx, chip.getBottom() - 1, accent)) << "bottom edge";
    EXPECT_TRUE(isRing(image, chip.getX(), chip.getCentreY(), accent) ||
                isRing(image, chip.getX(), chip.getCentreY() - 1, accent))
        << "left end";
    EXPECT_TRUE(isRing(image, chip.getRight() - 1, chip.getCentreY(), accent) ||
                isRing(image, chip.getRight() - 1, chip.getCentreY() - 1, accent))
        << "right end";
    EXPECT_FALSE(isRing(image, chip.getX(), chip.getY(), accent)) << "the corner is round, not squared off";
    const int radius = chip.getHeight() / 2;
    EXPECT_TRUE(isRing(image, chip.getX() + radius, chip.getY(), accent)) << "the arc meets the top edge at the radius";
    EXPECT_TRUE(isRing(image, chip.getX(), chip.getY() + radius, accent) ||
                isRing(image, chip.getX(), chip.getY() + radius - 1, accent))
        << "and the left end";
}

} // namespace

// Regression test for FRO1383: the panel's chip group sat flush in its own component and drew the ring one pixel
// outside the chip, so the top and bottom of the ring were cut off. Guards plot-edge-clip and layout-overlap here.
TEST(SoundFilterChipFocusRingTest, AGroupChipsFocusRingIsWholeAtTheTopAndBottomOfItsComponent) {
    RingFixture f;
    SoundFilterChipGroup group("Sound", "sounds");
    group.setLookAndFeel(&f.laf);
    group.setItems({{"keys", "Keys", false}, {"drums", "Drums", false}});
    group.setSize(240, group.heightForWidth(240));
    group.setFocusedIndex(0);
    const auto image = f.render(group);
    const auto chip = group.getChipBounds(0);
    EXPECT_EQ(chip.getY(), 0) << "the chips sit flush in the group, as in the panel";
    EXPECT_EQ(chip.getBottom(), group.getHeight());
    expectWholeRing(image, chip, f.accent());
    EXPECT_FALSE(isRing(image, group.getChipBounds(1).getCentreX(), 0, f.accent())) << "only the focused chip rings";
    group.setLookAndFeel(nullptr);
}

TEST(SoundFilterChipFocusRingTest, ARowChipsFocusRingIsWholeInsideTheRow) {
    RingFixture f;
    SoundFilterChipRow row;
    row.setLookAndFeel(&f.laf);
    row.setSize(280, SoundFilterChipRow::kHeight);
    row.setChips({{"a", "Keys", "Remove Keys"}, {"b", "Loops", "Remove Loops"}});
    row.finishMotionForTest();
    const auto image = f.render(row);
    const auto chip = row.getChipBounds("a");
    ASSERT_FALSE(chip.isEmpty());
    expectWholeRing(image, chip, f.accent());
    for (int y = 0; y < chip.getY(); ++y)
        for (int x = chip.getX(); x < chip.getRight(); ++x)
            EXPECT_FALSE(isRing(image, x, y, f.accent())) << "no ring outside the chip at " << x << "," << y;
    row.setLookAndFeel(nullptr);
}
