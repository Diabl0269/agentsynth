// CardBodyMoreRow.cpp -- the folded More row at the bottom of a card: every parameter the layout hides or
// does not place. It exists only when something is hidden. Unfolding or folding changes the card's
// height, so the card re-measures and the canvas makes room (or gives it back); a modulation cable
// dragged over the folded row unfolds it, so a hidden parameter can still take a new cable.
// docs/layout/module-card.md#the-more-row.
#include "CardBody.h"
#include "CardBodyMoreButton.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FocusRing.h"

namespace synth {

CardMoreButton::CardMoreButton(int hiddenCount)
    : juce::TextButton("More controls (" + juce::String(hiddenCount) + ")") {
    setComponentID("cardMoreRow");
    setTitle(getButtonText());
    setWantsKeyboardFocus(true);
    setUnfolded(false);
}

void CardMoreButton::setUnfolded(bool unfolded) {
    setTooltip(unfolded ? "Hide the controls this card's layout keeps out of view (Return or Space)"
                        : "Show the controls this card's layout keeps out of view (Return or Space)");
}

// Return and Space toggle at once rather than through juce::Button's posted click, so a keyboard
// toggle and its re-layout land in the same message-loop turn as the key.
bool CardMoreButton::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
        if (onToggle)
            onToggle();
        return true;
    }
    return juce::TextButton::keyPressed(key);
}

void CardMoreButton::clicked() {
    if (onToggle)
        onToggle();
}

void CardMoreButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    juce::TextButton::paintButton(g, highlighted, down);
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f);
}

void CardMoreButton::focusGained(FocusChangeType cause) {
    juce::TextButton::focusGained(cause);
    repaint();
}

void CardMoreButton::focusLost(FocusChangeType cause) {
    juce::TextButton::focusLost(cause);
    repaint();
}

// Built after every parameter widget, so it is the last child and the last Tab stop on the card body.
void CardBody::createMoreButton() {
    moreButton_ = std::make_unique<CardMoreButton>((int)plan_.more.size());
    moreButton_->onToggle = [this] { setMoreUnfolded(!moreUnfolded_); };
    card_.addAndMakeVisible(*moreButton_);
}

juce::Button* CardBody::getMoreButton() const { return moreButton_.get(); }

// A folded parameter's widget is hidden, not destroyed: it keeps its attachment, MIDI Learn entry and
// value, and its knob-bound CV jack shows in the gutter again while the knob is out of view.
void CardBody::applyMoreVisibility() {
    for (int index : plan_.more) {
        auto& item = plan_.items[(size_t)index];
        if (item.widget != nullptr)
            item.widget->setVisible(moreUnfolded_);
        if (item.label != nullptr)
            item.label->setVisible(moreUnfolded_);
    }
    if (moreButton_ != nullptr)
        moreButton_->setUnfolded(moreUnfolded_);
}

// A discrete event, never per value change: the card re-measures and the canvas pushes neighbours
// clear (or returns them) the same way any other card growth does.
void CardBody::setMoreUnfolded(bool unfolded) {
    if (!hasMoreRow() || unfolded == moreUnfolded_)
        return;
    moreUnfolded_ = unfolded;
    applyMoreVisibility();
    card_.updateLayout();
    card_.owner.handleModuleResized(&card_);
    card_.repaint();
}

bool CardBody::unfoldForCableDragAt(juce::Point<int> cardLocal) {
    if (moreButton_ == nullptr || moreUnfolded_ || !moreButton_->isVisible() ||
        !moreButton_->getBounds().contains(cardLocal))
        return false;
    setMoreUnfolded(true);
    return true;
}

int CardBody::layoutMoreRow(int y, const cardbody::BodyGeometry& g, bool apply) const {
    if (!hasMoreRow())
        return y;
    if (apply && moreButton_ != nullptr)
        moreButton_->setBounds(g.contentX, y, g.contentW, cardbody::kRowHeight);
    y += cardbody::kRowHeight + 2;
    if (moreUnfolded_)
        y = layoutItems(plan_.more, CardSection::kDefaultColumns, y, g, apply, false);
    return y;
}

} // namespace synth
