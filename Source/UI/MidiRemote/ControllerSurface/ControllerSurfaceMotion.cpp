// ControllerSurfaceMotion.cpp (docs/layout/animation.md#fading-things-in-and-out): a control added to the shown
// controller grows in on the surface, and a removed one shrinks away, the way controls do on a card
// (control_motion, Source/UI/Layout/ControlMotion.h). A rebuild of ANOTHER controller, and anything off screen or
// under Animations Off, lands at once. The cells are already final when the motion starts; it only scales and
// fades the arriving ones and paints a picture of each leaving one.

#include "ControllerSurfaceComponent.h"

#include "UI/Layout/FadeVisibility.h"
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

void ControllerSurfaceComponent::stepMotionForTest(float t) {
    if (t >= 1.0f) {
        motionPump_.stop();
        landMotion();
        return;
    }
    applyMotionFrame(t * (float)(motionReduced_ ? cm::kReducedMs : cm::kGrowMs));
}

} // namespace synth::ui
