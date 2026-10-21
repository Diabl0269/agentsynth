// StepGridKeys.cpp -- the keyboard path of the step grid. The grid is one Tab stop; inside it:
//   Left / Right      pick the previous / next step (the viewed page follows the focus)
//   Up / Down         change the focused lane's value (Pitch: one scale note, Shift: one octave; Slide: toggles;
//                     Condition: the next / previous condition)
//   Space             turn the focused step on or off (on the Slide lane: toggle the link)
//   Enter             on the Condition lane open its menu, elsewhere the same as Space
//   Page Up / Down    the lane above / below
//   Home / End        the first / last step
// Every key press that changes the pattern is one undo step.
#include "StepGridComponent.h"
#include <cmath>

namespace synth::ui {

using namespace stepgrid;

void StepGridComponent::setFocusStep(int step) {
    focusStep_ = juce::jlimit(0, std::max(0, stepCount() - 1), step);
    if (focusStep_ / kColumns != page_)
        setViewedPage(focusStep_ / kColumns);
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void StepGridComponent::moveFocusLane(int direction) {
    int index = (int)focusLane_;
    for (int i = 0; i < kSequencerLaneCount; ++i) {
        index = (index + direction + kSequencerLaneCount) % kSequencerLaneCount;
        if (laneShown((SequencerLane)index)) {
            focusLane_ = (SequencerLane)index;
            break;
        }
    }
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

bool StepGridComponent::changeFocusedValue(int direction, bool shift) {
    const int step = focusStep_;
    const auto before = module_.getExtraState();
    const auto& entry = module_.getPattern().steps[(size_t)step];
    switch (focusLane_) {
    case SequencerLane::Pitch: {
        const int amount = shift ? scale_.notesPerOctave() : 1;
        setPlayedPitch(step, scale_.stepBy(playedPitchOf(step), direction * amount));
        break;
    }
    case SequencerLane::Slide:
        editStep(step, [](SequencerStep& s) { s.slide = !s.slide; });
        break;
    case SequencerLane::Condition: {
        const auto& choices = conditionChoices();
        int index = 0;
        for (int i = 0; i < (int)choices.size(); ++i)
            if (choices[(size_t)i].first == entry.condHit && choices[(size_t)i].second == entry.condOf)
                index = i;
        index = juce::jlimit(0, (int)choices.size() - 1, index + direction);
        editStep(step, [c = choices[(size_t)index]](SequencerStep& s) {
            s.condHit = (std::uint8_t)c.first;
            s.condOf = (std::uint8_t)c.second;
        });
        break;
    }
    case SequencerLane::Repeat:
        editStep(step, [direction](SequencerStep& s) {
            s.repeat = (std::uint8_t)juce::jlimit(1, 8, (int)s.repeat + direction);
        });
        break;
    default: {
        const float delta = (shift ? 0.25f : 0.05f) * (float)direction;
        const float next = juce::jlimit(0.0f, 1.0f, laneNormal(focusLane_, entry) + delta);
        editStep(step, [lane = focusLane_, next](SequencerStep& s) { setLaneNormal(lane, s, next); });
        break;
    }
    }
    commit(before);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    return true;
}

bool StepGridComponent::moveFocus(int dx, int dy, bool shift) {
    if (dx != 0)
        setFocusStep(focusStep_ + dx);
    if (dy != 0)
        changeFocusedValue(dy, shift);
    return true;
}

bool StepGridComponent::keyPressed(const juce::KeyPress& key) {
    const bool shift = key.getModifiers().isShiftDown();
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey)
        return moveFocus(-1, 0, shift);
    if (code == juce::KeyPress::rightKey)
        return moveFocus(1, 0, shift);
    if (code == juce::KeyPress::upKey)
        return moveFocus(0, 1, shift);
    if (code == juce::KeyPress::downKey)
        return moveFocus(0, -1, shift);
    if (code == juce::KeyPress::homeKey) {
        setFocusStep(0);
        return true;
    }
    if (code == juce::KeyPress::endKey) {
        setFocusStep(stepCount() - 1);
        return true;
    }
    if (code == juce::KeyPress::pageUpKey) {
        moveFocusLane(-1);
        return true;
    }
    if (code == juce::KeyPress::pageDownKey) {
        moveFocusLane(1);
        return true;
    }
    const bool enter = code == juce::KeyPress::returnKey;
    if (enter && focusLane_ == SequencerLane::Condition) {
        openConditionMenu(focusStep_);
        return true;
    }
    if (code == juce::KeyPress::spaceKey || enter) {
        const auto before = module_.getExtraState();
        if (focusLane_ == SequencerLane::Slide)
            editStep(focusStep_, [](SequencerStep& s) { s.slide = !s.slide; });
        else
            editStep(focusStep_, [](SequencerStep& s) { s.on = !s.on; });
        commit(before);
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        return true;
    }
    return false;
}

} // namespace synth::ui
