#include "KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

template <typename T>
KeyedTween<T>::KeyedTween(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

template <typename T>
KeyedTween<T>::~KeyedTween() {
    driver_.stop(vblank_);
}

template <typename T>
bool KeyedTween<T>::animates() const {
    return (forceAnimateForTest_ || owner_.isShowing()) && !prefersReducedMotion();
}

template <typename T>
T KeyedTween<T>::offsetFor(Key key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? T() : TweenMath<T>::scale(it->second.offset, 1.0f - eased_);
}

// Every running offset is folded into its base so the one shared clock can restart at 0 without anything jumping.
template <typename T>
void KeyedTween<T>::bakeOffsets() {
    for (auto& [key, entry] : entries_)
        entry.offset = TweenMath<T>::scale(entry.offset, 1.0f - eased_);
    eased_ = 0.0f;
}

template <typename T>
std::vector<T> KeyedTween<T>::drawnValues() const {
    std::vector<T> values;
    values.reserve(entries_.size());
    for (const auto& [key, entry] : entries_)
        values.push_back(TweenMath<T>::add(entry.resting, TweenMath<T>::scale(entry.offset, 1.0f - eased_)));
    return values;
}

template <typename T>
void KeyedTween<T>::repaintAround(const std::vector<T>& values) {
    if (values.empty())
        return;
    auto box = TweenMath<T>::bounds(values.front());
    for (const auto& v : values)
        box = box.getUnion(TweenMath<T>::bounds(v));
    const auto dirty = box.expanded((float)paddingPx_).getSmallestIntegerContainer();
    if (onDirty)
        onDirty(dirty);
    else
        owner_.repaint(dirty);
}

template <typename T>
void KeyedTween<T>::retarget(Key key, T from, T to) {
    if (!animates() || from == to) {
        erase(key);
        return;
    }
    auto before = drawnValues();
    bakeOffsets();
    entries_[key] = {TweenMath<T>::sub(from, to), to};
    before.push_back(from);
    before.push_back(to);
    repaintAround(before);
    startRun();
}

template <typename T>
void KeyedTween<T>::erase(Key key) {
    if (entries_.erase(key) == 0)
        return;
    if (entries_.empty())
        driver_.stop(vblank_);
    owner_.repaint();
}

template <typename T>
void KeyedTween<T>::clear() {
    if (entries_.empty())
        return;
    const auto before = drawnValues();
    entries_.clear();
    eased_ = 0.0f;
    driver_.stop(vblank_);
    repaintAround(before);
}

template <typename T>
void KeyedTween<T>::startRun() {
    driver_.start(vblank_, kMs, easeOutCubic, [this](float eased) { onFrame(eased); }, [this] { clear(); });
}

template <typename T>
void KeyedTween<T>::onFrame(float eased) {
    const auto before = drawnValues();
    eased_ = eased;
    auto both = drawnValues();
    both.insert(both.end(), before.begin(), before.end());
    repaintAround(both);
}

template class KeyedTween<juce::Point<float>>;
template class KeyedTween<juce::Rectangle<float>>;

} // namespace synth::ui
