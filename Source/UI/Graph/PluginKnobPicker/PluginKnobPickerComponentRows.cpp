// PluginKnobPickerComponentRows.cpp -- the row list itself: search filtering, tick/untick, label
// edits, drag-reorder (scoped to the checked group, same as MacroPortConfigDialog's own direction-
// scoped reorder), touch-to-add's model-side half, and the one function every mutation ends in,
// applyCurrentLayout(). See docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerComponent.h"
#include "PluginKnobPickerRow.h"
#include <algorithm>

namespace synth::ui {

// ---- Building the visible list -----------------------------------------------------------------

// Checked rows first, in workingSlots_ order (the order the card shows them in); then the rest, in
// the instance's own parameter order. Both groups are filtered by the SAME live search text against
// displayName -- "checked rows first, then the rest, filtered live by a search box" per the design
// doc's mock. A checked row whose parameter has since gone missing has no row at all here (it lives
// in missingSlots_ and only ever shows up in the missing-count line).
void PluginKnobPickerComponent::rebuildRows() {
    rows_.clear();
    const auto search = searchEditor_.getText().trim().toLowerCase();
    const auto matches = [&](const juce::String& name) {
        return search.isEmpty() || name.toLowerCase().contains(search);
    };

    for (const auto& slot : workingSlots_) {
        auto it = std::find_if(allParams_.begin(), allParams_.end(),
                               [&](const ParamInfo& p) { return p.paramId == slot.paramId; });
        if (it == allParams_.end() || !matches(it->displayName))
            continue;
        addRow(*it, true, slot.label.value_or(juce::String()));
    }

    for (const auto& info : allParams_) {
        const bool alreadyChecked = std::any_of(workingSlots_.begin(), workingSlots_.end(),
                                                [&](const CardSlot& s) { return s.paramId == info.paramId; });
        if (alreadyChecked || !matches(info.displayName))
            continue;
        addRow(info, false, {});
    }

    layOutRows();
}

void PluginKnobPickerComponent::addRow(const ParamInfo& info, bool checked, const juce::String& labelOverride) {
    auto* row = rows_.add(new PluginKnobPickerRow(info.paramId, info.displayName));
    rowsContent_.addAndMakeVisible(row);
    row->setChecked(checked);
    row->setLabelText(labelOverride);

    const juce::String paramId = info.paramId;
    row->onToggled = [this, paramId](bool nowChecked) { setParamChecked(paramId, nowChecked); };
    row->onLabelCommitted = [this, paramId](const juce::String& text) { setParamLabel(paramId, text); };

    row->onDragStarted = [this, paramId] {
        draggingParamId_ = paramId;
        auto it = std::find_if(workingSlots_.begin(), workingSlots_.end(),
                               [&](const CardSlot& s) { return s.paramId == paramId; });
        dragStartIndexAmongChecked_ = it == workingSlots_.end() ? -1 : static_cast<int>(it - workingSlots_.begin());
    };
    row->onDragUpdated = [this](int deltaY) {
        if (draggingParamId_.isEmpty() || dragStartIndexAmongChecked_ < 0)
            return;
        const int slots = deltaY / PluginKnobPickerRow::kRowHeight;
        const int target =
            juce::jlimit(0, static_cast<int>(workingSlots_.size()) - 1, dragStartIndexAmongChecked_ + slots);
        if (target != dragStartIndexAmongChecked_)
            commitReorder(draggingParamId_, target);
    };
    row->onDragEnded = [this] {
        draggingParamId_.clear();
        dragStartIndexAmongChecked_ = -1;
    };
}

// ---- Mutations: every one re-applies to the current scope and, when the checked set or its order
// changed, rebuilds the row list. A label edit alone does not rebuild -- the row already shows the
// text it just committed, and rebuilding mid-edit would be a visible flicker for no reason. ----

void PluginKnobPickerComponent::setParamChecked(const juce::String& paramId, bool checked) {
    if (checked) {
        const bool already = std::any_of(workingSlots_.begin(), workingSlots_.end(),
                                         [&](const CardSlot& s) { return s.paramId == paramId; });
        if (!already) {
            auto it = std::find_if(allParams_.begin(), allParams_.end(),
                                   [&](const ParamInfo& p) { return p.paramId == paramId; });
            if (it == allParams_.end())
                return;
            CardSlot slot;
            slot.paramId = it->paramId;
            slot.indexHint = it->index;
            slot.kind = CardSlotKind::Auto;
            workingSlots_.push_back(slot);
        }
    } else {
        workingSlots_.erase(std::remove_if(workingSlots_.begin(), workingSlots_.end(),
                                           [&](const CardSlot& s) { return s.paramId == paramId; }),
                            workingSlots_.end());
    }
    applyCurrentLayout();
    rebuildRows();
}

void PluginKnobPickerComponent::setParamLabel(const juce::String& paramId, const juce::String& text) {
    for (auto& slot : workingSlots_)
        if (slot.paramId == paramId) {
            slot.label = text.isEmpty() ? std::optional<juce::String>() : std::optional<juce::String>(text);
            break;
        }
    applyCurrentLayout();
}

void PluginKnobPickerComponent::commitReorder(const juce::String& paramId, int newIndexAmongChecked) {
    auto it = std::find_if(workingSlots_.begin(), workingSlots_.end(),
                           [&](const CardSlot& s) { return s.paramId == paramId; });
    if (it == workingSlots_.end())
        return;
    const CardSlot moved = *it;
    workingSlots_.erase(it);
    newIndexAmongChecked = juce::jlimit(0, static_cast<int>(workingSlots_.size()), newIndexAmongChecked);
    workingSlots_.insert(workingSlots_.begin() + newIndexAmongChecked, moved);
    dragStartIndexAmongChecked_ = newIndexAmongChecked;
    applyCurrentLayout();
    rebuildRows();
}

// PluginKnobPickerTouchCapture already de-duplicates nothing on its own (a parameter touched twice
// reports twice) -- the set-membership check below is what makes a repeat touch a no-op.
void PluginKnobPickerComponent::handleParameterTouched(int parameterIndex) {
    auto it = std::find_if(allParams_.begin(), allParams_.end(),
                           [&](const ParamInfo& p) { return p.index == parameterIndex; });
    if (it == allParams_.end())
        return;
    setParamChecked(it->paramId, true);
}

// ---- Missing parameters (the founder "degrade visibly, repair explicitly" line) -----------------

void PluginKnobPickerComponent::partitionSlots(std::vector<CardSlot> slots) {
    workingSlots_.clear();
    missingSlots_.clear();
    for (auto& slot : slots) {
        const bool resolves = std::any_of(allParams_.begin(), allParams_.end(),
                                          [&](const ParamInfo& p) { return p.paramId == slot.paramId; });
        (resolves ? workingSlots_ : missingSlots_).push_back(std::move(slot));
    }
    updateMissingLabel();
}

void PluginKnobPickerComponent::updateMissingLabel() {
    if (missingSlots_.empty()) {
        missingLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    juce::StringArray names;
    for (const auto& slot : missingSlots_)
        names.add(slot.label.value_or(slot.paramId));
    missingLabel_.setText(juce::String(missingSlots_.size()) +
                              " parameters missing in this plugin version: " + names.joinIntoString(", "),
                          juce::dontSendNotification);
}

// ---- The one write path -------------------------------------------------------------------------

// "This instance" writes the node's own override; "All <plugin> instances" writes the plugin-type
// default AND clears this instance's override so it follows it (docs/control/plugin-card-layout.md's
// "Apply to" paragraph) -- every open instance without an override re-resolves from the store's
// broadcast. Either branch replays through AppUndoManager::recordNodeExtraStateChange with a
// layout-only before/after patch, exactly like a knob's own drag gesture does
// (ModuleComponent::handleHostedGesture), so "Choose knobs..." edits undo the same way.
void PluginKnobPickerComponent::applyCurrentLayout() {
    auto* module = module_.get();
    if (module == nullptr)
        return;

    CardLayout layout;
    layout.version = CardLayout::kCurrentVersion;
    layout.slots = workingSlots_;

    const auto before = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    if (applyToAllInstances_) {
        if (store_ != nullptr)
            store_->setDefault(identity_, layout);
        module->setCardLayoutOverride(juce::var());
    } else {
        module->setCardLayoutOverride(layout.toVar());
    }
    const auto after = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    if (undoManager_ != nullptr)
        undoManager_->recordNodeExtraStateChange(graph_, nodeId_, before, after);

    if (!missingSlots_.empty()) {
        missingSlots_.clear();
        updateMissingLabel();
        resized();
    }
}

} // namespace synth::ui
