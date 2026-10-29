// CardLayoutOnCardEditorDrag.cpp -- picking a control up, dragging it (the real widget moves with its
// outline), snapping to guides, dropping it (the neighbours it lands on are pushed aside) and writing the
// result. A write rebuilds the card, so the glide that follows runs on the rebuilt card's widgets, from
// where everything stood to where the layout put it. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/ReducedMotion.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr int kDragThreshold = 3;
constexpr double kPushMs = 160.0;
constexpr double kSettleMs = 140.0;
constexpr double kHideZoneFadeMs = 120.0;
// The origin of a control new to the card: it was never anywhere.
const juce::Rectangle<int> kNowhereRect(-100000, -100000, 0, 0);

juce::Rectangle<int> lerpRect(juce::Rectangle<int> from, juce::Rectangle<int> to, float t) {
    return AnimationDriver::lerpBounds(from, to, t);
}

} // namespace

std::vector<int> CardLayoutOnCardEditor::cellsOfSection(int section) const {
    std::vector<int> indices;
    for (int i = 0; i < (int)cells_.size(); ++i)
        if (cells_[(size_t)i].section == section)
            indices.push_back(i);
    return indices;
}

// Inside the section's content width, never above its top, and no lower than one cell below its last
// row, so a drop can make the section grow by a row but not fling a control off the card.
juce::Rectangle<int> CardLayoutOnCardEditor::clampToTarget(int section, juce::Rectangle<int> rect) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return rect;
    const auto clamped = oncard::clampToLimits(rect, limitsFor(*body, section, card_->getWidth()));
    return clamped.withY(std::min(clamped.getY(), body->getPlan().sections[(size_t)section].cellBottom));
}

juce::Rectangle<int> CardLayoutOnCardEditor::clampToSection(int cell, juce::Rectangle<int> rect) const {
    return clampToTarget(cells_[(size_t)cell].section, rect);
}

// The group's strip of the card: from its header down to the next group (or the card's bottom).
juce::Rectangle<int> CardLayoutOnCardEditor::sectionArea(int section) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return {};
    const auto& sections = body->getPlan().sections;
    const auto topOf = [&sections](int i) {
        const auto& s = sections[(size_t)i];
        return s.cellTop - (s.hasHeader() ? cardbody::kSectionHeaderHeight : 0);
    };
    int bottom = cardArea().getBottom();
    for (int i = section + 1; i < (int)sections.size(); ++i)
        if (sections[(size_t)i].visible && !sections[(size_t)i].footer && sections[(size_t)i].tabGroup < 0) {
            bottom = topOf(i);
            break;
        }
    const int top = topOf(section);
    return {0, top, cardArea().getWidth(), std::max(0, bottom - top)};
}

// The group a drag at `y` is over. Groups stand flush, so a group has no room under its last row: the upper half of
// the next group's header counts as that strip, where a drop starts a new row at the end of the group above.
int CardLayoutOnCardEditor::dropSectionAt(int y) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    const int section = editableSectionAt(y);
    if (body == nullptr || section < 0)
        return section;
    const auto& sections = body->getPlan().sections;
    const auto& s = sections[(size_t)section];
    if (!s.hasHeader() || y >= s.cellTop - cardbody::kSectionHeaderHeight / 2)
        return section;
    for (int i = section - 1; i >= 0; --i)
        if (sections[(size_t)i].visible && !sections[(size_t)i].footer && sections[(size_t)i].tabGroup < 0)
            return i;
    return section;
}

// A control's home keeps its position but takes the size the control has now (a fader swapped for a knob).
juce::Rectangle<int> CardLayoutOnCardEditor::homeRectOf(int cell) const {
    const auto& c = cells_[(size_t)cell];
    const auto home = homes_.find(c.key);
    return home == homes_.end() ? c.rect : c.rect.withPosition(home->second.getPosition());
}

// After a write of our own the controls keep their homes (the push it made is not a choice of the user's);
// after anything else (opening, an undo or redo, a tab switch) every control's home is where it stands.
void CardLayoutOnCardEditor::refreshHomes() {
    std::map<juce::String, juce::Rectangle<int>> homes, origins;
    for (const auto& c : cells_) {
        const auto kept = homes_.find(c.key);
        const auto first = origins_.find(c.key);
        homes[c.key] = keepHomes_ && kept != homes_.end() ? kept->second : c.rect;
        origins[c.key] = keepHomes_ && first != origins_.end() ? first->second : c.rect;
    }
    homes_ = std::move(homes);
    origins_ = std::move(origins);
}

// Where the other cells of `section` stand once `dropped` lands (`start` is where the dropped control stood;
// `except` its cell, or -1 for a control new to the card): crowded cells are pushed aside from where they
// stand now, then every cell that was pushed away from its home goes back when the home is free.
std::vector<juce::Rectangle<int>> CardLayoutOnCardEditor::pushedNeighbours(int section, int except,
                                                                           juce::Rectangle<int> dropped,
                                                                           juce::Rectangle<int> start) const {
    std::vector<juce::Rectangle<int>> standing, homes, origins;
    for (int i : cellsOfSection(section))
        if (i != except) {
            standing.push_back(cells_[(size_t)i].rect);
            homes.push_back(homeRectOf(i));
            origins.push_back(origins_.at(cells_[(size_t)i].key)
                                  .withSize(cells_[(size_t)i].rect.getWidth(), cells_[(size_t)i].rect.getHeight()));
        }
    const auto limits = limitsFor(*card_->getCardBody(), section, card_->getWidth());
    const auto droppedOrigin = except >= 0 ? origins_.at(cells_[(size_t)except].key) : kNowhereRect;
    return oncard::returnHome(oncard::pushAside(dropped, start, standing, limits), homes, origins, dropped,
                              droppedOrigin);
}

void CardLayoutOnCardEditor::pressOn(const juce::String& key, const juce::MouseEvent& e) {
    flushNudge();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    const int cell = indexOfCell(key);
    if (cell < 0 || closed_ || cells_[(size_t)cell].panelOnly)
        return;
    drag_ = {};
    drag_.cell = cell;
    drag_.pressed = true;
    drag_.pressPoint = e.getEventRelativeTo(this).getPosition();
    drag_.startRect = cells_[(size_t)cell].rect;
    drag_.pressOffset = drag_.pressPoint - drag_.startRect.getPosition();
    drag_.target = cells_[(size_t)cell].section;
    for (const auto& c : cells_)
        drag_.baseline.push_back(c.rect);
}

// The grab point stays under the pointer: the offset was taken once at the press, in this overlay's
// coordinates, because the outline under the pointer moves with it.
void CardLayoutOnCardEditor::dragTo(const juce::MouseEvent& e) {
    if (!drag_.pressed)
        return;
    const auto pointer = e.getEventRelativeTo(this).getPosition();
    if (!drag_.moving) {
        if (pointer.getDistanceFrom(drag_.pressPoint) < kDragThreshold)
            return;
        drag_.moving = true;
        escapeKey_.arm(*this, [this] { cancelDrag(); });
        setHideZoneShown(true);
        if (auto* outline = getOutlineForTest(cells_[(size_t)drag_.cell].key)) {
            outline->setLift(1.0f);
            showDragCursor(*outline);
        }
    }
    // Below the card the control stays where it last was (in the card) and a release hides it; back above the
    // edge it follows the pointer again.
    const bool below = pointer.y > cardArea().getBottom();
    if (below != drag_.hiding) {
        drag_.hiding = below;
        hideZone_.setActive(below);
    }
    if (below) {
        guides_.clear();
        restoreBaseline();
        repaint();
        return;
    }
    // The control may leave its group for another one: the group under the pointer is the one it would land in.
    const int own = cells_[(size_t)drag_.cell].section;
    const int under = cardArea().contains(pointer) ? dropSectionAt(pointer.y) : -1;
    drag_.target = under >= 0 ? under : own;
    const int newDrop = drag_.target != own ? drag_.target : -1;
    if (newDrop != dropSection_) {
        dropSection_ = newDrop;
        repaint();
    }
    std::vector<juce::Rectangle<int>> others;
    for (int i : cellsOfSection(drag_.target))
        if (i != drag_.cell)
            others.push_back(drag_.baseline[(size_t)i]);
    const auto raw = drag_.startRect.withPosition(pointer - drag_.pressOffset);
    const auto snapped = oncard::snapDraggedRect(raw, others, !e.mods.isCommandDown());
    const auto rect = clampToTarget(drag_.target, snapped.rect);
    previewRowDrop(rect, pointer, rect == snapped.rect ? snapped.guides : std::vector<oncard::Guide>{});
    repaint();
}

// Where the drop would leave everything, planned from the cells as they stood at the press (so a neighbour
// pushed aside returns the moment the dragged control moves off it) and shown live: the neighbours move to their
// planned rects and the dragged control sits on its row. The release writes this plan, so it equals the preview.
// A control going to another group is planned at the size that group gives it.
void CardLayoutOnCardEditor::previewRowDrop(juce::Rectangle<int> rect, juce::Point<int> pointer,
                                            const std::vector<oncard::Guide>& snapGuides) {
    const int cell = drag_.cell;
    const int target = drag_.target;
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return;
    std::vector<int> indices;
    std::vector<juce::Rectangle<int>> others;
    oncard::DropContext context;
    if (target == cells_[(size_t)cell].section)
        context.vacated = drag_.baseline[(size_t)cell];
    if (const auto origin = origins_.find(cells_[(size_t)cell].key); origin != origins_.end())
        context.origin = origin->second;
    for (int i : cellsOfSection(target))
        if (i != cell) {
            indices.push_back(i);
            others.push_back(drag_.baseline[(size_t)i]);
            context.homes.push_back(homeRectOf(i));
        }
    const auto size =
        target != cells_[(size_t)cell].section ? addedCellSize(target, cells_[(size_t)cell].key) : juce::Point<int>();
    const auto limits = limitsFor(*body, target, card_->getWidth());
    const auto drop =
        oncard::dropIntoRow(size.isOrigin() ? rect : rect.withSize(size.x, size.y), pointer, others, limits, context);
    drag_.placed.clear();
    for (size_t k = 0; k < indices.size(); ++k)
        drag_.placed.emplace_back(indices[k], drop.others[k]);
    drag_.droppedRect = drop.dropped;
    for (int i = 0; i < (int)cells_.size(); ++i)
        if (i != cell) {
            const auto want = placedRect(drag_.placed, i, drag_.baseline[(size_t)i]);
            if (cells_[(size_t)i].rect != want)
                moveCellTo(i, want);
        }
    moveCellTo(cell, drop.dropped.withSize(rect.getWidth(), rect.getHeight()));
    if (drop.newRowAt >= 0)
        guides_ = {{false, drop.newRowAt, limits.minX, limits.maxX}};
    else
        guides_ = drop.dropped.getPosition() == rect.getPosition() ? snapGuides : std::vector<oncard::Guide>{};
}

// Every cell but the dragged one back where it stood at the press.
void CardLayoutOnCardEditor::restoreBaseline() {
    drag_.placed.clear();
    drag_.droppedRect = {};
    for (int i = 0; i < (int)cells_.size() && i < (int)drag_.baseline.size(); ++i)
        if (i != drag_.cell && cells_[(size_t)i].rect != drag_.baseline[(size_t)i])
            moveCellTo(i, drag_.baseline[(size_t)i]);
}

void CardLayoutOnCardEditor::moveCellTo(int cell, juce::Rectangle<int> rect) {
    auto& c = cells_[(size_t)cell];
    c.rect = rect;
    placeCell(c, rect.getPosition());
    if (auto* outline = getOutlineForTest(c.key))
        outline->setCell(rect);
}

void CardLayoutOnCardEditor::releaseOn(const juce::MouseEvent&) {
    if (!drag_.pressed)
        return;
    const bool moved = drag_.moving;
    const bool hide = drag_.hiding;
    const int target = drag_.target;
    const int cell = drag_.cell;
    const auto start = drag_.startRect;
    const auto dropped = drag_.droppedRect;
    const auto baseline = std::move(drag_.baseline);
    const auto placed = std::move(drag_.placed);
    endDrag();
    if (moved && hide)
        hideDragged(cell);
    else if (moved && !dropped.isEmpty() && target >= 0 && target != cells_[(size_t)cell].section)
        commitMoveToSection(cell, dropped, target, placed);
    else if (moved && !dropped.isEmpty())
        commitRowDrop(cell, start, placed, baseline, dropped);
}

// A control let go below the card goes the way Backspace takes it: shrinking away from where it was let go, to
// the More row, as one undo step.
void CardLayoutOnCardEditor::hideDragged(int cell) {
    if (cell < 0 || cell >= (int)cells_.size() || closed_)
        return;
    const auto caption = cells_[(size_t)cell].caption;
    hideControl(cells_[(size_t)cell].key);
    announce(caption + " hidden");
}

// The "Drop to hide" area fades in when a drag starts and out when it ends: 120 ms, none under Reduce Motion.
void CardLayoutOnCardEditor::setHideZoneShown(bool shown) {
    hideZonePump_.stop();
    hideZone_.setActive(false);
    if (shown) {
        hideZone_.setVisible(true);
        hideZone_.toFront(false);
    }
    const float from = hideZone_.getAlpha();
    const float to = shown ? 1.0f : 0.0f;
    if (prefersReducedMotion() || !canAnimate() || from == to) {
        hideZone_.setAlpha(to);
        hideZone_.setVisible(shown);
        return;
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    hideZonePump_.run(
        kHideZoneFadeMs,
        [this, from, to, start] {
            const auto t =
                (float)juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - start) / kHideZoneFadeMs);
            hideZone_.setAlpha(from + (to - from) * easeOutCubic(t));
        },
        [this, shown] { hideZone_.setVisible(shown); });
}

void CardLayoutOnCardEditor::endDrag() {
    escapeKey_.disarm();
    guides_.clear();
    dropSection_ = -1;
    if (drag_.moving)
        setHideZoneShown(false);
    if (drag_.cell >= 0 && drag_.cell < (int)cells_.size())
        if (auto* outline = getOutlineForTest(cells_[(size_t)drag_.cell].key)) {
            outline->setLift(0.0f);
            endDragCursor(*outline);
        }
    drag_ = {};
    repaint();
}

// Esc mid-drag: the control goes back where it was (140 ms, easing in as a thing sent back does) and
// nothing is written.
void CardLayoutOnCardEditor::cancelDrag() {
    if (!drag_.pressed)
        return;
    const int cell = drag_.cell;
    const auto start = drag_.startRect;
    const auto here = cells_[(size_t)cell].rect;
    const auto key = cells_[(size_t)cell].key;
    restoreBaseline();
    endDrag();
    moveCellTo(cell, start);
    startGlide({{key, here, start, kSettleMs, true}});
}

// The final place of every cell in the dropped one's section, then ONE write: a drop that changes
// nothing (back where it started, nothing crowded) writes nothing.
void CardLayoutOnCardEditor::commitMove(int cell, juce::Rectangle<int> dropped, juce::Rectangle<int> start,
                                        bool announceMove) {
    if (cell < 0 || cell >= (int)cells_.size() || card_ == nullptr || card_->getCardBody() == nullptr)
        return;
    const int section = cells_[(size_t)cell].section;
    const auto indices = cellsOfSection(section);
    const auto pushed = pushedNeighbours(section, cell, dropped, start);

    std::vector<std::pair<int, juce::Rectangle<int>>> finals;
    std::vector<Move> moves;
    const auto caption = cells_[(size_t)cell].caption;
    bool changed = dropped != start;
    size_t next = 0;
    for (int i : indices) {
        const auto& c = cells_[(size_t)i];
        const auto target = i == cell ? dropped : pushed[next++];
        changed = changed || target != c.rect;
        finals.emplace_back(i, target);
        moves.push_back({c.key, c.rect, target, i == cell ? kSettleMs : kPushMs, false});
    }
    if (!changed) {
        moveCellTo(cell, start);
        return;
    }
    if (dropped != start)
        homes_[cells_[(size_t)cell].key] = dropped;
    writeSection(section, finals);
    if (announceMove)
        announce(oncard::describeMove(caption, dropped.getX() - start.getX(), dropped.getY() - start.getY()));
    startGlide(std::move(moves));
}

// The drop's plan written: every cell of the dropped one's section at its planned rect, then ONE write. A drop
// that changes nothing (back where it started, nobody moved) writes nothing.
void CardLayoutOnCardEditor::commitRowDrop(int cell, juce::Rectangle<int> start, const CellRects& placed,
                                           const std::vector<juce::Rectangle<int>>& baseline,
                                           juce::Rectangle<int> dropped) {
    if (cell < 0 || cell >= (int)cells_.size() || card_ == nullptr || card_->getCardBody() == nullptr)
        return;
    const int section = cells_[(size_t)cell].section;
    CellRects finals;
    std::vector<Move> moves;
    bool changed = dropped != start;
    for (int i : cellsOfSection(section)) {
        const auto& c = cells_[(size_t)i];
        const auto target = i == cell ? dropped : placedRect(placed, i, c.rect);
        changed = changed || (i < (int)baseline.size() && target != baseline[(size_t)i]);
        finals.emplace_back(i, target);
        moves.push_back({c.key, c.rect, target, i == cell ? kSettleMs : kPushMs, false});
    }
    if (!changed) {
        moveCellTo(cell, start);
        return;
    }
    const auto caption = cells_[(size_t)cell].caption;
    homes_[cells_[(size_t)cell].key] = dropped;
    writeSection(section, finals);
    announce(oncard::describeMove(caption, dropped.getX() - start.getX(), dropped.getY() - start.getY()));
    startGlide(std::move(moves));
}

namespace {
std::function<CardLayout(CardLayout, std::optional<int>)> showAs(const juce::String& paramId, CardWidget widget) {
    return [paramId, widget](CardLayout l, std::optional<int> section) {
        return withShowAs(std::move(l), paramId, widget, section);
    };
}
} // namespace

// A control shown as another widget (a fader for a knob) changes size, so its row is repacked in the same
// undo step as the change: the width it freed is reused, the width it needs is made. A group the card flows
// itself has nothing to repack.
void CardLayoutOnCardEditor::changeShowAs(const juce::String& paramId, CardWidget widget) {
    const int cell = indexOfCell(paramId);
    if (cell < 0 || source_ == nullptr || card_ == nullptr || card_->getCardBody() == nullptr)
        return editControl(paramId, showAs(paramId, widget));
    const auto before = cells_[(size_t)cell].rect;
    source_->beginGroupedWrite();
    editControl(paramId, showAs(paramId, widget));
    repackAfterResize(paramId, before);
    source_->endGroupedWrite();
}

void CardLayoutOnCardEditor::repackAfterResize(const juce::String& paramId, juce::Rectangle<int> before) {
    const int cell = indexOfCell(paramId);
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (cell < 0 || body == nullptr ||
        cells_[(size_t)cell].rect.getWidth() == before.getWidth() &&
            cells_[(size_t)cell].rect.getHeight() == before.getHeight())
        return;
    const int section = cells_[(size_t)cell].section;
    if (!body->getPlan().sections[(size_t)section].freeform)
        return;
    std::vector<int> indices = cellsOfSection(section);
    std::vector<juce::Rectangle<int>> rects;
    size_t changed = 0;
    for (size_t k = 0; k < indices.size(); ++k) {
        rects.push_back(cells_[(size_t)indices[k]].rect);
        if (indices[k] == cell)
            changed = k;
    }
    const auto packed = oncard::repackRow(rects, changed, before, limitsFor(*body, section, card_->getWidth()));
    if (packed == rects)
        return;
    CellRects finals;
    for (size_t k = 0; k < indices.size(); ++k) {
        finals.emplace_back(indices[k], packed[k]);
    }
    writeSection(section, finals);
}

// The section's cells, each at its final rect, become free positions in the node's layout; the source
// writes it and rebuilds the card, and this overlay re-syncs to the card that results.
void CardLayoutOnCardEditor::writeSection(int section, const std::vector<std::pair<int, juce::Rectangle<int>>>& rects) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr || source_ == nullptr)
        return;
    std::vector<OnCardCell> placed;
    for (const auto& [index, rect] : rects) {
        placed.push_back(cells_[(size_t)index]);
        placed.back().rect = rect;
    }
    const auto g = cardbody::BodyGeometry::forCardWidth(card_->getWidth());
    writeLayout(withCellPositions(body->explicitLayout(), placed, collectViews(*card_, section), g.contentX,
                                  body->getPlan().sections[(size_t)section].cellTop));
}

// The source writes `layout` and rebuilds the card; this overlay re-syncs to the card that results.
void CardLayoutOnCardEditor::writeLayout(const CardLayout& layout) {
    if (source_ == nullptr)
        return;
    writing_ = true;
    source_->apply(layout, applyToAll_);
    writing_ = false;
    keepHomes_ = true;
    syncToCard();
    keepHomes_ = false;
}

// From where every cell stood to where it is now: the pushed ones glide aside, the dropped one settles.
// Nothing slides under Reduce Motion or with nothing on screen.
void CardLayoutOnCardEditor::startGlide(std::vector<Move> moves) {
    if (closing_ || closed_ || !canAnimate() || prefersReducedMotion())
        return;
    std::erase_if(moves, [this](const Move& m) { return m.from == m.to || indexOfCell(m.key) < 0; });
    if (moves.empty())
        return;
    double longest = 0.0;
    for (const auto& m : moves) {
        longest = std::max(longest, m.ms);
        moveCellTo(indexOfCell(m.key), m.from);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto place = [this, moves](float elapsedMs) {
        for (const auto& m : moves)
            if (const int cell = indexOfCell(m.key); cell >= 0) {
                const float t = juce::jlimit(0.0f, 1.0f, elapsedMs / (float)m.ms);
                moveCellTo(cell, lerpRect(m.from, m.to, m.easeIn ? easeInCubic(t) : easeOutCubic(t)));
            }
    };
    finishGlide_ = [this, place] {
        glidePump_.stop();
        place(1.0e9f);
    };
    glidePump_.run(
        longest, [place, start] { place((float)(juce::Time::getMillisecondCounterHiRes() - start)); },
        [this] { finishGlide_ = nullptr; });
}

} // namespace synth::ui
