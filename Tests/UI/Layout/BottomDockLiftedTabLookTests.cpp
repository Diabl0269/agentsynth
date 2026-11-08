// BottomDockLiftedTabLookTests.cpp -- a dock tab you drag keeps its own look: the active tab wears the
// look-and-feel's toggled-on fill and text through the lift, the drag and the drop's glide, an inactive tab keeps
// the raised inactive look. Painted for real (software image, so Windows reads pixels too) under every theme.
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "BottomDockMainAreaFixture.h"
#include "FadeVisibilityTestGuard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::ui::BottomDockComponent;
using Tab = BottomDockComponent::Tab;

int distance(juce::Colour a, juce::Colour b) {
    return std::abs(a.getRed() - b.getRed()) + std::abs(a.getGreen() - b.getGreen()) +
           std::abs(a.getBlue() - b.getBlue());
}

class BottomDockLiftedTabLookTest : public BottomDockMainAreaTest {
protected:
    void SetUp() override {
        BottomDockMainAreaTest::SetUp();
        if (!mc_->isBottomDockConfiguredVisible())
            mc_->simulateToggleBottomPanelClick();
        mc_->resized();
    }
    void TearDown() override {
        mc_->setLookAndFeel(nullptr);
        // Opening the dock persists "bottomDockVisible" in the shared settings; later tests expect it hidden.
        if (auto* settings = mc_->getAppPropertiesForTest().getUserSettings()) {
            settings->removeValue("bottomDockVisible");
            settings->saveIfNeeded();
        }
        BottomDockMainAreaTest::TearDown();
    }
    juce::Component& tabButton(Tab tab) { return *dock().getSwapIcon(tab).getParentComponent(); }

    juce::Image paintDock() {
        juce::Image img(juce::Image::ARGB, dock().getWidth(), dock().getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        dock().paintEntireComponent(g, false);
        return img;
    }

    // Sampled inside the lifted tab's body (left of the label) and over its label; the tab is where the animator
    // draws it (dock coordinates), raised by the lift.
    struct Samples {
        juce::Colour body;
        juce::Colour label; // the pixel in the label band furthest from the body
    };
    Samples sample(Tab tab, const juce::Image& img) {
        auto& button = tabButton(tab);
        const int left = static_cast<int>(std::lround(dock().getLiftedTabLeftForTest()));
        const int top = button.getY() - 4; // raised by the lift
        Samples s;
        s.body = img.getPixelAt(left + 3, button.getY() + button.getHeight() / 2 - 2);
        int best = -1;
        for (int x = left + button.getWidth() / 4; x < left + 3 * button.getWidth() / 4; ++x)
            for (int y = top + 3; y < top + button.getHeight() - 3; ++y) {
                const auto px = img.getPixelAt(x, y);
                if (distance(px, s.body) > best) {
                    best = distance(px, s.body);
                    s.label = px;
                }
            }
        return s;
    }

    // A drop persists the new tab order in the shared settings file; put the original back so later tests see it.
    void restoreOrder(const std::vector<Tab>& original) {
        for (size_t i = 0; i < original.size(); ++i) {
            const auto order = dock().getTabOrderForTest();
            if (order[i] == original[i])
                continue;
            dock().reorderTabsForTest(order[i], original[i]);
        }
    }

    // Picks the tab up and drags it a little way so it is lifted at full lift.
    void liftAndDrag(Tab tab, double& now) {
        auto* grabbed = &tabButton(tab);
        grabbed->mouseDown(makeClickEvent(*grabbed, {10.0f, 8.0f}));
        grabbed->mouseDrag(makeDragEvent(*grabbed, {10.0f + 40.0f, 8.0f}, {10.0f, 8.0f}));
        now = 500.0;
        grabbed->mouseDrag(makeDragEvent(*grabbed, {50.0f, 8.0f}, {10.0f, 8.0f}));
        now = 1000.0;
        dock().stepReorderFrameForTest();
    }
};

} // namespace

TEST_F(BottomDockLiftedTabLookTest, TheLiftedActiveTabKeepsTheActiveFillAndTextAndAnInactiveOneKeepsTheInactiveLook) {
    FadeAnimateGuard guard;
    const auto originalOrder = dock().getTabOrderForTest();
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        mc_->setLookAndFeel(&lookAndFeel);
        mc_->resized();
        const auto& c = theme.colors;
        for (const bool dragActive : {true, false}) {
            dock().setActiveTab(Tab::Timeline);
            double now = 0.0;
            dock().setReorderAnimationForTest(true, [&now] { return now; });
            const Tab dragged = dragActive ? Tab::Timeline : Tab::Mixer;
            liftAndDrag(dragged, now);

            const auto s = sample(dragged, paintDock());
            const auto wantFill = dragActive ? c.accent : c.surfaceHi;
            const auto otherFill = dragActive ? c.surfaceHi : c.accent;
            const std::string what = theme.name.toStdString() + (dragActive ? " active tab" : " inactive tab");
            EXPECT_LT(distance(s.body, wantFill), distance(s.body, otherFill)) << what << " fill while dragged";
            EXPECT_LT(distance(s.body, wantFill), 60) << what << " fill is the look-and-feel's";
            const auto wantText = dragActive ? c.bg0 : c.textPrimary;
            EXPECT_LT(distance(s.label, wantText), distance(s.label, s.body)) << what << " text while dragged";

            tabButton(dragged).mouseUp(makeClickEvent(tabButton(dragged), {50.0f, 8.0f}));
            now = 1000.0 + 70.0;
            dock().stepReorderFrameForTest(); // a frame of the drop's glide
            const auto settle = sample(dragged, paintDock());
            EXPECT_LT(distance(settle.body, wantFill), distance(settle.body, otherFill))
                << what << " fill in the glide";
            EXPECT_LT(distance(settle.label, wantText), distance(settle.label, settle.body))
                << what << " text in the glide";
            now = 1.0e6;
            dock().stepReorderFrameForTest();
            restoreOrder(originalOrder);
        }
    }
}
