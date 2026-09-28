// PianoRollControllerLanes — construction, lane selection, the two menus, geometry and the ONE
// commit path every gesture ends in. Declared in PianoRollControllerLanes.h; the gestures live in
// PianoRollControllerLanesVelocity.cpp / PianoRollControllerLanesCC.cpp and the drawing in
// PianoRollControllerLanesPainting.cpp. See docs/timeline/piano-roll-lanes.md.

#include "PianoRollControllerLanes.h"

#include "AppUndoManager.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// The controllers the selector offers by name, in menu order.
constexpr int kCommonControllers[] = {1, 2, 11, 64};
} // namespace

// No keyboard focus, and a click must not take it from the roll: the strip has no keys of its own,
// and the roll's J (snap) / Q (quantise) / Delete must keep answering while the pointer works here.
PianoRollControllerLanes::PianoRollControllerLanes(PianoRollComponent& roll)
    : roll_(roll) {
    setComponentID("pianoRollControllerLanes");
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setOpaque(true);
}

PianoRollControllerLanes::~PianoRollControllerLanes() = default;

// ---- Lane selection ------------------------------------------------------------------------------

// Selecting a CC lane the clip does not have yet mutates nothing: the lane is created by the first
// committed stroke (setControllerLanePoints creates it), so browsing the selector never dirties the
// document or pushes an undo step.
void PianoRollControllerLanes::selectLane(int laneId) {
    if (laneId != kVelocityLane && (laneId < 0 || laneId > 127))
        return;
    if (laneId == selectedLane_)
        return;
    cancelGesture();
    selectedLane_ = laneId;
    repaint();
}

int PianoRollControllerLanes::getSelectedLane() const noexcept { return selectedLane_; }

int PianoRollControllerLanes::laneMenuItemId(int laneId) noexcept {
    return laneId == kVelocityLane ? 1 : 1000 + laneId;
}

juce::PopupMenu PianoRollControllerLanes::buildLaneMenu() const {
    juce::PopupMenu menu;
    const auto add = [&](juce::PopupMenu& target, int laneId) {
        target.addItem(laneMenuItemId(laneId), lanes::laneName(laneId), true, laneId == selectedLane_);
    };
    add(menu, kVelocityLane);
    menu.addSeparator();
    for (const int cc : kCommonControllers)
        add(menu, cc);

    // Lanes the open clip already carries, beyond the named ones — so a lane drawn earlier (or loaded
    // from a file) is always one click away.
    if (const auto* clip = openClip()) {
        bool first = true;
        for (const auto& lane : clip->controllers) {
            if (std::find(std::begin(kCommonControllers), std::end(kCommonControllers), lane.ccNumber) !=
                std::end(kCommonControllers))
                continue;
            if (first)
                menu.addSeparator();
            first = false;
            add(menu, lane.ccNumber);
        }
    }

    juce::PopupMenu other;
    for (int block = 0; block < 4; ++block) {
        juce::PopupMenu range;
        for (int cc = block * 32; cc < block * 32 + 32; ++cc)
            add(range, cc);
        other.addSubMenu("CC " + juce::String(block * 32) + "-" + juce::String(block * 32 + 31), range);
    }
    menu.addSeparator();
    menu.addSubMenu("Other CC...", other);
    return menu;
}

void PianoRollControllerLanes::handleLaneMenuResult(int itemId) {
    if (itemId == laneMenuItemId(kVelocityLane))
        selectLane(kVelocityLane);
    else if (itemId >= 1000 && itemId <= 1127)
        selectLane(itemId - 1000);
}

void PianoRollControllerLanes::showLaneMenu() {
    juce::Component::SafePointer<PianoRollControllerLanes> safe(this);
    buildLaneMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int result) {
        if (safe != nullptr && result != 0)
            safe->handleLaneMenuResult(result);
    });
}

// ---- Right-click menu ----------------------------------------------------------------------------

juce::PopupMenu PianoRollControllerLanes::buildContextMenu(juce::Point<int> pos) const {
    juce::PopupMenu menu;
    const auto* clip = openClip();
    if (clip == nullptr)
        return menu;
    if (selectedLane_ == kVelocityLane) {
        menu.addItem(ResetVelocities,
                     roll_.getSelection().isEmpty() ? "Set all velocities to 100" : "Set selected velocities to 100",
                     !clip->notes.empty());
        return menu;
    }
    if (const auto handle = handleAt(pos)) {
        const int curve = shownPoints()[*handle].curve;
        menu.addItem(DeletePoint, "Delete point");
        menu.addItem(CurveHold, "Curve: Hold", true, curve == static_cast<int>(synth::BreakpointCurve::Hold));
        menu.addItem(CurveLinear, "Curve: Linear", true, curve == static_cast<int>(synth::BreakpointCurve::Linear));
        menu.addSeparator();
    }
    const auto* lane = roll_.getTimelineDoc()->getControllerLane(clip->id, selectedLane_);
    menu.addItem(ClearLane, "Clear lane", lane != nullptr && !lane->points.empty());
    menu.addItem(RemoveLane, "Remove " + lanes::laneName(selectedLane_) + " lane", lane != nullptr);
    return menu;
}

PianoRollControllerLanes::ContextTarget PianoRollControllerLanes::contextTargetAt(juce::Point<int> pos) const {
    ContextTarget target;
    target.clip = roll_.getClipId();
    target.lane = selectedLane_;
    if (selectedLane_ != kVelocityLane)
        if (const auto handle = handleAt(pos))
            target.pointBeat = shownPoints()[*handle].beat;
    return target;
}

void PianoRollControllerLanes::performContextAction(int action, juce::Point<int> pos) {
    performContextAction(action, contextTargetAt(pos));
}

// Each action is one commit, hence one undo step, exactly like a gesture. The menu answers
// asynchronously, and in between an undo, another view's edit or a clip / lane switch can change
// what sits under the pointer — so the target captured at menu-open time is re-verified against
// the doc NOW (same clip open, same lane shown, a point still at that exact beat) and a stale one
// does nothing rather than editing whatever happens to be there instead.
void PianoRollControllerLanes::performContextAction(int action, const ContextTarget& target) {
    const auto* clip = openClip();
    if (clip == nullptr || clip->id != target.clip || target.lane != selectedLane_)
        return;
    cancelGesture();
    if (action == ResetVelocities) {
        if (selectedLane_ != kVelocityLane)
            return;
        const auto restrict = selectedNoteIds();
        std::vector<std::pair<synth::NoteId, int>> velocities;
        for (const auto& note : clip->notes)
            if (restrict.empty() || std::find(restrict.begin(), restrict.end(), note.id) != restrict.end())
                velocities.emplace_back(note.id, 100);
        commitVelocities(velocities);
        return;
    }
    if (selectedLane_ == kVelocityLane)
        return;
    auto points = docPoints();
    std::optional<size_t> handle;
    if (target.pointBeat)
        for (size_t i = 0; i < points.size(); ++i)
            if (points[i].beat == *target.pointBeat)
                handle = i;
    switch (action) {
    case ClearLane:
        commitPoints({});
        return;
    case RemoveLane:
        commitPoints({}, /*removeLane=*/true);
        return;
    case DeletePoint:
        if (handle) {
            points.erase(points.begin() + (std::ptrdiff_t)*handle);
            commitPoints(points);
        }
        return;
    case CurveHold:
    case CurveLinear:
        if (handle) {
            points[*handle].curve =
                static_cast<int>(action == CurveHold ? synth::BreakpointCurve::Hold : synth::BreakpointCurve::Linear);
            commitPoints(points);
        }
        return;
    default:
        return;
    }
}

void PianoRollControllerLanes::showContextMenu(juce::Point<int> pos) {
    auto menu = buildContextMenu(pos);
    if (menu.getNumItems() == 0)
        return;
    const auto target = contextTargetAt(pos); // captured NOW, verified when the answer arrives
    juce::Component::SafePointer<PianoRollControllerLanes> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, target](int result) {
        if (safe != nullptr && result != 0)
            safe->performContextAction(result, target);
    });
}

// ---- Mouse dispatch ------------------------------------------------------------------------------

void PianoRollControllerLanes::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (e.mods.isPopupMenu()) {
        showContextMenu(pos);
        return;
    }
    if (getSelectorBounds().contains(pos)) {
        showLaneMenu();
        return;
    }
    if (openClip() == nullptr || !getValueArea().expanded(0, 4).contains(pos))
        return;
    anchor_ = lastPos_ = pos;
    hoveredHandle_.reset(); // hover is an idle-only state; the gesture draws its own emphasis
    hoveredBarBeat_.reset();
    if (selectedLane_ == kVelocityLane)
        beginVelocityGesture(pos, e.mods.isShiftDown());
    else
        beginCcGesture(pos, e.mods);
}

void PianoRollControllerLanes::mouseDrag(const juce::MouseEvent& e) {
    if (gesture_ == Gesture::None)
        return;
    const auto pos = e.getPosition();
    if (selectedLane_ == kVelocityLane)
        dragVelocityGesture(pos);
    else
        dragCcGesture(pos);
    lastPos_ = pos;
}

void PianoRollControllerLanes::mouseUp(const juce::MouseEvent& e) {
    if (gesture_ == Gesture::None)
        return;
    if (openClip() == nullptr) {
        cancelGesture();
        return;
    }
    if (selectedLane_ == kVelocityLane)
        endVelocityGesture();
    else
        endCcGesture(e.mouseWasDraggedSinceMouseDown());
}

// ---- Roll hooks ----------------------------------------------------------------------------------

void PianoRollControllerLanes::cancelGesture() {
    gesture_ = Gesture::None;
    gestureNotes_.clear();
    setPreviewVelocities({});
    origPoints_.clear();
    preview_.clear();
    stroke_.clear();
    movingIndex_.reset();
    erased_.clear();
    repaint();
}

// An undo (or another view's edit) mid-gesture would make the preview describe a document that no
// longer exists, so a live gesture is dropped rather than committed over it.
void PianoRollControllerLanes::refreshFromDoc() {
    if (gesture_ != Gesture::None)
        cancelGesture();
    hoveredHandle_.reset(); // indices / beats into data that may just have changed
    hoveredBarBeat_.reset();
    repaint();
}

// ---- Geometry ------------------------------------------------------------------------------------

juce::Rectangle<int> PianoRollControllerLanes::getSelectorBounds() const noexcept {
    return {0, 0, std::min(getWidth(), roll_.leftGutterWidth()), getHeight()};
}

juce::Rectangle<int> PianoRollControllerLanes::getValueArea() const noexcept {
    const int gutter = roll_.leftGutterWidth();
    return {gutter, 8, std::max(0, getWidth() - gutter), std::max(1, getHeight() - 12)};
}

int PianoRollControllerLanes::yForValue(double value) const noexcept {
    const auto area = getValueArea();
    return area.getBottom() - (int)std::lround(juce::jlimit(0.0, 127.0, value) / 127.0 * (double)area.getHeight());
}

double PianoRollControllerLanes::valueForY(int y) const noexcept {
    const auto area = getValueArea();
    return juce::jlimit(0.0, 127.0, (double)(area.getBottom() - y) / (double)area.getHeight() * 127.0);
}

int PianoRollControllerLanes::xForClipBeat(double clipBeat) const noexcept {
    const auto* clip = openClip();
    return (int)std::lround(roll_.beatToX((clip != nullptr ? clip->startBeat : 0.0) + clipBeat));
}

// ---- Shared helpers ------------------------------------------------------------------------------

const synth::Clip* PianoRollControllerLanes::openClip() const {
    auto* doc = roll_.getTimelineDoc();
    return (doc != nullptr && roll_.isOpen()) ? doc->getClip(roll_.getClipId()) : nullptr;
}

// Clip-relative beat under x, clamped into the clip window. `snap` uses the SHARED snap division
// (the roll's own magnetism, which Snap off / J disables), so lane edits land on the same grid as
// note edits.
double PianoRollControllerLanes::clipBeatAtX(int x, bool snap) const {
    const auto* clip = openClip();
    if (clip == nullptr)
        return 0.0;
    double abs = roll_.xToBeat((double)x);
    if (snap)
        abs = roll_.snapBeatForLanes(abs);
    return juce::jlimit(0.0, clip->lengthBeats, abs - clip->startBeat);
}

std::vector<synth::ControllerPoint> PianoRollControllerLanes::docPoints() const {
    const auto* clip = openClip();
    if (clip == nullptr || selectedLane_ == kVelocityLane)
        return {};
    const auto* lane = roll_.getTimelineDoc()->getControllerLane(clip->id, selectedLane_);
    return lane != nullptr ? lane->points : std::vector<synth::ControllerPoint>{};
}

// While a CC gesture runs, the preview IS the lane; otherwise the doc is (copied into idleCache_ so
// both branches can hand out a reference).
const std::vector<synth::ControllerPoint>& PianoRollControllerLanes::shownPoints() const {
    if (gesture_ != Gesture::None)
        return preview_;
    idleCache_ = docPoints();
    return idleCache_;
}

std::optional<size_t> PianoRollControllerLanes::handleAt(juce::Point<int> pos) const {
    const auto& points = shownPoints();
    std::optional<size_t> best;
    int bestDistance = kHandleHitPx + 1;
    for (size_t i = 0; i < points.size(); ++i) {
        const int dx = std::abs(xForClipBeat(points[i].beat) - pos.x);
        const int dy = std::abs(yForValue(points[i].value) - pos.y);
        if (dx <= kHandleHitPx && dy <= kHandleHitPx && std::max(dx, dy) < bestDistance) {
            best = i;
            bestDistance = std::max(dx, dy);
        }
    }
    return best;
}

// THE commit: one recordTimelineChange around one doc call, so a gesture is one undo step and one
// snapshot republish. With no undo manager (a bare roll in a test) it still applies, like the roll.
void PianoRollControllerLanes::commitVelocities(const std::vector<std::pair<synth::NoteId, int>>& velocities) {
    auto* doc = roll_.getTimelineDoc();
    if (doc == nullptr || velocities.empty())
        return;
    auto mutate = [doc, velocities] { doc->setNoteVelocities(velocities); };
    if (auto* undo = roll_.getUndoManager())
        undo->recordTimelineChange(*doc, mutate);
    else
        mutate();
}

void PianoRollControllerLanes::commitPoints(const std::vector<synth::ControllerPoint>& points, bool removeLane) {
    auto* doc = roll_.getTimelineDoc();
    const auto* clip = openClip();
    if (doc == nullptr || clip == nullptr || selectedLane_ == kVelocityLane)
        return;
    const auto clipId = clip->id;
    const int cc = selectedLane_;
    auto mutate = [doc, clipId, cc, points, removeLane] {
        if (removeLane)
            doc->removeControllerLane(clipId, cc);
        else
            doc->setControllerLanePoints(clipId, cc, points);
    };
    if (auto* undo = roll_.getUndoManager())
        undo->recordTimelineChange(*doc, mutate);
    else
        mutate();
}

// ---- Test hooks ----------------------------------------------------------------------------------

bool PianoRollControllerLanes::isGestureActiveForTest() const noexcept { return gesture_ != Gesture::None; }

int PianoRollControllerLanes::displayedVelocityForTest(synth::NoteId id) const {
    const auto it = previewVelocities_.find(id.value);
    if (it != previewVelocities_.end())
        return it->second;
    const auto* doc = roll_.getTimelineDoc();
    const auto* note = doc != nullptr ? doc->getNote(id) : nullptr;
    return note != nullptr ? note->velocity : -1;
}

std::vector<synth::ControllerPoint> PianoRollControllerLanes::displayedPointsForTest() const { return shownPoints(); }

} // namespace synth::ui
