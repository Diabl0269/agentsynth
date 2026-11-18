#include "CardHoverLift.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
const juce::Identifier& propertyId() {
    static const juce::Identifier id("cardHoverLift");
    return id;
}
} // namespace

CardHoverLift::CardHoverLift(juce::Component& card)
    : card_(&card) {
    card.addMouseListener(this, true);
    card.addComponentListener(this);
}

CardHoverLift::~CardHoverLift() {
    driver_.stop();
    if (card_ != nullptr) {
        card_->removeMouseListener(this);
        card_->removeComponentListener(this);
    }
}

void CardHoverLift::attach(juce::Component& card) {
    if (find(card) != nullptr)
        return;
    card.getProperties().set(propertyId(), juce::var(new CardHoverLift(card)));
}

CardHoverLift* CardHoverLift::find(const juce::Component& card) {
    return dynamic_cast<CardHoverLift*>(card.getProperties()[propertyId()].getObject());
}

void CardHoverLift::refresh(juce::Component& card) {
    if (auto* l = find(card))
        l->setHovered(l->hovered_);
}

bool CardHoverLift::isRich() const {
    return card_ != nullptr && synth::theme::themeOf(*card_).treatment.depth == synth::theme::DepthLevel::Rich;
}

void CardHoverLift::mouseExit(const juce::MouseEvent&) {
    // The pointer passing from the card onto one of its children is an exit then an enter: only a pointer that has
    // really left the card drops it.
    setHovered(card_ != nullptr && card_->isMouseOver(true));
}

void CardHoverLift::setHovered(bool hovered) {
    hovered_ = hovered;
    moveTo(hovered && isRich() ? kLiftPx : 0.0f);
}

void CardHoverLift::moveTo(float target) {
    if (target == target_ && (target == lift_ || driver_.isRunning()))
        return;
    target_ = target;
    // Reduce Motion, Animations Off and a card off screen land at once: nothing runs.
    if (card_ == nullptr || prefersReducedMotion() || !FadeVisibility::canAnimateIn(card_) ||
        (target < lift_ && !isRich())) {
        driver_.stop();
        apply(target);
        return;
    }
    if (updater_ == nullptr)
        updater_ = std::make_unique<juce::VBlankAnimatorUpdater>(card_);
    const float from = lift_;
    const bool rising = target > from;
    driver_.start(
        *updater_, rising ? kInMs : kOutMs, rising ? easeOutCubic : easeInCubic,
        [this, from, target](float t) { apply(from + (target - from) * t); }, [this] { apply(target_); });
}

void CardHoverLift::apply(float lift) {
    lift_ = lift;
    if (card_ == nullptr)
        return;
    // Another motion owns the card's transform (the load reveal scales it): leave it alone.
    const auto current = card_->getTransform();
    if (!current.isIdentity() && current != applied_)
        return;
    applied_ = lift > 0.0f ? juce::AffineTransform::translation(0.0f, -lift) : juce::AffineTransform();
    card_->setTransform(applied_);
}

void CardHoverLift::landForTest() {
    if (driver_.isRunning())
        driver_.completeForTest();
}

void CardHoverLift::componentBeingDeleted(juce::Component& component) {
    driver_.stop();
    updater_.reset();
    component.removeMouseListener(this);
    component.removeComponentListener(this);
    card_ = nullptr;
}

} // namespace synth::ui
