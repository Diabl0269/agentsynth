// CardBodySwapMotion.cpp -- a card's controls swapping in place: SwapMotion (the two timed phases) and the
// CardBody glue that records what leaves and arrives around a condition re-read.
// docs/layout/animation.md#controls-swapping-in-place.
#include "CardBodySwapMotion.h"
#include "CardBody.h"
#include "CardBodyMoreButton.h"
#include "CardBodySwapTestSeam.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace synth {

namespace cm = ui::control_motion;

// One change of controls: its ghosts shrink, then its arrivals grow, on a clock that starts at `startMs`.
struct SwapMotion::Wave {
    Wave(juce::Component& card, std::vector<Departure> leaving, std::vector<Arrival> arriving, double start)
        : card_(card)
        , arriving_(std::move(arriving))
        , hasLeaving_(!leaving.empty())
        , hasArriving_(!arriving_.empty())
        , startMs_(start) {
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
        step(start);
    }

    bool ridesContainer() const noexcept { return containerMs_ > 0.0; }

    double shrinkMs() const noexcept {
        if (!hasLeaving_)
            return 0.0; // nothing to wait for: the arrivals grow from the first frame
        return ridesContainer() ? containerMs_ : cm::kSwapShrinkMs;
    }

    double growMs() const noexcept {
        if (!hasArriving_)
            return 0.0;
        return ridesContainer() ? containerMs_ : cm::kGrowMs;
    }

    bool holdsHidden(const juce::Component& component) const {
        if (growing_)
            return false;
        return ownsArrival(component);
    }

    bool ownsArrival(const juce::Component& component) const {
        for (const auto& arrival : arriving_)
            if (arrival.widget.getComponent() == &component || arrival.label.getComponent() == &component)
                return true;
        return false;
    }

    // An arrival that left the card or that a newer wave takes over: it stops being this wave's.
    void release(const std::function<bool(const Arrival&)>& drop) {
        for (auto it = arriving_.begin(); it != arriving_.end();) {
            if (!drop(*it)) {
                ++it;
                continue;
            }
            for (auto* part : {it->widget.getComponent(), it->label.getComponent()})
                if (part != nullptr) {
                    part->setAlpha(1.0f);
                    part->setTransform({});
                }
            it = arriving_.erase(it);
        }
    }

    // One frame, `elapsedMs` on the swap's clock. The first phase is the leaving controls shrinking, the arriving ones
    // held hidden; the second takes the ghosts away and grows the arriving ones. Riding the card's glide, the one
    // phase there is spans the glide and takes its ease. True once it has run its length.
    bool step(double elapsedMs) {
        elapsedMs -= startMs_;
        const double shrink = shrinkMs();
        if (elapsedMs < shrink) {
            float t = (float)(elapsedMs / shrink);
            // On the card's ease: the ghost draws 1 - easeInCubic(p), so p = cbrt(easeOutCubic(t)) draws it at
            // 1 - easeOutCubic(t), the card's own shrink curve.
            if (ridesContainer())
                t = std::cbrt(ui::easeOutCubic(t));
            for (auto* ghost : ghosts_)
                ghost->setProgress(t);
            return false;
        }
        if (!growing_) {
            growing_ = true;
            ghosts_.clear();
            for (const auto& arrival : arriving_)
                for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
                    if (part != nullptr)
                        part->setVisible(true);
        }
        const double grow = growMs();
        const float t = grow <= 0.0 ? 1.0f : (float)juce::jlimit(0.0, 1.0, (elapsedMs - shrink) / grow);
        if (t >= 1.0f)
            return true;
        const float scale = ridesContainer() ? ui::easeOutCubic(t) : cm::growScale(t);
        for (const auto& arrival : arriving_) {
            const auto cellAxis = cm::axisFor(arrival.rect);
            for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
                if (part != nullptr) {
                    part->setAlpha(juce::jlimit(0.0f, 1.0f, ui::easeOutCubic(t)));
                    part->setTransform(cm::scaleAbout(part->getBounds(), scale, cellAxis));
                }
        }
        return false;
    }

    void land() {
        ghosts_.clear();
        for (const auto& arrival : arriving_)
            for (auto* part : {arrival.widget.getComponent(), arrival.label.getComponent()})
                if (part != nullptr)
                    part->setVisible(true);
        release([](const Arrival&) { return true; });
    }

    juce::Component& card_;
    std::vector<Arrival> arriving_;
    juce::OwnedArray<cm::ShrinkGhost> ghosts_;
    bool hasLeaving_ = false;
    bool hasArriving_ = false;
    double containerMs_ = 0.0;
    double startMs_ = 0.0;
    bool growing_ = false;
};

SwapMotion::SwapMotion(juce::Component& card, std::vector<Departure> leaving, std::vector<Arrival> arriving)
    : card_(card)
    , pump_(card) {
    waves_.push_back(std::make_unique<Wave>(card, std::move(leaving), std::move(arriving), 0.0));
}

SwapMotion::~SwapMotion() {
    pump_.stop();
    land();
}

double SwapMotion::endMs() const {
    double end = 0.0;
    for (const auto& wave : waves_)
        end = std::max(end, wave->startMs_ + wave->shrinkMs() + wave->growMs());
    return end;
}

double SwapMotion::nowMs() const { return pumped_ ? juce::Time::getMillisecondCounterHiRes() - startWall_ : elapsed_; }

void SwapMotion::pump() {
    pump_.run(
        std::max(1.0, endMs() - nowMs()), [this] { frame(juce::Time::getMillisecondCounterHiRes() - startWall_); },
        [this] { finish(); });
}

void SwapMotion::run(std::function<void()> onDone) {
    onDone_ = std::move(onDone);
    startWall_ = juce::Time::getMillisecondCounterHiRes();
    pumped_ = true;
    pump();
}

void SwapMotion::rideContainer(double containerMs) {
    // A mixed swap keeps its two phases: the leaving controls go before the arriving ones take their cells.
    auto& wave = *waves_.back();
    wave.containerMs_ = (wave.hasLeaving_ && wave.hasArriving_) ? 0.0 : std::max(0.0, containerMs);
}

bool SwapMotion::canRetarget() const {
    if (finished_)
        return false;
    for (const auto& wave : waves_)
        if (wave->hasLeaving_ && wave->hasArriving_ && !wave->growing_)
            return false; // its arrivals are still held hidden behind the shrinking ghosts
    return true;
}

void SwapMotion::add(std::vector<Departure> leaving, std::vector<Arrival> arriving, double containerMs) {
    const double now = nowMs();
    for (auto& wave : waves_)
        wave->release([&](const Arrival& old) {
            auto* widget = old.widget.getComponent();
            if (widget == nullptr || !widget->isVisible())
                return true; // it has left the card
            return std::any_of(arriving.begin(), arriving.end(),
                               [&](const Arrival& fresh) { return fresh.widget == old.widget; });
        });
    waves_.push_back(std::make_unique<Wave>(card_, std::move(leaving), std::move(arriving), now));
    rideContainer(containerMs);
    // The ride changes the wave's phases: lay its first frame out again on them.
    waves_.back()->step(now);
    if (pumped_)
        pump();
}

int SwapMotion::numGhosts() const noexcept {
    int count = 0;
    for (const auto& wave : waves_)
        count += wave->ghosts_.size();
    return count;
}

bool SwapMotion::ridesContainer() const noexcept { return !waves_.empty() && waves_.front()->ridesContainer(); }

double SwapMotion::shrinkMs() const noexcept { return waves_.empty() ? 0.0 : waves_.front()->shrinkMs(); }

double SwapMotion::growMs() const noexcept { return waves_.empty() ? 0.0 : waves_.front()->growMs(); }

void SwapMotion::setElapsed(double elapsedMs) {
    pumped_ = false; // a test is driving the clock: a change added now starts at the elapsed time it set
    frame(elapsedMs);
}

void SwapMotion::frame(double elapsedMs) {
    if (finished_)
        return;
    elapsed_ = elapsedMs;
    for (auto it = waves_.begin(); it != waves_.end();) {
        if ((*it)->step(elapsedMs)) {
            (*it)->land();
            it = waves_.erase(it);
        } else
            ++it;
    }
    if (waves_.empty())
        finish();
}

void SwapMotion::land() {
    for (auto& wave : waves_)
        wave->land();
    waves_.clear();
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
    if (finished_)
        return false;
    return std::any_of(waves_.begin(), waves_.end(), [&](const auto& wave) { return wave->holdsHidden(component); });
}

std::vector<juce::Rectangle<float>> SwapMotion::ghostRects() const {
    std::vector<juce::Rectangle<float>> rects;
    for (const auto& wave : waves_)
        for (auto* ghost : wave->ghosts_)
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

namespace {
bool& animateSwapOffScreenForTest() {
    static bool animate = false;
    return animate;
}
} // namespace

void CardBody::setAnimateSwapOffScreenForTest(bool animate) noexcept { animateSwapOffScreenForTest() = animate; }

bool CardBody::canAnimateSwap() const {
    return (forceAnimateForTest_ || animateSwapOffScreenForTest() || card_.isShowing()) &&
           !ui::prefersReducedMotion() && !ui::animationsOff();
}

// The controls a swap can move: those a condition or a look governs.
bool CardBody::isSwapGoverned(int item) const {
    const auto& planned = plan_.items[(size_t)item];
    if (planned.widget == nullptr || planned.kind == CardBodyItem::Kind::View)
        return false;
    if (planned.swapGroup >= 0 || !planned.alsoIn.empty())
        return true;
    for (const auto& rule : plan_.dimRules)
        if (rule.hides && rule.item == item)
            return true; // a code rule shows and hides it
    // A conditional section that fades in or out when it comes or goes (CardBodyFades.cpp) is not a swap.
    if (planned.section >= 0 && (size_t)planned.section < sectionFades_.size() &&
        sectionFades_[(size_t)planned.section].isAttached() && ui::FadeVisibility::canAnimateIn(&card_))
        return false;
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
// moved arrive grown. Nothing runs when the card changed height at once (a mode switch the card makes whole); when
// it glides to its new height (`containerMs`, the Macros count) the controls ride the glide.
void CardBody::startSwapMotion(SwapSnapshot& snapshot, double containerMs) {
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
    if (swap_ != nullptr && swap_->isRunning() && swap_->canRetarget()) {
        swap_->add(std::move(leaving), std::move(arriving), containerMs);
        return;
    }
    swap_ = std::make_unique<SwapMotion>(card_, std::move(leaving), std::move(arriving));
    if (containerMs > 0.0)
        swap_->rideContainer(containerMs);
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

namespace cardbody_test {

void setAnimateSwapOffScreen(bool animate) { CardBody::setAnimateSwapOffScreenForTest(animate); }

void stepSwap(ModuleComponent& card, double elapsedMs) {
    if (auto* body = card.getCardBody())
        body->stepSwapMotionForTest(elapsedMs);
}

bool isSwapRunning(ModuleComponent& card) {
    auto* body = card.getCardBody();
    return body != nullptr && body->isSwapMotionRunning();
}

std::vector<juce::Rectangle<float>> swapGhostRects(ModuleComponent& card) {
    auto* body = card.getCardBody();
    return body != nullptr ? body->swapGhostRectsForTest() : std::vector<juce::Rectangle<float>>();
}

std::optional<ControlParts> controlParts(ModuleComponent& card, const juce::String& paramId) {
    auto* body = card.getCardBody();
    if (body == nullptr)
        return std::nullopt;
    for (const auto& item : body->getPlan().items)
        if (item.param != nullptr && item.param->paramID == paramId)
            return ControlParts{item.widget, item.label};
    return std::nullopt;
}

} // namespace cardbody_test

} // namespace synth
