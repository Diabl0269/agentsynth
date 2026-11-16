// RestoredColourGlide.cpp -- see RestoredColourGlide.h. The tween, restore watch and undo pointer live here so the
// header stays out of the wide include graph.
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"
#include "AppUndoManager.h"
#include "UI/Layout/KeyGlide/KeyedColourTween.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"

namespace synth::ui {

namespace {
bool& animateOffScreenForTest() noexcept {
    static bool on = false;
    return on;
}
} // namespace

struct RestoredColourGlide::Impl {
    explicit Impl(juce::Component& owner)
        : tween(owner) {}
    KeyedColourTween tween;
    RestoreWatch watch;
    AppUndoManager* undo = nullptr;
};

RestoredColourGlide::RestoredColourGlide(juce::Component& owner)
    : impl_(std::make_unique<Impl>(owner)) {}
RestoredColourGlide::~RestoredColourGlide() = default;
RestoredColourGlide::RestoredColourGlide(RestoredColourGlide&&) noexcept = default;
RestoredColourGlide& RestoredColourGlide::operator=(RestoredColourGlide&&) noexcept = default;

void RestoredColourGlide::setUndoManager(AppUndoManager* undo) {
    impl_->undo = undo;
    impl_->watch = RestoreWatch(undo != nullptr ? undo->getRestoreSerial() : 0);
}

bool RestoredColourGlide::takeRestore() {
    return impl_->undo != nullptr && impl_->watch.take(impl_->undo->isRestoring(), impl_->undo->getRestoreSerial());
}

void RestoredColourGlide::note(Key key, juce::Colour was, juce::Colour now, bool restored) {
    if (was == now)
        return;
    auto& tween = impl_->tween;
    tween.forceAnimateForTest(animateOffScreenForTest());
    if (restored)
        tween.retarget(key, tween.drawnAt(key, TweenColour::from(was)), TweenColour::from(now));
    else
        tween.erase(key);
}

void RestoredColourGlide::glideFrom(Key key, juce::Colour from, juce::Colour now) {
    auto& tween = impl_->tween;
    tween.forceAnimateForTest(animateOffScreenForTest());
    if (from != now)
        tween.retarget(key, TweenColour::from(from), TweenColour::from(now));
}

void RestoredColourGlide::forget(Key key) { impl_->tween.erase(key); }

juce::Colour RestoredColourGlide::drawn(Key key, juce::Colour resting) const {
    return impl_->tween.drawnAt(key, TweenColour::from(resting)).toColour();
}

bool RestoredColourGlide::isGliding() const noexcept { return impl_->tween.isAnimating(); }
void RestoredColourGlide::stepForTest(double ms) { impl_->tween.stepForTest(ms); }
void RestoredColourGlide::setAnimateOffScreenForTest(bool on) noexcept { animateOffScreenForTest() = on; }
void RestoredColourGlide::setOnDirty(std::function<void(juce::Rectangle<int>)> onDirty) {
    impl_->tween.onDirty = std::move(onDirty);
}

} // namespace synth::ui
