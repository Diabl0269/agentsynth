// WavetableTabStrip unit tests: page assignment, page visibility, pinned controls, tallest-page
// sizing and the click path, against plain sliders/combos with no ModuleComponent or module involved.

#include "UI/Graph/ModuleComponent/WavetableTabStrip.h"

#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <vector>

namespace {

// One card's worth of controls, named like the Wavetable module's parameter display names.
class WavetableTabStripTest : public ::testing::Test {
protected:
    static constexpr int kContentW = 520;

    void SetUp() override {
        for (const char* name : {"Position", "Warp Amt", "Octave", "Coarse", "Fine", "Level", "Unison", "Detune",
                                 "Blend", "Width", "Phase", "Rand Phase", "Spread", "Sub", "Pan"})
            addSlider(name);
        for (const char* name : {"Table", "Warp", "Stack", "Sub Oct", "Sub Wave", "Sync In", "Import", "Interp"})
            addCombo(name);

        strip = std::make_unique<WavetableTabStrip>(4242);
        for (size_t i = 0; i < sliders.size(); ++i)
            strip->addSlider(*sliders[i], *sliderLabels[i]);
        for (size_t i = 0; i < combos.size(); ++i)
            strip->addCombo(*combos[i], *comboLabels[i]);

        parent.setSize(kContentW + 40, 600);
        for (auto& s : sliders)
            parent.addAndMakeVisible(*s);
        for (auto& l : sliderLabels)
            parent.addAndMakeVisible(*l);
        for (auto& c : combos)
            parent.addAndMakeVisible(*c);
        for (auto& l : comboLabels)
            parent.addAndMakeVisible(*l);
        parent.addAndMakeVisible(*strip);

        strip->applyVisibility();
        bottom = strip->layoutBody(10, 20, kContentW, true);
    }

    void addSlider(const char* name) {
        sliders.push_back(std::make_unique<juce::Slider>());
        sliders.back()->setComponentID(name);
        sliderLabels.push_back(std::make_unique<juce::Label>(juce::String(), name));
    }

    void addCombo(const char* name) {
        combos.push_back(std::make_unique<juce::ComboBox>());
        combos.back()->setComponentID(name);
        comboLabels.push_back(std::make_unique<juce::Label>(juce::String(), name));
    }

    std::vector<juce::TextButton*> tabButtons() {
        std::vector<juce::TextButton*> tabs;
        for (auto* child : strip->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(child))
                tabs.push_back(b);
        return tabs;
    }

    // Button's mouse handlers are protected and triggerClick() posts asynchronously with no message
    // pump here, so this does what Button::internalClickCallback does for a real click: flip the
    // toggle (which the radio group propagates), then invoke onClick.
    void clickTab(int page) {
        auto* tab = tabButtons().at((size_t)page);
        tab->setToggleState(true, juce::dontSendNotification);
        tab->onClick();
    }

    std::set<juce::String> visibleSliders() {
        std::set<juce::String> names;
        for (auto& s : sliders)
            if (s->isVisible())
                names.insert(s->getComponentID());
        return names;
    }

    std::set<juce::String> visibleCombos() {
        std::set<juce::String> names;
        for (auto& c : combos)
            if (c->isVisible())
                names.insert(c->getComponentID());
        return names;
    }

    juce::Component parent;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Label>> sliderLabels;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::Label>> comboLabels;
    std::unique_ptr<WavetableTabStrip> strip;
    int bottom = 0;
};

} // namespace

TEST(WavetableTabStripPageForTest, MapsEveryNamedControlToItsPage) {
    using S = WavetableTabStrip;
    EXPECT_EQ(S::pageFor("Position"), S::kPinned);
    EXPECT_EQ(S::pageFor("Warp"), S::kPinned);
    EXPECT_EQ(S::pageFor("Warp Amt"), S::kPinned);
    EXPECT_EQ(S::pageFor("Table"), S::kChrome);
    EXPECT_EQ(S::pageFor("Octave"), 0);
    EXPECT_EQ(S::pageFor("Detune"), 1);
    EXPECT_EQ(S::pageFor("Rand Phase"), 2);
    EXPECT_EQ(S::pageFor("Sync In"), 3);
    EXPECT_EQ(S::pageFor("Interp"), 4);
    EXPECT_EQ(S::pageFor("Something New"), 0) << "an unclassified control lands on the first page";
}

TEST_F(WavetableTabStripTest, BuildsOneRadioTabPerPageWithTheRepoTitles) {
    const auto tabs = tabButtons();
    ASSERT_EQ((int)tabs.size(), WavetableTabStrip::kNumPages);

    const char* titles[] = {"Tune", "Unison", "Phase", "Sub", "File"};
    for (int i = 0; i < WavetableTabStrip::kNumPages; ++i) {
        EXPECT_EQ(tabs[(size_t)i]->getButtonText(), titles[i]);
        EXPECT_EQ(tabs[(size_t)i]->getComponentID(), "wtTab" + juce::String(i));
        EXPECT_EQ(tabs[(size_t)i]->getRadioGroupId(), 4242);
    }
    EXPECT_TRUE(tabs[0]->getToggleState());
}

TEST_F(WavetableTabStripTest, ClickingATabShowsOnlyThatPagesControls) {
    EXPECT_EQ(strip->getActivePage(), 0);
    EXPECT_TRUE(visibleSliders().count("Octave"));
    EXPECT_FALSE(visibleSliders().count("Detune"));

    clickTab(1);
    EXPECT_EQ(strip->getActivePage(), 1);
    EXPECT_TRUE(visibleSliders().count("Detune"));
    EXPECT_FALSE(visibleSliders().count("Octave"));
    EXPECT_TRUE(visibleCombos().count("Stack"));
    EXPECT_FALSE(visibleCombos().count("Sub Oct"));

    clickTab(3);
    EXPECT_TRUE(visibleCombos().count("Sub Oct"));
    EXPECT_FALSE(visibleCombos().count("Stack"));

    const auto tabs = tabButtons();
    for (int i = 0; i < (int)tabs.size(); ++i)
        EXPECT_EQ(tabs[(size_t)i]->getToggleState(), i == 3) << "tab " << i;
}

TEST_F(WavetableTabStripTest, PinnedAndChromeControlsStayVisibleOnEveryPage) {
    for (int page = 0; page < WavetableTabStrip::kNumPages; ++page) {
        clickTab(page);
        EXPECT_TRUE(visibleSliders().count("Position")) << "page " << page;
        EXPECT_TRUE(visibleSliders().count("Warp Amt")) << "page " << page;
        EXPECT_TRUE(visibleCombos().count("Warp")) << "page " << page;
        EXPECT_TRUE(visibleCombos().count("Table")) << "page " << page;
    }
}

TEST_F(WavetableTabStripTest, EveryControlIsReachableFromSomePage) {
    std::set<juce::String> seen;
    for (int page = 0; page < WavetableTabStrip::kNumPages; ++page) {
        clickTab(page);
        for (const auto& n : visibleSliders())
            seen.insert(n);
        for (const auto& n : visibleCombos())
            seen.insert(n);
    }
    EXPECT_EQ(seen.size(), sliders.size() + combos.size());
}

TEST_F(WavetableTabStripTest, OnPageChangedFiresOncePerRealChangeAndNotForTheActivePage) {
    int fired = 0;
    strip->onPageChanged = [&] { ++fired; };

    clickTab(0); // already active
    EXPECT_EQ(fired, 0);
    clickTab(2);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(strip->getActivePage(), 2);
}

TEST_F(WavetableTabStripTest, PreferredHeightIsTheTallestPageNotTheActiveOne) {
    // Sub page: 3 combos (Sub Oct, Sub Wave, Sync In) -> one row of combos + 2 knobs (Sub, Pan) -> one
    // knob row. Unison page: 1 combo (Stack) + 4 knobs, the same two rows. Tune: 4 knobs only.
    const int tallest = strip->getTallestPageHeight();
    EXPECT_GT(tallest, 0);

    for (int page = 0; page < WavetableTabStrip::kNumPages; ++page) {
        clickTab(page);
        EXPECT_EQ(strip->layoutBody(10, 20, kContentW, false), bottom) << "measured height changed on page " << page;
        EXPECT_EQ(strip->getTallestPageHeight(), tallest);
    }

    // The body ends exactly a tallest-page below where the active page starts.
    EXPECT_GE(bottom, strip->getBottom() + 8 + tallest);
}

TEST_F(WavetableTabStripTest, LayoutPlacesTheStripBelowThePinnedRowAndKeepsControlsInside) {
    EXPECT_EQ(strip->getX(), 20);
    EXPECT_EQ(strip->getWidth(), kContentW);
    EXPECT_GT(strip->getY(), 10);

    const auto* position = sliders[0].get();
    EXPECT_LT(position->getBottom(), strip->getY()) << "the pinned row sits above the tab strip";

    for (int page = 0; page < WavetableTabStrip::kNumPages; ++page) {
        clickTab(page);
        parent.setSize(parent.getWidth(), bottom);
        strip->layoutBody(10, 20, kContentW, true);
        for (auto& s : sliders)
            if (s->isVisible())
                EXPECT_TRUE(parent.getLocalBounds().contains(s->getBounds()))
                    << s->getComponentID() << " on page " << page;
        for (auto& c : combos)
            if (c->isVisible() && c->getComponentID() != "Table") // Table is placed by the card, not the strip
                EXPECT_TRUE(parent.getLocalBounds().contains(c->getBounds()))
                    << c->getComponentID() << " on page " << page;
    }
}

TEST_F(WavetableTabStripTest, IsChromeComboIdentifiesOnlyTheTableSelector) {
    for (size_t i = 0; i < combos.size(); ++i)
        EXPECT_EQ(strip->isChromeCombo(*combos[i]), combos[i]->getComponentID() == "Table");

    juce::ComboBox stranger;
    EXPECT_FALSE(strip->isChromeCombo(stranger));
}

TEST_F(WavetableTabStripTest, TabButtonsFillTheStripWidthEdgeToEdge) {
    const auto tabs = tabButtons();
    int previousRight = 0;
    for (auto* tab : tabs) {
        EXPECT_EQ(tab->getX(), previousRight);
        EXPECT_EQ(tab->getHeight(), strip->getHeight());
        previousRight = tab->getRight();
    }
    EXPECT_LE(previousRight, strip->getWidth());
    EXPECT_GT(previousRight, strip->getWidth() - (int)tabs.size());
}
