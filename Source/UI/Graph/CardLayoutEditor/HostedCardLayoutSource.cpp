// HostedCardLayoutSource.cpp -- a hosted plugin instance as the layout editor's source. Writes stay the
// flat version 1 slot list, so a project opened in an older build keeps its plugin cards.
// docs/control/plugin-card-layout.md#choosing-knobs.
#include "HostedCardLayoutSource.h"
#include "CardLayoutEditorModel.h"
#include "Modules/CardLayoutJson.h"
#include "Plugin/Hosting/HostedPluginCardLayout.h"

namespace synth::ui {

namespace {

CardLayout asSlots(const CardLayout& layout) {
    CardLayout flat;
    flat.version = CardLayout::kCurrentVersion;
    flat.slots = layout.flatSlots();
    return flat;
}

} // namespace

CardLayout hostedLayoutAsSection(const CardLayout& layout) {
    if (!layout.sections.empty())
        return layout;
    CardLayout section;
    section.version = CardLayout::kCurrentVersion;
    section.sections.push_back(detail::sectionFromSlots(layout.slots));
    return section;
}

HostedCardLayoutSource::HostedCardLayoutSource(HostedPluginModule& module, PluginCardLayoutStore* store,
                                               juce::AudioProcessorGraph& graph,
                                               juce::AudioProcessorGraph::NodeID nodeId, AppUndoManager* undoManager)
    : module_(&module)
    , store_(store)
    , graph_(graph)
    , nodeId_(nodeId)
    , undoManager_(undoManager)
    , identity_(module.getIdentity())
    , pluginName_(module.getPluginName()) {
    for (const auto& info : module.getInstanceParameters())
        params_.push_back({info.paramId, info.displayName, info.index, {}});
}

juce::String HostedCardLayoutSource::title() const { return "Knobs for \"" + pluginName_ + "\""; }

juce::String HostedCardLayoutSource::allScopeText() const { return "All " + pluginName_ + " instances"; }

juce::String HostedCardLayoutSource::resetTooltip() const {
    return "Remove the chosen scope's knobs, so the card goes back to the plugin's default or its first knobs";
}

CardLayout HostedCardLayoutSource::currentLayout() const {
    auto* module = module_.get();
    if (module == nullptr)
        return {};
    return hostedLayoutAsSection(resolveHostedCardLayout(*module, store_).layout);
}

// Replays through recordNodeExtraStateChange with a layout-only before/after patch, exactly like a
// knob's own drag gesture on the card, so each edit is one undo step.
void HostedCardLayoutSource::writeOverride(const juce::var& layout) {
    auto* module = module_.get();
    if (module == nullptr)
        return;
    const auto before = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    module->setCardLayoutOverride(layout);
    const auto after = HostedPluginModule::makeCardLayoutPatch(module->getCardLayoutOverride());
    if (undoManager_ != nullptr)
        undoManager_->recordNodeExtraStateChange(graph_, nodeId_, before, after);
}

// "All instances" writes the plugin's default and clears this instance's override so it follows it;
// every open instance without an override re-resolves from the store's broadcast.
void HostedCardLayoutSource::apply(const CardLayout& layout, bool allOfType) {
    if (module_.get() == nullptr)
        return;
    const auto flat = asSlots(layout);
    if (allOfType) {
        if (store_ != nullptr)
            store_->setDefault(identity_, flat);
        writeOverride(juce::var());
    } else {
        writeOverride(flat.toVar());
    }
}

CardLayout HostedCardLayoutSource::reset(bool allOfType) {
    if (module_.get() == nullptr)
        return {};
    if (allOfType && store_ != nullptr)
        store_->clearDefault(identity_);
    writeOverride(juce::var());
    return currentLayout();
}

namespace {
CardLayoutEditorModel loadedModel(const HostedCardLayoutSource& source) {
    CardLayoutEditorModel model(source.parameters(), source.hiddenRows(), source.supportsGroups());
    model.load(source.currentLayout());
    return model;
}
} // namespace

HostedCardLayoutSource::ParameterState HostedCardLayoutSource::parameterState(const juce::String& paramId) const {
    const auto model = loadedModel(*this);
    if (model.findParam(paramId) == nullptr)
        return ParameterState::Unknown;
    return model.isShown(paramId) ? ParameterState::OnCard : ParameterState::Available;
}

// The same model edit and write the picker's tick makes (CardLayoutEditorComponent::setChecked ->
// applyCurrentLayout), so the layout, the scope and the undo step cannot drift between the two paths.
bool HostedCardLayoutSource::showParameter(const juce::String& paramId) {
    if (module_.get() == nullptr)
        return false;
    auto model = loadedModel(*this);
    if (model.findParam(paramId) == nullptr || model.isShown(paramId))
        return false;
    model.setShown(paramId, true);
    apply(model.toLayout(), /*allOfType*/ false);
    return true;
}

juce::StringArray HostedCardLayoutSource::listPresets() const {
    return store_ != nullptr ? store_->listPresets(identity_) : juce::StringArray();
}

bool HostedCardLayoutSource::savePreset(const juce::String& name, const CardLayout& layout) {
    return store_ != nullptr && store_->savePreset(identity_, name, asSlots(layout));
}

std::optional<CardLayout> HostedCardLayoutSource::loadPreset(const juce::String& name) const {
    if (store_ == nullptr)
        return std::nullopt;
    const auto result = store_->loadPreset(identity_, name);
    if (result.status != PluginCardLayoutStore::LoadStatus::Ok)
        return std::nullopt;
    return hostedLayoutAsSection(result.layout);
}

bool HostedCardLayoutSource::deletePreset(const juce::String& name) {
    return store_ != nullptr && store_->deletePreset(identity_, name);
}

} // namespace synth::ui
