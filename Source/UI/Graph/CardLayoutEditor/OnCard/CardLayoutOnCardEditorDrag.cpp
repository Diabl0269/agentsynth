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

void CardLayoutOnCardEditor::pressOn(const juce::String& key, const juce::MouseEvent& e) {
    flushNudge();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    const int cell = indexOfCell(key);
    if (cell < 0 || closed_)
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
    std::vector<juce::Rectangle<int>> others;
    for (int i : indices)
        if (i != cell)
            others.push_back(cells_[(size_t)i].rect);
    const auto limits = limitsFor(*card_->getCardBody(), section, card_->getWidth());
    const auto pushed = oncard::pushAside(dropped, start, others, limits);

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
    const auto layout = withCellPositions(body->explicitLayout(), placed, collectViews(*card_, section), g.contentX,
                                          body->getPlan().sections[(size_t)section].cellTop);
    writing_ = true;
    source_->apply(layout, false);
    writing_ = false;
    syncToCard();
}

// From where every cell stood to where it is now: the pushed ones glide aside, the dropped one settles.
// Nothing slides under Reduce Motion or with nothing on screen.
void CardLayoutOnCardEditor::startGlide(std::vector<Move> moves) {
    if (closing_ || closed_ || !isShowing() || prefersReducedMotion())
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
