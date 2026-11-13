// Concern: BottomDockComponent tab motion -- the strip's buttons while a tab leaves it (its panel opens in its own
// window, or goes to the main area) or comes back (docs/layout/animation.md#bottom-dock-tabs).
#include "BottomDockComponent.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"

namespace synth::ui {

std::vector<BottomDockComponent::Tab> BottomDockComponent::offeredTabs() const {
    std::vector<Tab> offered;
    for (Tab t : tabModel_.getOrder())
        if (isTabOfferedInStrip(t))
            offered.push_back(t);
    return offered;
}

// The frames come from the shared reorder frame pump (a VBlank pump that runs only while the motion does), so a
// settled strip costs no timer and no repaint. The caller has already checked the strip is on screen and animations
// are on; everything else lands at once in layoutTabButtons().
void BottomDockComponent::startTabMotion(const std::vector<Tab>& before, const std::vector<Tab>& after) {
    if (!tabMotion_.begin(before, after, prefersReducedMotion()))
        return;
    tabMotionStartMs_ = juce::Time::getMillisecondCounterHiRes();
    // A tab on its way out cannot be clicked: activating it would select a panel that has left the strip.
    for (const auto& placement : tabMotion_.placements(tabButtonsArea_))
        buttonForTab(placement.tab).setInterceptsMouseClicks(!placement.leaving, !placement.leaving);
    tabMotionFrames_.run(tabMotion_.totalMs() + 20.0, [this] { onTabMotionFrame(); }, [this] { finishTabMotion(); });
}

void BottomDockComponent::onTabMotionFrame() {
    if (!tabMotion_.isRunning())
        return;
    tabMotion_.applyAtMs(juce::Time::getMillisecondCounterHiRes() - tabMotionStartMs_);
    placeTabsForMotion();
}

void BottomDockComponent::stepTabMotionForTest(double elapsedMs) {
    if (!tabMotion_.isRunning())
        return;
    if (tabMotion_.applyAtMs(elapsedMs)) {
        finishTabMotion();
        return;
    }
    placeTabsForMotion();
}

// Every tab the motion draws gets its slot, its shrink about the slot's centre and (Reduce Motion) its fade; a tab
// it does not draw is hidden.
void BottomDockComponent::placeTabsForMotion() {
    const auto placements = tabMotion_.placements(tabButtonsArea_);
    for (Tab t : tabModel_.getOrder()) {
        bool drawn = false;
        for (const auto& p : placements) {
            if (p.tab != t)
                continue;
            drawn = true;
            auto& button = buttonForTab(t);
            button.setBounds(p.bounds);
            button.setVisible(p.visible);
            button.setAlpha(p.alpha);
            const auto centre = p.bounds.toFloat().getCentre();
            button.setTransform(p.scale < 1.0f ? juce::AffineTransform::scale(p.scale, p.scale, centre.x, centre.y)
                                               : juce::AffineTransform());
        }
        if (!drawn)
            buttonForTab(t).setVisible(false);
    }
    repaintTabStrip();
}

void BottomDockComponent::finishTabMotion() {
    if (!tabMotion_.isRunning())
        return;
    laidOutTabs_ = tabMotion_.getTarget();
    tabMotion_.finish();
    for (Tab t : tabModel_.getOrder()) {
        auto& button = buttonForTab(t);
        button.setTransform({});
        button.setAlpha(1.0f);
        button.setInterceptsMouseClicks(true, true);
    }
    layoutTabButtons();
    applyReorderOffsets();
    repaintTabStrip();
}

} // namespace synth::ui
