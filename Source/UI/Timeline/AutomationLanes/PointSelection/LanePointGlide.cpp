#include "LanePointGlide.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>

namespace synth::ui {

namespace {
bool samePoint(const LaneBreakpoint& a, const LaneBreakpoint& b) {
    return a.beat == b.beat && a.value == b.value && a.tension == b.tension && a.curve == b.curve;
}

// Points of `a` that `b` does not hold unchanged.
std::vector<LaneBreakpoint> missingFrom(const std::vector<LaneBreakpoint>& a, const std::vector<LaneBreakpoint>& b) {
    std::vector<LaneBreakpoint> out;
    for (const auto& p : a)
        if (std::none_of(b.begin(), b.end(), [&](const auto& q) { return samePoint(p, q); }))
            out.push_back(p);
    return out;
}
} // namespace

LanePointGlide::LanePointGlide(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

LanePointGlide::~LanePointGlide() { driver_.stop(vblank_); }

bool LanePointGlide::animates() const {
    return (forceAnimateForTest_ || owner_.isShowing()) && !prefersReducedMotion();
}

bool LanePointGlide::animatesMelt() const { return (forceAnimateForTest_ || owner_.isShowing()) && !animationsOff(); }

void LanePointGlide::meltNext(double startBeat, double endBeat, std::function<juce::Rectangle<int>()> meltArea) {
    meltArmed_ = true;
    meltStart_ = startBeat;
    meltEnd_ = endBeat;
    meltArea_ = std::move(meltArea);
}

// A melt repaints its own range, not the whole lane; everything else repaints the owner.
void LanePointGlide::repaintOwner() {
    if (melting_ && meltArea_)
        owner_.repaint(meltArea_());
    else
        owner_.repaint();
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
    amount_ = 1.0f;
    before_.clear();
    leaving_.clear();
    entering_.clear();
    if (wasMelting && meltArea_)
        owner_.repaint(meltArea_());
    else
        owner_.repaint();
}

// A removal and an addition cannot both be told apart from a move (a moved point is one of each), so only a pure
// one animates: every point of one side is held unchanged by the other.
void LanePointGlide::pointsChanged(std::vector<LaneBreakpoint> now) {
    auto gone = missingFrom(shown_, now);
    auto arrived = missingFrom(now, shown_);
    const bool pure = gone.empty() != arrived.empty();
    if (gone.empty() && arrived.empty())
        return;
    const bool melt = meltArmed_;
    meltArmed_ = false;
    if (melt ? !animatesMelt() : (!pure || !animates())) {
        reset(std::move(now));
        return;
    }

    const bool leaves = !gone.empty();
    before_ = shown_;
    leaving_ = std::move(gone);
    entering_ = std::move(arrived);
    shown_ = std::move(now);
    running_ = true;
    melting_ = melt;
    amount_ = 0.0f;
    driver_.start(
        vblank_, melt ? motionMs(kMeltMs, kMeltReducedMs) : (leaves ? kOutMs : kInMs),
        melt || !leaves ? easeOutCubic : easeInCubic,
        [this](float e) {
            amount_ = e;
            repaintOwner();
        },
        [this] { finish(); });
}

float LanePointGlide::presence(double beat) const {
    if (!running_)
        return 1.0f;
    for (const auto& p : leaving_)
        if (p.beat == beat)
            return 1.0f - amount_;
    for (const auto& p : entering_)
        if (p.beat == beat)
            return amount_;
    return 1.0f;
}

} // namespace synth::ui
