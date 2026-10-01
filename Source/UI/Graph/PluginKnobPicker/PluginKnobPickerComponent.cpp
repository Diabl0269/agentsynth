// PluginKnobPickerComponent.cpp -- the hosted plugin's card layout editor: the shared editor over the
// instance's parameters, and touch-to-add. docs/control/plugin-card-layout.md#choosing-knobs.
#include "PluginKnobPickerComponent.h"
#include "PluginKnobPickerTouchCapture.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"
#include <algorithm>

namespace synth::ui {

PluginKnobPickerComponent::PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                                                     juce::AudioProcessorGraph& graph,
                                                     juce::AudioProcessorGraph::NodeID nodeId,
                                                     AppUndoManager* undoManager, const ShortcutManager* shortcuts)
    : CardLayoutEditorComponent(std::make_unique<HostedCardLayoutSource>(module, store, graph, nodeId, undoManager),
                                shortcuts)
    , nodeId_(nodeId) {
    touchCapture_ = std::make_unique<PluginKnobPickerTouchCapture>(module);
    touchCapture_->onParameterTouched = [this](int index) { handleParameterTouched(index); };
    touchCapture_->onRequestOpenEditor = [this] {
        if (onOpenPluginEditorRequested)
            onOpenPluginEditorRequested();
    };
    // A value change on a parameter already ticked is the editor's own tick (or the card's own knob
    // moving it), never a new touch. Gestures need no such filter: a gesture start is deliberate.
    touchCapture_->isParameterAlreadyInLayout = [this](int index) { return isParameterIndexShown(index); };

    touchToAddToggle_.setComponentID("knobPickerTouchToAdd");
    touchToAddToggle_.setTooltip("While ticked, a control you move in the plugin's own window is added");
    touchToAddToggle_.onClick = [this] { touchCapture_->setArmed(touchToAddToggle_.getToggleState()); };
    addAndMakeVisible(touchToAddToggle_);
    resized();
}

// touchCapture_'s destructor disarms and unhooks from every instance parameter.
PluginKnobPickerComponent::~PluginKnobPickerComponent() = default;

int PluginKnobPickerComponent::layoutExtraControls(juce::Rectangle<int> area) {
    touchToAddToggle_.setBounds(area);
    return area.getHeight();
}

bool PluginKnobPickerComponent::isParameterIndexShown(int parameterIndex) const {
    const auto& params = getParameters();
    const auto it = std::find_if(params.begin(), params.end(),
                                 [&](const CardLayoutEditorParam& p) { return p.index == parameterIndex; });
    return it != params.end() && isParameterShown(it->paramId);
}

// The capture reports a parameter touched twice twice; ticking an already ticked one changes nothing.
void PluginKnobPickerComponent::handleParameterTouched(int parameterIndex) {
    const auto& params = getParameters();
    const auto it = std::find_if(params.begin(), params.end(),
                                 [&](const CardLayoutEditorParam& p) { return p.index == parameterIndex; });
    if (it != params.end() && !isParameterShown(it->paramId))
        showParameter(it->paramId);
}

// Sets the toggle's visible state and arms exactly as its onClick would.
void PluginKnobPickerComponent::setTouchToAddArmedForTest(bool armed) {
    touchToAddToggle_.setToggleState(armed, juce::dontSendNotification);
    touchCapture_->setArmed(armed);
}

bool PluginKnobPickerComponent::isTouchToAddArmedForTest() const { return touchCapture_->isArmed(); }

void PluginKnobPickerComponent::simulateTouchGestureForTest(int parameterIndex) {
    touchCapture_->simulateGestureStartForTest(parameterIndex);
}

} // namespace synth::ui
