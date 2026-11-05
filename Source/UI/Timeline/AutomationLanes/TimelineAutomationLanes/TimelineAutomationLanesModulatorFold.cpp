// Concern: TimelineAutomationLanes' modulator row fold (one line or three, tweened) and order (the keyboard move, on
// into the next lane past either end, and the one function that writes the order), both per lane and per routing.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMinFoldedHeight = 12;
constexpr double kFoldMs = 160.0;

juce::KeyPress defaultMoveKey(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::altModifier, 0); }
} // namespace

bool TimelineAutomationLanes::isModulatorCollapsed(const juce::String& key) const {
    return collapsedMods_.count(key) > 0;
}

bool TimelineAutomationLanes::isModulatorCollapsed(synth::LaneId lane, int index) const {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.infos.size())
        return false;
    return isModulatorCollapsed(it->second.infos[(size_t)index].key());
}

// 1 = open, 0 = folded to the title line; the tween's current value while one is in flight.
float TimelineAutomationLanes::modulatorOpenness(const juce::String& key) const {
    const auto tween = foldTweens_.find(key);
    if (tween != foldTweens_.end())
        return tween->second.now;
    return isModulatorCollapsed(key) ? 0.0f : 1.0f;
}

// A folded row is one title line tall: the line its controls are laid out in (a third of the full height), but never
// so small the title and the arrow are clipped. A row growing in (a new routing) counts only its share so far.
int TimelineAutomationLanes::modulatorRowHeightFor(synth::LaneId lane, int index) const {
    const int height = modulatorRowFoldHeight(lane, index);
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.infos.size())
        return height;
    const float presence = arrivingPresence(rowMotionKey(lane, it->second.infos[(size_t)index].key()));
    return presence >= 1.0f ? height : (int)std::lround((double)height * (double)presence);
}

// The row's height from its fold alone, before any grow-in.
int TimelineAutomationLanes::modulatorRowFoldHeight(synth::LaneId lane, int index) const {
    const int full = modulatorRowHeight(lane, index);
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.infos.size())
        return full;
    // A row too short for three lines is one line already: folding changes nothing.
    const int line = ModulatorRow::isCrowdedAt(full / 3) ? full : std::max(full / 3, std::min(full, kMinFoldedHeight));
    const float open = modulatorOpenness(it->second.infos[(size_t)index].key());
    return line + (int)std::lround((double)(full - line) * (double)open);
}

// A row travelling into another lane is drawn whole while the slot it is heading for still grows; any other row is
// drawn at its slot's height.
int TimelineAutomationLanes::modulatorRowDrawHeight(synth::LaneId lane, int index) const {
    if (!travellingRows_.empty())
        if (const auto* row = modulatorRowFor(lane, index);
            row != nullptr && travellingRows_.count(row->getInfo().key()))
            return modulatorRowFoldHeight(lane, index);
    return modulatorRowHeightFor(lane, index);
}

// How far the edit tray of the lane's header (or of its `modulatorIndex`th row) pushes what lies below it, px.
int TimelineAutomationLanes::trayExtentOf(synth::LaneId lane) const {
    const auto it = headers_.find(lane);
    return it != headers_.end() ? it->second->getControlStrip().trayExtent() : 0;
}

int TimelineAutomationLanes::trayExtentOf(synth::LaneId lane, int modulatorIndex) const {
    auto* row = modulatorRowFor(lane, modulatorIndex);
    return row != nullptr ? row->getControlStrip().trayExtent() : 0;
}

// Where the lane's `count`th modulator row starts under the lane row, px: past the lane header's edit tray (if open),
// every row above it with its own, the gaps of rows leaving before it (TimelineAutomationLanesBlockMotion.cpp) and the
// room a dragged row makes or leaves (TimelineAutomationLanesDragGap.cpp).
int TimelineAutomationLanes::modulatorOffset(synth::LaneId lane, int count) const {
    int offset = trayExtentOf(lane);
    for (int i = 0; i < count; ++i)
        offset +=
            dragGapAt(lane, i) + leavingRowsHeightAt(lane, i) + modulatorRowHeightFor(lane, i) + trayExtentOf(lane, i);
    return offset + dragGapAt(lane, count) + (leaving_.empty() ? 0 : leavingRowsHeightAt(lane, count));
}

// The rows show the fold state; a folding row keeps its controls (they are clipped away as it shrinks) until it lands.
void TimelineAutomationLanes::applyModulatorFold() {
    for (auto& [id, entry] : modulators_)
        for (size_t i = 0; i < entry.rows.size() && i < entry.infos.size(); ++i) {
            const auto& key = entry.infos[i].key();
            const bool folded = isModulatorCollapsed(key);
            entry.rows[i]->setCollapsed(folded);
            entry.rows[i]->setCompact(folded && foldTweens_.count(key) == 0);
        }
}

void TimelineAutomationLanes::setModulatorCollapsed(const juce::String& key, bool collapsed) {
    if (isModulatorCollapsed(key) == collapsed)
        return;
    const float from = modulatorOpenness(key);
    if (collapsed)
        collapsedMods_.insert(key);
    else
        collapsedMods_.erase(key);

    // Movement: it lands at once when the panel is not showing, under Reduce Motion, or for a row nobody can see.
    const bool animate = FadeVisibility::canAnimateIn(&headerParent_) && !prefersReducedMotion();
    if (!animate) {
        foldTweens_.erase(key);
        if (foldTweens_.empty())
            foldDriver_.stop(foldUpdater_);
        applyModulatorFold();
        if (onLayoutChanged)
            onLayoutChanged();
        return;
    }
    for (auto& [other, tween] : foldTweens_)
        tween.from = tween.now; // a restart retargets every fold in flight from where it is
    foldTweens_[key] = {from, collapsed ? 0.0f : 1.0f, from};
    applyModulatorFold();
    foldDriver_.start(
        foldUpdater_, kFoldMs, easeOutCubic, [this](float t) { frameModulatorFold(t); },
        [this] { landModulatorFold(); });
}

void TimelineAutomationLanes::frameModulatorFold(float t) {
    for (auto& [key, tween] : foldTweens_)
        tween.now = tween.from + (tween.to - tween.from) * t;
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::landModulatorFold() {
    foldDriver_.stop(foldUpdater_);
    foldTweens_.clear();
    applyModulatorFold();
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::stepModulatorFoldForTest(float t) {
    if (foldTweens_.empty())
        return;
    if (t >= 1.0f)
        landModulatorFold();
    else
        frameModulatorFold(t);
}

//==============================================================================
// Order

// -1 for the Move Lane Up key, +1 for Move Lane Down, else 0 (rebindable; Alt+Up/Down without a shortcut manager).
int TimelineAutomationLanes::laneMoveDirection(const juce::KeyPress& key) const {
    const auto matches = [this, &key](const char* action, int keyCode) {
        if (shortcuts_ == nullptr)
            return key == defaultMoveKey(keyCode);
        return ShortcutManager::keyPressMatches(shortcuts_->getBinding(action), key);
    };
    if (matches("timelineMoveLaneUp", juce::KeyPress::upKey))
        return -1;
    return matches("timelineMoveLaneDown", juce::KeyPress::downKey) ? 1 : 0;
}

// Offered by a focused modulator row, one of its controls, or its band. Consumed even at an end, so the combo or the
// knob nudge never sees an Alt+arrow. The row's height keys (TimelineAutomationLanesHeight.cpp) resize just this
// row.
bool TimelineAutomationLanes::handleModulatorKey(synth::LaneId lane, const ModulatorInfo& info,
                                                 const juce::KeyPress& key) {
    const int direction = laneMoveDirection(key);
    if (direction == 0)
        return handleRowHeightKey({lane, modulatorOrderKey(info)}, key);
    moveModulatorBy(lane, modulatorOrderKey(info), direction);
    return true;
}

bool TimelineAutomationLanes::moveModulatorBy(synth::LaneId lane, const juce::String& orderKey, int delta) {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end())
        return false;
    const auto& infos = it->second.infos;
    const auto at = std::find_if(infos.begin(), infos.end(), [&orderKey](const ModulatorInfo& info) {
        return modulatorOrderKey(info) == orderKey;
    });
    if (at == infos.end())
        return false;
    const int to = (int)(at - infos.begin()) + delta;
    if (to < 0 || to >= (int)infos.size())
        return moveModulatorToNeighbourLane(lane, ModulatorInfo(*at), delta);
    std::map<juce::String, int> oldYs;
    for (size_t i = 0; i < it->second.rows.size() && i < infos.size(); ++i)
        oldYs[infos[i].key()] = it->second.rows[i]->getY();
    if (!moveModulatorInOrder(lane, orderKey, to))
        return false;
    glideModulatorRows(lane, oldYs);
    return true;
}

// Past a lane's first or last row the move carries on through the lanes on screen around it, across tracks, to the
// first one that can take the routing (a lane whose jack already has this source cannot). The row lands at the near end
// of that lane: its top going down, its bottom going up. It is the drop's move (ONE undo step, the routing re-created
// with a new key): the old row's gap closes while the new one's opens, and the row itself travels from where it stood
// into its new slot (settleMovedRow), so it never jumps; keyboard focus follows it there.
bool TimelineAutomationLanes::moveModulatorToNeighbourLane(synth::LaneId lane, const ModulatorInfo& info, int delta) {
    if (doc_ == nullptr || host_ == nullptr || delta == 0)
        return false;
    std::vector<synth::LaneId> order;
    for (const auto& track : doc_->getTracks())
        if (isVisibleLane(track))
            for (const auto id : shownLanesOf(track))
                order.push_back(id);
    const auto at = std::find(order.begin(), order.end(), lane);
    if (at == order.end())
        return false;
    const bool hadFocus = modulatorHasKeyboardFocus(lane, info.key());
    int fromY = 0;
    for (int i = 0; i < modulatorCount(lane); ++i)
        if (modulatorRowFor(lane, i)->getInfo().key() == info.key())
            fromY = modulatorRowFor(lane, i)->getY();
    const int step = delta > 0 ? 1 : -1;
    for (int i = (int)(at - order.begin()) + step; i >= 0 && i < (int)order.size(); i += step) {
        const auto dest = order[(size_t)i];
        if (!host_->canMoveModulatorToLane(info, dest))
            continue;
        const int index = step > 0 ? 0 : modulatorCount(dest);
        droppedRoutingKey_ = info.key();
        landing_ = {dest, 0.0f, 0.0f, fromY, {}, true};
        const bool moved = host_->moveModulatorToLane(info, dest, index);
        droppedRoutingKey_.clear();
        const auto landing = std::exchange(landing_, {});
        if (!moved)
            return false;
        settleMovedRow(landing);
        if (auto* row = modulatorRowFor(dest, index); hadFocus && row != nullptr)
            row->grabKeyboardFocus();
        return true;
    }
    return false;
}

bool TimelineAutomationLanes::modulatorHasKeyboardFocus(synth::LaneId lane, const juce::String& key) const {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end())
        return false;
    for (size_t i = 0; i < it->second.infos.size() && i < it->second.rows.size(); ++i)
        if (it->second.infos[i].key() == key)
            return it->second.rows[i]->hasKeyboardFocus(true) ||
                   (i < it->second.bands.size() && it->second.bands[i]->hasKeyboardFocus(true));
    return false;
}

// The offset left to travel (px, positive = still below its slot), applied on top of the row's slot by the layout.
int TimelineAutomationLanes::modulatorGlideOffset(const juce::String& key) const {
    const auto tween = glideTweens_.find(key);
    return tween == glideTweens_.end() ? 0 : (int)std::lround((double)tween->second.now);
}

void TimelineAutomationLanes::glideModulatorRows(synth::LaneId lane, const std::map<juce::String, int>& oldYs) {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || !FadeVisibility::canAnimateIn(&headerParent_) || prefersReducedMotion())
        return;
    for (size_t i = 0; i < it->second.rows.size() && i < it->second.infos.size(); ++i) {
        const auto& key = it->second.infos[i].key();
        const auto old = oldYs.find(key);
        if (old == oldYs.end())
            continue;
        const float offset = (float)(old->second - it->second.rows[i]->getY());
        if (offset != 0.0f)
            glideTweens_[key] = {offset, 0.0f, offset};
    }
    runModulatorGlide();
}

// Starts (or restarts) the glide of every row in glideTweens_ from its offset now.
void TimelineAutomationLanes::runModulatorGlide() {
    if (glideTweens_.empty())
        return;
    for (auto& [key, tween] : glideTweens_)
        tween.from = tween.now;
    glideDriver_.start(
        foldUpdater_, kFoldMs, easeOutCubic, [this](float t) { frameModulatorGlide(t); },
        [this] { landModulatorGlide(); });
    frameModulatorGlide(0.0f); // the first frame already shows the rows where they stood
}

// An undo or redo of a row move: the held refresh permuted the rows (noting where each stood) and laid them out in
// their restored slots; they glide there from their old ones, like Alt+Up/Down glided them forward.
void TimelineAutomationLanes::glideModulatorsReorderedByRestore() {
    const auto glides = std::exchange(restoreGlides_, {});
    for (const auto& [lane, oldYs] : glides)
        glideModulatorRows(lane, oldYs);
}

void TimelineAutomationLanes::frameModulatorGlide(float t) {
    for (auto& [key, tween] : glideTweens_)
        tween.now = tween.from + (tween.to - tween.from) * t;
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::landModulatorGlide() {
    glideDriver_.stop(foldUpdater_);
    glideTweens_.clear();
    travellingRows_.clear();
    if (onLayoutChanged)
        onLayoutChanged();
}

void TimelineAutomationLanes::stepModulatorGlideForTest(float t) {
    if (glideTweens_.empty())
        return;
    if (t >= 1.0f)
        landModulatorGlide();
    else
        frameModulatorGlide(t);
}

// The order is computed from the rows as they are now (the stored order may be empty or stale) and stored whole.
bool TimelineAutomationLanes::moveModulatorInOrder(synth::LaneId lane, const juce::String& orderKey, int newIndex) {
    const auto it = modulators_.find(lane);
    if (doc_ == nullptr || it == modulators_.end())
        return false;
    std::vector<juce::String> order;
    for (const auto& info : it->second.infos)
        order.push_back(modulatorOrderKey(info));
    const auto at = std::find(order.begin(), order.end(), orderKey);
    if (at == order.end())
        return false;
    const int to = juce::jlimit(0, (int)order.size() - 1, newIndex);
    if ((int)(at - order.begin()) == to)
        return false;
    order.erase(at);
    order.insert(order.begin() + to, orderKey);
    return setModulatorOrderUndoable(*doc_, undo_, lane, std::move(order));
}

// The routings are the same, only their order changed: the rows and bands (keyboard focus and all) move to their new
// slots instead of being rebuilt. The caller relayouts.
void TimelineAutomationLanes::permuteModulators(LaneModulators& entry, const std::vector<ModulatorInfo>& infos) {
    std::vector<std::unique_ptr<ModulatorRow>> rows;
    std::vector<std::unique_ptr<ModulatorBand>> bands;
    for (const auto& info : infos) {
        const auto at = std::find_if(entry.infos.begin(), entry.infos.end(),
                                     [&info](const ModulatorInfo& old) { return old.key() == info.key(); });
        const auto i = (size_t)(at - entry.infos.begin());
        rows.push_back(std::move(entry.rows[i]));
        bands.push_back(std::move(entry.bands[i]));
    }
    entry.rows = std::move(rows);
    entry.bands = std::move(bands);
    entry.infos = infos;
}

} // namespace synth::ui
