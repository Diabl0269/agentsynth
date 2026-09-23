// Concern: the pick-target overlay's measuring, painting and click resolution (see the header).

#include "UI/Graph/PickTargetOverlay/PickTargetOverlay.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

// Component::isShowing() also needs a native peer, which a headless test host never has; the overlay's
// parent is always on screen while it is active, so every ancestor being visible is the whole question.
bool isVisibleInHierarchy(const juce::Component& c) {
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (!p->isVisible())
            return false;
    return true;
}

} // namespace

PickTargetOverlay::PickTargetOverlay() {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setVisible(false);
}

PickTargetOverlay::~PickTargetOverlay() {
    if (watchedParent_ != nullptr)
        watchedParent_->removeComponentListener(this);
}

void PickTargetOverlay::begin(std::vector<PickCandidate> candidates) {
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;
    if (watchedParent_ != parent) {
        if (watchedParent_ != nullptr)
            watchedParent_->removeComponentListener(this);
        parent->addComponentListener(this);
        watchedParent_ = parent;
    }

    candidates_ = std::move(candidates);
    active_ = true;
    setBounds(parent->getLocalBounds());
    refreshOutlines();
    setVisible(true);
    toFront(false);
    if (isShowing())
        grabKeyboardFocus(); // a hidden or peer-less host (a headless test) cannot take focus
}

void PickTargetOverlay::setCandidates(std::vector<PickCandidate> candidates) {
    if (!active_)
        return;
    candidates_ = std::move(candidates);
    refreshOutlines();
}

void PickTargetOverlay::setPassThrough(std::vector<juce::Component*> components) {
    passThrough_.clear();
    for (auto* c : components)
        passThrough_.emplace_back(c);
}

void PickTargetOverlay::end() {
    active_ = false;
    candidates_.clear();
    outlines_.clear();
    setVisible(false);
}

// A candidate's outline is its bounds in overlay space, clipped by every ancestor between it and the
// overlay's parent -- a card scrolled out of the canvas, or a control under the dock's edge, must
// not paint an outline over something it is not visible on. Rebuilt from the live components each
// time, so a stale (destroyed or moved) control never keeps an outline or answers a click.
void PickTargetOverlay::refreshOutlines() {
    outlines_.clear();
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    for (size_t i = 0; i < candidates_.size(); ++i) {
        auto* c = candidates_[i].component.getComponent();
        if (c == nullptr || !isVisibleInHierarchy(*c) || !parent->isParentOf(c))
            continue;
        auto r = getLocalArea(c, c->getLocalBounds());
        for (auto* p = c->getParentComponent(); p != nullptr && p != parent; p = p->getParentComponent())
            r = r.getIntersection(getLocalArea(p, p->getLocalBounds()));
        r = r.getIntersection(getLocalBounds());
        if (!r.isEmpty())
            outlines_.push_back({i, r});
    }
    repaint();
}

const PickCandidate* PickTargetOverlay::findCandidateAt(juce::Point<int> point) {
    refreshOutlines();
    std::vector<const Outline*> hits;
    for (const auto& o : outlines_)
        if (o.bounds.contains(point))
            hits.push_back(&o);
    if (hits.empty())
        return nullptr;
    if (hits.size() > 1) {
        // Overlapping candidates (a card's control over another card): ask the parent which component is
        // actually on top there. The overlay steps out of the way for that one query.
        probing_ = true;
        auto* parent = getParentComponent();
        auto* top = parent->getComponentAt(parent->getLocalPoint(this, point));
        probing_ = false;
        for (auto* o : hits) {
            auto* c = candidates_[o->candidate].component.getComponent();
            if (c == top || c->isParentOf(top))
                return &candidates_[o->candidate];
        }
    }
    return &candidates_[hits.front()->candidate];
}

void PickTargetOverlay::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    for (const auto& o : outlines_) {
        g.setColour(accent.withAlpha(0.10f));
        g.fillRect(o.bounds);
        g.setColour(accent.withAlpha(0.85f));
        g.drawRect(o.bounds, 1);
    }
}

bool PickTargetOverlay::hitTest(int x, int y) {
    if (!active_ || probing_)
        return false;
    for (auto& p : passThrough_)
        if (auto* c = p.getComponent();
            c != nullptr && isVisibleInHierarchy(*c) && getLocalArea(c, c->getLocalBounds()).contains(x, y))
            return false;
    return true;
}

void PickTargetOverlay::mouseDown(const juce::MouseEvent& e) {
    if (!active_)
        return;
    const PickCandidate* hit =
        e.mods.isLeftButtonDown() && !e.mods.isPopupMenu() ? findCandidateAt(e.getPosition()) : nullptr;
    finish(hit != nullptr, hit != nullptr ? hit->target : synth::midi::PickTarget());
}

bool PickTargetOverlay::keyPressed(const juce::KeyPress& key) {
    if (!active_ || key != juce::KeyPress::escapeKey)
        return false;
    finish(false, {});
    return true;
}

void PickTargetOverlay::finish(bool picked, const synth::midi::PickTarget& target) {
    end();
    if (picked) {
        if (onPicked)
            onPicked(target);
    } else if (onCancelled) {
        onCancelled();
    }
}

void PickTargetOverlay::componentMovedOrResized(juce::Component& component, bool, bool wasResized) {
    if (!active_ || &component != watchedParent_.getComponent() || !wasResized)
        return;
    setBounds(component.getLocalBounds());
    refreshOutlines();
}

} // namespace synth::ui
