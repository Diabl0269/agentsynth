// CardBodySwapMotion.cpp -- a card's controls swapping in place: SwapMotion (the two timed phases) and the
// CardBody glue that records what leaves and arrives around a condition re-read.
// docs/layout/animation.md#controls-swapping-in-place.
#include "CardBodySwapMotion.h"
#include "CardBody.h"
#include "CardBodyMoreButton.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <utility>

namespace synth {

namespace cm = ui::control_motion;

SwapMotion::SwapMotion(juce::Component& card, std::vector<Departure> leaving, std::vector<Arrival> arriving)
    : card_(card)
    , arriving_(std::move(arriving))
    , pump_(card) {
    for (auto& departure : leaving) {
        auto* ghost = ghosts_.add(
            new cm::ShrinkGhost(std::move(departure.image), departure.rect, cm::axisFor(departure.rect), false));
        card_.addAndMakeVisible(ghost);
    }
    for (const auto& arrival : arriving_)
        for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
            if (part != nullptr) {
                part->setVisible(false);
                part->setAlpha(0.0f);
            }
    setElapsed(0.0);
}

SwapMotion::~SwapMotion() {
    pump_.stop();
    land();
}

void SwapMotion::run(std::function<void()> onDone) {
    onDone_ = std::move(onDone);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    pump_.run(
        totalMs(), [this, start] { setElapsed(juce::Time::getMillisecondCounterHiRes() - start); },
        [this] { finish(); });
}

// One frame. The first phase is the leaving controls shrinking, the arriving ones held hidden; the second
// takes the ghosts away and grows the arriving ones. Only one of the two is ever on the card in a frame.
void SwapMotion::setElapsed(double elapsedMs) {
    if (finished_)
        return;
    const double shrink = cm::kSwapShrinkMs;
    if (elapsedMs < shrink) {
        const float t = (float)(elapsedMs / shrink);
        for (auto* ghost : ghosts_)
            ghost->setProgress(t);
        return;
    }
    if (!growing_) {
        growing_ = true;
        ghosts_.clear();
        for (const auto& arrival : arriving_)
            for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
                if (part != nullptr)
                    part->setVisible(true);
    }
    const float t = (float)juce::jlimit(0.0, 1.0, (elapsedMs - shrink) / cm::kGrowMs);
    if (t >= 1.0f) {
        finish();
        return;
    }
    for (const auto& arrival : arriving_) {
        const auto cellAxis = cm::axisFor(arrival.rect);
        for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
            if (part != nullptr) {
                part->setAlpha(juce::jlimit(0.0f, 1.0f, ui::easeOutCubic(t)));
                part->setTransform(cm::scaleAbout(part->getBounds(), cm::growScale(t), cellAxis));
            }
    }
}

void SwapMotion::land() {
    ghosts_.clear();
    for (const auto& arrival : arriving_)
        for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
            if (part != nullptr) {
                part->setVisible(true);
                part->setAlpha(1.0f);
                part->setTransform({});
            }
    arriving_.clear();
}

void SwapMotion::finish() {
    if (finished_)
        return;
    finished_ = true;
    pump_.stop();
    land();
    if (auto done = std::exchange(onDone_, nullptr))
        done();
}

bool SwapMotion::holdsHidden(const juce::Component& component) const {
    if (finished_ || growing_)
        return false;
    for (const auto& arrival : arriving_)
        if (arrival.widget.getComponent() == &component || arrival.label.getComponent() == &component)
            return true;
    return false;
}

std::vector<juce::Rectangle<float>> SwapMotion::ghostRects() const {
    std::vector<juce::Rectangle<float>> rects;
    for (auto* ghost : ghosts_)
        rects.push_back(ghost->pictureRect().translated((float)ghost->getX(), (float)ghost->getY()));
    return rects;
}

// ---- The card body's side ---------------------------------------------------------------------------

namespace {

// The cell a widget and its caption fill, in card pixels.
juce::Rectangle<int> cellOf(const CardBodyItem& item) {
    auto rect = item.widget->getBounds();
    if (item.label != nullptr)
        rect = rect.getUnion(item.label->getBounds());
    return rect;
}

} // namespace

CardBody::~CardBody() { *widgetsAlive_ = false; }

bool CardBody::canAnimateSwap() const {
    return (forceAnimateForTest_ || card_.isShowing()) && !ui::prefersReducedMotion() && !ui::animationsOff();
}

// The controls a swap can move: those a condition or a look governs.
bool CardBody::isSwapGoverned(int item) const {
    const auto& planned = plan_.items[(size_t)item];
    if (planned.widget == nullptr || planned.kind == CardBodyItem::Kind::View)
        return false;
    if (planned.swapGroup >= 0 || !planned.alsoIn.empty())
        return true;
    return planned.section >= 0 && plan_.sections[(size_t)planned.section].visibleWhen.has_value();
}

// Before a re-read: the cell and visibility of every governed control.
CardBody::SwapSnapshot CardBody::snapshotForSwap() const {
    SwapSnapshot snapshot;
    for (int i = 0; i < (int)plan_.items.size(); ++i)
        if (isSwapGoverned(i) && plan_.items[(size_t)i].widget->isVisible())
            snapshot.shown.push_back({i, cellOf(plan_.items[(size_t)i]), {}});
    return snapshot;
}

// After the re-read has changed which controls show, and before the card shows it: a picture of each control
// about to leave (or to move, a look's shared control standing elsewhere in the other look).
void CardBody::pictureLeavingControls(SwapSnapshot& snapshot) const {
    for (auto& part : snapshot.shown) {
        const auto& item = plan_.items[(size_t)part.item];
        const bool stays = plan_.isOnCard(part.item) && item.shown;
        if (stays && item.alsoIn.empty())
            continue;
        part.image = card_.createComponentSnapshot(part.rect, true, 2.0f);
    }
}

// After the card has laid out: the controls that left or moved leave as ghosts, the ones that arrived or
// moved arrive grown. Nothing runs when the card changed height (a mode switch the card makes whole).
void CardBody::startSwapMotion(SwapSnapshot& snapshot) {
    std::vector<SwapMotion::Departure> leaving;
    std::vector<SwapMotion::Arrival> arriving;
    for (const auto& part : snapshot.shown) {
        const auto& item = plan_.items[(size_t)part.item];
        const bool nowShown = item.widget->isVisible() && plan_.isOnCard(part.item) && item.shown;
        const auto now = cellOf(item);
        if (!nowShown || now != part.rect)
            if (!part.image.isNull())
                leaving.push_back({part.image, part.rect});
        if (nowShown && now != part.rect)
            arriving.push_back({item.widget, item.label, now});
    }
    for (int i = 0; i < (int)plan_.items.size(); ++i) {
        if (!isSwapGoverned(i) || !plan_.isOnCard(i) || !plan_.items[(size_t)i].shown)
            continue;
        const auto& item = plan_.items[(size_t)i];
        const bool wasShown =
            std::any_of(snapshot.shown.begin(), snapshot.shown.end(), [i](const auto& part) { return part.item == i; });
        if (!wasShown && item.widget->isVisible())
            arriving.push_back({item.widget, item.label, cellOf(item)});
    }
    if (leaving.empty() && arriving.empty())
        return;
    swap_ = std::make_unique<SwapMotion>(card_, std::move(leaving), std::move(arriving));
    swap_->run([this] {
        if (onConditionsApplied)
            onConditionsApplied();
    });
}

void CardBody::finishSwapMotion() {
    if (swap_ != nullptr)
        swap_->finish();
}

bool CardBody::isHeldBySwap(const juce::Component& component) const {
    return swap_ != nullptr && swap_->holdsHidden(component);
}

bool CardBody::isSwapMotionRunning() const { return swap_ != nullptr && swap_->isRunning(); }

void CardBody::stepSwapMotionForTest(double elapsedMs) {
    if (swap_ != nullptr)
        swap_->setElapsed(elapsedMs);
}

std::vector<juce::Rectangle<float>> CardBody::swapGhostRectsForTest() const {
    return swap_ != nullptr ? swap_->ghostRects() : std::vector<juce::Rectangle<float>>();
}

} // namespace synth
