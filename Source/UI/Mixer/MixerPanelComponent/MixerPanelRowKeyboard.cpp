// Concern: MixerPanelComponent's keyboard walk through a column's send and insert rows -- entering
// them, stepping, nudging a send level, removing a row, opening it, leaving -- plus the focused row's
// ring and its screen-reader text. Row focus is state of the panel (like focusedColumnIndex_), never
// real focus on a child, so the panel stays the mixer's single focusable leaf.
#include "MixerPanelComponent.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerSendList.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr float kLevelStepDb = 1.0f;
constexpr float kFineLevelStepDb = 0.1f;
constexpr float kRowRingRadius = 3.0f;
} // namespace

MixerInsertList* MixerPanelComponent::focusedInsertList() const {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size() ||
        sectionLayout_.isHidden(MixerSection::Inserts))
        return nullptr;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind == ColumnEntry::Kind::Strip)
        return &static_cast<MixerColumnComponent*>(entry.component)->getInsertList();
    if (entry.kind == ColumnEntry::Kind::Master)
        return &static_cast<MixerMasterColumn*>(entry.component)->getInsertList();
    return nullptr; // Direct has no rows.
}

MixerSendList* MixerPanelComponent::focusedSendList() const {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size() ||
        sectionLayout_.isHidden(MixerSection::Sends))
        return nullptr;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip)
        return nullptr; // Master and Direct have no sends.
    return &static_cast<MixerColumnComponent*>(entry.component)->getSendList();
}

std::vector<MixerRowRef> MixerPanelComponent::focusedRows() const {
    std::vector<MixerRowRef> rows;
    if (const auto* inserts = focusedInsertList())
        for (int i = 0; i < inserts->getRowCount(); ++i)
            rows.push_back({MixerRowKind::Insert, i});
    if (const auto* sends = focusedSendList())
        for (int i = 0; i < sends->getRowCount(); ++i)
            rows.push_back({MixerRowKind::Send, i});
    return rows;
}

// Left unhandled (false) with no column focused or nothing to enter, so Tab still reaches the
// app-wide region cycle in those cases.
bool MixerPanelComponent::enterRows() {
    if (rowFocus_.has_value())
        return false;
    const auto rows = focusedRows();
    if (rows.empty())
        return false;
    setRowFocus(rows.front());
    return true;
}

void MixerPanelComponent::exitRows() {
    if (rowFocus_.has_value())
        setRowFocus(std::nullopt);
}

// Up and Down step rows, Left and Right change a focused send's level, Return opens, Delete removes and
// Esc leaves. Arrow keys on an insert row are swallowed rather than walking columns, which would carry
// the row focus to a different column. Every other key keeps its column-mode meaning.
bool MixerPanelComponent::handleRowKey(const juce::KeyPress& key) {
    const float step = key.getModifiers().isShiftDown() ? kFineLevelStepDb : kLevelStepDb;
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        exitRows();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::upKey))
        return stepRow(-1);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return stepRow(1);
    if (key.isKeyCode(juce::KeyPress::leftKey))
        return nudgeFocusedSend(-step);
    if (key.isKeyCode(juce::KeyPress::rightKey))
        return nudgeFocusedSend(step);
    if (key.isKeyCode(juce::KeyPress::returnKey))
        return openFocusedRow();
    const auto mods = key.getModifiers();
    if ((key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey)) &&
        !mods.isCommandDown() && !mods.isCtrlDown() && !mods.isAltDown())
        return removeFocusedRow();
    if (matchesAction(key, "mixerToggleRowBypass", juce::KeyPress('b', juce::ModifierKeys::noModifiers, 0)))
        return toggleFocusedRowBypass();
    return false;
}

// Clamped at either end, like the column walk.
bool MixerPanelComponent::stepRow(int direction) {
    const auto rows = focusedRows();
    const auto at = rowFocus_.has_value() ? std::find(rows.begin(), rows.end(), *rowFocus_) : rows.end();
    if (rows.empty() || at == rows.end()) {
        exitRows();
        return true;
    }
    const int index = juce::jlimit(0, (int)rows.size() - 1, (int)(at - rows.begin()) + direction);
    setRowFocus(rows[(size_t)index]);
    return true;
}

bool MixerPanelComponent::nudgeFocusedSend(float deltaDb) {
    if (rowFocus_.has_value() && rowFocus_->kind == MixerRowKind::Send)
        if (auto* sends = focusedSendList()) {
            sends->nudgeLevel(rowFocus_->index, deltaDb);
            refreshRowAccessibility();
        }
    return true;
}

// The same removal a click on a send's "x" or a menu's Remove makes: one undo step through the list,
// which normally rebuilds the mixer and destroys that list, so nothing may touch it afterwards. A
// branching insert chain is read-only and keeps its rows.
bool MixerPanelComponent::removeFocusedRow() {
    if (!rowFocus_.has_value())
        return true;
    const auto row = *rowFocus_;
    if (row.kind == MixerRowKind::Send) {
        if (auto* sends = focusedSendList())
            sends->removeRow(row.index);
    } else if (auto* inserts = focusedInsertList(); inserts != nullptr && inserts->isLinear()) {
        inserts->removeRow(row.index);
    }
    reconcileRowFocus();
    return true;
}

// The same toggle a click on the row's bypass button makes: one undo step through the list. The mixer rebuild it
// triggers destroys that list, so nothing may touch it afterwards; reconcileRowFocus() re-derives the row (and so its
// screen-reader text, which now says "bypassed") from the new lists.
bool MixerPanelComponent::toggleFocusedRowBypass() {
    if (!rowFocus_.has_value())
        return true;
    const auto row = *rowFocus_;
    if (row.kind == MixerRowKind::Send) {
        if (auto* sends = focusedSendList())
            sends->toggleBypassForRow(row.index);
    } else if (auto* inserts = focusedInsertList()) {
        inserts->toggleBypassForRow(row.index);
    }
    reconcileRowFocus();
    refreshRowAccessibility();
    return true;
}

// An insert opens the way the EQ thumbnail's click does -- it selects the module's channel on the
// canvas. A send has nothing to open.
bool MixerPanelComponent::openFocusedRow() {
    if (rowFocus_.has_value() && rowFocus_->kind == MixerRowKind::Insert)
        if (const auto* inserts = focusedInsertList())
            selectOnCanvas(inserts->getRowUuid(rowFocus_->index));
    return true;
}

// The focused strip's first Parametric EQ, while the EQ section is shown.
bool MixerPanelComponent::openFocusedEq() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size() ||
        sectionLayout_.isHidden(MixerSection::Eq))
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip)
        return false;
    const auto eqNode = static_cast<MixerColumnComponent*>(entry.component)->getEqNodeId();
    if (eqNode == juce::AudioProcessorGraph::NodeID{} || !onOpenEqWindow)
        return false;
    onOpenEqWindow(eqNode);
    return true;
}

void MixerPanelComponent::openEqWindowOnCanvas(juce::AudioProcessorGraph::NodeID nodeId) {
    if (graphEditor_ == nullptr)
        return;
    for (auto* card : graphEditor_->getModuleComponents())
        if (card != nullptr && card->getNodeId() == nodeId) {
            card->openEqWindow();
            return;
        }
}

void MixerPanelComponent::setRowFocus(std::optional<MixerRowRef> row) {
    rowFocus_ = row;
    syncRowVisuals();
    refreshRowAccessibility();
    repaint();
}

// After a rebuild or a section toggle the row the focus names may have gone: clamp it into its
// list, fall back to the other list, or leave row mode when no row remains.
void MixerPanelComponent::reconcileRowFocus() {
    if (!rowFocus_.has_value())
        return;
    const auto rows = focusedRows();
    if (rows.empty()) {
        setRowFocus(std::nullopt);
        return;
    }
    auto wanted = *rowFocus_;
    if (std::find(rows.begin(), rows.end(), wanted) == rows.end()) {
        const auto sameKind =
            std::count_if(rows.begin(), rows.end(), [&](const MixerRowRef& r) { return r.kind == wanted.kind; });
        wanted = sameKind > 0 ? MixerRowRef{wanted.kind, (int)sameKind - 1} : rows.front();
    }
    setRowFocus(wanted);
}

void MixerPanelComponent::focusLost(FocusChangeType) { exitRows(); }

// Only the focused column's lists carry a focused row; every other list is cleared.
void MixerPanelComponent::syncRowVisuals() {
    for (size_t i = 0; i < columnEntries_.size(); ++i) {
        auto& entry = columnEntries_[i];
        const bool focused = (int)i == focusedColumnIndex_ && rowFocus_.has_value();
        if (entry.kind == ColumnEntry::Kind::Strip) {
            auto* column = static_cast<MixerColumnComponent*>(entry.component);
            column->getInsertList().setFocusedRow(focused && rowFocus_->kind == MixerRowKind::Insert ? rowFocus_->index
                                                                                                     : -1);
            column->getSendList().setFocusedRow(focused && rowFocus_->kind == MixerRowKind::Send ? rowFocus_->index
                                                                                                 : -1);
        } else if (entry.kind == ColumnEntry::Kind::Master) {
            static_cast<MixerMasterColumn*>(entry.component)
                ->getInsertList()
                .setFocusedRow(focused && rowFocus_->kind == MixerRowKind::Insert ? rowFocus_->index : -1);
        }
    }
}

// The row's text goes on the panel itself, the one focused component, and its change is announced.
void MixerPanelComponent::refreshRowAccessibility() {
    juce::String text;
    if (rowFocus_.has_value()) {
        if (rowFocus_->kind == MixerRowKind::Insert) {
            if (const auto* inserts = focusedInsertList())
                text = inserts->describeRow(rowFocus_->index);
        } else if (const auto* sends = focusedSendList()) {
            text = sends->describeRow(rowFocus_->index);
        }
    }
    if (text == getDescription())
        return;
    setDescription(text);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
    // The screen reader's own focus sits on the column's fader (see grabAccessibilityFocus in the
    // column walk), not on the panel, so a description change alone is never spoken: announce it.
    if (text.isNotEmpty())
        juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

juce::Rectangle<int> MixerPanelComponent::focusedRowBounds() const {
    if (!rowFocus_.has_value())
        return {};
    juce::Component* list = nullptr;
    juce::Rectangle<int> row;
    if (rowFocus_->kind == MixerRowKind::Insert) {
        auto* inserts = focusedInsertList();
        list = inserts;
        row = inserts != nullptr ? inserts->getRowBounds(rowFocus_->index) : row;
    } else {
        auto* sends = focusedSendList();
        list = sends;
        row = sends != nullptr ? sends->getRowBounds(rowFocus_->index) : row;
    }
    auto* frame = list != nullptr ? list->findParentComponentOfClass<MixerSectionViewport>() : nullptr;
    if (frame == nullptr || row.isEmpty())
        return {};
    return getLocalArea(list, row).getIntersection(getLocalArea(frame, frame->getLocalBounds()));
}

void MixerPanelComponent::paintRowFocusRing(juce::Graphics& g) const {
    const auto bounds = focusedRowBounds();
    if (!bounds.isEmpty())
        paintFocusRing(g, bounds.toFloat(), *this, kRowRingRadius);
}

} // namespace synth::ui
