// SelectFocusRingTests.cpp -- every select field shows the shared accent focus ring (FocusRing.h) when it
// holds keyboard focus. Walks the routing pane's focusable controls and every combo box of a real MainComponent
// (MainWindowSelects.h), so a new select joins by existing. A headless window cannot hold keyboard focus, so
// "focused" is the ring's own test seam (setFocusRingsShownForTest), and the proof is in the pixels: a control that
// paints a ring draws something different with it on than off.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestWait.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainWindowSelects.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/TimelineRoutingPane/TimelineRoutingPane.h"
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <string>

namespace {

struct RingsShown {
    RingsShown() { synth::ui::setFocusRingsShownForTest(true); }
    ~RingsShown() { synth::ui::setFocusRingsShownForTest(false); }
};

juce::Image render(juce::Component& c) {
    juce::Image img(juce::Image::ARGB, std::max(1, c.getWidth()), std::max(1, c.getHeight()), true,
                    juce::SoftwareImageType());
    juce::Graphics g(img);
    c.paintEntireComponent(g, false);
    return img;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

// True when `c` draws something more with the focus ring shown than without.
bool paintsARing(juce::Component& c) {
    const auto without = render(c);
    RingsShown shown;
    return !sameImage(without, render(c));
}

juce::String nameOf(juce::Component& c) {
    return (c.getTitle().isNotEmpty() ? c.getTitle() : c.getName()) + " (" + typeid(c).name() + ")";
}

class SelectFocusRingTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        // The app installs its look and feel as the desktop default; every combo box picks its ring up from it.
        lookAndFeel_.applyTheme(synth::theme::makeObsidian());
        juce::Desktop::getInstance().setDefaultLookAndFeel(&lookAndFeel_);
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
    }
    void TearDown() override {
        mc_.reset();
        juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }
    synth::theme::AppLookAndFeel lookAndFeel_;
    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace

// The look every select shares: a stock combo box on the app's look and feel rings when focused.
TEST(SelectFocusRingLookTest, AStockComboBoxPaintsTheSharedRingWhenFocused) {
    synth::theme::AppLookAndFeel lookAndFeel;
    lookAndFeel.applyTheme(synth::theme::makeObsidian());
    juce::ComboBox box;
    box.setLookAndFeel(&lookAndFeel);
    box.addItem("one", 1);
    box.setSelectedId(1, juce::dontSendNotification);
    box.setBounds(0, 0, 120, 28);
    EXPECT_TRUE(paintsARing(box));
    box.setLookAndFeel(nullptr);
}

// Regression test: the routing pane's selects showed no focus indication when reached by keys.
TEST_F(SelectFocusRingTest, EveryFocusableControlInTheRoutingPanePaintsARingWhenFocused) {
    ASSERT_TRUE(mc_->setMainViewForTest(synth::ui::BottomDockComponent::Tab::Timeline));
    auto& doc = mc_->getTimelineDoc();
    auto& panel = mc_->getTimelinePanel();
    const auto midi = doc.addTrack(synth::TrackKind::Midi, "Lead");
    const auto audio = doc.addTrack(synth::TrackKind::Audio, "Vox");
    panel.toggleSidePane(true);
    std::set<std::string> seen;
    for (const auto track : {midi, audio}) {
        panel.selectTracks({track}, track);
        mc_->resized();
        synth::test::flushMessageQueue();
        auto& pane = panel.getRoutingPane();
        const auto stops = synth::ui::focusableDescendants(pane);
        ASSERT_GE(stops.size(), 2u) << "the pane shows its selects for the track";
        for (auto* stop : stops) {
            if (stop->getWidth() <= 0 || !main_window_selects::isOnScreenInside(*stop, pane))
                continue;
            seen.insert(nameOf(*stop).toStdString());
            EXPECT_TRUE(paintsARing(*stop)) << nameOf(*stop) << " paints no focus ring";
        }
    }
    EXPECT_GE(seen.size(), 4u) << "MIDI and audio rows together: selects, links and the monitor switch";
}

// The same for every combo box anywhere in the main window: the shared look rings them all.
TEST_F(SelectFocusRingTest, EveryComboBoxInTheMainWindowPaintsARingWhenFocused) {
    int checked = 0;
    main_window_selects::forEachScene(*mc_, [&](const char* scene, std::vector<juce::ComboBox*>& selects) {
        for (auto* select : selects) {
            ++checked;
            EXPECT_TRUE(paintsARing(*select)) << scene << " / " << nameOf(*select) << " paints no focus ring";
        }
    });
    EXPECT_GE(checked, 3) << "the scenes show selects (the song Key field, the card note-length boxes, ...)";
}
