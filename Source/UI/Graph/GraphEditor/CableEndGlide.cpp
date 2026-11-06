// CableEndGlide.cpp -- see CableEndGlide.h.
#include "CableEndGlide.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"

CableEndGlide::CableEndGlide(juce::VBlankAnimatorUpdater& updater,
                             std::function<const std::vector<VisibleCable>&()> cablesNow, std::function<void()> onFrame)
    : updater_(updater)
    , cablesNow_(std::move(cablesNow))
    , onFrame_(std::move(onFrame)) {}

CableEndGlide::~CableEndGlide() { driver_.stop(); }

// The recorded points come from the cables as drawn (overlays included), so a toggle that reverses a glide in flight
// starts from where the end is on screen. Every entry restarts at progress 0 together, whichever card it belongs to.
// The glide lasts as long as the row's own fade (160 ms in, 110 ms out, FadeVisibility), eased like the other cable
// motion; under Reduce Motion it is the plain short linear move, and under Animations Off or off screen the end
// lands at once.
void CableEndGlide::begin(ModuleComponent& card, bool unfolding) {
    if (!synth::ui::FadeVisibility::canAnimateIn(&card)) {
        finish();
        return;
    }
    const auto& drawn = cablesNow_();
    std::vector<Entry> next;
    const auto record = [&](ModuleComponent& owner, const VisibleCable& cable,
                            std::optional<juce::Point<float>> folded) {
        next.push_back({&owner, cable.id, cable.p2 - owner.getPosition().toFloat(), folded});
    };
    for (const auto& old : entries_)
        if (old.card != nullptr && old.card.getComponent() != &card)
            for (const auto& cable : drawn)
                if (cable.id == old.id) {
                    record(*old.card, cable, old.foldedRelative);
                    break;
                }
    for (const auto& cable : drawn)
        if (cable.destNodeId == card.getNodeId().uid)
            record(card, cable,
                   !unfolding && cable.landsOnKnob ? card.foldedJackCentre(cable.destChannel) : std::nullopt);
    if (next.empty()) {
        finish();
        return;
    }
    entries_ = std::move(next);
    progress_ = 0.0f;

    const bool reduced = synth::ui::prefersReducedMotion();
    const double ms =
        synth::ui::motionMs(unfolding ? synth::ui::FadeVisibility::kFadeInMs : synth::ui::FadeVisibility::kFadeOutMs,
                            synth::ui::FadeVisibility::kReducedMs);
    std::function<float(float)> easing = reduced     ? std::function<float(float)>([](float t) { return t; })
                                         : unfolding ? std::function<float(float)>(synth::ui::easeOutCubic)
                                                     : std::function<float(float)>(synth::ui::easeInCubic);
    driver_.start(
        updater_, ms, std::move(easing),
        [this](float e) {
            progress_ = e;
            if (onFrame_)
                onFrame_();
        },
        [this] {
            finish();
            if (onFrame_)
                onFrame_();
        });
    if (onFrame_)
        onFrame_(); // the memo was built before the glide existed
}

// The live anchor is the settled one (the row's state flips at once, see ModuleComponent::shownRingSliderIndex), so
// the end runs from the old place to wherever the cable lands now; a moving card carries both ends. A folding knob that
// also has a gutter jack still reads as the knob until the fade ends, so its end runs to the jack's recorded place.
void CableEndGlide::applyTo(std::vector<VisibleCable>& cables) const {
    if (entries_.empty())
        return;
    for (const auto& entry : entries_) {
        if (entry.card == nullptr)
            continue;
        for (auto& cable : cables) {
            if (cable.id != entry.id || cable.destNodeId != entry.card->getNodeId().uid)
                continue;
            const auto from = entry.card->getPosition().toFloat() + entry.oldRelative;
            const auto to =
                entry.foldedRelative ? entry.card->getPosition().toFloat() + *entry.foldedRelative : cable.p2;
            cable.p2 = from + (to - from) * progress_;
            break;
        }
    }
}

void CableEndGlide::advanceForTest(float e) {
    progress_ = e;
    if (onFrame_)
        onFrame_();
}

void CableEndGlide::finish() {
    driver_.stop();
    entries_.clear();
    progress_ = 1.0f;
}
