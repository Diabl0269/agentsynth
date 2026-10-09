// PortConnectionsPanelPages.cpp -- the panel's second page and what feeds it: "Add connection" swaps the list for the
// search page (a cross-fade while the height settles), a pick connects, "Pick on canvas" picks a jack or knob on the
// canvas. docs/layout/cables.md#port-connections-panel.

#include "PortConnectionsPanel.h"

#include "PortPanelController.h"
#include "UI/Graph/ModDot/ModDotMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth::ui {

int PortConnectionsPanel::preferredHeight() const { return settledHeight(searchOpen_); }

int PortConnectionsPanel::settledHeight(bool searchOpen) const {
    return searchOpen ? searchPage_->preferredHeight() : desiredHeight_;
}

// The panel is the list page and the search page laid over one another. Each keeps its own height; the panel's edge
// follows a mix of the two as far as the swap has got, and each page's alpha is its share of it. Under Reduce Motion
// the height is the new page's from the first frame, so only the fade is seen.
void PortConnectionsPanel::applyPages() {
    const float a = searchAmount_;
    listLayer_.setAlpha(1.0f - a);
    listLayer_.setVisible(a < 1.0f);
    searchPage_->setAlpha(a);
    searchPage_->setVisible(a > 0.0f);
    const int searchHeight = searchPage_->preferredHeight();
    searchPage_->setBounds(0, 0, kWidth, searchHeight);
    const int height = heightFollowsFade_
                           ? juce::roundToInt((float)desiredHeight_ + (float)(searchHeight - desiredHeight_) * a)
                           : settledHeight(searchOpen_);
    if (height != getHeight() || getWidth() != kWidth)
        setSize(kWidth, height);
    heightChanged();
    repaint();
}

void PortConnectionsPanel::applyPageTweenAt(float t) {
    searchAmount_ = pageFrom_ + (pageTo_ - pageFrom_) * t;
    applyPages();
}

void PortConnectionsPanel::setSearchOpen(bool open) {
    if (searchOpen_ == open)
        return;
    searchOpen_ = open;
    if (open) {
        stopPick();
        refreshTargets();
        searchPage_->reset();
    } else if (controller_ != nullptr) {
        controller_->clearPreview(); // no row of the page is under the pointer any more
    }
    split_.setLeftLit(open);
    const bool focusWasInSearch = searchPage_->isParentOf(juce::Component::getCurrentlyFocusedComponent());
    listLayer_.setInterceptsMouseClicks(!open, !open);
    searchPage_->setInterceptsMouseClicks(open, open);

    pageFrom_ = searchAmount_;
    pageTo_ = open ? 1.0f : 0.0f;
    const auto settle = [this, open, focusWasInSearch] {
        searchAmount_ = pageTo_;
        heightFollowsFade_ = true;
        applyPages();
        if (open)
            searchPage_->focusEntry();
        else if (focusWasInSearch)
            focusEntry();
    };
    if (!(isShowing() || forceAnimate_)) {
        pageAnim_.stop(pageUpdater_);
        settle();
        return;
    }
    const bool reduced = prefersReducedMotion();
    heightFollowsFade_ = !reduced;
    pageAnim_.start(
        pageUpdater_, reduced ? kPageReducedMs : (open ? kPageInMs : kPageOutMs),
        reduced ? std::function<float(float)>([](float t) { return t; })
                : (open ? std::function<float(float)>(easeOutCubic) : std::function<float(float)>(easeInCubic)),
        [this](float t) { applyPageTweenAt(t); }, settle);
    if (open)
        searchPage_->focusEntry();
}

// The targets the search page lists and a canvas pick may land on, as of now.
void PortConnectionsPanel::refreshTargets() {
    auto modules = listPortTargets(editor_, port_);
    if (picker_ != nullptr)
        pickTargets_ = modules;
    if (searchOpen_)
        searchPage_->setTargets(std::move(modules), listNewModuleTargets(port_));
}

// One undo step (PortPanelController::connect); the list takes the new row in as the page swaps back.
void PortConnectionsPanel::connectTarget(const PortTarget& target) {
    if (controller_ == nullptr || target.connected)
        return;
    stopPick();
    const PortTarget picked = target; // the page's rows are rebuilt by the refresh below
    if (!controller_->connect(port_, picked))
        return;
    sync();
    setSearchOpen(false);
}

// ---- Pick on canvas ----

const PortTarget* PortConnectionsPanel::pickableTargetFor(const ModDotCanvasPicker::JackHit& hit) const {
    const PortTarget* target =
        hit.knob ? findKnobTarget(pickTargets_, hit.node, hit.index)
                 : findJackTarget(pickTargets_, PortRef{hit.node, hit.index, hit.isInput, hit.isMidi});
    return target != nullptr && !target->connected ? target : nullptr;
}

void PortConnectionsPanel::startPick() {
    if (picker_ != nullptr)
        return;
    retiredPicker_.reset();
    pickTargets_ = listPortTargets(editor_, port_);
    picker_ = std::make_unique<ModDotCanvasPicker>(
        editor_, [this](const ModDotCanvasPicker::JackHit& hit) { return pickableTargetFor(hit) != nullptr; });
    picker_->onPickedJack = [this](const ModDotCanvasPicker::JackHit& hit) {
        if (const auto* target = pickableTargetFor(hit))
            connectTarget(*target);
    };
    picker_->onEscape = [this] { stopPick(); };
    picker_->begin();
    split_.setRightLit(true);
    juce::AccessibilityHandler::postAnnouncement("Pick on canvas. Click a jack or knob to connect to. Escape stops.",
                                                 juce::AccessibilityHandler::AnnouncementPriority::medium);
}

// The layer is parked, not deleted: this can run from inside one of its own event handlers.
void PortConnectionsPanel::stopPick() {
    split_.setRightLit(false);
    if (picker_ == nullptr)
        return;
    picker_->setVisible(false);
    if (auto* parent = picker_->getParentComponent())
        parent->removeChildComponent(picker_.get());
    retiredPicker_ = std::move(picker_);
}

void PortConnectionsPanel::togglePick() {
    if (picker_ != nullptr)
        stopPick();
    else
        startPick();
}

} // namespace synth::ui
