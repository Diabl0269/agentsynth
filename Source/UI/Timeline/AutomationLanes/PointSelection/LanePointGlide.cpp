#include "LanePointGlide.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>

namespace synth::ui {

namespace {
bool samePoint(const LaneBreakpoint& a, const LaneBreakpoint& b) {
    return a.beat == b.beat && a.value == b.value && a.tension == b.tension && a.curve == b.curve;
}

bool holds(const std::vector<LaneBreakpoint>& points, const LaneBreakpoint& p) {
    return std::any_of(points.begin(), points.end(), [&](const auto& q) { return samePoint(p, q); });
}

// Points of `a` that `b` does not hold unchanged.
std::vector<LaneBreakpoint> missingFrom(const std::vector<LaneBreakpoint>& a, const std::vector<LaneBreakpoint>& b) {
    std::vector<LaneBreakpoint> out;
    for (const auto& p : a)
        if (!holds(b, p))
            out.push_back(p);
    return out;
}
} // namespace

LanePointGlide::LanePointGlide(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

LanePointGlide::~LanePointGlide() { driver_.stop(vblank_); }

// Reduce Motion still animates: as a plain fade (fadesOnly), never a jump.
bool LanePointGlide::animates() const { return (forceAnimateForTest_ || owner_.isShowing()) && !animationsOff(); }

void LanePointGlide::meltNext(double startBeat, double endBeat, std::function<juce::Rectangle<int>()> meltArea) {
    meltArmed_ = true;
    meltStart_ = startBeat;
    meltEnd_ = endBeat;
    meltArea_ = std::move(meltArea);
}

// The clock runs linear; each user eases it its own way. Points leaving accelerate away and points arriving settle in;
// a melt settles in.
float LanePointGlide::amount() const noexcept {
    return leaves_ && !melting_ ? easeInCubic(progress_) : easeOutCubic(progress_);
}

// The line settles into its new shape whichever way the points go, so it eases out on the points' clock.
float LanePointGlide::lineWeight() const noexcept {
    return running_ && !melting_ && !fadeOnly_ ? 1.0f - easeOutCubic(progress_) : 0.0f;
}

// A melt repaints its own range, not the whole lane; everything else repaints the owner (the line moves across it).
void LanePointGlide::repaintOwner() {
    if (melting_ && meltArea_)
        owner_.repaint(meltArea_());
    else
        repaintMotionArea(owner_, owner_.getLocalBounds());
}

void LanePointGlide::reset(std::vector<LaneBreakpoint> now) {
    driver_.stop(vblank_);
    shown_ = std::move(now);
    finish();
}

void LanePointGlide::finish() {
    const bool wasMelting = melting_;
    running_ = false;
    melting_ = false;
    fadeOnly_ = false;
    progress_ = 1.0f;
    before_.clear();
    leaving_.clear();
    entering_.clear();
    leavingFrom_.clear();
    enteringFrom_.clear();
    if (wasMelting && meltArea_)
        owner_.repaint(meltArea_());
    else
        repaintMotionArea(owner_, owner_.getLocalBounds());
}

// A removal and an addition cannot both be told apart from a move (a moved point is one of each), so only a pure
// one animates: every point of one side is held unchanged by the other.
bool LanePointGlide::pointsChanged(std::vector<LaneBreakpoint> now) {
    auto gone = missingFrom(shown_, now);
    auto arrived = missingFrom(now, shown_);
    const bool pure = gone.empty() != arrived.empty();
    if (gone.empty() && arrived.empty())
        return false;
    // An edit that changes its points one after another (a curve pick over a stretch) feeds each step before the
    // melt's first frame: the later steps join that melt instead of cutting it off.
    if (isMelting() && progress_ == 0.0f && !meltArmed_) {
        shown_ = std::move(now);
        return false;
    }
    const bool melt = meltArmed_;
    meltArmed_ = false;
    if (!animates() || (!melt && !pure)) {
        reset(std::move(now));
        return false;
    }

    before_ = shown_;
    if (melt) {
        leaving_.clear();
        entering_.clear();
        leavingFrom_.clear();
        enteringFrom_.clear();
        leaves_ = false;
        fadeOnly_ = false;
    } else {
        startFades(std::move(gone), std::move(arrived), now);
    }
    shown_ = std::move(now);
    running_ = true;
    melting_ = melt;
    progress_ = 0.0f;
    driver_.start(
        vblank_, melt ? motionMs(kMeltMs, kMeltReducedMs) : motionMs(leaves_ ? kOutMs : kInMs, kMeltReducedMs),
        [](float t) { return t; },
        [this](float t) {
            progress_ = t;
            repaintOwner();
        },
        [this] { finish(); });
    return !melt;
}

// Every point starts its fade from the presence it is drawn at now, so a second change while one runs (an undo
// straight after a delete) turns a fading dot around instead of blinking it; a point still fading out that the new
// change does not bring back keeps fading, and one still growing in that stays keeps growing.
void LanePointGlide::startFades(std::vector<LaneBreakpoint> gone, std::vector<LaneBreakpoint> arrived,
                                const std::vector<LaneBreakpoint>& now) {
    std::vector<float> goneFrom, arrivedFrom;
    for (const auto& p : gone)
        goneFrom.push_back(presence(p.beat));
    for (const auto& p : arrived)
        arrivedFrom.push_back(running_ && holds(leaving_, p) ? presence(p.beat) : 0.0f);
    for (const auto& p : leaving_)
        if (!holds(now, p) && !holds(gone, p) && presence(p.beat) > 0.0f) {
            goneFrom.push_back(presence(p.beat));
            gone.push_back(p);
        }
    for (const auto& p : entering_)
        if (holds(now, p) && !holds(arrived, p) && presence(p.beat) < 1.0f) {
            arrivedFrom.push_back(presence(p.beat));
            arrived.push_back(p);
        }
    leaves_ = !gone.empty();
    fadeOnly_ = prefersReducedMotion();
    leaving_ = std::move(gone);
    entering_ = std::move(arrived);
    leavingFrom_ = std::move(goneFrom);
    enteringFrom_ = std::move(arrivedFrom);
}

float LanePointGlide::presence(double beat) const {
    if (!running_)
        return 1.0f;
    const float a = amount();
    for (std::size_t i = 0; i < leaving_.size(); ++i)
        if (leaving_[i].beat == beat)
            return leavingFrom_[i] * (1.0f - a);
    for (std::size_t i = 0; i < entering_.size(); ++i)
        if (entering_[i].beat == beat)
            return enteringFrom_[i] + (1.0f - enteringFrom_[i]) * a;
    return 1.0f;
}

} // namespace synth::ui
