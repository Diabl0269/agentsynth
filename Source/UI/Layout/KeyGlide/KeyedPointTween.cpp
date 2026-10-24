#include "KeyedPointTween.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

KeyedPointTween::KeyedPointTween(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

KeyedPointTween::~KeyedPointTween() { driver_.stop(vblank_); }

bool KeyedPointTween::animates() const {
    return (forceAnimateForTest_ || owner_.isShowing()) && !prefersReducedMotion();
}

juce::Point<float> KeyedPointTween::offsetFor(Key key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? juce::Point<float>() : it->second.offset * (1.0f - eased_);
}

// Every running offset is folded into its base so the one shared clock can restart at 0 without any point jumping.
void KeyedPointTween::bakeOffsets() {
    for (auto& [key, entry] : entries_)
        entry.offset = entry.offset * (1.0f - eased_);
    eased_ = 0.0f;
}

std::vector<juce::Point<float>> KeyedPointTween::drawnPoints() const {
    std::vector<juce::Point<float>> points;
    points.reserve(entries_.size());
    for (const auto& [key, entry] : entries_)
        points.push_back(entry.resting + entry.offset * (1.0f - eased_));
    return points;
}

void KeyedPointTween::repaintAround(const std::vector<juce::Point<float>>& points) {
    if (points.empty())
        return;
    auto box = juce::Rectangle<float>(points.front(), points.front());
    for (const auto& p : points)
        box = box.getUnion(juce::Rectangle<float>(p, p));
    const auto dirty = box.expanded((float)paddingPx_).getSmallestIntegerContainer();
    if (onDirty)
        onDirty(dirty);
    else
        owner_.repaint(dirty);
}

void KeyedPointTween::retarget(Key key, juce::Point<float> from, juce::Point<float> to) {
    if (!animates() || from == to) {
        erase(key);
        return;
    }
    auto before = drawnPoints();
    bakeOffsets();
    entries_[key] = {from - to, to};
    before.push_back(from);
    before.push_back(to);
    repaintAround(before);
    startRun();
}

void KeyedPointTween::erase(Key key) {
    if (entries_.erase(key) == 0)
        return;
    if (entries_.empty())
        driver_.stop(vblank_);
    owner_.repaint();
}

void KeyedPointTween::clear() {
    if (entries_.empty())
        return;
    const auto before = drawnPoints();
    entries_.clear();
    eased_ = 0.0f;
    driver_.stop(vblank_);
    repaintAround(before);
}

void KeyedPointTween::startRun() {
    driver_.start(vblank_, kMs, easeOutCubic, [this](float eased) { onFrame(eased); }, [this] { clear(); });
}

void KeyedPointTween::onFrame(float eased) {
    const auto before = drawnPoints();
    eased_ = eased;
    auto both = drawnPoints();
    both.insert(both.end(), before.begin(), before.end());
    repaintAround(both);
}

} // namespace synth::ui
