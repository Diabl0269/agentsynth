#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the cat logo on the Welcome card: it sits inside the card above the title, adds its slot to the card
// height, and reveals (fade + rise) when the overlay is shown, landing at once under Reduced Motion.

namespace {

class WelcomeLogoTest : public ::testing::Test {
protected:
    void TearDown() override { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST_F(WelcomeLogoTest, LogoSitsInsideTheCardAboveTheTitle) {
    synth::ui::WelcomeScreenComponent welcome;
    welcome.setSize(1600, 900);
    const auto logo = welcome.getLogoBoundsForTest();
    const auto card = welcome.getCardBoundsForTest();
    EXPECT_EQ(logo.getWidth(), 96);
    EXPECT_EQ(logo.getHeight(), 96);
    EXPECT_TRUE(card.contains(logo));
    EXPECT_EQ(logo.getCentreX(), card.getCentreX());
    EXPECT_GE(card.getHeight(), logo.getHeight() + 34 + 22);
}

TEST_F(WelcomeLogoTest, RevealLandsAtOnceUnderReducedMotion) {
    synth::ui::setReducedMotionForTest(true);
    synth::ui::WelcomeScreenComponent welcome;
    welcome.setSize(1600, 900);
    welcome.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    welcome.setVisible(false);
    welcome.setVisible(true);
    EXPECT_FLOAT_EQ(welcome.getLogoRevealForTest(), 1.0f);
}

TEST_F(WelcomeLogoTest, RevealStartsHiddenThenSettles) {
    synth::ui::setReducedMotionForTest(false);
    synth::ui::WelcomeScreenComponent welcome;
    welcome.setSize(1600, 900);
    welcome.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    welcome.setVisible(false);
    welcome.setVisible(true);
    if (!welcome.isShowing())
        GTEST_SKIP() << "no peer in this environment";
    EXPECT_LT(welcome.getLogoRevealForTest(), 1.0f);
    welcome.completeLogoRevealForTest();
    EXPECT_FLOAT_EQ(welcome.getLogoRevealForTest(), 1.0f);
}
