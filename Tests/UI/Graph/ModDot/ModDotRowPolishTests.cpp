// The mod dot's panel icons and rows: the icons are drawn in colour from the first paint (never grey until a focus
// change), each has a tooltip and a screen-reader name and the panel's own window shows it, a row keeps a gap between
// its fader, amount and icons, and the amount fader is the Design System slider with the old drag semantics.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "UI/Graph/ModDot/ModDotAmountBar.h"
#include "UI/Graph/ModDot/ModDotGlyphButton.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/ModDot/ModDotSourceRow.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace {

using synth::ui::ModDotAmountBar;
using synth::ui::ModDotGlyph;
using synth::ui::ModDotGlyphButton;
using synth::ui::ModDotSourceRow;

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// True when some solid pixel of the button's resting paint is `expected` (within a small tolerance).
bool paintsColour(juce::Component& c, juce::Colour expected) {
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        c.paintEntireComponent(g, true);
    }
    auto near = [](juce::uint8 a, juce::uint8 b) { return std::abs((int)a - (int)b) <= 24; };
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const auto p = image.getPixelAt(x, y);
            if (p.getAlpha() > 200 && near(p.getRed(), expected.getRed()) && near(p.getGreen(), expected.getGreen()) &&
                near(p.getBlue(), expected.getBlue()))
                return true;
        }
    return false;
}

juce::MouseEvent clickAt(juce::Component& c, juce::Point<int> p) { return makeModuleClickWithMods(c, p, kPlain); }

} // namespace

TEST_F(ModuleComponentTest, PanelIconsAreDrawnInColourAtRestNotGrey) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    auto& row = *panel->sourcesPage().rowAt(0);
    const auto p = synth::ui::modDotPaletteFor(row);

    for (auto* button : {&row.timelineButton(), &row.removeButton()}) {
        button->setSize(ModDotGlyphButton::kSize, ModDotGlyphButton::kSize);
        EXPECT_FALSE(button->hasKeyboardFocus(false)) << "no focus or hover has happened yet";
    }
    const auto timelineColour = synth::ui::modDotGlyphColour(p, ModDotGlyph::Timeline);
    const auto trashColour = synth::ui::modDotGlyphColour(p, ModDotGlyph::Trash);
    EXPECT_NE(timelineColour, p.muted);
    EXPECT_EQ(trashColour, p.negative);
    EXPECT_NE(trashColour, timelineColour) << "remove stays distinct";
    EXPECT_TRUE(paintsColour(row.timelineButton(), timelineColour));
    EXPECT_TRUE(paintsColour(row.removeButton(), trashColour));
    EXPECT_FALSE(paintsColour(row.removeButton(), p.muted));

    auto& split = panel->sourcesPage().splitButton();
    split.setSize(240, synth::ui::ModDotSplitButton::kHeight);
    EXPECT_TRUE(paintsColour(split.listHalf(), synth::ui::modDotGlyphColour(p, ModDotGlyph::List)));
    EXPECT_TRUE(paintsColour(split.pickHalf(), synth::ui::modDotGlyphColour(p, ModDotGlyph::Crosshair)));
}

TEST_F(ModuleComponentTest, EveryPanelIconHasATooltipAndAScreenReaderName) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    auto& row = *panel->sourcesPage().rowAt(0);
    auto& split = panel->sourcesPage().splitButton();
    for (juce::Button* button : {&row.timelineButton(), &row.removeButton(), &split.listHalf(), &split.pickHalf()}) {
        auto* client = dynamic_cast<juce::SettableTooltipClient*>(button);
        ASSERT_NE(client, nullptr);
        EXPECT_TRUE(client->getTooltip().isNotEmpty()) << button->getTitle();
        EXPECT_TRUE(button->getTitle().isNotEmpty());
        EXPECT_TRUE(button->getWantsKeyboardFocus());
    }
    EXPECT_EQ(row.timelineButton().getTooltip(), "Show " + row.source().sourceName + " in timeline");
}

TEST_F(ModuleComponentTest, ThePanelWindowOwnsATooltipThatShowsInsideIt) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    auto& removeButton = panel->sourcesPage().rowAt(0)->removeButton();
    auto content = std::move(f.held);
    auto* contentPtr = content.get();
    synth::ui::ModDotPanelFrame frame(std::move(content), *f.vcaCard->getModDotButton(f.gainChannel),
                                      {500, 300, 12, 12}, {0, 0, 1600, 1000});
    EXPECT_EQ(frame.tooltipWindow(), nullptr);
    frame.installTooltipWindow(nullptr);
    auto* tips = frame.tooltipWindow();
    ASSERT_NE(tips, nullptr);
    EXPECT_EQ(tips->getParentComponent(), &frame) << "a child of the panel's own window, not of the app window";
    ASSERT_TRUE(contentPtr->isParentOf(&removeButton));

    // The tip appears beside the pointer, inside the frame, with the button's own text.
    const auto at = removeButton.getScreenBounds().getCentre();
    tips->displayTip(at, removeButton.getTooltip());
    EXPECT_TRUE(tips->isVisible());
    EXPECT_TRUE(frame.getLocalBounds().contains(tips->getBounds())) << "never cut off by the frame";
    EXPECT_FALSE(tips->suppresses(removeButton));
    frame.setVisible(false);
}

TEST_F(ModuleComponentTest, ARowKeepsAGapBetweenItsFaderAmountAndIcons) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);
    row.setSize(synth::ui::ModDotPage::kWidth, ModDotSourceRow::kHeight);
    const int gap = synth::ui::modDotPaletteFor(row).space;
    ASSERT_GT(gap, 0);

    const auto bar = row.bar().getBounds();
    const auto amount = row.amountButton().getBounds();
    const auto timeline = row.timelineButton().getBounds();
    const auto remove = row.removeButton().getBounds();
    EXPECT_GT(bar.getWidth(), 60) << "the fader keeps room to drag";
    EXPECT_GE(amount.getX() - bar.getRight(), gap);
    EXPECT_GE(timeline.getX() - amount.getRight(), gap);
    EXPECT_GT(remove.getX() - timeline.getRight(), 0);
    for (const auto& icon : {timeline, remove}) {
        EXPECT_FALSE(bar.intersects(icon));
        EXPECT_GE(icon.getX() - bar.getRight(), gap);
    }
    EXPECT_LE(remove.getRight(), row.getWidth());
    EXPECT_EQ(row.getHeight(), ModDotSourceRow::kHeight);
}

TEST_F(ModuleComponentTest, TheAmountBarIsTheDesignSystemHorizontalFader) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);
    row.setSize(synth::ui::ModDotPage::kWidth, ModDotSourceRow::kHeight);
    auto& bar = row.bar();
    juce::Slider& slider = bar; // the very widget a module card's fader is
    EXPECT_EQ(slider.getSliderStyle(), juce::Slider::LinearHorizontal);
    EXPECT_EQ(slider.getTextBoxPosition(), juce::Slider::NoTextBox);
    EXPECT_NEAR(bar.valueForX(bar.xForValue(0.37f)), 0.37f, 1e-4f);
    EXPECT_LT(bar.xForValue(-1.0f), bar.xForValue(1.0f));
}

TEST_F(ModuleComponentTest, DraggingTheFaderPastItsEndsClampsAndStaysOneUndoStep) {
    NoMotion motion;
    Fixture f;
    auto& row = *f.clickDot()->sourcesPage().rowAt(0);
    row.setSize(synth::ui::ModDotPage::kWidth, ModDotSourceRow::kHeight);
    auto& bar = row.bar();

    bar.mouseDown(clickAt(bar, {(int)bar.xForValue(0.5f), 5}));
    bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(1.0f) + 40, 5}));
    EXPECT_NEAR(f.amount(f.attenId), 1.0f, 1e-4f);
    bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(-0.3f), 5}));
    bar.mouseUp(clickAt(bar, {(int)bar.xForValue(-0.3f), 5}));
    EXPECT_NEAR(f.amount(f.attenId), -0.3f, 0.011f);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_EQ(bar.getValueText(), synth::ui::ModDotAmountBar::percentText(bar.amount()));
}
