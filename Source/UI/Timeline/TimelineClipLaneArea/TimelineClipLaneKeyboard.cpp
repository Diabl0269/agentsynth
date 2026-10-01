// TimelineClipLaneKeyboard.cpp
//
// Keyboard clip mode for TimelineClipLaneArea: stepping between clips, opening and moving the
// keyboard clip, its accent ring, and its screen-reader description. TimelineClipLaneArea is
// declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files hold the rest of the
// class.
//
// The keyboard clip is not a second piece of selection state: it is remembered only as the id that
// must still be the ONE selected clip. Anything that changes the selection (a marquee, a click on
// another clip, Delete, undo) therefore ends keyboard mode without this file being told, and every
// existing selection-based verb (copy, split, mute, loop the selection) acts on it unchanged.

#include "TimelineClipLaneArea.h"

#include "AppUndoManager.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Timeline/ClipAccessibilityText.h"
#include "UI/Timeline/ClipKeyboardNav.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

// Exposes the keyboard clip's description as the lane area's value, so a screen reader reads the
// clip the arrow keys landed on rather than just "Clips".
class KeyboardClipValueInterface : public juce::AccessibilityTextValueInterface {
public:
    explicit KeyboardClipValueInterface(const TimelineClipLaneArea& lane)
        : lane_(lane) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return lane_.getKeyboardClipAccessibilityText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const TimelineClipLaneArea& lane_;
};

// The fraction of the view a clip is scrolled in from the edge it came from.
constexpr double kScrollMarginFraction = 0.1;
// The grid step when the snap division is Off.
constexpr double kFallbackStepBeats = 1.0;

} // namespace

//==============================================================================
std::unique_ptr<juce::AccessibilityHandler> TimelineClipLaneArea::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<KeyboardClipValueInterface>(*this)});
}

synth::ClipId TimelineClipLaneArea::getKeyboardClip() const {
    if (doc_ == nullptr || !keyboardClip_.isValid() || selection_.size() != 1 || !selection_.contains(keyboardClip_) ||
        doc_->getClip(keyboardClip_) == nullptr)
        return {};
    return keyboardClip_;
}

juce::String TimelineClipLaneArea::getKeyboardClipAccessibilityText() const {
    const auto id = getKeyboardClip();
    if (!id.isValid())
        return {};
    const auto* clip = doc_->getClip(id);
    const auto* track = doc_->getTrackForClip(id);
    if (clip == nullptr || track == nullptr)
        return {};
    return describeClipForAccessibility(*clip, *track, currentBeatsPerBar());
}

void TimelineClipLaneArea::setKeyboardClip(synth::ClipId id) {
    if (doc_ == nullptr || doc_->getClip(id) == nullptr) {
        keyboardClip_ = {};
        repaint();
        return;
    }
    keyboardClip_ = id;
    selection_.setSelection({id});
    publishKeyboardClip();
}

// A description is announced only when it differs from the last one handed out, so an unrelated
// doc change (which calls this through refreshFromDoc) never re-reads the same clip.
void TimelineClipLaneArea::refreshKeyboardClip() {
    if (!keyboardClip_.isValid())
        return;
    if (!getKeyboardClip().isValid()) {
        keyboardClip_ = {};
        announcedKeyboardText_ = {};
        return;
    }
    const auto text = getKeyboardClipAccessibilityText();
    if (text == announcedKeyboardText_)
        return;
    announcedKeyboardText_ = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void TimelineClipLaneArea::publishKeyboardClip() {
    if (const auto* clip = doc_ != nullptr ? doc_->getClip(keyboardClip_) : nullptr)
        scrollClipIntoView(*clip);
    repaint();
    refreshKeyboardClip();
    if (onKeyboardClipChanged)
        onKeyboardClipChanged(keyboardClip_);
}

// Brings the clip fully on screen when it fits, else its start. A clip coming in from the left is
// placed a little in from the edge so the clip before it stays partly visible, and symmetrically
// from the right.
void TimelineClipLaneArea::scrollClipIntoView(const synth::Clip& clip) {
    if (getWidth() <= 0 || viewState_.pixelsPerBeat <= 0.0)
        return;
    const double visibleBeats = (double)getWidth() / viewState_.pixelsPerBeat;
    const double first = viewState_.firstVisibleBeat;
    const double start = clip.startBeat;
    const double end = clip.startBeat + clip.lengthBeats;
    const bool fits = clip.lengthBeats <= (1.0 - kScrollMarginFraction) * visibleBeats;

    double newFirst = first;
    if (start < first)
        newFirst = fits ? start - kScrollMarginFraction * visibleBeats : start;
    else if (end > first + visibleBeats)
        newFirst = fits ? end - (1.0 - kScrollMarginFraction) * visibleBeats : start;
    newFirst = std::max(0.0, newFirst);
    if (std::abs(newFirst - first) < 1e-9)
        return;

    viewState_.firstVisibleBeat = newFirst;
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
    repaint();
}

//==============================================================================
bool TimelineClipLaneArea::matchesClipAction(const juce::KeyPress& key, const juce::String& actionId,
                                             const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

synth::ClipId TimelineClipLaneArea::keyboardVerbTarget() const {
    if (const auto id = getKeyboardClip(); id.isValid())
        return id;
    if (doc_ == nullptr || selection_.size() != 1)
        return {};
    const auto id = selection_.getSelected().front();
    return doc_->getClip(id) != nullptr ? id : synth::ClipId{};
}

// Escape is a fixed platform key (like Delete) and so is not in the shortcut table; every other
// verb resolves through a Timeline-category action. Nothing here runs mid-drag or while the inline
// rename editor owns the keyboard.
bool TimelineClipLaneArea::handleKeyboardClipKey(const juce::KeyPress& key) {
    if (doc_ == nullptr || renameEditor_ != nullptr || dragMode_ != DragMode::None)
        return false;

    if (key == juce::KeyPress::escapeKey) {
        const auto id = getKeyboardClip();
        const auto* track = id.isValid() ? doc_->getTrackForClip(id) : nullptr;
        if (track == nullptr)
            return false;
        const auto trackId = track->id;
        keyboardClip_ = {};
        announcedKeyboardText_ = {};
        repaint();
        if (onReturnToTrackHeaderRequested)
            onReturnToTrackHeaderRequested(trackId);
        return true;
    }

    const auto id = keyboardVerbTarget();
    if (!id.isValid())
        return false;
    const auto* track = doc_->getTrackForClip(id);
    if (track == nullptr)
        return false;

    const auto plain = [](int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); };
    const auto withAlt = [](int code) { return juce::KeyPress(code, juce::ModifierKeys::altModifier, 0); };

    synth::ClipId target;
    if (matchesClipAction(key, "timelineClipPrevious", plain(juce::KeyPress::leftKey)))
        target = clipnav::adjacentOnTrack(*track, id, -1);
    else if (matchesClipAction(key, "timelineClipNext", plain(juce::KeyPress::rightKey)))
        target = clipnav::adjacentOnTrack(*track, id, 1);
    else if (matchesClipAction(key, "timelineClipAbove", plain(juce::KeyPress::upKey)))
        target = clipnav::nearestOnAdjacentTrack(*doc_, id, -1);
    else if (matchesClipAction(key, "timelineClipBelow", plain(juce::KeyPress::downKey)))
        target = clipnav::nearestOnAdjacentTrack(*doc_, id, 1);
    else if (matchesClipAction(key, "timelineClipOpen", plain(juce::KeyPress::returnKey))) {
        // The same hook a double-click on the clip fires.
        if (onClipDoubleClicked)
            onClipDoubleClicked(id);
        return true;
    } else if (matchesClipAction(key, "timelineClipMoveEarlier", withAlt(juce::KeyPress::leftKey))) {
        moveKeyboardClipByStep(id, -1);
        return true;
    } else if (matchesClipAction(key, "timelineClipMoveLater", withAlt(juce::KeyPress::rightKey))) {
        moveKeyboardClipByStep(id, 1);
        return true;
    } else
        return false;

    // A step with nowhere to go (the first or last clip, no clipped track above) is consumed but
    // changes nothing, like the track header's clamped Up/Down. A clip picked with the mouse still
    // becomes the keyboard clip, so the ring and description follow the first key press.
    setKeyboardClip(target.isValid() ? target : id);
    return true;
}

//==============================================================================
double TimelineClipLaneArea::keyboardGridStepBeats() const {
    // The chosen division even when the snap switch is off: the keys step a known musical amount.
    const double step = viewState_.divisionBeatsRaw(currentBeatsPerBar());
    return step > 0.0 ? step : kFallbackStepBeats;
}

// One recordTimelineChange around the same synth::TimelineDoc::moveClip a pointer move commits
// with. The clip keeps its id, so it stays the keyboard clip; it is clamped at beat 0.
void TimelineClipLaneArea::moveKeyboardClipByStep(synth::ClipId id, int direction) {
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return;
    const double newStart = std::max(0.0, clip->startBeat + (double)direction * keyboardGridStepBeats());
    if (std::abs(newStart - clip->startBeat) < 1e-9)
        return;

    auto mutate = [this, id, newStart] { doc_->moveClip(id, newStart); };
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    keyboardClip_ = id;
    selection_.setSelection({id});
    publishKeyboardClip();
}

//==============================================================================
// The keyboard clip's ring: the shared small-control focus ring, around the clip, drawn only while
// the lane holds keyboard focus (paintFocusRing checks that itself).
void TimelineClipLaneArea::paintKeyboardClipRing(juce::Graphics& g) {
    const auto id = getKeyboardClip();
    if (!id.isValid())
        return;
    const auto rect = getClipRect(id);
    if (rect.isEmpty())
        return;
    synth::ui::paintFocusRing(g, rect.toFloat().expanded(1.5f), *this, 4.0f);
}

void TimelineClipLaneArea::focusGained(juce::Component::FocusChangeType) {
    if (keyboardClip_.isValid())
        repaint();
}

void TimelineClipLaneArea::focusLost(juce::Component::FocusChangeType) {
    if (keyboardClip_.isValid())
        repaint();
}

} // namespace synth::ui
