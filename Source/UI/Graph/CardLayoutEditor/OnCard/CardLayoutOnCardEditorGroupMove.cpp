// CardLayoutOnCardEditorGroupMove.cpp -- moving a whole group: a titled group's title row is its handle. Dragging it
// carries the group up or down among its neighbours (they make room with the shared reorder motion), Alt+Up and
// Alt+Down step it one place, and the drop or key writes the new order of the card's sections as ONE write. The
// preview and the write both come from the animator's new order and oncard::stackedTops.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>
#include <utility>

namespace synth::ui {

namespace {

constexpr double kPushMs = 160.0;
constexpr double kSettleMs = 140.0;
/** The handle reaches this far either side of the title, room for the outline round the group. */
constexpr int kGroupPadX = 4;

const juce::String kGroupKey = "group:";
const juce::String kViewKey = "view:";

// A group the editor can move or rename: a titled grid group, shown, outside tabs, alternative looks and rows.
bool isMovable(const CardBodyPlan::Section& s) {
    return s.hasHeader() && !s.row && !s.footer && s.tabGroup < 0 && s.altGroup < 0 && s.visible && !s.holding &&
           s.header != nullptr && s.id.isNotEmpty();
}

int headerTopOf(const CardBodyPlan::Section& s) { return s.cellTop - cardbody::kSectionHeaderHeight; }

} // namespace

juce::Rectangle<int> CardLayoutOnCardEditor::groupBlockOf(int planSection) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr || planSection < 0 || planSection >= (int)body->getPlan().sections.size())
        return {};
    const auto& s = body->getPlan().sections[(size_t)planSection];
    if (s.header == nullptr)
        return {};
    return {s.header->getX(), headerTopOf(s), s.header->getWidth(), s.cellBottom - headerTopOf(s)};
}

int CardLayoutOnCardEditor::planSectionOfGroup(const juce::String& sectionId) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return -1;
    const auto& sections = body->getPlan().sections;
    for (int i = 0; i < (int)sections.size(); ++i)
        if (sections[(size_t)i].id == sectionId && isMovable(sections[(size_t)i]))
            return i;
    return -1;
}

// The groups `planSection` can trade places with: the movable groups standing flush against it, above and below
// (a group between them that is not movable, or a gap, ends the run), in card order. Empty when it cannot move or
// be renamed at all.
std::vector<int> CardLayoutOnCardEditor::groupRunOf(int planSection) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr || planSection < 0)
        return {};
    const auto& sections = body->getPlan().sections;
    std::vector<int> spaced; // the groups that take room on the card, top to bottom
    for (int i = 0; i < (int)sections.size(); ++i)
        if (!sections[(size_t)i].footer && (sections[(size_t)i].visible || sections[(size_t)i].holding))
            spaced.push_back(i);
    const auto at = std::find(spaced.begin(), spaced.end(), planSection);
    if (at == spaced.end() || !isMovable(sections[(size_t)planSection]))
        return {};
    const auto flush = [&sections](int upper, int lower) {
        return headerTopOf(sections[(size_t)lower]) == sections[(size_t)upper].cellBottom;
    };
    auto first = at;
    auto last = at;
    while (first != spaced.begin() && isMovable(sections[(size_t)*(first - 1)]) && flush(*(first - 1), *first))
        --first;
    while (last + 1 != spaced.end() && isMovable(sections[(size_t)*(last + 1)]) && flush(*last, *(last + 1)))
        ++last;
    return {first, last + 1};
}

std::vector<CardLayoutOnCardEditor::GroupBase> CardLayoutOnCardEditor::readGroupRun(const std::vector<int>& run) const {
    std::vector<GroupBase> bases;
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return bases;
    for (int section : run) {
        const auto& s = body->getPlan().sections[(size_t)section];
        GroupBase base;
        base.section = section;
        base.id = s.id;
        base.title = s.title.value_or(juce::String()).trim();
        base.block = groupBlockOf(section);
        base.header = s.header != nullptr ? s.header->getBounds() : juce::Rectangle<int>();
        for (int i : cellsOfSection(section))
            base.cells.emplace_back(i, cells_[(size_t)i].rect);
        for (const auto& view : collectViews(*card_, section))
            base.views.emplace_back(view.view, view.rect);
        bases.push_back(std::move(base));
    }
    return bases;
}

// One handle per movable group, kept (with its keyboard focus) across a write; a group that stops being movable
// loses its handle.
void CardLayoutOnCardEditor::reconcileGroupHandles() {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    std::vector<int> movable;
    if (body != nullptr)
        for (int i = 0; i < (int)body->getPlan().sections.size(); ++i)
            if (isMovable(body->getPlan().sections[(size_t)i]))
                movable.push_back(i);
    for (int i = groupHandles_.size(); --i >= 0;) {
        const bool kept = std::any_of(movable.begin(), movable.end(), [&](int s) {
            return body->getPlan().sections[(size_t)s].id == groupHandles_[i]->getSectionId();
        });
        if (!kept)
            groupHandles_.remove(i);
    }
    for (int section : movable) {
        const auto& s = body->getPlan().sections[(size_t)section];
        auto* handle = getGroupHandleForTest(s.id);
        if (handle == nullptr) {
            handle = groupHandles_.add(new CardLayoutGroupHandle(s.id, s.title->trim()));
            wireGroupHandle(*handle);
            addAndMakeVisible(handle);
            handle->toBack();
        }
        handle->setGroupTitle(s.title->trim());
        handle->setBlock(groupBlockOf(section).expanded(kGroupPadX, 0), cardbody::kSectionHeaderHeight);
    }
}

CardLayoutGroupHandle* CardLayoutOnCardEditor::getGroupHandleForTest(const juce::String& sectionId) const {
    for (auto* handle : groupHandles_)
        if (handle->getSectionId() == sectionId)
            return handle;
    return nullptr;
}

void CardLayoutOnCardEditor::wireGroupHandle(CardLayoutGroupHandle& handle) {
    const auto id = handle.getSectionId();
    handle.onPress = [this, id](const juce::MouseEvent& e) { pressGroup(id, e); };
    handle.onDrag = [this](const juce::MouseEvent& e) { dragGroup(e); };
    handle.onRelease = [this](const juce::MouseEvent& e) { releaseGroup(e); };
    handle.onKey = [this, id](const juce::KeyPress& press) { return handleGroupKey(id, press); };
    handle.onRename = [this, id] { startRename(id); };
}

juce::Rectangle<int> CardLayoutOnCardEditor::getGroupBlockForTest(const juce::String& sectionId) const {
    return groupBlockOf(planSectionOfGroup(sectionId));
}

std::vector<std::pair<juce::String, int>> CardLayoutOnCardEditor::getGroupDropPlanForTest() const {
    std::vector<std::pair<juce::String, int>> plan;
    if (!groupDrag_.pressed || groupDrag_.run.size() < 2)
        return plan;
    std::vector<oncard::GroupBlock> blocks;
    for (const auto& g : groupDrag_.run)
        blocks.push_back({g.block.getY(), g.block.getHeight()});
    const auto tops = oncard::stackedTops(blocks, groupSession_.animator().getNewOrder());
    for (size_t k = 0; k < tops.size(); ++k)
        plan.emplace_back(groupDrag_.run[k].id, tops[k]);
    return plan;
}

// ---- Dragging ---------------------------------------------------------------------------------------------

void CardLayoutOnCardEditor::pressGroup(const juce::String& sectionId, const juce::MouseEvent& e) {
    flushNudge();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    cancelRename();
    const int section = planSectionOfGroup(sectionId);
    const auto run = groupRunOf(section);
    if (closed_ || run.empty())
        return;
    groupDrag_ = {};
    groupDrag_.pressed = true;
    groupDrag_.run = readGroupRun(run);
    for (int k = 0; k < (int)run.size(); ++k)
        if (run[(size_t)k] == section)
            groupDrag_.dragged = k;
    if (run.size() < 2)
        return;
    std::vector<ReorderDragAnimator::Slot> slots;
    for (const auto& g : groupDrag_.run)
        slots.push_back({(float)g.block.getY(), (float)g.block.getHeight()});
    const float pointer = e.getEventRelativeTo(this).position.y;
    groupSession_.begin(slots, groupDrag_.dragged, pointer - slots[(size_t)groupDrag_.dragged].start, pointer);
}

void CardLayoutOnCardEditor::dragGroup(const juce::MouseEvent& e) {
    if (!groupDrag_.pressed || groupDrag_.run.size() < 2)
        return;
    if (!groupSession_.dragTo(e.getEventRelativeTo(this).position.y))
        return;
    if (auto* handle = getGroupHandleForTest(groupDrag_.run[(size_t)groupDrag_.dragged].id))
        handle->setLifted(true);
    placeGroups();
}

// Every group of the run where the animator has it now: the dragged one under the pointer, the others in the slots
// they hold (gliding there). Cells, title, views and the group's handle move together, vertically only.
void CardLayoutOnCardEditor::placeGroups() {
    if (!groupDrag_.pressed || groupDrag_.run.size() < 2 || card_ == nullptr)
        return;
    const auto& animator = groupSession_.animator();
    for (int k = 0; k < (int)groupDrag_.run.size(); ++k) {
        const auto& g = groupDrag_.run[(size_t)k];
        // An aborted drag that does not animate is idle at once: every group stands at its rest slot.
        const float top = k == groupDrag_.dragged && animator.isReordering() ? animator.getDraggedStart()
                                                                             : animator.getLayoutStart(k);
        const int dy = juce::roundToInt(top) - g.block.getY();
        for (const auto& [cell, rect] : g.cells)
            if (cell < (int)cells_.size() && cells_[(size_t)cell].rect != rect.translated(0, dy))
                moveCellTo(cell, rect.translated(0, dy));
        moveKeyTo(kGroupKey + g.id, g.header.translated(0, dy));
        for (const auto& [view, rect] : g.views)
            moveKeyTo(kViewKey + juce::String((int)view), rect.translated(0, dy));
    }
}

void CardLayoutOnCardEditor::releaseGroup(const juce::MouseEvent&) {
    if (!groupDrag_.pressed)
        return;
    const auto drag = std::move(groupDrag_);
    groupDrag_ = {};
    if (auto* handle = getGroupHandleForTest(drag.run[(size_t)drag.dragged].id))
        handle->setLifted(false);
    if (drag.run.size() < 2)
        return;
    if (groupSession_.end() != ReorderDragSession::End::Commit)
        return;
    const auto order = groupSession_.animator().getNewOrder();
    if (oncard::isIdentityOrder(order)) {
        // Dropped where it began: nothing is written and everything glides home.
        std::vector<float> starts;
        for (const auto& g : drag.run)
            starts.push_back((float)g.block.getY());
        groupDrag_ = drag;
        groupSession_.release(starts);
        placeGroups();
        groupDrag_ = {};
        return;
    }
    groupSession_.discard();
    commitGroupOrder(drag.run, order, drag.dragged);
}

// ---- The write --------------------------------------------------------------------------------------------

// The new order of the run written as ONE change of the layout, then every cell, title and view glides from where
// it stood (the dragged group, from where it was let go) to where the card put it.
void CardLayoutOnCardEditor::commitGroupOrder(const std::vector<GroupBase>& run, const std::vector<int>& order,
                                              int moved) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || order.size() != run.size())
        return;
    const auto layout = body->explicitLayout();
    std::vector<int> slots;
    std::vector<juce::String> titles;
    for (const auto& g : run) {
        slots.push_back(layoutSectionIndexOfPlan(layout, g.section));
        titles.push_back(g.title);
    }
    if (std::find(slots.begin(), slots.end(), -1) != slots.end())
        return;
    const auto written = oncard::withGroupsReordered(layout, slots, order);
    if (written == layout)
        return;

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> before; // move key, rect now
    for (const auto& c : cells_)
        before.emplace_back(c.key, c.rect);
    for (const auto& g : run) {
        const auto& s = body->getPlan().sections[(size_t)g.section];
        before.emplace_back(kGroupKey + g.id, s.header->getBounds());
        for (const auto& [view, rect] : g.views)
            if (const auto* component = body->findView(view))
                before.emplace_back(kViewKey + juce::String((int)view), component->getBounds());
    }
    const auto movedId = run[(size_t)moved].id;
    std::vector<int> movedCells;
    for (const auto& [cell, rect] : run[(size_t)moved].cells)
        movedCells.push_back(cell);
    std::vector<juce::String> movedKeys;
    for (int cell : movedCells)
        movedKeys.push_back(cells_[(size_t)cell].key);

    writeLayout(written);
    announce(oncard::describeGroupMove(titles, order, moved));

    const auto* after = card_ != nullptr ? card_->getCardBody() : nullptr;
    std::vector<Move> moves;
    for (const auto& [key, rect] : before) {
        juce::Rectangle<int> to;
        if (key.startsWith(kGroupKey)) {
            const int section = planSectionOfGroup(key.substring(kGroupKey.length()));
            if (section >= 0 && after != nullptr)
                to = after->getPlan().sections[(size_t)section].header->getBounds();
        } else if (key.startsWith(kViewKey)) {
            if (const auto* component = after != nullptr
                                            ? after->findView((CardView)key.substring(kViewKey.length()).getIntValue())
                                            : nullptr)
                to = component->getBounds();
        } else if (const int cell = indexOfCell(key); cell >= 0) {
            to = cells_[(size_t)cell].rect;
        }
        const bool isMoved =
            key == kGroupKey + movedId || std::find(movedKeys.begin(), movedKeys.end(), key) != movedKeys.end();
        moves.push_back({key, rect, to, isMoved ? kSettleMs : kPushMs, false});
    }
    startGlide(std::move(moves));
}

// ---- The keyboard -----------------------------------------------------------------------------------------

// Alt+Up or Alt+Down moves the focused group one place, as a drop would, and says where it went.
void CardLayoutOnCardEditor::moveGroupByKey(const juce::String& sectionId, int direction) {
    flushNudge();
    const int section = planSectionOfGroup(sectionId);
    const auto run = groupRunOf(section);
    if (closed_ || run.empty())
        return;
    const auto bases = readGroupRun(run);
    const int at = (int)(std::find(run.begin(), run.end(), section) - run.begin());
    const int to = at + direction;
    if (to < 0 || to >= (int)run.size()) {
        announce(bases[(size_t)at].title + " is already " + (direction < 0 ? "first" : "last"));
        return;
    }
    commitGroupOrder(bases, oncard::orderAfterMoving((int)run.size(), at, to), at);
}

// `sectionId` is the group whose handle holds focus. A key the editor does not use is left to bubble.
bool CardLayoutOnCardEditor::handleGroupKey(const juce::String& sectionId, const juce::KeyPress& press) {
    if (closed_)
        return false;
    if (keyPressed(press))
        return true;
    if (groupDrag_.pressed)
        return false;
    const auto alt = juce::ModifierKeys::altModifier;
    for (const int direction : {-1, 1})
        if (press == juce::KeyPress(direction < 0 ? juce::KeyPress::upKey : juce::KeyPress::downKey, alt, 0)) {
            moveGroupByKey(sectionId, direction);
            return true;
        }
    return false;
}

// ---- Moving what is drawn ----------------------------------------------------------------------------------

bool CardLayoutOnCardEditor::hasMoveTarget(const juce::String& key) const {
    if (key.startsWith(kGroupKey))
        return planSectionOfGroup(key.substring(kGroupKey.length())) >= 0;
    if (key.startsWith(kViewKey))
        return card_ != nullptr && card_->getCardBody() != nullptr &&
               card_->getCardBody()->findView((CardView)key.substring(kViewKey.length()).getIntValue()) != nullptr;
    return indexOfCell(key) >= 0;
}

// A glide's moving thing: a cell, a group's title (with its handle) or a view. Groups only ever move vertically.
void CardLayoutOnCardEditor::moveKeyTo(const juce::String& key, juce::Rectangle<int> rect) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return;
    if (key.startsWith(kGroupKey)) {
        const auto id = key.substring(kGroupKey.length());
        const int section = planSectionOfGroup(id);
        if (section < 0)
            return;
        auto* header = body->getPlan().sections[(size_t)section].header;
        header->setTopLeftPosition(header->getX(), rect.getY());
        if (auto* handle = getGroupHandleForTest(id))
            handle->setTopLeftPosition(handle->getX(), rect.getY());
    } else if (key.startsWith(kViewKey)) {
        if (auto* view = body->findView((CardView)key.substring(kViewKey.length()).getIntValue()))
            view->setBounds(rect);
    } else if (const int cell = indexOfCell(key); cell >= 0) {
        moveCellTo(cell, rect);
    }
}

} // namespace synth::ui
