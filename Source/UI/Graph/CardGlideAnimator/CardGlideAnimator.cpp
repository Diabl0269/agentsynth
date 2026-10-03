// CardGlideAnimator.cpp
//
// The glide overlay. Real cards keep their FINAL bounds the moment a mutation lands (hit-tests, hulls and cable
// geometry all read them), so the slide is an illusion: each moved card is hidden with alpha 0 (JUCE paints nothing
// for it, yet mouse routing still resolves to its final rect) and a snapshot of it is drawn interpolating from the old
// rect to the new one, on top of the canvas. Time-bounded: the driver repaints only while it runs.

#include "CardGlideAnimator.h"

#include "UI/Graph/GraphEditor/GraphEditorTypes.h"

#include <algorithm>
#include <limits>

namespace {
constexpr double kGlideMs = 160.0;

// The part of the canvas the card's viewer shows, in canvas coordinates: the canvas (the card's parent) is a
// transformed child of the editor that frames it. Everything when the card has no such grandparent.
juce::Rectangle<int> visibleCanvasArea(const juce::Component& card) {
    auto* canvas = card.getParentComponent();
    auto* view = canvas != nullptr ? canvas->getParentComponent() : nullptr;
    if (view == nullptr)
        return {std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::min() / 2,
                std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    return canvas->getLocalArea(view, view->getLocalBounds());
}

juce::Rectangle<float> lerpRect(juce::Rectangle<int> a, juce::Rectangle<int> b, float t) {
    auto lerp = [t](int x, int y) { return static_cast<float>(x) + static_cast<float>(y - x) * t; };
    return {lerp(a.getX(), b.getX()), lerp(a.getY(), b.getY()), lerp(a.getWidth(), b.getWidth()),
            lerp(a.getHeight(), b.getHeight())};
}
} // namespace

CardGlideAnimator::~CardGlideAnimator() {
    if (hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
}

void CardGlideAnimator::setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

std::vector<CardGlideAnimator::Captured> CardGlideAnimator::capture(const std::vector<Entry>& cards) {
    std::vector<Captured> out;
    for (const auto& e : cards)
        if (e.comp != nullptr && e.comp->isVisible() && !e.comp->getBounds().isEmpty())
            out.push_back({juce::Component::SafePointer<juce::Component>(e.comp), e.nodeUid, e.comp->getBounds()});
    return out;
}

juce::Rectangle<float> CardGlideAnimator::currentRect(const Item& item) const noexcept {
    return lerpRect(item.from, item.to, progress_);
}

// A card already gliding retargets from where it is drawn right now (docs/layout/animation.md "Interruption"), and
// keeps the alpha saved when it was first hidden: re-saving would record the 0 this class set. Every other live card
// is rebased the same way because the driver restarts its clock at 0.
// A card whose whole path (the union of its old and new rects, which every in-between rect stays inside) is outside
// the visible canvas is neither hidden nor snapshotted: nothing of it can be seen, and rendering a snapshot per moved
// card is what made an undo of a many-track arrange stall for most of a second. It keeps an item without a snapshot,
// so cables touching it still slide (offsetFor).
bool CardGlideAnimator::arm(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale) {
    std::vector<const Entry*> changed;
    std::vector<juce::Rectangle<int>> fromRects;
    for (const auto& e : now) {
        if (e.comp == nullptr || !e.comp->isVisible() || e.comp->getBounds().isEmpty() ||
            e.comp->isMouseButtonDown(true))
            continue;
        // The same component, or (a restore that rebuilt every card) the card now standing for the same node.
        const auto was = std::find_if(before.begin(), before.end(), [&e](const Captured& c) {
            return c.comp.getComponent() == e.comp || (e.nodeUid != 0 && c.comp == nullptr && c.nodeUid == e.nodeUid);
        });
        if (was == before.end() || was->bounds == e.comp->getBounds())
            continue;
        changed.push_back(&e);
        fromRects.push_back(was->bounds);
    }
    if (changed.empty())
        return false;

    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const Item& it) { return it.comp == nullptr; }),
                 items_.end());
    for (auto& item : items_)
        item.from = currentRect(item).toNearestInt();
    const auto visible = visibleCanvasArea(*changed.front()->comp);
    for (size_t i = 0; i < changed.size(); ++i) {
        auto* comp = changed[i]->comp;
        auto existing = std::find_if(items_.begin(), items_.end(),
                                     [comp](const Item& it) { return it.comp.getComponent() == comp; });
        if (existing != items_.end()) {
            existing->to = comp->getBounds(); // `from` already rebased to the drawn position
            if (existing->snapshot.isNull() && existing->from.getUnion(existing->to).intersects(visible)) {
                existing->snapshot = comp->createComponentSnapshot(comp->getLocalBounds(), true, snapshotScale);
                comp->setAlpha(0.0f); // was off-screen, now crosses the view: its alpha was never touched
            }
            continue;
        }
        Item item;
        item.comp = comp;
        item.nodeUid = changed[i]->nodeUid;
        item.from = fromRects[i];
        item.to = comp->getBounds();
        item.savedAlpha = comp->getAlpha();
        if (item.from.getUnion(item.to).intersects(visible)) {
            item.snapshot = comp->createComponentSnapshot(comp->getLocalBounds(), true, snapshotScale);
            comp->setAlpha(0.0f);
        }
        items_.push_back(std::move(item));
    }
    progress_ = 0.0f;
    ++armCount_;
    return true;
}

void CardGlideAnimator::applyTweenAt(float t) noexcept {
    progress_ = juce::jlimit(0.0f, 1.0f, t);
    // A card the user grabs mid-glide stops gliding: it is theirs now.
    for (auto& item : items_)
        if (item.comp != nullptr && item.comp->isMouseButtonDown(true)) {
            item.comp->setAlpha(item.savedAlpha);
            item.comp = nullptr;
            item.snapshot = {};
        }
    // An off-screen card's item has no snapshot but a live card; one whose card is gone or grabbed has neither.
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [](const Item& it) { return it.snapshot.isNull() && it.comp == nullptr; }),
                 items_.end());
}

void CardGlideAnimator::finish() noexcept {
    for (auto& item : items_)
        if (auto* comp = item.comp.getComponent(); comp != nullptr && comp->getAlpha() == 0.0f)
            comp->setAlpha(item.savedAlpha);
    items_.clear();
    progress_ = 0.0f;
}

void CardGlideAnimator::paint(juce::Graphics& g) const {
    for (const auto& item : items_) {
        if (item.snapshot.isNull())
            continue;
        g.drawImage(item.snapshot, currentRect(item), juce::RectanglePlacement::stretchToFit);
    }
}

juce::Point<float> CardGlideAnimator::offsetFor(uint32_t nodeUid) const noexcept {
    if (nodeUid == 0)
        return {};
    for (const auto& item : items_)
        if (item.nodeUid == nodeUid) {
            const auto cur = currentRect(item);
            return {cur.getX() - static_cast<float>(item.to.getX()), cur.getY() - static_cast<float>(item.to.getY())};
        }
    return {};
}

// Every chain endpoint is a real src/dst node (an attenuverter chain carries its hidden node in attenUid only), so
// shifting p1/p2 by the card offset keeps both the jack anchor and a knob-landing anchor on the gliding card.
void CardGlideAnimator::applyTo(std::vector<graph_editor_types::VisibleCable>& cables) const {
    if (items_.empty())
        return;
    for (auto& c : cables) {
        c.p1 += offsetFor(c.id.srcUid);
        c.p2 += offsetFor(c.id.dstUid);
    }
}

juce::Rectangle<int> CardGlideAnimator::dirtyArea() const noexcept {
    juce::Rectangle<int> area;
    for (const auto& item : items_)
        area = area.getUnion(item.from.getUnion(item.to));
    return area;
}

juce::Rectangle<int> CardGlideAnimator::currentRectFor(const juce::Component* comp) const noexcept {
    for (const auto& item : items_)
        if (item.comp.getComponent() == comp)
            return currentRect(item).toNearestInt();
    return {};
}

void CardGlideAnimator::startDriver() {
    if (hooks_.updater == nullptr)
        return; // no VBlank to drive it: the test seams advance and finish by hand
    driver_.start(
        *hooks_.updater, kGlideMs, synth::ui::easeOutCubic,
        [this](float t) {
            applyTweenAt(t);
            ++repaintCount_;
            if (hooks_.repaint)
                hooks_.repaint();
        },
        [this]() {
            finish();
            ++repaintCount_;
            if (hooks_.repaint)
                hooks_.repaint();
        });
}

CardGlideAnimator::Scope::Scope(CardGlideAnimator& animator)
    : animator_(animator) {
    if (animator_.depth_++ == 0 && animator_.hooks_.cards)
        animator_.before_ = capture(animator_.hooks_.cards());
}

CardGlideAnimator::Scope::~Scope() {
    if (--animator_.depth_ != 0 || !animator_.hooks_.cards)
        return;
    const float scale = animator_.hooks_.snapshotScale ? animator_.hooks_.snapshotScale() : 1.0f;
    const bool armed = animator_.arm(animator_.before_, animator_.hooks_.cards(), scale);
    animator_.before_.clear();
    if (!armed)
        return;
    if (animator_.hooks_.repaint)
        animator_.hooks_.repaint();
    animator_.startDriver();
}
