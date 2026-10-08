// PluginTouchToAdd.cpp -- "add by moving a control in the plugin": the touch capture wired to the card's layout.
// docs/control/plugin-card-layout.md#add-by-moving-a-control-in-the-plugin.
#include "PluginTouchToAdd.h"
#include "PluginKnobPickerTouchCapture.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"

namespace synth::ui {

PluginTouchToAdd::PluginTouchToAdd(HostedPluginModule& module, PluginCardLayoutStore* store,
                                   juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                                   AppUndoManager* undoManager)
    : module_(&module)
    , store_(store)
    , graph_(graph)
    , nodeId_(nodeId)
    , undoManager_(undoManager)
    , capture_(std::make_unique<PluginKnobPickerTouchCapture>(module)) {
    capture_->onParameterTouched = [this](int index) { handleParameterTouched(index); };
    capture_->onRequestOpenEditor = [this] {
        if (onRequestOpenEditor)
            onRequestOpenEditor();
    };
    // A value change on a parameter already on the card is the card's own knob (or an earlier add) moving it, never
    // a new touch. Gestures need no such filter: a gesture start is deliberate.
    capture_->isParameterAlreadyInLayout = [this](int index) {
        auto* module = module_.get();
        const auto id = paramIdFor(index);
        return module != nullptr && id.has_value() &&
               HostedCardLayoutSource(*module, store_, graph_, nodeId_, undoManager_).parameterState(*id) ==
                   HostedCardLayoutSource::ParameterState::OnCard;
    };
}

// capture_'s destructor disarms and unhooks from every instance parameter.
PluginTouchToAdd::~PluginTouchToAdd() = default;

void PluginTouchToAdd::start() { capture_->setArmed(true); }

bool PluginTouchToAdd::isOn() const noexcept { return capture_->isArmed(); }

std::optional<juce::String> PluginTouchToAdd::paramIdFor(int parameterIndex) const {
    auto* module = module_.get();
    if (module == nullptr)
        return std::nullopt;
    for (const auto& info : module->getInstanceParameters())
        if (info.index == parameterIndex)
            return info.paramId;
    return std::nullopt;
}

// Touching a parameter twice adds it once; an unknown index adds nothing.
void PluginTouchToAdd::handleParameterTouched(int parameterIndex) {
    auto* module = module_.get();
    const auto id = paramIdFor(parameterIndex);
    if (module == nullptr || !module->hasInstance() || !id.has_value())
        return;
    HostedCardLayoutSource(*module, store_, graph_, nodeId_, undoManager_).showParameter(*id);
}

void PluginTouchToAdd::simulateTouchGestureForTest(int parameterIndex) {
    capture_->simulateGestureStartForTest(parameterIndex);
}

} // namespace synth::ui
