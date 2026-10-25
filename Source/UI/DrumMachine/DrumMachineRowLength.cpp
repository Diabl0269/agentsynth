// DrumMachineComponent: a drum's own length and swing (Clip::drumRows). This unit reads and edits them, keeps what is
// shown in step with them (the ghost repeats past a drum's end fade in, its end edge slides), and handles the
// Alt+arrow length actions. The end edge's drawing and drag are DrumMachineEdges.cpp; the row menu is
// DrumMachineRowMenu.cpp. docs/timeline/drum-machine.md#per-drum-length-and-swing.

#include "DrumMachineComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

struct LengthAction {
    const char* id;
    int key;
    int modifiers;
    int direction; // -1 shorter, +1 longer
    bool byBar;
};

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kAltShift = juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier;
// The bar versions come first: Alt+Shift+Left must not be taken for Alt+Left.
const LengthAction kLengthActions[] = {
    {"drumMachineRowShorterBar", juce::KeyPress::leftKey, kAltShift, -1, true},
    {"drumMachineRowLongerBar", juce::KeyPress::rightKey, kAltShift, 1, true},
    {"drumMachineRowShorter", juce::KeyPress::leftKey, kAlt, -1, false},
    {"drumMachineRowLonger", juce::KeyPress::rightKey, kAlt, 1, false},
};
} // namespace

//==============================================================================
// What the clip stores for a drum, and what is in effect. A length at or past the clip's step count changes nothing
// (the snapshot repeats only a pattern shorter than the clip), so it reads as "follows the clip".
int DrumMachineComponent::rawRowLength(int pitch) const {
    const auto* c = clip();
    if (c == nullptr)
        return 0;
    const auto it = std::find_if(c->drumRows.begin(), c->drumRows.end(),
                                 [pitch](const synth::Clip::DrumRow& row) { return row.pitch == pitch; });
    return it == c->drumRows.end() ? 0 : it->lengthSteps;
}

int DrumMachineComponent::storedRowLength(int pitch) const {
    const int raw = rawRowLength(pitch);
    return raw > 0 && raw < getStepCount() ? raw : 0;
}

int DrumMachineComponent::getRowLength(int pitch) const {
    const auto preview = rowPreview_.find(pitch);
    return preview != rowPreview_.end() ? preview->second : storedRowLength(pitch);
}

bool DrumMachineComponent::hasRowSwing(int pitch) const {
    const auto* c = clip();
    if (c == nullptr)
        return false;
    return std::any_of(c->drumRows.begin(), c->drumRows.end(),
                       [pitch](const synth::Clip::DrumRow& row) { return row.pitch == pitch && row.swing >= 0.0; });
}

double DrumMachineComponent::getRowSwing(int pitch) const {
    const auto* c = clip();
    if (c == nullptr)
        return synth::Clip::kStraightSwing;
    for (const auto& row : c->drumRows)
        if (row.pitch == pitch && row.swing >= 0.0)
            return row.swing;
    return c->swing;
}

int DrumMachineComponent::ghostSourceStep(int row, int step) const {
    const int pitch = getLanePitch(row);
    const int length = pitch < 0 ? 0 : getRowLength(pitch);
    return length > 0 && step >= length ? step % length : -1;
}

//==============================================================================
// Edits: each is ONE undo step through the TimelineDoc mutators.
void DrumMachineComponent::setRowLength(int pitch, int steps) {
    if (clip() == nullptr || pitch < 0 || pitch > 127 || !synth::Clip::isValidRowLength(steps) ||
        rawRowLength(pitch) == steps)
        return;
    const auto id = clipId_;
    record([this, id, pitch, steps] { doc_->setClipRowLength(id, pitch, steps); });
    refreshFromDoc();
}

void DrumMachineComponent::setRowSwing(int pitch, double swing) {
    if (clip() == nullptr || pitch < 0 || pitch > 127 || !std::isfinite(swing))
        return;
    swing = swing < 0.0 ? -1.0 : swing;
    if (swing >= 0.0 && !synth::Clip::isValidSwing(swing))
        return;
    if (hasRowSwing(pitch) ? std::abs(getRowSwing(pitch) - swing) < 1.0e-9 : swing < 0.0)
        return;
    const auto id = clipId_;
    record([this, id, pitch, swing] { doc_->setClipRowSwing(id, pitch, swing); });
    refreshFromDoc();
}

void DrumMachineComponent::resetRow(int pitch) {
    if (clip() == nullptr || (rawRowLength(pitch) == 0 && !hasRowSwing(pitch)))
        return;
    const auto id = clipId_;
    record([this, id, pitch] {
        doc_->setClipRowLength(id, pitch, 0);
        doc_->setClipRowSwing(id, pitch, -1.0);
    });
    refreshFromDoc();
}

// The Alt+arrow actions. The drum's end moves by `deltaSteps` inside 1 to the clip's step count; reaching the clip's
// end clears the length, since a drum as long as the clip has no length of its own.
bool DrumMachineComponent::changeRowLength(int pitch, int deltaSteps) {
    const int steps = getStepCount();
    if (clip() == nullptr || pitch < 0 || steps < 2)
        return false;
    const int stored = storedRowLength(pitch);
    const int edge = stored > 0 ? stored : steps;
    const int target = juce::jlimit(1, steps, edge + deltaSteps);
    const int next = target >= steps ? 0 : std::min(target, synth::Clip::kMaxRowLengthSteps);
    if (target == edge || next == rawRowLength(pitch))
        return false;
    setRowLength(pitch, next);
    const auto name = drumLaneForPitch(pitch).name;
    announce(next > 0 ? name + " length " + juce::String(next) + (next == 1 ? " step" : " steps")
                      : name + " follows the clip's length");
    return true;
}

// The row the keyboard means: the one whose name button has focus, else the grid's focused row.
int DrumMachineComponent::keyboardRowPitch() const {
    for (const auto& slot : slots_)
        if (const auto* name = labels_.getNameButton(slot.lane.pitch); name != nullptr && name->hasKeyboardFocus(false))
            return slot.lane.pitch;
    return getLanePitch(focus_.row);
}

bool DrumMachineComponent::handleRowLengthKey(const juce::KeyPress& key) {
    for (const auto& action : kLengthActions) {
        if (!matchesAction(key, action.id, juce::KeyPress(action.key, juce::ModifierKeys(action.modifiers), 0)))
            continue;
        const int pitch = keyboardRowPitch();
        if (pitch >= 0)
            changeRowLength(pitch, action.direction * (action.byBar ? stepsPerBar() : 1));
        return true;
    }
    return false;
}

//==============================================================================
// Showing it. `shownLengths_` is each drum's length as last drawn. When one changes, the cells that just became ghosts
// fade in and the end edge slides to its new place (Reduce Motion: the fade only, the edge jumps; Animations Off or off
// screen: both land at once). A drag's own preview moves the edge by hand, so it never slides.
void DrumMachineComponent::syncRowLengths(bool animate) {
    const bool moving = animate && motionAllowed();
    const int steps = getStepCount();
    bool rebased = false, slid = false;
    for (const auto& slot : slots_) {
        const int pitch = slot.lane.pitch;
        const int now = getRowLength(pitch);
        const auto found = shownLengths_.find(pitch);
        if (found == shownLengths_.end()) {
            shownLengths_[pitch] = now;
            continue;
        }
        const int was = found->second;
        if (was == now)
            continue;
        const double from = displayedEdge(pitch);
        found->second = now;
        const int oldEdge = was > 0 ? was : steps;
        if (!moving) {
            slides_.erase(pitch);
            continue;
        }
        if (now > 0 && now < oldEdge) {
            for (int s = now; s < oldEdge; ++s)
                ghostFades_[drumgrid::cellKey(pitch, s)] = nowMs();
            startMotionClock();
        }
        if (rowPreview_.count(pitch) > 0 || prefersReducedMotion()) {
            slides_.erase(pitch);
            continue;
        }
        if (!rebased) {
            for (auto& [other, slide] : slides_)
                slide.from += (slide.to - slide.from) * static_cast<double>(slideT_);
            slideT_ = 0.0f;
            rebased = true;
        }
        slides_[pitch] = {from, static_cast<double>(now > 0 ? now : steps)};
        slid = true;
    }
    if (slid)
        startEdgeSlide();
    repaint(gridRegion());
}

// Every state of the drum lengths on screen: a new clip, or none, starts from nothing.
void DrumMachineComponent::clearRowLengthState() {
    rowPreview_.clear();
    shownLengths_.clear();
    slides_.clear();
    slideT_ = 1.0f;
    ghostFades_.clear();
    hoverEdgeRow_ = -1;
    edgeAnim_.stop(vblank_);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void DrumMachineComponent::startEdgeSlide() {
    edgeAnim_.start(
        vblank_, motionMs(kRowMotionMs, kReducedMotionMs), easeOutCubic,
        [this](float t) {
            slideT_ = t;
            repaint(gridRegion());
        },
        [this] {
            slides_.clear();
            slideT_ = 1.0f;
            repaint(gridRegion());
        });
}

// Where a drum's end is drawn, in steps: its length (the clip's step count when it has none), or between two while it
// slides.
double DrumMachineComponent::displayedEdge(int pitch) const {
    const auto slide = slides_.find(pitch);
    if (slide != slides_.end())
        return slide->second.from + (slide->second.to - slide->second.from) * static_cast<double>(slideT_);
    const auto shown = shownLengths_.find(pitch);
    const int length = shown != shownLengths_.end() ? shown->second : getRowLength(pitch);
    return static_cast<double>(length > 0 ? length : getStepCount());
}

double DrumMachineComponent::getEdgePosition(int pitch) const { return displayedEdge(pitch); }

float DrumMachineComponent::ghostFadeAt(drumgrid::CellKey key) const {
    const auto it = ghostFades_.find(key);
    if (it == ghostFades_.end())
        return 1.0f;
    const double duration = motionMs(kHitMotionMs, kReducedMotionMs);
    return juce::jlimit(0.0f, 1.0f, static_cast<float>((nowMs() - it->second) / duration));
}

bool DrumMachineComponent::isGhostFadeActive(int row, int step) const {
    return slotIndexForRow(row) >= 0 && ghostFades_.count(drumgrid::cellKey(slots_[(size_t)row].lane.pitch, step)) > 0;
}

// What a screen reader adds to a row's name: ", length, 12 steps, swing 40%" for what is set.
juce::String DrumMachineComponent::describeRowSettings(int pitch) const {
    juce::String text;
    if (const int length = getRowLength(pitch); length > 0)
        text += ", length, " + juce::String(length) + (length == 1 ? " step" : " steps");
    if (hasRowSwing(pitch))
        text += ", swing " + juce::String(juce::roundToInt(swingToPercent(getRowSwing(pitch)))) + "%";
    return text;
}

} // namespace synth::ui
