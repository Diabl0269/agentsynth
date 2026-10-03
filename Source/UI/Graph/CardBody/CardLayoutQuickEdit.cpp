// CardLayoutQuickEdit.cpp -- the right-click quick path: the card's current layout as explicit items,
// the one-parameter edits on it, the undoable write that rebuilds the card and makes room, and the menu
// items that offer them. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutQuickEdit.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "CardBody.h"
#include "CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>

namespace synth {

namespace {

CardParamItem* findPlaced(CardLayout& layout, const juce::String& paramId) {
    for (auto& section : layout.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item); param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
}

// A parameter the layout never placed (a v1 layout's unnamed ones, one a later release added) goes
// at the end of the last section when shown: the layout has no place of its own for it. The footer row
// is never that section (it is drawn last wherever the layout lists it); a layout with only a footer
// gets a main section first.
CardParamItem& placeAtEnd(CardLayout& layout, const juce::String& paramId) {
    auto target = std::find_if(layout.sections.rbegin(), layout.sections.rend(),
                               [](const CardSection& section) { return section.id != CardSection::kFooterId; });
    if (target == layout.sections.rend()) {
        CardSection section;
        section.id = "main";
        layout.sections.insert(layout.sections.begin(), section);
        target = std::prev(layout.sections.rend());
    }
    CardParamItem item;
    item.paramId = paramId;
    auto& items = target->items;
    items.emplace_back(item);
    return std::get<CardParamItem>(items.back());
}

ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == nodeId)
            return comp;
    return nullptr;
}

} // namespace

// The automatic layout written out: one section per plan section, every item in card order with the
// automatic widget, so building from it gives the same card (CardLayoutQuickEditTests pins that).
CardLayout CardBody::explicitLayout() const {
    if (layout_.has_value() && !layout_->sections.empty()) {
        // A layout read back from the v1 form hides every id its slots did not name, including the
        // header buttons' and the ones edited elsewhere on the card; keep only what the card shows.
        auto layout = *layout_;
        juce::StringArray hidden;
        for (const auto& id : layout.hidden)
            if (plan_.findParam(id) >= 0)
                hidden.add(id);
        layout.hidden = hidden;
        return layout;
    }
    CardLayout layout;
    for (const auto& planned : plan_.sections) {
        CardSection section;
        section.id = layout.sections.empty() ? juce::String("main") : "section-" + juce::String(layout.sections.size());
        section.columns = planned.columns;
        for (int index : planned.items) {
            const auto& item = plan_.items[(size_t)index];
            if (item.kind == CardBodyItem::Kind::View) {
                CardViewItem view;
                view.view = item.view;
                section.items.emplace_back(view);
            } else {
                CardParamItem param;
                param.paramId = item.param->paramID;
                section.items.emplace_back(param);
            }
        }
        layout.sections.push_back(std::move(section));
    }
    for (int index : plan_.more)
        layout.hidden.add(plan_.items[(size_t)index].param->paramID);
    return layout;
}

bool CardBody::drawsFromLayout() const { return cardBodyLayoutIsDataDriven(module_); }

// Hiding keeps the item where it is, so showing it again puts it back exactly there.
CardLayout applyCardQuickEdit(CardLayout layout, const juce::String& paramId, CardQuickEdit edit) {
    switch (edit) {
    case CardQuickEdit::Hide:
        layout.hidden.addIfNotAlreadyThere(paramId);
        break;
    case CardQuickEdit::ShowOnCard:
        layout.hidden.removeString(paramId);
        if (findPlaced(layout, paramId) == nullptr)
            placeAtEnd(layout, paramId);
        break;
    case CardQuickEdit::ShowAsFader:
    case CardQuickEdit::ShowAsKnob: {
        auto* item = findPlaced(layout, paramId);
        if (item == nullptr)
            item = &placeAtEnd(layout, paramId);
        item->widget = edit == CardQuickEdit::ShowAsFader ? CardWidget::FaderV : CardWidget::Knob;
        break;
    }
    }
    return layout;
}

// One undo step holds all of it: the override write, the rebuilt card and the neighbours it pushed
// aside or gave room back to. The card is rebuilt by updateComponents (it notices the override no
// longer matches what the card was built from), the same path an undo of this step takes.
bool performCardQuickEdit(GraphEditor& editor, AppUndoManager* undo, juce::AudioProcessorGraph::NodeID nodeId,
                          const juce::String& paramId, CardQuickEdit edit) {
    auto* card = cardFor(editor, nodeId);
    const auto* body = card != nullptr ? card->getCardBody() : nullptr;
    if (body == nullptr || !body->drawsFromLayout())
        return false;
    const auto before = body->explicitLayout();
    const auto after = applyCardQuickEdit(before, paramId, edit);
    if (after == before)
        return false;

    auto& graph = editor.getAudioEngine().getGraph();
    auto apply = [&editor, &graph, nodeId, after] {
        setCardLayoutOverride(graph, nullptr, nodeId, after);
        editor.updateComponents();
        if (auto* rebuilt = cardFor(editor, nodeId))
            editor.handleModuleResized(rebuilt);
    };
    if (undo != nullptr)
        undo->recordStructuralChange(graph, apply);
    else
        apply();
    return true;
}

void appendEditLayoutMenuItem(juce::PopupMenu& menu, std::function<void()> open) {
    menu.addItem("Edit Layout...", std::move(open));
}

// The actions run after the menu closes, by which time the card may be gone (and is, once an edit
// rebuilds it), so they hold the editor through a SafePointer and the node by id, never the card.
void appendCardLayoutMenuItems(juce::PopupMenu& menu, GraphEditor& editor, AppUndoManager* undo,
                               juce::AudioProcessorGraph::NodeID nodeId, const CardBody* body,
                               const juce::String& paramId) {
    if (body == nullptr || !body->drawsFromLayout())
        return;
    const auto& plan = body->getPlan();
    const int index = plan.findParam(paramId);
    if (index < 0 || plan.items[(size_t)index].widget == nullptr)
        return;

    juce::Component::SafePointer<GraphEditor> safeEditor(&editor);
    auto action = [safeEditor, undo, nodeId, paramId](CardQuickEdit edit) {
        return [safeEditor, undo, nodeId, paramId, edit] {
            if (auto* ed = safeEditor.getComponent())
                performCardQuickEdit(*ed, undo, nodeId, paramId, edit);
        };
    };

    if (menu.getNumItems() > 0)
        menu.addSeparator();
    const bool inMore = std::find(plan.more.begin(), plan.more.end(), index) != plan.more.end();
    menu.addItem(inMore ? "Show on card" : "Hide from card",
                 action(inMore ? CardQuickEdit::ShowOnCard : CardQuickEdit::Hide));
    const auto kind = plan.items[(size_t)index].kind;
    if (!inMore && isContinuousKind(kind)) {
        const bool fader = kind == CardBodyItem::Kind::FaderV || kind == CardBodyItem::Kind::FaderH;
        menu.addItem(fader ? "Show as knob" : "Show as fader",
                     action(fader ? CardQuickEdit::ShowAsKnob : CardQuickEdit::ShowAsFader));
    }
    appendEditLayoutMenuItem(menu, [safeEditor, nodeId] {
        if (auto* ed = safeEditor.getComponent())
            if (auto* card = cardFor(*ed, nodeId))
                card->showCardLayoutEditor();
    });
}

} // namespace synth
