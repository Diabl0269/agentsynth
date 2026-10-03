// BuiltInCardLayoutSource.cpp -- a built-in module's card as the layout editor's source: parameters,
// the live write that rebuilds the card, Apply to all through the per-type store, presets, and the
// session's single undo step. docs/layout/module-card-layout.md#editing-a-layout.
#include "BuiltInCardLayoutSource.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/CardBody/ModuleCardLayoutBinding.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

namespace {

CardWidget widgetFor(CardBodyItem::Kind kind) {
    using Kind = CardBodyItem::Kind;
    switch (kind) {
    case Kind::Choice:
        return CardWidget::Choice;
    case Kind::Toggle:
        return CardWidget::Toggle;
    case Kind::KnobLarge:
        return CardWidget::KnobLarge;
    case Kind::FaderV:
        return CardWidget::FaderV;
    case Kind::FaderH:
        return CardWidget::FaderH;
    case Kind::Segmented:
        return CardWidget::Segmented;
    case Kind::Stepper:
        return CardWidget::Stepper;
    case Kind::Knob:
    case Kind::View:
        break;
    }
    return CardWidget::Knob;
}

// The widgets that suit `param`: its automatic one first, then every other the card would really draw
// it as (cardBodyKindFor answers the automatic kind for one that does not suit, which is left out).
std::vector<CardWidget> widgetChoicesFor(const juce::RangedAudioParameter& param) {
    std::vector<CardWidget> choices;
    const auto automatic = cardBodyKindFor(param, CardWidget::Auto);
    if (!automatic.has_value())
        return choices;
    choices.push_back(widgetFor(*automatic));
    for (auto widget : {CardWidget::Knob, CardWidget::KnobLarge, CardWidget::FaderV, CardWidget::FaderH,
                        CardWidget::Choice, CardWidget::Segmented, CardWidget::Stepper}) {
        const auto kind = cardBodyKindFor(param, widget);
        if (kind.has_value() && widgetFor(*kind) == widget && widget != choices.front())
            choices.push_back(widget);
    }
    return choices;
}

} // namespace

// The "before" of the session's undo step is taken here, before anything is written.
BuiltInCardLayoutSource::BuiltInCardLayoutSource(GraphEditor& editor, AppUndoManager* undo,
                                                 juce::AudioProcessorGraph::NodeID nodeId)
    : editor_(&editor)
    , undo_(undo)
    , nodeId_(nodeId) {
    auto& graph = editor.getAudioEngine().getGraph();
    if (auto* node = graph.getNodeForId(nodeId)) {
        openedOn_ = node->getProcessor();
        moduleType_ = AIStateMapper::getFactoryTypeName(openedOn_);
        openingOverride_ = getCardLayoutOverride(graph, nodeId).clone();
    }
    if (undo_ != nullptr)
        sessionBefore_ = AIStateMapper::graphToJSON(graph);
}

// Closing the editor ends the session: every write it made (this card's layout, the neighbours its
// growth pushed aside, an Apply to all clearing the override) becomes one undo step. The per-type file
// an Apply to all wrote is a setting, not part of the graph, so undo leaves it in place.
BuiltInCardLayoutSource::~BuiltInCardLayoutSource() {
    if (undo_ != nullptr)
        if (auto* g = graph())
            undo_->recordGraphChangeSince(*g, sessionBefore_);
}

juce::AudioProcessorGraph* BuiltInCardLayoutSource::graph() const {
    auto* editor = dynamic_cast<GraphEditor*>(editor_.getComponent());
    return editor != nullptr ? &editor->getAudioEngine().getGraph() : nullptr;
}

juce::AudioProcessor* BuiltInCardLayoutSource::module() const {
    auto* g = graph();
    auto* node = g != nullptr ? g->getNodeForId(nodeId_) : nullptr;
    return node != nullptr && node->getProcessor() == openedOn_ ? openedOn_ : nullptr;
}

ModuleComponent* BuiltInCardLayoutSource::card() const {
    auto* editor = dynamic_cast<GraphEditor*>(editor_.getComponent());
    if (editor == nullptr)
        return nullptr;
    for (auto* comp : editor->getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == nodeId_)
            return comp;
    return nullptr;
}

ModuleCardLayoutStore* BuiltInCardLayoutSource::store() const {
    auto* editor = editor_.getComponent();
    return editor != nullptr ? findModuleCardLayoutStore(*editor) : nullptr;
}

bool BuiltInCardLayoutSource::isAlive() const { return module() != nullptr; }

juce::String BuiltInCardLayoutSource::title() const { return moduleType_ + " layout"; }

juce::String BuiltInCardLayoutSource::allScopeText() const { return "All " + moduleType_ + " modules"; }

juce::String BuiltInCardLayoutSource::resetTooltip() const {
    return "Remove the chosen scope's layout, so the card goes back to the default";
}

std::vector<CardLayoutEditorParam> BuiltInCardLayoutSource::parameters() const {
    std::vector<CardLayoutEditorParam> params;
    auto* processor = module();
    if (processor == nullptr)
        return params;
    for (const auto& item : CardBodyPlan::forModule(*processor, std::nullopt).items) {
        if (item.param == nullptr)
            continue;
        params.push_back({item.param->paramID, item.param->getName(100), -1, widgetChoicesFor(*item.param)});
    }
    return params;
}

CardLayout BuiltInCardLayoutSource::currentLayout() const {
    auto* comp = card();
    const auto* body = comp != nullptr ? comp->getCardBody() : nullptr;
    return body != nullptr ? body->explicitLayout() : CardLayout();
}

// The same rebuild the quick path does: updateComponents swaps in a card built from the new layout,
// and the new card makes room for (or gives back) its change in height. A card the store's listener
// already rebuilt is left alone.
void BuiltInCardLayoutSource::refreshCard() {
    auto* editor = dynamic_cast<GraphEditor*>(editor_.getComponent());
    if (editor == nullptr)
        return;
    auto* before = card();
    editor->updateComponents();
    if (auto* rebuilt = card(); rebuilt != nullptr && rebuilt != before)
        editor->handleModuleResized(rebuilt);
}

// Apply to all clears this module's own layout first, so the store's listener, which runs inside
// setDefault, rebuilds this card with every other one of its type. Without a store the layout stays
// on this module.
void BuiltInCardLayoutSource::apply(const CardLayout& layout, bool allOfType) {
    auto* g = graph();
    if (g == nullptr || !isAlive())
        return;
    auto* types = store();
    if (allOfType && types != nullptr) {
        setCardLayoutOverride(*g, nullptr, nodeId_, std::nullopt);
        types->setDefault(moduleType_, layout);
    } else {
        setCardLayoutOverride(*g, nullptr, nodeId_, layout);
    }
    refreshCard();
}

CardLayout BuiltInCardLayoutSource::reset(bool allOfType) {
    auto* g = graph();
    if (g == nullptr || !isAlive())
        return currentLayout();
    setCardLayoutOverride(*g, nullptr, nodeId_, std::nullopt);
    if (auto* types = store(); allOfType && types != nullptr)
        types->clearDefault(moduleType_);
    refreshCard();
    return currentLayout();
}

void BuiltInCardLayoutSource::restoreOpeningLayout() {
    auto* g = graph();
    if (g == nullptr || !isAlive())
        return;
    restoreCardLayoutOverride(*g, nodeId_, openingOverride_);
    refreshCard();
}

juce::StringArray BuiltInCardLayoutSource::listPresets() const {
    auto* types = store();
    return types != nullptr ? types->listPresets(moduleType_) : juce::StringArray();
}

bool BuiltInCardLayoutSource::savePreset(const juce::String& name, const CardLayout& layout) {
    auto* types = store();
    return types != nullptr && types->savePreset(moduleType_, name, layout);
}

std::optional<CardLayout> BuiltInCardLayoutSource::loadPreset(const juce::String& name) const {
    auto* types = store();
    if (types == nullptr)
        return std::nullopt;
    auto loaded = types->loadPreset(moduleType_, name);
    if (loaded.status != ModuleCardLayoutStore::LoadStatus::Ok)
        return std::nullopt;
    juce::StringArray ids;
    for (const auto& param : parameters())
        ids.add(param.paramId);
    return upgradeV1(loaded.layout, ids);
}

bool BuiltInCardLayoutSource::deletePreset(const juce::String& name) {
    auto* types = store();
    return types != nullptr && types->deletePreset(moduleType_, name);
}

} // namespace synth::ui
