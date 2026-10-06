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
juce::Rectangle<int> CardLayoutOnCardEditor::clampToSection(int cell, juce::Rectangle<int> rect) const {
    const auto& c = cells_[(size_t)cell];
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return rect;
    const auto clamped = oncard::clampToLimits(rect, limitsFor(*body, c.section, card_->getWidth()));
    return clamped.withY(std::min(clamped.getY(), body->getPlan().sections[(size_t)c.section].cellBottom));
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
    drag_.sectionCells = cellsOfSection(cells_[(size_t)cell].section);
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
        if (auto* outline = getOutlineForTest(cells_[(size_t)drag_.cell].key)) {
            outline->setLift(1.0f);
            showDragCursor(*outline);
        }
    }
    std::vector<juce::Rectangle<int>> others;
    for (int i : drag_.sectionCells)
        if (i != drag_.cell)
            others.push_back(cells_[(size_t)i].rect);
    const auto raw = drag_.startRect.withPosition(pointer - drag_.pressOffset);
    const auto snapped = oncard::snapDraggedRect(raw, others, !e.mods.isCommandDown());
    const auto rect = clampToSection(drag_.cell, snapped.rect);
    guides_ = rect == snapped.rect ? snapped.guides : std::vector<oncard::Guide>{};
    moveCellTo(drag_.cell, rect);
    repaint();
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
    const int cell = drag_.cell;
    const auto start = drag_.startRect;
    const auto dropped = cells_[(size_t)cell].rect;
    endDrag();
    if (moved)
        commitMove(cell, dropped, start, true);
}

void CardLayoutOnCardEditor::endDrag() {
    escapeKey_.disarm();
    guides_.clear();
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
