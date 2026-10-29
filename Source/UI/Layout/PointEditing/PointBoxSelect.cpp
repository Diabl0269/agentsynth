#include "PointBoxSelect.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// A plain press on empty space cannot be told from the start of a box, so it only arms; the first drag turns it
// into a box that replaces the selection, the release without one deselects. Shift, Cmd and Ctrl start the box at
// once and keep what was selected.
void PointBoxGesture::press(juce::Point<int> pos, bool additive) {
    anchor_ = pos;
    if (additive)
        begin();
    else
        pending_ = true;
}

void PointBoxGesture::begin() {
    pending_ = false;
    active_ = true;
    box_ = {};
}

bool PointBoxGesture::advance(juce::Point<int> pos) {
    if (pending_)
        begin();
    if (!active_)
        return false;
    const auto previous = box_;
    box_ = juce::Rectangle<int>::leftTopRightBottom(std::min(anchor_.x, pos.x), std::min(anchor_.y, pos.y),
                                                    std::max(anchor_.x, pos.x), std::max(anchor_.y, pos.y));
    owner_.repaint(previous.getUnion(box_).expanded(2));
    return true;
}

PointBoxGesture::Release PointBoxGesture::release() {
    if (active_) {
        cancel();
        return Release::box;
    }
    if (pending_) {
        pending_ = false;
        return Release::click;
    }
    return Release::none;
}

void PointBoxGesture::cancel() {
    pending_ = false;
    if (active_) {
        active_ = false;
        owner_.repaint(box_.expanded(2));
        box_ = {};
    }
}

// The accent comes from the theme of the owner; a bare owner (no AppLookAndFeel) falls back to yellow.
void PointBoxGesture::paintMarquee(juce::Graphics& g) const {
    if (!active_ || box_.isEmpty())
        return;
    juce::Colour accent = juce::Colours::yellow;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&owner_.getLookAndFeel()))
        accent = lf->getTheme().colors.accent;
    g.setColour(accent.withAlpha(0.14f));
    g.fillRect(box_);
    g.setColour(accent.withAlpha(0.8f));
    g.drawRect(box_, 1);
}

} // namespace synth::ui
