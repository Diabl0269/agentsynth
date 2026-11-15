// SearchPillFieldTests.cpp -- the app's one search box (docs/layout/search-field.md): the shortcut hint shows only
// while the field is empty and wide enough, the clear button only while there is text, and neither ever overlaps the
// magnifier, the text or the placeholder. Headless: nothing is on screen, so changes land at once unless a test turns
// the animated path on and steps it by hand.

#include "../../TestWait.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/SearchPillField.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::SearchPillField;

const juce::String kCmdF = juce::String::fromUTF8("\xe2\x8c\x98"
                                                  "F");

juce::MouseEvent mouseEventOn(juce::Component& comp, juce::Point<int> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

struct Rig {
    synth::theme::AppLookAndFeel laf;
    juce::Component parent;
    SearchPillField field;

    explicit Rig(int width = 220) {
        parent.setLookAndFeel(&laf);
        parent.setSize(400, 80);
        parent.addAndMakeVisible(field);
        field.setPlaceholder("Search modules");
        field.setShortcutHint(kCmdF);
        field.setBounds(20, 20, width, SearchPillField::kHeight);
    }
    ~Rig() { parent.setLookAndFeel(nullptr); }

    int placeholderWidth() const {
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(field.getFont(), field.getPlaceholder()));
    }
};

TEST(SearchPillFieldTest, ShowsItsHintWhileEmptyAndWideEnough) {
    Rig rig;
    EXPECT_TRUE(rig.field.isShortcutHintShown());
    EXPECT_FALSE(rig.field.isClearButtonShown());
    EXPECT_TRUE(rig.field.getLocalBounds().contains(rig.field.getShortcutHintBounds()));
}

TEST(SearchPillFieldTest, TheHintGoesWhileTyping) {
    Rig rig;
    rig.field.insertTextAtCaret("a");
    EXPECT_FALSE(rig.field.isShortcutHintShown());
    EXPECT_TRUE(rig.field.isClearButtonShown());
    EXPECT_TRUE(rig.field.getClearButton().isVisible());
}

TEST(SearchPillFieldTest, TheHintComesBackWhenTheTextIsDeleted) {
    Rig rig;
    rig.field.insertTextAtCaret("a");
    rig.field.setText({}, false);
    EXPECT_TRUE(rig.field.isShortcutHintShown());
    EXPECT_FALSE(rig.field.isClearButtonShown());
}

TEST(SearchPillFieldTest, ASilentSetTextAlsoUpdatesTheParts) {
    Rig rig;
    rig.field.setText("saw", juce::dontSendNotification);
    EXPECT_FALSE(rig.field.isShortcutHintShown());
    EXPECT_TRUE(rig.field.isClearButtonShown());
}

TEST(SearchPillFieldTest, TheHintIsLeftOutWhereTheFieldIsTooNarrow) {
    Rig rig(90);
    EXPECT_FALSE(rig.field.isShortcutHintShown());
    rig.field.setSize(260, SearchPillField::kHeight);
    EXPECT_TRUE(rig.field.isShortcutHintShown()) << "widening brings it back";
}

TEST(SearchPillFieldTest, NoHintMeansNothingIsReserved) {
    Rig rig;
    rig.field.setShortcutHint({});
    EXPECT_FALSE(rig.field.isShortcutHintShown());
    EXPECT_GT(rig.field.getTextArea().getRight(), rig.field.getShortcutHintBounds().getX() - 1);
}

// At every width the hint's cap keeps clear of the magnifier, the clear button, the text and the placeholder.
TEST(SearchPillFieldTest, TheHintNeverOverlapsAnything) {
    Rig rig;
    for (int width = 30; width <= 320; width += 5) {
        rig.field.setText({}, false);
        rig.field.setSize(width, SearchPillField::kHeight);
        const auto text = rig.field.getTextArea();
        if (rig.field.isShortcutHintShown()) {
            const auto cap = rig.field.getShortcutHintBounds();
            EXPECT_TRUE(rig.field.getLocalBounds().contains(cap)) << "width " << width;
            EXPECT_FALSE(cap.intersects(rig.field.getMagnifierBounds())) << "width " << width;
            EXPECT_FALSE(cap.intersects(text)) << "width " << width;
            EXPECT_LE(rig.placeholderWidth(), text.getWidth()) << "the placeholder is cut at width " << width;
        }
        rig.field.setText("abc", false);
        EXPECT_FALSE(rig.field.isShortcutHintShown()) << "width " << width;
        EXPECT_FALSE(rig.field.getClearButtonBounds().intersects(rig.field.getTextArea())) << "width " << width;
        if (width >= 50) // narrower than magnifier plus clear button, nothing can fit
            EXPECT_FALSE(rig.field.getClearButtonBounds().intersects(rig.field.getMagnifierBounds()))
                << "width " << width;
    }
}

TEST(SearchPillFieldTest, TheClearButtonShowsOnlyWithText) {
    Rig rig;
    EXPECT_FALSE(rig.field.getClearButton().isVisible());
    rig.field.setText("abc", false);
    EXPECT_TRUE(rig.field.getClearButton().isVisible());
    rig.field.setText({}, false);
    EXPECT_FALSE(rig.field.getClearButton().isVisible());
}

// The real mouse path: the click lands on the button the field hit-tests there, then the field is empty again, still
// focus-ready, and reports the change.
TEST(SearchPillFieldTest, ClickingTheClearButtonEmptiesTheFieldAndReportsIt) {
    Rig rig;
    int changes = 0;
    rig.field.onTextChange = [&] { ++changes; };
    rig.field.setText("saw", false);

    const auto centre = rig.field.getClearButtonBounds().getCentre();
    auto* hit = rig.field.getComponentAt(centre);
    ASSERT_EQ(hit, &rig.field.getClearButton()) << "the x is where a click reaches it";

    const auto pos = hit->getLocalBounds().getCentre();
    hit->mouseDown(mouseEventOn(*hit, pos));
    hit->mouseUp(mouseEventOn(*hit, pos));

    EXPECT_TRUE(rig.field.getText().isEmpty());
    synth::test::waitUntil([&] { return changes > 0; });
    EXPECT_GT(changes, 0);
    EXPECT_TRUE(rig.field.isShortcutHintShown());
    EXPECT_FALSE(rig.field.isClearButtonShown());
}

TEST(SearchPillFieldTest, TheClearButtonIsNamedForAScreenReader) {
    Rig rig;
    EXPECT_EQ(rig.field.getClearButton().getTitle(), "Clear search");
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&rig.field.getClearButton());
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(tip->getTooltip(), "Clear search");
}

TEST(SearchPillFieldTest, TheTooltipNamesTheKey) {
    Rig rig;
    rig.field.setSearchTooltip("Search modules");
    EXPECT_TRUE(rig.field.getTooltip().contains("Search modules"));
    EXPECT_TRUE(rig.field.getTooltip().contains(kCmdF));
    rig.field.setShortcutHint({});
    EXPECT_EQ(rig.field.getTooltip(), "Search modules");
}

TEST(SearchPillFieldTest, ThePillPaintsItsOwnFillAndOutline) {
    Rig rig;
    EXPECT_TRUE(rig.field.findColour(juce::TextEditor::backgroundColourId).isTransparent());
    EXPECT_TRUE(rig.field.findColour(juce::TextEditor::outlineColourId).isTransparent());
    EXPECT_TRUE(rig.field.findColour(juce::TextEditor::focusedOutlineColourId).isTransparent());
    EXPECT_EQ(rig.field.findColour(juce::TextEditor::textColourId), rig.laf.getTheme().colors.textPrimary);
}

TEST(SearchPillFieldTest, TheTextStartsRightOfTheMagnifier) {
    Rig rig;
    EXPECT_GE(rig.field.getTextArea().getX(), rig.field.getMagnifierBounds().getRight());
}

TEST(SearchPillFieldTest, HintAndClearButtonSwapWithAFadeNotAPop) {
    FadeAnimateGuard guard;
    Rig rig;
    rig.field.insertTextAtCaret("a");
    EXPECT_TRUE(rig.field.getClearButton().isVisible());
    EXPECT_LT(rig.field.getClearButton().getAlpha(), 1.0f) << "it fades in from nothing";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    synth::ui::FadeAmount::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(rig.field.getClearButton().getAlpha(), 1.0f);
    EXPECT_FALSE(rig.field.isShortcutHintShown());
}

} // namespace
