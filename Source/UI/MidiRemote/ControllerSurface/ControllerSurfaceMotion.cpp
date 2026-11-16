// ControllerSurfaceMotion.cpp (docs/layout/animation.md#fading-things-in-and-out): a control added to the shown
// controller grows in on the surface, and a removed one shrinks away, the way controls do on a card
// (control_motion, Source/UI/Layout/ControlMotion.h). A rebuild of ANOTHER controller, and anything off screen or
// under Animations Off, lands at once. The cells are already final when the motion starts; it only scales and
// fades the arriving ones and paints a picture of each leaving one. A cell that stays but lands on another grid place
// (an undo or redo of a move) glides there from where it is drawn (KeyedRectTween, docs/layout/undo-glide.md): its
// bounds are final at once and a translation on the cell carries the rest; under Reduce Motion it lands and fades in.
// A live drag never glides (its drop rebuilds the cells where they already are), and a drag that starts on a gliding
// cell ends the glide so the cell is under the pointer.

#include "ControllerSurfaceComponent.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"

#include <algorithm>

namespace synth::ui {

namespace cm = control_motion;

std::vector<ControllerSurfaceComponent::Departure>
ControllerSurfaceComponent::pictureDepartures(const std::vector<CellModel>& keep) const {
    std::vector<Departure> departures;
    for (auto* cell : cells_) {
        const auto id = cell->getControlId();
        const bool stays = std::any_of(keep.begin(), keep.end(), [&](const auto& m) { return m.control.id == id; });
        if (stays)
            continue;
        auto image = cell->createComponentSnapshot(cell->getLocalBounds(), true, 2.0f);
        if (!image.isNull())
            departures.push_back({std::move(image), cell->getBounds(), cm::axisFor(cell->getBounds())});
    }
    return departures;
}

void ControllerSurfaceComponent::startCellMotion(std::vector<Departure> departures,
                                                 const std::vector<juce::String>& previousIds, bool animate) {
    arriving_.clear();
    if (!animate) {
        landMotion();
        return;
    }
    motionReduced_ = prefersReducedMotion();
    for (auto& departure : departures) {
        auto* ghost = ghosts_.add(
            new cm::ShrinkGhost(std::move(departure.image), departure.bounds, departure.axis, motionReduced_));
        content_.addAndMakeVisible(ghost);
    }
    for (auto* cell : cells_)
        if (std::find(previousIds.begin(), previousIds.end(), cell->getControlId()) == previousIds.end())
            arriving_.emplace_back(cell);
    if (ghosts_.isEmpty() && arriving_.empty())
        return;

    motionStartMs_ = juce::Time::getMillisecondCounterHiRes();
    applyMotionFrame(0.0f);
    motionPump_.run(
        motionReduced_ ? cm::kReducedMs : cm::kGrowMs,
        [this] { applyMotionFrame((float)(juce::Time::getMillisecondCounterHiRes() - motionStartMs_)); },
        [this] { landMotion(); });
}

// One frame, `elapsedMs` into the motion: arrivals grow over kGrowMs, each ghost shrinks over its own duration.
void ControllerSurfaceComponent::applyMotionFrame(float elapsedMs) {
    const float t = juce::jlimit(0.0f, 1.0f, elapsedMs / (float)(motionReduced_ ? cm::kReducedMs : cm::kGrowMs));
    for (auto& cell : arriving_) {
        if (cell == nullptr)
            continue;
        cell->setAlpha(juce::jlimit(0.0f, 1.0f, easeOutCubic(t)));
        cell->setTransform(motionReduced_ ? juce::AffineTransform()
                                          : cm::scaleAbout(cell->getBounds(), cm::growScale(t), cm::Axis::both));
    }
    for (int i = ghosts_.size(); --i >= 0;) {
        auto* ghost = ghosts_[i];
        ghost->setProgress(elapsedMs / (float)ghost->durationMs());
        if (ghost->progress() >= 1.0f)
            ghosts_.remove(i);
    }
}

void ControllerSurfaceComponent::landMotion() {
    ghosts_.clear();
    for (auto& cell : arriving_)
        if (cell != nullptr) {
            cell->setAlpha(1.0f);
            cell->setTransform({});
        }
    arriving_.clear();
}

bool ControllerSurfaceComponent::isMoveGlidingForTest() const noexcept { return moveTween_->isAnimating(); }
void ControllerSurfaceComponent::stepMoveGlideForTest(double ms) { moveTween_->stepForTest(ms); }
void ControllerSurfaceComponent::forceMoveGlideAnimateForTest(bool force) noexcept {
    moveTween_->forceAnimateForTest(force);
}

namespace {
std::int64_t glideKey(const juce::String& id) { return id.hashCode64(); }
} // namespace

ControllerSurfaceComponent::DrawnRects ControllerSurfaceComponent::drawnCellRects() const {
    DrawnRects drawn;
    for (auto* cell : cells_)
        drawn[cell->getControlId()] = moveTween_->drawnAt(glideKey(cell->getControlId()), cell->getBounds().toFloat());
    return drawn;
}

// The rebuilt cells sit at their final bounds; each one that was drawn elsewhere restarts its tween from that drawn
// place, so a second step mid-glide continues from where the cell is. Cells that left, and cells already in place,
// are forgotten by the tween.
void ControllerSurfaceComponent::startMoveGlide(const DrawnRects& before, bool animate) {
    if (!animate) {
        moveTween_->clear();
        applyMoveGlide();
        return;
    }
    for (const auto& [id, rect] : before)
        if (findCellForTest(id) == nullptr)
            moveTween_->erase(glideKey(id));
    for (auto* cell : cells_) {
        const auto key = glideKey(cell->getControlId());
        const auto it = before.find(cell->getControlId());
        const auto now = cell->getBounds().toFloat();
        if (it == before.end() || it->second == now)
            moveTween_->erase(key);
        else
            moveTween_->retarget(key, it->second, now, true);
    }
    applyMoveGlide();
}

// Puts the tween's offsets and fades on the cells it has touched (the cells themselves rest at their final bounds).
void ControllerSurfaceComponent::applyMoveGlide() {
    for (auto* cell : cells_) {
        const auto key = glideKey(cell->getControlId());
        const auto offset = moveTween_->offsetFor(key);
        const float alpha = moveTween_->alphaFor(key);
        const bool moving = offset.getX() != 0.0f || offset.getY() != 0.0f || alpha < 1.0f;
        if (moving)
            movingKeys_.insert(key);
        else if (movingKeys_.erase(key) == 0)
            continue; // never touched by the glide: arrivals keep their own transform and opacity
        cell->setTransform(juce::AffineTransform::translation(offset.getX(), offset.getY()));
        cell->setAlpha(alpha);
    }
}

juce::Rectangle<float> ControllerSurfaceComponent::drawnCellBoundsForTest(const juce::String& controlId) const {
    const auto* cell = findCellForTest(controlId);
    return cell == nullptr ? juce::Rectangle<float>()
                           : moveTween_->drawnAt(glideKey(controlId), cell->getBounds().toFloat());
}

float ControllerSurfaceComponent::drawnCellAlphaForTest(const juce::String& controlId) const {
    return moveTween_->alphaFor(glideKey(controlId));
}

void ControllerSurfaceComponent::stepMotionForTest(float t) {
    if (t >= 1.0f) {
        motionPump_.stop();
        landMotion();
        return;
    }
    applyMotionFrame(t * (float)(motionReduced_ ? cm::kReducedMs : cm::kGrowMs));
}

} // namespace synth::ui
