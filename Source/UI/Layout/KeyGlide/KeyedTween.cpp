#include "KeyedTween.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

namespace {
KeyedTweenDirtyProbe& dirtyProbe() {
    static KeyedTweenDirtyProbe probe;
    return probe;
}
} // namespace

void setKeyedTweenDirtyProbeForTest(KeyedTweenDirtyProbe probe) { dirtyProbe() = std::move(probe); }

template <typename T>
KeyedTween<T>::KeyedTween(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner)
    , fadeVblank_(&owner) {}

template <typename T>
KeyedTween<T>::~KeyedTween() {
    driver_.stop(vblank_);
    fadeDriver_.stop(fadeVblank_);
}

template <typename T>
bool KeyedTween<T>::animates() const {
    return (forceAnimateForTest_ || owner_.isShowing()) && !prefersReducedMotion();
}

template <typename T>
bool KeyedTween<T>::fadesInstead() const {
    return (forceAnimateForTest_ || owner_.isShowing()) && prefersReducedMotion() && !animationsOff();
}

template <typename T>
float KeyedTween<T>::alphaFor(Key key) const {
    constexpr float kFloor = 0.2f;
    return fading_.count(key) > 0 ? kFloor + (1.0f - kFloor) * fadeEased_ : 1.0f;
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
    // Not Rectangle::getUnion: it drops empty rectangles, and a point's bounds are empty.
    auto box = TweenMath<T>::bounds(values.front());
    for (const auto& v : values) {
        const auto b = TweenMath<T>::bounds(v);
        box = juce::Rectangle<float>::leftTopRightBottom(std::min(box.getX(), b.getX()), std::min(box.getY(), b.getY()),
                                                         std::max(box.getRight(), b.getRight()),
                                                         std::max(box.getBottom(), b.getBottom()));
    }
    requestRepaint(tightRepaint_ ? box.expanded((float)paddingPx_).getSmallestIntegerContainer()
                                 : owner_.getLocalBounds());
}

template <typename T>
void KeyedTween<T>::requestRepaint(juce::Rectangle<int> area) {
    if (const auto& probe = dirtyProbe())
        probe(owner_, area);
    if (onDirty)
        onDirty(area);
    else if (area == owner_.getLocalBounds())
        owner_.repaint();
    else
        owner_.repaint(area);
}

template <typename T>
void KeyedTween<T>::retarget(Key key, T from, T to, bool fadeWhenReduced) {
    if (fadeWhenReduced && from != to && fadesInstead()) {
        startFade(key, to);
        return;
    }
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
    const bool faded = fading_.erase(key) > 0;
    if (entries_.erase(key) == 0 && !faded)
        return;
    if (entries_.empty())
        driver_.stop(vblank_);
    if (fading_.empty())
        fadeDriver_.stop(fadeVblank_);
    requestRepaint(owner_.getLocalBounds());
}

template <typename T>
void KeyedTween<T>::clear() {
    if (entries_.empty() && fading_.empty())
        return;
    auto before = drawnValues();
    for (const auto& [key, resting] : fading_)
        before.push_back(resting);
    entries_.clear();
    fading_.clear();
    eased_ = 0.0f;
    fadeEased_ = 0.0f;
    driver_.stop(vblank_);
    fadeDriver_.stop(fadeVblank_);
    repaintAround(before);
}

// Reduce Motion: the thing is already at its new place; it fades in there instead of gliding.
template <typename T>
void KeyedTween<T>::startFade(Key key, T to) {
    entries_.erase(key);
    if (entries_.empty())
        driver_.stop(vblank_);
    fading_[key] = to;
    fadeEased_ = 0.0f;
    std::vector<T> places;
    for (const auto& [id, resting] : fading_)
        places.push_back(resting);
    repaintAround(places);
    fadeDriver_.start(
        fadeVblank_, control_motion::kReducedMs, easeOutCubic,
        [this, places](float eased) {
            fadeEased_ = eased;
            repaintAround(places);
        },
        [this] {
            auto places = std::vector<T>();
            for (const auto& [id, resting] : fading_)
                places.push_back(resting);
            fading_.clear();
            fadeEased_ = 0.0f;
            repaintAround(places);
        });
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
